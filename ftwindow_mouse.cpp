#include "ftwindow_common.h"

// ---------------------------------------------------------------------------
//  Mouse
// ---------------------------------------------------------------------------
void FtWindow::mousePressEvent(QMouseEvent *event)
{
    // Check history slot clicks (panel 3)
    for (int i = 0; i < HISTORY_SLOTS; i++) {
        if (m_history[i].occupied && m_historyRects[i].contains(event->pos())) {
            HistoryEntry clicked = std::move(m_history[i]);

            for (int j = i; j < HISTORY_SLOTS - 1; j++)
                m_history[j] = std::move(m_history[j + 1]);
            m_history[HISTORY_SLOTS - 1].occupied = false;

            if (!m_image.isNull()) {
                for (int j = HISTORY_SLOTS - 1; j > 0; j--)
                    m_history[j] = std::move(m_history[j - 1]);
                m_history[0].image        = m_image;
                m_history[0].path         = m_imagePath;
                m_history[0].rawPixels    = m_imageRawPixels;
                m_history[0].minVal       = m_imageMinVal;
                m_history[0].maxVal       = m_imageMaxVal;
                m_history[0].pixelSize    = m_pixelSize;
                m_history[0].powerSpecImg = computePowerSpecMasked(m_image);
                m_history[0].occupied     = true;
            }

            m_image          = clicked.image;
            m_imagePath      = clicked.path;
            m_imageRawPixels = std::move(clicked.rawPixels);
            m_imageMinVal    = clicked.minVal;
            m_imageMaxVal    = clicked.maxVal;
            m_pixelSize      = clicked.pixelSize;

            m_ftComputed = false;
            m_displayMode = 2;
            m_modeBtn->setText(modeLabel());
            m_modeBtn->hide();
            m_maskBtn->hide();
            m_maskBtn->setChecked(false);
            m_maskCenter = false;

            if (!m_image.isNull()) {
                m_zoom[0].reset(m_image.width(), m_image.height());
                computeFFT();
            }

            saveHistory();
            QSettings settings("ft", "ft");
            settings.setValue("lastFile", m_imagePath);
            update();
            return;
        }
    }

    // Check panel 1 tool button clicks (left edge)
    if (m_p1BtnRects[0].contains(event->pos()) && !m_image.isNull()) {
        m_image = m_image.flipped(Qt::Horizontal);
        extractImageData();
        if (m_ftComputed) computeFFT();
        update();
        return;
    }
    if (m_p1BtnRects[1].contains(event->pos()) && !m_image.isNull()) {
        m_image = m_image.flipped(Qt::Vertical);
        extractImageData();
        if (m_ftComputed) computeFFT();
        update();
        return;
    }
    if (m_p1BtnRects[2].contains(event->pos())) {
        m_shiftActive = !m_shiftActive;
        if (m_shiftActive) m_rotateActive = false;
        update();
        return;
    }
    if (m_p1BtnRects[3].contains(event->pos())) {
        m_rotateActive = !m_rotateActive;
        if (m_rotateActive) m_shiftActive = false;
        update();
        return;
    }
    if (m_p1BtnRects[4].contains(event->pos())) {
        m_binActive = !m_binActive;
        m_binCombo->setVisible(m_binActive);
        m_binKeepSizeBtn->setVisible(m_binActive);
        m_applyBinBtn->setVisible(m_binActive);
        update();
        return;
    }

    // Shift/rotate: start drag on panel 1 image
    if ((m_shiftActive || m_rotateActive) && !m_image.isNull()) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (di.valid && di.zoomIdx == 0 && di.screenRect.contains(event->pos())) {
                m_p1Dragging = true;
                m_p1DragStart = event->pos();
                return;
            }
        }
    }

    // Check tool button clicks (panel 2 right edge)
    auto deactivateAllTools = [&]() {
        m_eraserActive = false; m_brushActive = false;
        m_bandpassActive = false; m_directionalActive = false;
        m_latticeActive = false; m_ftRotateActive = false;
    };
    auto showToolWidgets = [&]() {
        bool showFilter = m_bandpassActive || m_directionalActive;
        m_smoothEdit->setVisible(showFilter);
        m_bandEraseOutside->setVisible(showFilter);
        m_applyBandBtn->setVisible(showFilter);

        m_brushValueLabel->setVisible(m_brushActive);
        m_brushValueEdit->setVisible(m_brushActive);
        m_brushDiamLabel->setVisible(m_brushActive);
        m_brushDiameterEdit->setVisible(m_brushActive);

        m_eraserDiamLabel->setVisible(m_eraserActive);
        m_eraserDiameterEdit->setVisible(m_eraserActive);

        m_latticeSmoothLabel->setVisible(m_latticeActive);
        m_latticeSmoothEdit->setVisible(m_latticeActive);
        m_latticeDotDiamLabel->setVisible(m_latticeActive);
        m_latticeDotDiamEdit->setVisible(m_latticeActive);
        m_latticeEraseOutside->setVisible(m_latticeActive);
        m_latticeApplyBtn->setVisible(m_latticeActive);
    };
    if (m_toolBtnRects[0].contains(event->pos())) {
        bool was = m_eraserActive; deactivateAllTools();
        m_eraserActive = !was; showToolWidgets(); update(); return;
    }
    if (m_toolBtnRects[1].contains(event->pos())) {
        bool was = m_brushActive; deactivateAllTools();
        m_brushActive = !was;
        if (m_brushActive && m_ftComputed) {
            double bv = brushValue();
            m_brushValueEdit->setText(bv > 0 ? QString::number(bv, 'g', 5) : "1");
        }
        showToolWidgets(); update(); return;
    }
    if (m_toolBtnRects[2].contains(event->pos())) {
        bool was = m_bandpassActive; deactivateAllTools();
        m_bandpassActive = !was; showToolWidgets(); update(); return;
    }
    if (m_toolBtnRects[3].contains(event->pos())) {
        bool was = m_directionalActive; deactivateAllTools();
        m_directionalActive = !was; showToolWidgets(); update(); return;
    }
    if (m_toolBtnRects[4].contains(event->pos())) {
        bool was = m_latticeActive; deactivateAllTools();
        m_latticeActive = !was; showToolWidgets(); update(); return;
    }
    if (m_toolBtnRects[5].contains(event->pos())) {
        bool was = m_ftRotateActive; deactivateAllTools();
        m_ftRotateActive = !was; showToolWidgets(); update(); return;
    }

    // Lattice vector drag
    if (m_latticeActive && m_ftComputed && m_fftN > 0) {
        double halfN = m_fftN / 2.0;
        double imgCenter = halfN + 0.5;
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (!di.valid || di.zoomIdx < 1) continue;
            if (!di.screenRect.contains(event->pos())) continue;

            ZoomState &z = m_zoom[di.zoomIdx];
            QRectF src = z.visibleRect(di.imgW, di.imgH);
            double imgX = src.x() + (event->pos().x() - di.screenRect.x())
                          / (double)di.screenRect.width() * src.width();
            double imgY = src.y() + (event->pos().y() - di.screenRect.y())
                          / (double)di.screenRect.height() * src.height();
            double dx = imgX - imgCenter;
            double dy = imgY - imgCenter;

            double tol = 5.0 * src.width() / di.screenRect.width();
            double distU = std::sqrt((dx - m_latticeUx) * (dx - m_latticeUx)
                                   + (dy - m_latticeUy) * (dy - m_latticeUy));
            double distV = std::sqrt((dx - m_latticeVx) * (dx - m_latticeVx)
                                   + (dy - m_latticeVy) * (dy - m_latticeVy));

            if (distU < tol && distU <= distV) {
                m_latticeDragging = 1; m_toolDragging = true; return;
            }
            if (distV < tol) {
                m_latticeDragging = 2; m_toolDragging = true; return;
            }
            break;
        }
    }

    // FT rotate: start drag on panel 2 FFT
    if (m_ftRotateActive && m_ftComputed) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (di.valid && di.zoomIdx >= 1 && di.screenRect.contains(event->pos())) {
                m_p2Dragging = true;
                m_p2DragStart = event->pos();
                return;
            }
        }
    }

    // Bandpass ring drag
    if (m_bandpassActive && m_ftComputed && m_fftN > 0) {
        double halfN = m_fftN / 2.0;
        double imgCenter = halfN + 0.5;
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (!di.valid || di.zoomIdx < 1) continue;
            if (!di.screenRect.contains(event->pos())) continue;

            ZoomState &z = m_zoom[di.zoomIdx];
            QRectF src = z.visibleRect(di.imgW, di.imgH);
            double imgX = src.x() + (event->pos().x() - di.screenRect.x())
                          / (double)di.screenRect.width() * src.width();
            double imgY = src.y() + (event->pos().y() - di.screenRect.y())
                          / (double)di.screenRect.height() * src.height();
            double dx = (imgX - imgCenter) / halfN;
            double dy = (imgY - imgCenter) / halfN;
            double dist = std::sqrt(dx * dx + dy * dy);

            double tol = 3.0 * src.width() / (di.screenRect.width() * halfN);
            tol = std::max(tol, 0.02);
            if (std::abs(dist - m_bandInnerR) < tol) {
                m_bandDragging = 1; m_toolDragging = true; return;
            }
            if (std::abs(dist - m_bandOuterR) < tol) {
                m_bandDragging = 2; m_toolDragging = true; return;
            }
            break;
        }
    }

    // Directional wedge drag
    if (m_directionalActive && m_ftComputed && m_fftN > 0) {
        double halfN = m_fftN / 2.0;
        double imgCenter = halfN + 0.5;
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (!di.valid || di.zoomIdx < 1) continue;
            if (!di.screenRect.contains(event->pos())) continue;

            ZoomState &z = m_zoom[di.zoomIdx];
            QRectF src = z.visibleRect(di.imgW, di.imgH);
            double imgX = src.x() + (event->pos().x() - di.screenRect.x())
                          / (double)di.screenRect.width() * src.width();
            double imgY = src.y() + (event->pos().y() - di.screenRect.y())
                          / (double)di.screenRect.height() * src.height();
            double angle = std::atan2(imgY - imgCenter, imgX - imgCenter) * 180.0 / M_PI;

            auto angleDiff = [](double a, double b) {
                double d = a - b;
                while (d > 180) d -= 360;
                while (d < -180) d += 360;
                return std::abs(d);
            };
            double tol = 4.0;
            if (angleDiff(angle, m_dirAngle1) < tol || angleDiff(angle, m_dirAngle1 + 180) < tol) {
                m_dirDragging = 1; m_toolDragging = true; return;
            }
            if (angleDiff(angle, m_dirAngle2) < tol || angleDiff(angle, m_dirAngle2 + 180) < tol) {
                m_dirDragging = 2; m_toolDragging = true; return;
            }
            break;
        }
    }

    // Eraser / Brush
    if ((m_eraserActive || m_brushActive) && m_ftComputed) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (di.valid && di.zoomIdx >= 1 && di.screenRect.contains(event->pos())) {
                if (m_eraserActive) eraserApply(event->pos());
                else                brushApply(event->pos());
                m_toolDragging = true;
                return;
            }
        }
    }

    // FT arrow
    if (!m_image.isNull()) {
        QRect arrowRect = upperArrowBounds();
        if (arrowRect.contains(event->pos())) {
            computeFFT();
            update();
            return;
        }
    }

    // FT⁻¹ arrow
    if (m_ftComputed) {
        QRect iftRect = lowerArrowBounds();
        if (iftRect.contains(event->pos())) {
            computeInverseFFT();
            update();
            return;
        }
    }
}

void FtWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_toolDragging) {
        bool wasPainting = (m_eraserActive || m_brushActive)
                           && m_bandDragging == 0 && m_dirDragging == 0
                           && m_latticeDragging == 0;
        m_toolDragging = false;
        m_bandDragging = 0;
        m_dirDragging = 0;
        m_latticeDragging = 0;
        if (wasPainting && m_ftComputed) {
            computeInverseFFT();
            update();
        }
    }

    if (m_p2Dragging && m_ftComputed && m_fftN > 0) {
        m_p2Dragging = false;

        // Find the panel 2 display item to compute rotation angle
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (!di.valid || di.zoomIdx < 1) continue;

            QRect sr = di.screenRect;
            double cx = sr.center().x(), cy = sr.center().y();
            double a1 = std::atan2(m_p2DragStart.y() - cy, m_p2DragStart.x() - cx);
            double a2 = std::atan2(event->pos().y() - cy, event->pos().x() - cx);
            double angleDeg = (a2 - a1) * 180.0 / M_PI;
            double angleRad = angleDeg * M_PI / 180.0;

            int N = m_fftN;
            int halfN = N / 2;
            double cosA = std::cos(-angleRad);   // inverse rotation
            double sinA = std::sin(-angleRad);
            double nyquistR = halfN;
            double edgeW = 10.0;
            double innerR = nyquistR - edgeW;

            // ---- Step 1: inverse FFT to get real-space image ----
            std::vector<Complex> freq(m_fftData);
            fftShift(freq, N);            // un-shift DC to [0,0]
            fft2d(freq, N, true);         // inverse FFT → real space

            // ---- Step 2: circular mask with cosine edge ----
            {
                double sum = 0;
                for (int i = 0; i < N * N; i++) sum += freq[i].real();
                double avg = sum / (N * N);

                double mcx = N / 2.0, mcy = N / 2.0;
                double maxR = N / 2.0;
                double maskEdge = 10.0;
                double maskInner = maxR - maskEdge;

                for (int y = 0; y < N; y++) {
                    for (int x = 0; x < N; x++) {
                        double dx = x - mcx, dy = y - mcy;
                        double dist = std::sqrt(dx * dx + dy * dy);
                        if (dist >= maxR) {
                            freq[y * N + x] = Complex(avg, 0);
                        } else if (dist > maskInner) {
                            double t = (dist - maskInner) / maskEdge;
                            double fade = 0.5 * (1.0 + std::cos(t * M_PI));
                            double v = freq[y * N + x].real();
                            freq[y * N + x] = Complex(v * fade + avg * (1.0 - fade), 0);
                        }
                    }
                }
            }

            // ---- Step 3: FFT back to Fourier space (unshifted: DC at [0,0]) ----
            fft2d(freq, N, false);

            // ---- Step 4: multiply by (-1)^(u+v) to shift real-space origin
            //              to image center. This is a phase ramp, NOT fftShift. ----
            // DFT shift theorem: multiplying F(u,v) by (-1)^(u+v) = e^{jπ(u+v)}
            // circularly shifts the spatial image by (N/2, N/2), moving the
            // content at the image center to the DFT origin (0,0).
            for (int v = 0; v < N; v++)
                for (int u = 0; u < N; u++)
                    if ((u + v) & 1)
                        freq[v * N + u] = -freq[v * N + u];

            // ---- Step 5: rotate in unshifted domain using signed frequencies ----
            // DC is at index [0,0]. Rotation around frequency origin =
            // rotation around spatial origin, which is now the image center.
            std::vector<Complex> rotated(N * N, Complex(0, 0));
            for (int v = 0; v < N; v++) {
                for (int u = 0; u < N; u++) {
                    // Friedel: process each pair only once
                    int mu = (N - u) % N;
                    int mv = (N - v) % N;
                    if (mv > v || (mv == v && mu > u)) continue;

                    // Signed frequency
                    double us = (u <= halfN) ? (double)u : (double)(u - N);
                    double vs = (v <= halfN) ? (double)v : (double)(v - N);

                    // Nyquist low-pass
                    double dist = std::sqrt(us * us + vs * vs);
                    double lp = 1.0;
                    if (dist >= nyquistR)
                        lp = 0.0;
                    else if (dist > innerR) {
                        double t = (dist - innerR) / edgeW;
                        lp = 0.5 * (1.0 + std::cos(t * M_PI));
                    }

                    // Inverse-rotate source frequency
                    double uSrc = us * cosA - vs * sinA;
                    double vSrc = us * sinA + vs * cosA;

                    // Nearest-neighbor with periodic wrapping
                    int su = ((int)std::round(uSrc) % N + N) % N;
                    int sv = ((int)std::round(vSrc) % N + N) % N;
                    Complex val = freq[sv * N + su] * lp;

                    rotated[v * N + u] = val;
                    if (mu != u || mv != v)
                        rotated[mv * N + mu] = std::conj(val);
                }
            }

            // ---- Step 6: multiply by (-1)^(u+v) to undo the shift ----
            for (int v = 0; v < N; v++)
                for (int u = 0; u < N; u++)
                    if ((u + v) & 1)
                        rotated[v * N + u] = -rotated[v * N + u];

            // ---- Convert to centered layout for m_fftData storage ----
            fftShift(rotated, N);
            m_fftData = std::move(rotated);
            recomputeDisplayImages();
            computeInverseFFT();
            update();
            break;
        }
    }

    if (m_p1Dragging && !m_image.isNull()) {
        m_p1Dragging = false;

        // Find the panel 1 display item to convert screen delta to pixel delta
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (!di.valid || di.zoomIdx != 0) continue;

            QRectF src = m_zoom[0].visibleRect(di.imgW, di.imgH);
            double dxPx = (event->pos().x() - m_p1DragStart.x())
                          / (double)di.screenRect.width() * src.width();
            double dyPx = (event->pos().y() - m_p1DragStart.y())
                          / (double)di.screenRect.height() * src.height();

            if (m_shiftActive) {
                // Shift with periodic boundary conditions
                int shiftX = -(int)std::round(dxPx);
                int shiftY = -(int)std::round(dyPx);
                int w = m_image.width(), h = m_image.height();
                QImage shifted(w, h, m_image.format());
                for (int y = 0; y < h; y++) {
                    for (int x = 0; x < w; x++) {
                        int sx = ((x + shiftX) % w + w) % w;
                        int sy = ((y + shiftY) % h + h) % h;
                        shifted.setPixelColor(x, y, m_image.pixelColor(sx, sy));
                    }
                }
                m_image = shifted;
            } else if (m_rotateActive) {
                // Compute rotation angle from drag
                QRect sr = di.screenRect;
                double cx = sr.center().x(), cy = sr.center().y();
                double a1 = std::atan2(m_p1DragStart.y() - cy, m_p1DragStart.x() - cx);
                double a2 = std::atan2(event->pos().y() - cy, event->pos().x() - cx);
                double angleDeg = (a2 - a1) * 180.0 / M_PI;

                // Compute average grey for fill
                QImage gray = m_image.convertToFormat(QImage::Format_Grayscale8);
                int w = gray.width(), h = gray.height();
                double sum = 0;
                for (int y = 0; y < h; y++) {
                    const uchar *row = gray.constScanLine(y);
                    for (int x = 0; x < w; x++) sum += row[x];
                }
                int avg = (int)(sum / (w * h));

                QImage rotated(w, h, m_image.format());
                rotated.fill(QColor(avg, avg, avg));
                QPainter rp(&rotated);
                rp.setRenderHint(QPainter::SmoothPixmapTransform, true);
                rp.translate(w / 2.0, h / 2.0);
                rp.rotate(angleDeg);
                rp.translate(-w / 2.0, -h / 2.0);
                rp.drawImage(0, 0, m_image);
                rp.end();

                // Apply circular mask with 10px cosine edge
                {
                    QImage masked = rotated.convertToFormat(QImage::Format_Grayscale8);
                    double mcx = w / 2.0, mcy = h / 2.0;
                    double maxR = std::min(w, h) / 2.0;
                    double edgeW = 10.0;
                    double innerR = maxR - edgeW;
                    for (int y = 0; y < h; y++) {
                        uchar *row = masked.scanLine(y);
                        for (int x = 0; x < w; x++) {
                            double dist = std::sqrt((x - mcx) * (x - mcx) + (y - mcy) * (y - mcy));
                            if (dist >= maxR) {
                                row[x] = (uchar)avg;
                            } else if (dist > innerR) {
                                double t = (dist - innerR) / edgeW;
                                double fade = 0.5 * (1.0 + std::cos(t * M_PI));
                                row[x] = (uchar)std::clamp((int)(row[x] * fade + avg * (1.0 - fade)), 0, 255);
                            }
                        }
                    }
                    rotated = masked;
                }

                m_image = rotated;
            }

            extractImageData();
            if (m_ftComputed) computeFFT();
            update();
            break;
        }
    }
}

void FtWindow::mouseMoveEvent(QMouseEvent *event)
{
    m_mousePos = event->pos();

    if (m_toolDragging && m_ftComputed) {
        if (m_bandDragging > 0 && m_fftN > 0) {
            double halfN = m_fftN / 2.0;
            double imgCenter = halfN + 0.5;
            for (int i = 0; i < m_numDispItems; i++) {
                const DisplayItem &di = m_dispItems[i];
                if (!di.valid || di.zoomIdx < 1) continue;
                ZoomState &z = m_zoom[di.zoomIdx];
                QRectF src = z.visibleRect(di.imgW, di.imgH);
                double imgX = src.x() + (event->pos().x() - di.screenRect.x())
                              / (double)di.screenRect.width() * src.width();
                double imgY = src.y() + (event->pos().y() - di.screenRect.y())
                              / (double)di.screenRect.height() * src.height();
                double dx = (imgX - imgCenter) / halfN;
                double dy = (imgY - imgCenter) / halfN;
                double dist = std::clamp(std::sqrt(dx * dx + dy * dy), 0.01, 1.42);
                if (m_bandDragging == 1)
                    m_bandInnerR = std::min(dist, m_bandOuterR - 0.02);
                else
                    m_bandOuterR = std::max(dist, m_bandInnerR + 0.02);
                update();
                break;
            }
            return;
        }
        if (m_dirDragging > 0 && m_fftN > 0) {
            double halfN = m_fftN / 2.0;
            double imgCenter = halfN + 0.5;
            for (int i = 0; i < m_numDispItems; i++) {
                const DisplayItem &di = m_dispItems[i];
                if (!di.valid || di.zoomIdx < 1) continue;
                ZoomState &z = m_zoom[di.zoomIdx];
                QRectF src = z.visibleRect(di.imgW, di.imgH);
                double imgX = src.x() + (event->pos().x() - di.screenRect.x())
                              / (double)di.screenRect.width() * src.width();
                double imgY = src.y() + (event->pos().y() - di.screenRect.y())
                              / (double)di.screenRect.height() * src.height();
                double angle = std::atan2(imgY - imgCenter, imgX - imgCenter) * 180.0 / M_PI;
                if (m_dirDragging == 1) m_dirAngle1 = angle;
                else                    m_dirAngle2 = angle;
                update();
                break;
            }
            return;
        }
        if (m_latticeDragging > 0 && m_fftN > 0) {
            double halfN = m_fftN / 2.0;
            double imgCenter = halfN + 0.5;
            for (int i = 0; i < m_numDispItems; i++) {
                const DisplayItem &di = m_dispItems[i];
                if (!di.valid || di.zoomIdx < 1) continue;
                ZoomState &z = m_zoom[di.zoomIdx];
                QRectF src = z.visibleRect(di.imgW, di.imgH);
                double imgX = src.x() + (event->pos().x() - di.screenRect.x())
                              / (double)di.screenRect.width() * src.width();
                double imgY = src.y() + (event->pos().y() - di.screenRect.y())
                              / (double)di.screenRect.height() * src.height();
                if (m_latticeDragging == 1) {
                    m_latticeUx = imgX - imgCenter;
                    m_latticeUy = imgY - imgCenter;
                } else {
                    m_latticeVx = imgX - imgCenter;
                    m_latticeVy = imgY - imgCenter;
                }
                update();
                break;
            }
            return;
        }
        if (m_eraserActive) eraserApply(event->pos());
        else if (m_brushActive) brushApply(event->pos());
        return;
    }

    update();
}

// ---------------------------------------------------------------------------
//  Wheel – zoom
// ---------------------------------------------------------------------------
void FtWindow::wheelEvent(QWheelEvent *event)
{
    QPoint pos = event->position().toPoint();

    for (int i = 0; i < m_numDispItems; i++) {
        const DisplayItem &di = m_dispItems[i];
        if (!di.valid || di.zoomIdx < 0) continue;
        if (!di.screenRect.contains(pos)) continue;

        ZoomState &z = m_zoom[di.zoomIdx];

        double step = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        double newFactor = z.factor * step;
        if (newFactor < 1.0) newFactor = 1.0;
        if (newFactor > 64.0) newFactor = 64.0;

        if (newFactor <= 1.0) {
            z.reset(di.imgW, di.imgH);
        } else {
            QRectF src = z.visibleRect(di.imgW, di.imgH);
            double relX = (pos.x() - di.screenRect.x()) / (double)di.screenRect.width();
            double relY = (pos.y() - di.screenRect.y()) / (double)di.screenRect.height();
            double imgX = src.x() + relX * src.width();
            double imgY = src.y() + relY * src.height();

            z.factor = newFactor;
            double newVW = di.imgW / newFactor;
            double newVH = di.imgH / newFactor;
            z.centerX = imgX + (0.5 - relX) * newVW;
            z.centerY = imgY + (0.5 - relY) * newVH;

            double hw = newVW / 2.0, hh = newVH / 2.0;
            z.centerX = std::clamp(z.centerX, hw, di.imgW - hw);
            z.centerY = std::clamp(z.centerY, hh, di.imgH - hh);
        }

        if ((m_displayMode == 0 || m_displayMode == 1) &&
            (di.zoomIdx == 1 || di.zoomIdx == 2)) {
            int other = (di.zoomIdx == 1) ? 2 : 1;
            m_zoom[other] = m_zoom[di.zoomIdx];
        }

        update();
        event->accept();
        return;
    }
}
