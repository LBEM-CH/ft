#include "ftwindow_common.h"

// ---------------------------------------------------------------------------
// Amyloid filament drawing
// ---------------------------------------------------------------------------

void FtWindow::onAmyloidCancel()
{
    m_amyloidActive = false;
    m_amyloidPlacing = 0;
    m_amyloidFilaments.clear();
    m_amyloidRiseEdit->hide();
    m_amyloidTwistEdit->hide();
    m_amyloidMapCombo->hide();
    m_amyloidSizeCombo->hide();
    m_amyloidNoiseBtn->hide();
    m_amyloidNoiseEdit->hide();
    m_amyloidPersistEdit->hide();
    m_amyloidWaveEdit->hide();
    m_amyloidAmplEdit->hide();
    m_amyloidSignalBtn->hide();
    m_amyloidCancelBtn->hide();
    m_amyloidComputeBtn->hide();
    update();
}

void FtWindow::onAmyloidCompute()
{
    try {
        onAmyloidComputeImpl();
    } catch (const std::bad_alloc &) {
        rollbackAfterCalcOOM(tr("render the amyloid filaments"));
    }
}

void FtWindow::onAmyloidComputeImpl()
{
    if (m_amyloidFilaments.empty()) {
        int defSz = m_amyloidSizeCombo->currentText().toInt();
        if (defSz <= 0) defSz = 1024;
        int w = m_image.isNull() ? defSz : m_image.width();
        int h = m_image.isNull() ? defSz : m_image.height();
        AmyloidFilament fil;
        fil.pts.push_back(QPointF(0.05 * w, 0.5 * h));
        fil.pts.push_back(QPointF(0.95 * w, 0.5 * h));
        m_amyloidFilaments.push_back(fil);
    }

    // ---- Read parameters ----
    bool okR = false, okT = false, okNs = false, okLp = false, okWv = false, okAm = false;
    double rise      = m_amyloidRiseEdit->text().toDouble(&okR);
    double twistDeg  = m_amyloidTwistEdit->text().toDouble(&okT);
    double noiseFrac = m_amyloidNoiseEdit->text().toDouble(&okNs);
    double persistLpUm = m_amyloidPersistEdit->text().toDouble(&okLp);
    double curveWaveVal = m_amyloidWaveEdit->text().toDouble(&okWv);
    double curveAmplVal = m_amyloidAmplEdit->text().toDouble(&okAm);
    bool addNoise    = m_amyloidNoiseBtn->isChecked();
    bool blackSignal = m_amyloidBlackSignal;
    if (!okR  || rise <= 0.0)     rise = 4.75;
    if (!okT)                     twistDeg = -1.0;
    if (!okNs || noiseFrac <= 0.0) noiseFrac = 0.3;
    if (!okLp || persistLpUm < 0.0) persistLpUm = 14.0;
    if (!okWv || curveWaveVal <= 0.0) curveWaveVal = 50.0;
    if (!okAm) curveAmplVal = 1.0;
    // Persistence length in pixels (1 px = 1 Å): μm → Å
    double persistLpPx = persistLpUm * 1e4;

    // ---- Get the selected 2D cross-section map ----
    int mapIdx = m_amyloidMapCombo->currentIndex();
    if (mapIdx < 0 || mapIdx >= HISTORY_SLOTS) return;
    if (!m_history[mapIdx].occupied || m_history[mapIdx].rawPixels.empty()) return;
    int mapW = m_history[mapIdx].image.width();
    int mapH = m_history[mapIdx].image.height();
    if (mapW <= 0 || mapH <= 0) return;
    // Copy map data and pixel size for the lambda
    std::vector<double> mapPixels = m_history[mapIdx].rawPixels;
    double mapPixSize = m_history[mapIdx].pixelSize;
    if (mapPixSize <= 0.0) mapPixSize = 1.0;

    // Subtract the mean grey value of the border pixels so the surrounding
    // background becomes (close to) zero. The cross-section typically shows
    // a bright structure on a ~uniform dark background, so we only want the
    // central structure contributing to the projection.
    {
        double edgeSum = 0.0;
        int    edgeCnt = 0;
        for (int x = 0; x < mapW; x++) {
            edgeSum += mapPixels[x];
            edgeSum += mapPixels[(mapH - 1) * mapW + x];
            edgeCnt += 2;
        }
        for (int y = 1; y < mapH - 1; y++) {
            edgeSum += mapPixels[y * mapW];
            edgeSum += mapPixels[y * mapW + (mapW - 1)];
            edgeCnt += 2;
        }
        double edgeMean = (edgeCnt > 0) ? edgeSum / edgeCnt : 0.0;
        for (auto &v : mapPixels) v -= edgeMean;
    }

    // Measure the radius of the central structure so we can skip the
    // (now essentially zero) background during the slab loop. Threshold
    // at 5 % of the peak absolute value; take the largest distance of
    // any above-threshold pixel from the map centre.
    double mapStructR = 0.0;
    {
        const double cx = mapW / 2.0;
        const double cy = mapH / 2.0;
        double peakAbs = 0.0;
        for (double v : mapPixels)
            if (std::fabs(v) > peakAbs) peakAbs = std::fabs(v);
        double thresh = 0.05 * peakAbs;
        double maxR2  = 0.0;
        for (int mj = 0; mj < mapH; mj++) {
            for (int mi = 0; mi < mapW; mi++) {
                if (std::fabs(mapPixels[mj * mapW + mi]) > thresh) {
                    double du = mi - cx, dv = mj - cy;
                    double r2 = du * du + dv * dv;
                    if (r2 > maxR2) maxR2 = r2;
                }
            }
        }
        mapStructR = std::sqrt(maxR2) + 1.0; // small margin in map pixels
    }

    storeUndoSnapshot();

    // ---- Create fresh output image at the size selected in the pulldown ----
    int outSz = m_amyloidSizeCombo->currentText().toInt();
    if (outSz <= 0) outSz = 1024;

    // Rescale existing filament control points from the current image size
    // (where the user drew them) to the newly-selected output size.
    int srcW = m_image.isNull() ? outSz : m_image.width();
    int srcH = m_image.isNull() ? outSz : m_image.height();
    double sx = (srcW > 0) ? (double)outSz / (double)srcW : 1.0;
    double sy = (srcH > 0) ? (double)outSz / (double)srcH : 1.0;
    if (sx != 1.0 || sy != 1.0) {
        for (auto &fil : m_amyloidFilaments) {
            for (auto &pt : fil.pts) pt = QPointF(pt.x() * sx, pt.y() * sy);
        }
    }

    m_image = QImage(outSz, outSz, QImage::Format_Grayscale8);
    m_image.fill(0);
    m_imageRawPixels.assign((size_t)outSz * outSz, 0.0);
    m_pixelSize = 1.0;  // 1 Å/pixel for the synthetic image

    if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS) {
        m_zoom[0].reset(outSz, outSz);
    }

    // ---- Enforce persistence length: relax control points so that the
    //      local curvature never exceeds 1/Lp anywhere along the trajectory.
    //      We iterate over all interior control points and, for every triplet
    //      (prev, current, next), compute the circumradius R.  If R < Lp the
    //      middle point is pushed toward the straight line connecting its
    //      neighbours until R = Lp.  Multiple passes ensure convergence when
    //      adjacent triplets interact. ----
    if (persistLpPx > 0.0) {
        const int maxIter = 50;
        for (auto &fil : m_amyloidFilaments) {
            int nPts = (int)fil.pts.size();
            if (nPts < 3) continue;
            for (int iter = 0; iter < maxIter; iter++) {
                bool changed = false;
                for (int j = 1; j < nPts - 1; j++) {
                    QPointF A = fil.pts[j - 1];
                    QPointF B = fil.pts[j];
                    QPointF C = fil.pts[j + 1];

                    // Circumradius of triangle ABC
                    double abx = B.x() - A.x(), aby = B.y() - A.y();
                    double bcx = C.x() - B.x(), bcy = C.y() - B.y();
                    double cross = abx * bcy - aby * bcx;
                    if (std::abs(cross) < 1e-9) continue; // collinear → infinite R

                    double ab = std::sqrt(abx * abx + aby * aby);
                    double bc = std::sqrt(bcx * bcx + bcy * bcy);
                    double cax = A.x() - C.x(), cay = A.y() - C.y();
                    double ca = std::sqrt(cax * cax + cay * cay);
                    double R = (ab * bc * ca) / (2.0 * std::abs(cross));

                    if (R >= persistLpPx) continue; // curvature OK

                    // Move B toward the midpoint of A–C to reduce curvature.
                    // The midpoint of A–C is the straight-line position (R → ∞).
                    QPointF mid((A.x() + C.x()) * 0.5, (A.y() + C.y()) * 0.5);
                    double halfChord = ca * 0.5;

                    // Maximum sagitta (perpendicular distance from mid to B)
                    // for a circle of radius Lp through A and C:
                    //   sagitta = Lp − sqrt(Lp² − halfChord²)
                    double disc = persistLpPx * persistLpPx - halfChord * halfChord;
                    double maxSag = (disc > 0.0)
                        ? (persistLpPx - std::sqrt(disc))
                        : persistLpPx;

                    double dx = B.x() - mid.x();
                    double dy = B.y() - mid.y();
                    double dist = std::sqrt(dx * dx + dy * dy);
                    if (dist <= maxSag) continue; // already within limit

                    // Clamp B along the mid→B direction at maxSag
                    double scale = maxSag / dist;
                    fil.pts[j] = QPointF(mid.x() + dx * scale,
                                         mid.y() + dy * scale);
                    changed = true;
                }
                if (!changed) break;
            }
        }
    }

    const double twistRad = twistDeg * M_PI / 180.0;

    m_toolProgress = 0.1;
    update();

    auto filaments = m_amyloidFilaments;

    chainSteps({
        [this, outSz, filaments, rise, twistRad,
         addNoise, noiseFrac, blackSignal,
         curveWaveVal, curveAmplVal,
         mapPixels, mapW, mapH, mapPixSize, mapStructR]() {

            // True helical fibril simulation:
            //
            // The selected 2D map provides the cross-section shape.
            // We place this cross-section (as a thin 3D slab) along each
            // fibril trajectory at helical-rise spacing, rotating each
            // successive copy by the helical twist around the trajectory
            // axis. The 3D result is then projected along the viewing
            // direction (Z) to produce the output 2D image.
            //
            // Coordinate system:
            //   Image plane = X,Y  (1024×1024 at 1 Å/pixel)
            //   Z = perpendicular to image, i.e. the viewing/projection axis
            //
            // At each subunit position along the trajectory:
            //   T = unit tangent along the trajectory (in the XY plane)
            //   N = in-plane normal (-Ty, Tx)
            //   Z-hat = out-of-plane axis (0,0,1)
            //
            // The cross-section slab sits in the plane spanned by N and Z-hat,
            // perpendicular to the trajectory axis T.
            //
            // Helical twist rotates the cross-section around T.
            // After rotation, the cross-section voxel at local (u,v) maps to:
            //   u' = u·cos(φ) - v·sin(φ)   (along N direction in 3D)
            //   v' = u·sin(φ) + v·cos(φ)   (along Z direction)
            //
            // Projection sums along Z, so image position = pos + u'·N
            // Use the source map at its native resolution.
            // Each source pixel (mi, mj) has physical offset from map centre:
            //   u = (mi - mapW/2) * mapPixSize   (Å, along in-plane normal)
            //   v = (mj - mapH/2) * mapPixSize   (Å, along Z / viewing axis)
            // After helical rotation and projection, u' maps to image coords
            // and v' is depth-clipped by fibrilWidth.
            const double mapCX = mapW / 2.0;  // centre of source map (pixels)
            const double mapCY = mapH / 2.0;

            // Slab thickness along the trajectory axis: Gaussian distributed
            // over 11 pixel planes (offsets -5..+5, i.e. 10 pixel total
            // thickness) with FWHM = 2.5 pixels.
            // sigma = FWHM / (2*sqrt(2*ln 2)) = FWHM / 2.3548
            const int    nSlabSteps  = 11;
            const double slabFWHM    = 2.5; // pixels
            const double slabSigma   = slabFWHM / 2.3548200450309493;
            // Chess-board waviness of the slab: displaces the thickness
            // coordinate (slab-local T axis) by the product of sine waves
            // along the slab's two lateral axes (pre-rotation u and v).
            // waveShift = A * sin(2*pi*u/L) * sin(2*pi*v/L).
            // u,v are in Å; output image is 1 Å/px, so amplitude in Å
            // equals amplitude in pixels for that image.
            const double curveAmpl   = curveAmplVal; // pixels
            const double curveWave   = curveWaveVal; // pixels
            double slabWeights[nSlabSteps];
            double slabWSum = 0.0;
            for (int sl = 0; sl < nSlabSteps; sl++) {
                double k = sl - (nSlabSteps - 1) / 2.0;  // -5,-4,...,0,...,+5 for nSlabSteps = 11
                slabWeights[sl] = std::exp(-(k * k) / (2.0 * slabSigma * slabSigma));
                slabWSum += slabWeights[sl];
            }
            for (int sl = 0; sl < nSlabSteps; sl++) slabWeights[sl] /= slabWSum;

            for (const auto &fil : filaments) {
                if (fil.pts.size() < 2) continue;
                int nCP = (int)fil.pts.size();

                // ---- Build arc-length parameterised Catmull-Rom spline ----
                const int samplesPerSeg = 50;
                struct PathSample { double x, y, s; };
                std::vector<PathSample> path;

                auto catmullRom = [&](int seg, double t) -> QPointF {
                    QPointF p0 = fil.pts[std::max(0, seg - 1)];
                    QPointF p1 = fil.pts[seg];
                    QPointF p2 = fil.pts[seg + 1];
                    QPointF p3 = fil.pts[std::min(nCP - 1, seg + 2)];
                    double t2 = t * t, t3 = t2 * t;
                    double c0 = -0.5*t3 + t2 - 0.5*t;
                    double c1 =  1.5*t3 - 2.5*t2 + 1.0;
                    double c2 = -1.5*t3 + 2.0*t2 + 0.5*t;
                    double c3 =  0.5*t3 - 0.5*t2;
                    return QPointF(c0*p0.x() + c1*p1.x() + c2*p2.x() + c3*p3.x(),
                                   c0*p0.y() + c1*p1.y() + c2*p2.y() + c3*p3.y());
                };

                double cumLen = 0.0;
                QPointF prev(fil.pts[0].x(), fil.pts[0].y());
                path.push_back({prev.x(), prev.y(), 0.0});
                for (int seg = 0; seg < nCP - 1; seg++) {
                    for (int step = 1; step <= samplesPerSeg; step++) {
                        double t = step / (double)samplesPerSeg;
                        QPointF cur = catmullRom(seg, t);
                        double dx = cur.x() - prev.x();
                        double dy = cur.y() - prev.y();
                        cumLen += std::sqrt(dx * dx + dy * dy);
                        path.push_back({cur.x(), cur.y(), cumLen});
                        prev = cur;
                    }
                }
                double totalLen = cumLen;
                if (totalLen < 1.0) continue;

                // Evaluate spline path at arc-length s → (position, tangent)
                auto evalPath = [&](double s) -> std::pair<QPointF, QPointF> {
                    s = std::max(0.0, std::min(totalLen, s));
                    int lo = 0, hi = (int)path.size() - 1;
                    while (lo + 1 < hi) {
                        int mid = (lo + hi) / 2;
                        if (path[mid].s <= s) lo = mid; else hi = mid;
                    }
                    double segLen = path[hi].s - path[lo].s;
                    double frac = (segLen > 1e-9) ? (s - path[lo].s) / segLen : 0.0;
                    QPointF pos(path[lo].x + frac * (path[hi].x - path[lo].x),
                                path[lo].y + frac * (path[hi].y - path[lo].y));
                    int il = std::max(0, lo - 1);
                    int ih = std::min((int)path.size() - 1, hi + 1);
                    QPointF tang(path[ih].x - path[il].x, path[ih].y - path[il].y);
                    double tl = std::sqrt(tang.x() * tang.x() + tang.y() * tang.y());
                    if (tl > 0) tang /= tl;
                    return {pos, tang};
                };

                // ---- Place cross-section slabs along the trajectory ----
                int nLayers = (int)(totalLen / rise) + 1;
                for (int n = 0; n < nLayers; n++) {
                    double s = n * rise;
                    if (s > totalLen) break;

                    auto [pos, tang] = evalPath(s);
                    QPointF norm(-tang.y(), tang.x());

                    double phi = n * twistRad;
                    double cosPhi = std::cos(phi);
                    double sinPhi = std::sin(phi);

                    // Restrict the inner loop to pixels within the measured
                    // structure radius – everything further out is background.
                    const double mapR2 = mapStructR * mapStructR;
                    int miLo = std::max(0,       (int)std::floor(mapCX - mapStructR));
                    int miHi = std::min(mapW - 1, (int)std::ceil (mapCX + mapStructR));
                    int mjLo = std::max(0,       (int)std::floor(mapCY - mapStructR));
                    int mjHi = std::min(mapH - 1, (int)std::ceil (mapCY + mapStructR));

                    // For each pixel in the source map, compute its physical
                    // offset from centre, apply helical twist, and project.
                    for (int mj = mjLo; mj <= mjHi; mj++) {
                        for (int mi = miLo; mi <= miHi; mi++) {
                            double du = mi - mapCX, dv = mj - mapCY;
                            if (du * du + dv * dv > mapR2) continue;

                            double val = mapPixels[mj * mapW + mi];
                            if (std::fabs(val) < 1e-15) continue;

                            // Physical offset from map centre (Å)
                            double u = du * mapPixSize;
                            double v = dv * mapPixSize;

                            // Pre-rotation waviness: modify the slab in its
                            // own local (u, v) frame with a product of sine
                            // waves, producing a chess-board displacement of
                            // the thickness coordinate. Twist rotates around
                            // T, so this T-displacement is preserved.
                            double curveDs = curveAmpl
                                * std::sin(2.0 * M_PI * u / curveWave)
                                * std::sin(2.0 * M_PI * v / curveWave);

                            // Apply helical twist rotation around tangent axis
                            double uRot = u * cosPhi - v * sinPhi;
                            double vRot = u * sinPhi + v * cosPhi;

                            // Splat Gaussian-weighted slab planes along the tangent
                            // using bilinear interpolation for sub-pixel placement.
                            for (int sl = 0; sl < nSlabSteps; sl++) {
                                double k = sl - (nSlabSteps - 1) / 2.0;  // -5..+5 pixels
                                double ds = k * mapPixSize + curveDs;
                                double imgX = pos.x() + uRot * norm.x() + ds * tang.x();
                                double imgY = pos.y() + uRot * norm.y() + ds * tang.y();

                                int x0 = (int)std::floor(imgX);
                                int y0 = (int)std::floor(imgY);
                                double fx = imgX - x0;
                                double fy = imgY - y0;
                                double w00 = (1.0 - fx) * (1.0 - fy);
                                double w10 = fx         * (1.0 - fy);
                                double w01 = (1.0 - fx) * fy;
                                double w11 = fx         * fy;
                                double contrib = val * slabWeights[sl];
                                if (x0 >= 0 && x0 < outSz && y0 >= 0 && y0 < outSz)
                                    m_imageRawPixels[y0 * outSz + x0] += contrib * w00;
                                if (x0 + 1 >= 0 && x0 + 1 < outSz && y0 >= 0 && y0 < outSz)
                                    m_imageRawPixels[y0 * outSz + (x0 + 1)] += contrib * w10;
                                if (x0 >= 0 && x0 < outSz && y0 + 1 >= 0 && y0 + 1 < outSz)
                                    m_imageRawPixels[(y0 + 1) * outSz + x0] += contrib * w01;
                                if (x0 + 1 >= 0 && x0 + 1 < outSz && y0 + 1 >= 0 && y0 + 1 < outSz)
                                    m_imageRawPixels[(y0 + 1) * outSz + (x0 + 1)] += contrib * w11;
                            }
                        }
                    }
                }
            }

            // Find peak signal for noise scaling
            double peakSig = 0.0;
            for (double v : m_imageRawPixels)
                if (std::fabs(v) > peakSig) peakSig = std::fabs(v);

            // Add Gaussian noise
            if (addNoise && peakSig > 0.0) {
                double noiseSigma = noiseFrac * peakSig;
                std::mt19937 rng(42);
                std::normal_distribution<double> dist(0.0, noiseSigma);
                for (int i = 0; i < outSz * outSz; i++)
                    m_imageRawPixels[i] += dist(rng);
            }

            // Black signal: negate
            if (blackSignal) {
                double curMin = m_imageRawPixels[0];
                double curMax = m_imageRawPixels[0];
                for (double v : m_imageRawPixels) {
                    if (v < curMin) curMin = v;
                    if (v > curMax) curMax = v;
                }
                double sum = curMin + curMax;
                for (int i = 0; i < outSz * outSz; i++)
                    m_imageRawPixels[i] = sum - m_imageRawPixels[i];
            }

            // Recompute min/max
            m_imageMinVal = m_imageRawPixels[0];
            m_imageMaxVal = m_imageRawPixels[0];
            for (double v : m_imageRawPixels) {
                if (v < m_imageMinVal) m_imageMinVal = v;
                if (v > m_imageMaxVal) m_imageMaxVal = v;
            }
            if (!m_imageContrastLocked) {
                m_imageDispMin = m_imageMinVal;
                m_imageDispMax = m_imageMaxVal;
            }

            rebuildImageFromRaw();
            m_toolProgress = 0.5;
        },
        [this]() {
            if (m_ftComputed)
                computeFFT();

            if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS) {
                m_history[m_activeSlot].image     = m_image;
                m_history[m_activeSlot].rawPixels = m_imageRawPixels;
                m_history[m_activeSlot].minVal    = m_imageMinVal;
                m_history[m_activeSlot].maxVal    = m_imageMaxVal;
                m_history[m_activeSlot].pixelSize = m_pixelSize;
                m_history[m_activeSlot].occupied  = true;
                if (m_ftComputed)
                    m_history[m_activeSlot].powerSpecImg = computePowerSpecMasked(m_image);
            }

            m_amyloidRendered = true;

            saveHistory();
#ifndef __EMSCRIPTEN__
            {
                QSettings settings("ft", "ft");
                settings.setValue("amyloidRise",       m_amyloidRiseEdit->text());
                settings.setValue("amyloidTwist",      m_amyloidTwistEdit->text());
                settings.setValue("amyloidMapIdx",     m_amyloidMapCombo->currentIndex());
                settings.setValue("amyloidSizeIdx",    m_amyloidSizeCombo->currentIndex());
                settings.setValue("amyloidNoise",      m_amyloidNoiseBtn->isChecked());
                settings.setValue("amyloidNoiseSigma", m_amyloidNoiseEdit->text());
                settings.setValue("amyloidWave",       m_amyloidWaveEdit->text());
                settings.setValue("amyloidAmpl",       m_amyloidAmplEdit->text());
                settings.setValue("amyloidBlackSignal", m_amyloidBlackSignal);
            }
#endif
            m_toolProgress = -1;
            update();
        }
    });
}
