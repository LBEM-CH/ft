#include "ftwindow_common.h"

// ---------------------------------------------------------------------------
//  Drawing helpers
// ---------------------------------------------------------------------------
void FtWindow::drawShadowRect(QPainter &p, const QRect &rect)
{
    // Shadow: per-pixel, darker towards the outside, directional offset
    int shBlur = 28;
    double shOffX = 10.0, shOffY = 10.0;
    int maxAlpha = 140;

    int sx0 = rect.left()  - shBlur;
    int sy0 = rect.top()   - shBlur;
    int sx1 = rect.right() + shBlur + (int)shOffX;
    int sy1 = rect.bottom()+ shBlur + (int)shOffY;

    QImage shadowImg(sx1 - sx0 + 1, sy1 - sy0 + 1, QImage::Format_ARGB32_Premultiplied);
    shadowImg.fill(Qt::transparent);

    int imgW2 = shadowImg.width(), imgH2 = shadowImg.height();

    double frameL = rect.left()   - sx0;
    double frameT = rect.top()    - sy0;
    double frameR = rect.right()  - sx0;
    double frameB = rect.bottom() - sy0;
    double fcx = (frameL + frameR) / 2.0;
    double fcy = (frameT + frameB) / 2.0;
    double fhw = (frameR - frameL) / 2.0;
    double fhh = (frameB - frameT) / 2.0;

    for (int py = 0; py < imgH2; py++) {
        QRgb *line = reinterpret_cast<QRgb*>(shadowImg.scanLine(py));
        for (int px = 0; px < imgW2; px++) {
            if (px >= frameL && px <= frameR && py >= frameT && py <= frameB)
                continue;
            double rx = (px - fcx) / fhw;
            double ry = (py - fcy) / fhh;
            double tx = std::clamp(0.5 + 0.5 * rx, 0.0, 1.0);
            double ty = std::clamp(0.5 + 0.5 * ry, 0.0, 1.0);
            double localOffX = shOffX * tx;
            double localOffY = shOffY * ty;
            double srcX = px - localOffX;
            double srcY = py - localOffY;
            double dx = std::max({frameL - srcX, srcX - frameR, 0.0});
            double dy = std::max({frameT - srcY, srcY - frameB, 0.0});
            double dist = std::sqrt(dx * dx + dy * dy);
            if (dist >= shBlur) continue;
            double t = dist / shBlur;
            int alpha = (dist <= 0) ? maxAlpha
                                    : (int)(maxAlpha * (1.0 - t) * (1.0 - t));
            if (alpha > 0)
                line[px] = qRgba(0, 0, 0, alpha);
        }
    }
    p.drawImage(sx0, sy0, shadowImg);

    // White background
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255));
    p.drawRect(rect);

    // Dark grey border, 3 pixels wide
    p.setPen(QPen(QColor(60, 60, 60), 3));
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect);
}

void FtWindow::drawImageWithFrame(QPainter &p, const QRect &frame,
                                   const QImage &img,
                                   const ZoomState &zoom,
                                   int imgW, int imgH)
{
    p.setPen(QPen(QColor(255, 255, 0), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(frame);

    QRect target = frame.adjusted(2, 2, -2, -2);
    QRectF src = zoom.visibleRect(imgW, imgH);
    p.drawImage(QRectF(target), img, src);
}

void FtWindow::drawAxes(QPainter &p, const QRect &frame,
                         const ZoomState &zoom,
                         int imgW, int imgH, bool reciprocal,
                         double pixelSize, bool yAxisRight)
{
    QFont axisFont;
    axisFont.setPixelSize(11);
    p.setFont(axisFont);
    p.setPen(Qt::white);
    QFontMetrics fm(axisFont);

    QRectF src = zoom.visibleRect(imgW, imgH);

    double xStart, xEnd, yStart, yEnd;
    if (reciprocal) {
        int halfN = imgW / 2;
        xStart = src.left()  - halfN;
        xEnd   = src.right() - halfN;
        yStart = src.top()   - halfN;
        yEnd   = src.bottom()- halfN;
    } else {
        xStart = src.left();
        xEnd   = src.left() + src.width() - 1;
        yStart = src.top();
        yEnd   = src.top() + src.height() - 1;
    }

    int numTicks = 5;
    int lineH = fm.height();

    auto physLabel = [&](double val, bool isReciprocal) -> QString {
        if (isReciprocal) {
            double freq = val / (imgW * pixelSize);
            if (std::abs(freq) < 1e-12)
                return QString::fromUtf8("(\u221E \u00C5)\u207B\u00B9");
            double res = 1.0 / std::abs(freq);
            return QString("(%1 %2)%3")
                       .arg(res, 0, 'g', 3)
                       .arg(QString::fromUtf8("\u00C5"))
                       .arg(QString::fromUtf8("\u207B\u00B9"));
        } else {
            double ang = val * pixelSize;
            return QString::number(ang, 'g', 4) + QString::fromUtf8(" \u00C5");
        }
    };

    // horizontal axis
    int axisY1 = frame.bottom() + 4;
    int axisY2 = axisY1 + lineH + 1;
    for (int i = 0; i < numTicks; i++) {
        double frac = i / (double)(numTicks - 1);
        double val  = xStart + frac * (xEnd - xStart);
        int sx = frame.left() + (int)(frac * frame.width());

        QString lab1 = QString::number((int)std::round(val));
        QString lab2 = physLabel(val, reciprocal);
        int lw1 = fm.horizontalAdvance(lab1);
        int lw2 = fm.horizontalAdvance(lab2);

        if (reciprocal) {
            if (i == 0) {
                p.drawText(frame.left(), axisY1 + fm.ascent(), lab1);
                p.drawText(frame.left(), axisY2 + fm.ascent(), lab2);
            } else if (i == numTicks - 1) {
                p.drawText(frame.right() - lw1, axisY1 + fm.ascent(), lab1);
                p.drawText(frame.right() - lw2, axisY2 + fm.ascent(), lab2);
            } else {
                p.drawText(sx - lw1 / 2, axisY1 + fm.ascent(), lab1);
                p.drawText(sx - lw2 / 2, axisY2 + fm.ascent(), lab2);
            }
        } else {
            p.drawText(sx - lw1 / 2, axisY1 + fm.ascent(), lab1);
            p.drawText(sx - lw2 / 2, axisY2 + fm.ascent(), lab2);
        }
    }

    // vertical axis
    for (int i = 0; i < numTicks; i++) {
        double frac = i / (double)(numTicks - 1);
        double val  = yStart + frac * (yEnd - yStart);
        int sy = frame.top() + (int)(frac * frame.height());

        QString lab1 = QString::number((int)std::round(val));
        QString lab2 = physLabel(val, reciprocal);
        int lw1 = fm.horizontalAdvance(lab1);
        int lw2 = fm.horizontalAdvance(lab2);

        auto tickY = [&](int idx) -> int {
            if (reciprocal) {
                if (idx == 0) return frame.top() + fm.ascent();
                if (idx == numTicks - 1) return frame.bottom();
            }
            return sy + fm.ascent() / 2 - 1;
        };
        int ty1 = tickY(i);
        int ty2 = ty1 + lineH + 1;

        if (yAxisRight) {
            int x0 = frame.right() + 4;
            p.drawText(x0, ty1, lab1);
            p.drawText(x0, ty2, lab2);
        } else {
            int maxLw = std::max(lw1, lw2);
            int x0 = frame.left() - 4 - maxLw;
            p.drawText(x0 + (maxLw - lw1), ty1, lab1);
            p.drawText(x0 + (maxLw - lw2), ty2, lab2);
        }
    }
}

void FtWindow::drawMinMax(QPainter &p, const QRect &frame,
                           double minVal, double maxVal,
                           double curVal, bool hasCur)
{
    QFont f;
    f.setPixelSize(11);
    p.setFont(f);
    p.setPen(Qt::white);

    QFontMetrics fm(f);
    QString minMaxText = QString("Min: %1     Max: %2")
                   .arg(minVal, 0, 'g', 5)
                   .arg(maxVal, 0, 'g', 5);
    int mmw = fm.horizontalAdvance(minMaxText);
    p.drawText(frame.right() - mmw, frame.top() - 5, minMaxText);
    if (hasCur) {
        QString curText = QString("Current: %1").arg(curVal, 0, 'g', 5);
        int cw = fm.horizontalAdvance(curText);
        p.drawText(frame.right() - cw, frame.top() - 5 - fm.height(), curText);
    }
}

void FtWindow::drawHistogram(QPainter &p, const QRect &frame,
                              const std::vector<double> &vals,
                              double minVal, double maxVal,
                              int availableBelow,
                              int histIndex,
                              double dispMin, double dispMax)
{
    if (vals.empty()) return;

    int histH = std::max(16, availableBelow / 3);
    int histW = frame.width() / 2;
    int histX = frame.x() + (frame.width() - histW) / 2;
    int histY = frame.bottom() + histH;

    // Store the histogram rect for mouse interaction
    QRect histRect(histX, histY, histW, histH);
    if (histIndex >= 0 && histIndex < NUM_HISTS)
        m_histRects[histIndex] = histRect;

    const int nBins = 128;
    std::vector<int> bins(nBins, 0);
    double range = maxVal - minVal;
    if (range <= 0) range = 1;

    for (double v : vals) {
        int bin = std::clamp((int)((v - minVal) / range * (nBins - 1)), 0, nBins - 1);
        bins[bin]++;
    }

    int maxCount = *std::max_element(bins.begin(), bins.end());
    if (maxCount == 0) return;

    // Draw lighter grey background above the histogram for the display range
    bool hasCustomRange = (dispMin != dispMax) && (dispMin != minVal || dispMax != maxVal);
    if (hasCustomRange) {
        double fracLeft  = std::clamp((dispMin - minVal) / range, 0.0, 1.0);
        double fracRight = std::clamp((dispMax - minVal) / range, 0.0, 1.0);
        int x0 = histX + (int)(fracLeft * histW);
        int x1 = histX + (int)(fracRight * histW);
        if (x1 > x0) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(128, 128, 128));
            p.drawRect(x0, histY, x1 - x0, histH);
        }
    }

    p.setPen(QPen(QColor(200, 200, 200), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(histX, histY, histW, histH);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(150, 200, 150));
    double barW = histW / (double)nBins;
    for (int i = 0; i < nBins; i++) {
        int barH = (int)(bins[i] / (double)maxCount * histH);
        if (barH > 0) {
            int bx = histX + (int)(i * barW);
            int by = histY + histH - barH;
            p.drawRect(bx, by, std::max(1, (int)barW), barH);
        }
    }

    QFont f;
    f.setPixelSize(10);
    p.setFont(f);
    p.setPen(Qt::white);
    QFontMetrics fm(f);

    QString minStr = QString::number(minVal, 'g', 4);
    QString maxStr = QString::number(maxVal, 'g', 4);

    p.drawText(histX - fm.horizontalAdvance(minStr) - 2,
               histY + histH + fm.ascent(), minStr);
    p.drawText(histX + histW + 2,
               histY + histH + fm.ascent(), maxStr);

    // Draw drag selection preview
    if (histIndex >= 0 && m_histDragging && m_histDragTarget == histIndex) {
        int x1 = std::clamp(m_histDragStartX, histX, histX + histW);
        int x2 = std::clamp(m_mousePos.x(), histX, histX + histW);
        if (x1 > x2) std::swap(x1, x2);
        if (x2 - x1 > 1) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(128, 128, 128, 120));
            p.drawRect(x1, histY, x2 - x1, histH);
        }
    }

    // Tooltip hint text above histogram
    if (histIndex >= 0) {
        p.setPen(QColor(180, 180, 180));
        QFont hf;
        hf.setPixelSize(9);
        p.setFont(hf);
        QFontMetrics hfm(hf);
        QString hint = "Click to adjust display parameters";
        int hintX = histX + (histW - hfm.horizontalAdvance(hint)) / 2;
        p.drawText(hintX, histY - 2, hint);
    }
}

void FtWindow::drawLattice(QPainter &p, const QRect &screenRect,
                            const ZoomState &zoom, int imgW, int imgH)
{
    int N = m_fftN;
    if (N == 0) return;

    QRectF src = zoom.visibleRect(imgW, imgH);
    double imgCenter = N / 2.0 + 0.5;

    double scaleX = screenRect.width() / src.width();
    double scaleY = screenRect.height() / src.height();

    double scrCx = screenRect.x() + (imgCenter - src.x()) * scaleX;
    double scrCy = screenRect.y() + (imgCenter - src.y()) * scaleY;

    double dotDiam = m_latticeDotDiamEdit->text().toDouble();
    double dotR = dotDiam / 2.0 * std::min(scaleX, scaleY);
    if (dotR < 1.5) dotR = 1.5;

    // Convert lattice vectors to screen pixels
    double uScrX = m_latticeUx * scaleX;
    double uScrY = m_latticeUy * scaleY;
    double vScrX = m_latticeVx * scaleX;
    double vScrY = m_latticeVy * scaleY;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setClipRect(screenRect);

    // Determine range of i,j to cover the visible area
    // Use inverse lattice to find bounds
    double det = m_latticeUx * m_latticeVy - m_latticeUy * m_latticeVx;
    if (std::abs(det) < 0.5) { p.restore(); return; }  // degenerate lattice

    double halfN = N / 2.0;
    int maxIdx = (int)(halfN / std::min(std::sqrt(m_latticeUx * m_latticeUx + m_latticeUy * m_latticeUy),
                                         std::sqrt(m_latticeVx * m_latticeVx + m_latticeVy * m_latticeVy))) + 2;
    if (maxIdx > 200) maxIdx = 200;

    // Draw lattice dots with light blue edge for contrast
    for (int i = -maxIdx; i <= maxIdx; i++) {
        for (int j = -maxIdx; j <= maxIdx; j++) {
            double lx = i * m_latticeUx + j * m_latticeVx;
            double ly = i * m_latticeUy + j * m_latticeVy;
            if (std::abs(lx) > halfN || std::abs(ly) > halfN) continue;
            double sx = scrCx + lx * scaleX;
            double sy = scrCy + ly * scaleY;
            p.setPen(QPen(QColor(150, 200, 255), std::max(1.0, dotR * 0.3)));
            p.setBrush(QColor(100, 160, 255, 180));
            p.drawEllipse(QPointF(sx, sy), dotR, dotR);
        }
    }

    // Draw yellow arrows for u and v vectors
    auto drawArrow = [&](double tipSx, double tipSy, const QString &label) {
        p.setPen(QPen(QColor(255, 220, 0), 4));
        p.drawLine(QPointF(scrCx, scrCy), QPointF(tipSx, tipSy));

        // Arrowhead
        double dx = tipSx - scrCx, dy = tipSy - scrCy;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len > 5) {
            double ux2 = dx / len, uy2 = dy / len;
            double aLen = std::min(15.0, len * 0.45);
            double px1 = tipSx - aLen * (ux2 * 0.86 + uy2 * 0.5);
            double py1 = tipSy - aLen * (uy2 * 0.86 - ux2 * 0.5);
            double px2 = tipSx - aLen * (ux2 * 0.86 - uy2 * 0.5);
            double py2 = tipSy - aLen * (uy2 * 0.86 + ux2 * 0.5);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 220, 0));
            QPainterPath ah;
            ah.moveTo(tipSx, tipSy);
            ah.lineTo(px1, py1);
            ah.lineTo(px2, py2);
            ah.closeSubpath();
            p.drawPath(ah);
        }

        // Label
        QFont lf; lf.setPixelSize(18); lf.setBold(true);
        p.setFont(lf);
        p.setPen(QColor(255, 220, 0));
        p.drawText((int)(tipSx + 6), (int)(tipSy - 6), label);
    };

    double uTipX = scrCx + uScrX, uTipY = scrCy + uScrY;
    double vTipX = scrCx + vScrX, vTipY = scrCy + vScrY;
    drawArrow(uTipX, uTipY, "u");
    drawArrow(vTipX, vTipY, "v");

    p.restore();
}

void FtWindow::drawBandpassRing(QPainter &p, const QRect &screenRect,
                                 const ZoomState &zoom, int imgW, int imgH)
{
    int N = m_fftN;
    if (N == 0) return;

    QRectF src = zoom.visibleRect(imgW, imgH);
    double centerImg = N / 2.0 + 0.5;

    double scaleX = screenRect.width() / src.width();
    double scaleY = screenRect.height() / src.height();
    double scale = std::min(scaleX, scaleY);

    double scrCx = screenRect.x() + (centerImg - src.x()) * scaleX;
    double scrCy = screenRect.y() + (centerImg - src.y()) * scaleY;

    double halfN = N / 2.0;
    double innerPx = m_bandInnerR * halfN * scale;
    double outerPx = m_bandOuterR * halfN * scale;

    p.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath ring;
    ring.addEllipse(QPointF(scrCx, scrCy), outerPx, outerPx);
    QPainterPath hole;
    hole.addEllipse(QPointF(scrCx, scrCy), innerPx, innerPx);
    ring = ring.subtracted(hole);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(100, 160, 255, 60));
    p.setClipRect(screenRect);
    p.drawPath(ring);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(140, 200, 255), 1));
    p.drawEllipse(QPointF(scrCx, scrCy), outerPx, outerPx);
    p.drawEllipse(QPointF(scrCx, scrCy), innerPx, innerPx);

    p.setClipping(false);
    p.setRenderHint(QPainter::Antialiasing, false);
}

void FtWindow::drawLineFilter(QPainter &p, const QRect &screenRect,
                              const ZoomState &zoom, int imgW, int imgH)
{
    int N = m_fftN;
    if (N == 0) return;

    bool okWidth = false;
    bool okAngle = false;
    double lineWidth = m_lineWidthEdit->text().toDouble(&okWidth);
    double angleDeg = m_lineDirectionEdit->text().toDouble(&okAngle);
    if (!okWidth || lineWidth <= 0.0) lineWidth = 1.0;
    if (!okAngle) angleDeg = 0.0;

    QRectF src = zoom.visibleRect(imgW, imgH);
    double imgCenter = N / 2.0 + 0.5;
    double scaleX = screenRect.width() / src.width();
    double scaleY = screenRect.height() / src.height();
    double scrCx = screenRect.x() + (imgCenter - src.x()) * scaleX;
    double scrCy = screenRect.y() + (imgCenter - src.y()) * scaleY;

    double angle = angleDeg * M_PI / 180.0;
    double dirX = std::cos(angle);
    double dirY = std::sin(angle);
    double normX = -std::sin(angle);
    double normY =  std::cos(angle);

    double widthPx = lineWidth * std::min(scaleX, scaleY);
    if (widthPx < 1.0) widthPx = 1.0;

    auto clipLine = [&](double bx, double by, double shift) {
        double x0 = bx + shift * normX;
        double y0 = by + shift * normY;
        double tMin = -1e9, tMax = 1e9;

        auto clipAxis = [&](double p0, double dp, double lo, double hi) {
            if (std::abs(dp) < 1e-9) {
                if (p0 < lo || p0 > hi) { tMin = 1.0; tMax = 0.0; }
                return;
            }
            double t1 = (lo - p0) / dp;
            double t2 = (hi - p0) / dp;
            if (t1 > t2) std::swap(t1, t2);
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
        };

        clipAxis(x0, dirX, screenRect.left(), screenRect.right());
        clipAxis(y0, dirY, screenRect.top(), screenRect.bottom());

        return QLineF(x0 + tMin * dirX, y0 + tMin * dirY,
                      x0 + tMax * dirX, y0 + tMax * dirY);
    };

    // Draw both the line and its Friedel symmetric mate (at -offset).
    // When the offset is zero, both positions coincide, so draw only once.
    double offsets[2] = { m_lineOffset, -m_lineOffset };
    int offsetCount = (m_lineOffset == 0.0) ? 1 : 2;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setClipRect(screenRect);
    for (int i = 0; i < offsetCount; ++i) {
        double off = offsets[i];
        double baseX = scrCx + off * normX * scaleX;
        double baseY = scrCy + off * normY * scaleY;
        QLineF mid   = clipLine(baseX, baseY, 0.0);
        QLineF upper = clipLine(baseX, baseY, widthPx / 2.0);
        QLineF lower = clipLine(baseX, baseY, -widthPx / 2.0);
        p.setPen(QPen(QColor(100, 160, 255, 80), std::max(1.0, widthPx)));
        p.drawLine(mid);
        p.setPen(QPen(QColor(80, 130, 255), 2));
        p.drawLine(mid);
        p.setPen(QPen(QColor(80, 130, 255, 180), 1));
        p.drawLine(upper);
        p.drawLine(lower);
    }
    p.restore();
}

void FtWindow::drawDirectionalWedge(QPainter &p, const QRect &screenRect,
                                     const ZoomState &zoom, int imgW, int imgH)
{
    int N = m_fftN;
    if (N == 0) return;

    QRectF src = zoom.visibleRect(imgW, imgH);
    double imgCenter = N / 2.0 + 0.5;
    double scaleX = screenRect.width() / src.width();
    double scaleY = screenRect.height() / src.height();

    double scrCx = screenRect.x() + (imgCenter - src.x()) * scaleX;
    double scrCy = screenRect.y() + (imgCenter - src.y()) * scaleY;

    double maxR = N * 0.8 * std::max(scaleX, scaleY);

    auto makeWedge = [&](double a1deg, double a2deg) {
        double a1 = a1deg * M_PI / 180.0;
        double a2 = a2deg * M_PI / 180.0;
        QPainterPath wp;
        wp.moveTo(scrCx, scrCy);
        int steps = 20;
        for (int s = 0; s <= steps; s++) {
            double a = a1 + (a2 - a1) * s / steps;
            wp.lineTo(scrCx + maxR * std::cos(a), scrCy + maxR * std::sin(a));
        }
        wp.closeSubpath();
        return wp;
    };

    QPainterPath w1 = makeWedge(m_dirAngle1, m_dirAngle2);
    QPainterPath w2 = makeWedge(m_dirAngle1 + 180, m_dirAngle2 + 180);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setClipRect(screenRect);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(100, 160, 255, 60));
    p.drawPath(w1);
    p.drawPath(w2);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(80, 130, 255), 1));
    p.drawPath(w1);
    p.drawPath(w2);

    p.restore();
}
