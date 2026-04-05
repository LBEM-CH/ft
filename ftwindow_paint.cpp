#include "ftwindow_common.h"

// ---------------------------------------------------------------------------
//  Painting
// ---------------------------------------------------------------------------
void FtWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    int cx = width() / 2;
    int hy = height() - height() / 5;

    // scaled label font sizes
    int labelFontMain = std::clamp(std::min(cx, hy) / 20, 12, 48);
    int labelFontHist = std::clamp(std::min(cx / 5, (height() - hy)) / 6, 8, 24);

    // background
    p.fillRect(rect(), QColor(75, 75, 75));

    m_numDispItems = 0;   // reset display item tracking

    // helper: look up the raw pixel value under the mouse for a given display area
    auto sampleValue = [&](const QRect &inner, const ZoomState &zoom,
                           int imgW, int imgH,
                           const std::vector<double> &vals,
                           double &outVal) -> bool {
        if (!inner.contains(m_mousePos) || vals.empty()) return false;
        QRectF src = zoom.visibleRect(imgW, imgH);
        double relX = (m_mousePos.x() - inner.x()) / (double)inner.width();
        double relY = (m_mousePos.y() - inner.y()) / (double)inner.height();
        int ix = (int)(src.x() + relX * src.width());
        int iy = (int)(src.y() + relY * src.height());
        if (ix < 0 || ix >= imgW || iy < 0 || iy >= imgH) return false;
        outVal = vals[iy * imgW + ix];
        return true;
    };

    // ---- panel titles ----------------------------------------------------------
    {
        QFont tf;
        tf.setBold(true);
        tf.setPixelSize(14);
        p.setFont(tf);
        p.setPen(Qt::white);
        QFontMetrics tfm(tf);

        QString t1 = "Real Space";
        p.drawText((cx - 1 - tfm.horizontalAdvance(t1)) / 2, 8 + tfm.ascent(), t1);

        QString t2 = "Fourier Space";
        p.drawText(cx + 2 + (width() - cx - 2 - tfm.horizontalAdvance(t2)) / 2, 8 + tfm.ascent(), t2);
    }

    // ---- Tool button columns (8 squares each) ----------------------------------
    {
        int btnSide = std::max(width() * 5 / 400, 20);
        int gap = 2;
        int totalH = 8 * btnSide + 7 * gap;
        int startY = (hy - totalH) / 2;

        int offset = btnSide / 2;

        // Panel 1: left edge
        for (int i = 0; i < 8; i++) {
            int by = startY + i * (btnSide + gap);
            QRect r(offset, by, btnSide, btnSide);
            m_p1BtnRects[i] = r;

            p.setPen(QPen(Qt::white, 1));
            if ((i == 0 && m_p1EraserActive) || (i == 1 && m_p1BrushActive) ||
                (i == 4 && m_shiftActive) || (i == 5 && m_rotateActive) || (i == 6 && m_binActive))
                p.setBrush(QColor(60, 60, 60));
            else
                p.setBrush(QColor(0, 0, 0));
            p.drawRect(r);

            // Eraser icon (button 0): reuse panel 2 eraser icon
            if (i == 0) {
                p.setRenderHint(QPainter::Antialiasing, true);
                QRect ir = r.adjusted(3, 3, -3, -3);
                int ix = ir.x(), iy2 = ir.y(), iw = ir.width(), ih = ir.height();

                QPainterPath ep;
                ep.moveTo(ix + iw * 0.2, iy2 + ih * 0.1);
                ep.lineTo(ix + iw * 0.9, iy2 + ih * 0.1);
                ep.lineTo(ix + iw * 0.8, iy2 + ih * 0.9);
                ep.lineTo(ix + iw * 0.1, iy2 + ih * 0.9);
                ep.closeSubpath();
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(200, 180, 160));
                p.drawPath(ep);

                QPainterPath tp;
                tp.moveTo(ix + iw * 0.1, iy2 + ih * 0.9);
                tp.lineTo(ix + iw * 0.15, iy2 + ih * 0.55);
                tp.lineTo(ix + iw * 0.85, iy2 + ih * 0.55);
                tp.lineTo(ix + iw * 0.8, iy2 + ih * 0.9);
                tp.closeSubpath();
                p.setBrush(QColor(230, 100, 100));
                p.drawPath(tp);

                p.setPen(QPen(QColor(120, 80, 60), std::max(1, iw / 10)));
                p.drawLine(ix + iw * 0.15, iy2 + ih * 0.55, ix + iw * 0.85, iy2 + ih * 0.55);

                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Eraser";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(40, 40, 40));
                    p.drawRect(ttx, tty, ttw, tth);
                    p.drawText(ttx + 4, tty + 2 + ttfm.ascent(), tip);
                }
            }

            // Paint brush icon (button 1): reuse panel 2 brush icon
            if (i == 1) {
                p.setRenderHint(QPainter::Antialiasing, true);
                QRect ir = r.adjusted(3, 3, -3, -3);
                int bx2 = ir.x(), by3 = ir.y(), bw = ir.width(), bh = ir.height();

                p.setPen(QPen(QColor(160, 120, 60), std::max(1, bw / 6)));
                p.drawLine(bx2 + bw * 0.5, by3 + bh * 0.05, bx2 + bw * 0.5, by3 + bh * 0.45);

                p.setPen(Qt::NoPen);
                p.setBrush(QColor(180, 180, 180));
                p.drawRect(bx2 + bw * 0.25, by3 + bh * 0.40, bw * 0.5, bh * 0.15);

                p.setBrush(QColor(200, 160, 80));
                QPainterPath br;
                br.moveTo(bx2 + bw * 0.2, by3 + bh * 0.55);
                br.lineTo(bx2 + bw * 0.8, by3 + bh * 0.55);
                br.lineTo(bx2 + bw * 0.65, by3 + bh * 0.95);
                br.lineTo(bx2 + bw * 0.35, by3 + bh * 0.95);
                br.closeSubpath();
                p.drawPath(br);

                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Paint brush";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(40, 40, 40));
                    p.drawRect(ttx, tty, ttw, tth);
                    p.drawText(ttx + 4, tty + 2 + ttfm.ascent(), tip);
                }
            }

            // Flip horizontal icon (button 2): double arrow left-right
            if (i == 2) {
                p.setRenderHint(QPainter::Antialiasing, true);
                double cx2 = r.x() + r.width() / 2.0;
                double cy2 = r.y() + r.height() / 2.0;
                double hw = r.width() * 0.35;
                double hh = r.height() * 0.2;

                p.setPen(Qt::NoPen);
                p.setBrush(Qt::white);
                QPainterPath la;
                la.moveTo(cx2 - hw, cy2);
                la.lineTo(cx2 - hw * 0.3, cy2 - hh);
                la.lineTo(cx2 - hw * 0.3, cy2 + hh);
                la.closeSubpath();
                p.drawPath(la);
                QPainterPath ra;
                ra.moveTo(cx2 + hw, cy2);
                ra.lineTo(cx2 + hw * 0.3, cy2 - hh);
                ra.lineTo(cx2 + hw * 0.3, cy2 + hh);
                ra.closeSubpath();
                p.drawPath(ra);
                p.setPen(QPen(Qt::white, std::max(1, btnSide / 12)));
                p.drawLine(cx2 - hw * 0.3, cy2, cx2 + hw * 0.3, cy2);

                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Flip horizontally";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(40, 40, 40));
                    p.drawRect(ttx, tty, ttw, tth);
                    p.drawText(ttx + 4, tty + 2 + ttfm.ascent(), tip);
                }
            }

            // Flip vertical icon (button 3): double arrow up-down
            if (i == 3) {
                p.setRenderHint(QPainter::Antialiasing, true);
                double cx2 = r.x() + r.width() / 2.0;
                double cy2 = r.y() + r.height() / 2.0;
                double hw = r.width() * 0.2;
                double hh = r.height() * 0.35;

                p.setPen(Qt::NoPen);
                p.setBrush(Qt::white);
                QPainterPath ua;
                ua.moveTo(cx2, cy2 - hh);
                ua.lineTo(cx2 - hw, cy2 - hh * 0.3);
                ua.lineTo(cx2 + hw, cy2 - hh * 0.3);
                ua.closeSubpath();
                p.drawPath(ua);
                QPainterPath da;
                da.moveTo(cx2, cy2 + hh);
                da.lineTo(cx2 - hw, cy2 + hh * 0.3);
                da.lineTo(cx2 + hw, cy2 + hh * 0.3);
                da.closeSubpath();
                p.drawPath(da);
                p.setPen(QPen(Qt::white, std::max(1, btnSide / 12)));
                p.drawLine(cx2, cy2 - hh * 0.3, cx2, cy2 + hh * 0.3);

                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Flip vertically";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(40, 40, 40));
                    p.drawRect(ttx, tty, ttw, tth);
                    p.drawText(ttx + 4, tty + 2 + ttfm.ascent(), tip);
                }
            }

            // Shift image icon (button 4): arrow pointing right
            if (i == 4) {
                p.setRenderHint(QPainter::Antialiasing, true);
                double cx2 = r.x() + r.width() / 2.0;
                double cy2 = r.y() + r.height() / 2.0;
                double hw = r.width() * 0.35;
                double hh = r.height() * 0.22;

                p.setPen(Qt::NoPen);
                p.setBrush(m_shiftActive ? QColor(180, 180, 255) : Qt::white);
                // Four-direction arrow
                // Right
                QPainterPath ar;
                ar.moveTo(cx2 + hw, cy2);
                ar.lineTo(cx2 + hw * 0.4, cy2 - hh);
                ar.lineTo(cx2 + hw * 0.4, cy2 + hh);
                ar.closeSubpath();
                p.drawPath(ar);
                // Left
                QPainterPath al;
                al.moveTo(cx2 - hw, cy2);
                al.lineTo(cx2 - hw * 0.4, cy2 - hh);
                al.lineTo(cx2 - hw * 0.4, cy2 + hh);
                al.closeSubpath();
                p.drawPath(al);
                // Up
                QPainterPath au;
                au.moveTo(cx2, cy2 - hw);
                au.lineTo(cx2 - hh, cy2 - hw * 0.4);
                au.lineTo(cx2 + hh, cy2 - hw * 0.4);
                au.closeSubpath();
                p.drawPath(au);
                // Down
                QPainterPath ad;
                ad.moveTo(cx2, cy2 + hw);
                ad.lineTo(cx2 - hh, cy2 + hw * 0.4);
                ad.lineTo(cx2 + hh, cy2 + hw * 0.4);
                ad.closeSubpath();
                p.drawPath(ad);

                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Shift image";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(40, 40, 40));
                    p.drawRect(ttx, tty, ttw, tth);
                    p.drawText(ttx + 4, tty + 2 + ttfm.ascent(), tip);
                }
            }

            // Rotate image icon (button 5): curved arrow
            if (i == 5) {
                p.setRenderHint(QPainter::Antialiasing, true);
                double cx2 = r.x() + r.width() / 2.0;
                double cy2 = r.y() + r.height() / 2.0;
                double rad = std::min(r.width(), r.height()) * 0.32;

                p.setPen(QPen(m_rotateActive ? QColor(180, 180, 255) : Qt::white,
                              std::max(1, (int)(rad * 0.25))));
                p.setBrush(Qt::NoBrush);
                p.drawArc(QRectF(cx2 - rad, cy2 - rad, rad * 2, rad * 2),
                          30 * 16, 280 * 16);

                // arrowhead at end of arc (~310 degrees = -50 degrees)
                double aAngle = -50.0 * M_PI / 180.0;
                double ax = cx2 + rad * std::cos(aAngle);
                double ay = cy2 - rad * std::sin(aAngle);
                double sz = rad * 0.5;
                p.setPen(Qt::NoPen);
                p.setBrush(m_rotateActive ? QColor(180, 180, 255) : Qt::white);
                QPainterPath ah;
                ah.moveTo(ax + sz * std::cos(aAngle + 0.3), ay - sz * std::sin(aAngle + 0.3));
                ah.lineTo(ax - sz * 0.4 * std::cos(aAngle - 0.8), ay + sz * 0.4 * std::sin(aAngle - 0.8));
                ah.lineTo(ax + sz * 0.4 * std::cos(aAngle + 1.5), ay - sz * 0.4 * std::sin(aAngle + 1.5));
                ah.closeSubpath();
                p.drawPath(ah);

                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Rotate image";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(40, 40, 40));
                    p.drawRect(ttx, tty, ttw, tth);
                    p.drawText(ttx + 4, tty + 2 + ttfm.ascent(), tip);
                }
            }

            // Bin image icon (button 6): 2x2 grid representing pixel binning
            if (i == 6) {
                if (m_binActive) {
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(60, 60, 60));
                    p.drawRect(r);
                }

                QColor col = m_binActive ? QColor(180, 180, 255) : Qt::white;
                int m = std::max(2, btnSide / 5);  // margin
                QRect inner = r.adjusted(m, m, -m, -m);
                int midX = inner.x() + inner.width() / 2;
                int midY = inner.y() + inner.height() / 2;

                // Draw 4 filled squares (2x2 grid) with gaps
                int g = std::max(1, btnSide / 10);  // gap between cells
                p.setPen(Qt::NoPen);
                p.setBrush(col);
                p.drawRect(QRect(inner.x(), inner.y(), midX - inner.x() - g, midY - inner.y() - g));
                p.setBrush(col.darker(140));
                p.drawRect(QRect(midX + g, inner.y(), inner.right() - midX - g + 1, midY - inner.y() - g));
                p.setBrush(col.darker(170));
                p.drawRect(QRect(inner.x(), midY + g, midX - inner.x() - g, inner.bottom() - midY - g + 1));
                p.setBrush(col.darker(120));
                p.drawRect(QRect(midX + g, midY + g, inner.right() - midX - g + 1, inner.bottom() - midY - g + 1));

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Bin image";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(40, 40, 40));
                    p.drawRect(ttx, tty, ttw, tth);
                    p.drawText(ttx + 4, tty + 2 + ttfm.ascent(), tip);
                }
            }
        }

        // Panel 2: right edge
        for (int i = 0; i < 8; i++) {
            int by = startY + i * (btnSide + gap);
            QRect r(width() - btnSide - offset, by, btnSide, btnSide);
            m_toolBtnRects[i] = r;

            p.setPen(QPen(Qt::white, 1));
            if ((i == 0 && m_eraserActive) || (i == 1 && m_brushActive) || (i == 2 && m_bandpassActive) || (i == 3 && m_directionalActive) || (i == 4 && m_latticeActive) || (i == 5 && m_ftRotateActive))
                p.setBrush(QColor(60, 60, 60));
            else
                p.setBrush(QColor(0, 0, 0));
            p.drawRect(r);

            // Eraser icon in first button
            if (i == 0) {
                p.setRenderHint(QPainter::Antialiasing, true);
                QRect ir = r.adjusted(3, 3, -3, -3);
                int ix = ir.x(), iy = ir.y(), iw = ir.width(), ih = ir.height();

                QPainterPath ep;
                ep.moveTo(ix + iw * 0.2, iy + ih * 0.1);
                ep.lineTo(ix + iw * 0.9, iy + ih * 0.1);
                ep.lineTo(ix + iw * 0.8, iy + ih * 0.9);
                ep.lineTo(ix + iw * 0.1, iy + ih * 0.9);
                ep.closeSubpath();
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(200, 180, 160));
                p.drawPath(ep);

                QPainterPath tp;
                tp.moveTo(ix + iw * 0.1, iy + ih * 0.9);
                tp.lineTo(ix + iw * 0.15, iy + ih * 0.55);
                tp.lineTo(ix + iw * 0.85, iy + ih * 0.55);
                tp.lineTo(ix + iw * 0.8, iy + ih * 0.9);
                tp.closeSubpath();
                p.setBrush(QColor(230, 100, 100));
                p.drawPath(tp);

                p.setPen(QPen(QColor(120, 80, 60), std::max(1, iw / 10)));
                p.drawLine(ix + iw * 0.15, iy + ih * 0.55, ix + iw * 0.85, iy + ih * 0.55);

                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Eraser";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.left() - ttw - 4;
                    int tty = r.center().y() - tth / 2;
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(40, 40, 40));
                    p.drawRect(ttx, tty, ttw, tth);
                    p.drawText(ttx + 4, tty + 2 + ttfm.ascent(), tip);
                }
            }

            // Paint brush icon in second button
            if (i == 1) {
                p.setRenderHint(QPainter::Antialiasing, true);
                QRect ir = r.adjusted(3, 3, -3, -3);
                int bx = ir.x(), by2 = ir.y(), bw = ir.width(), bh = ir.height();

                p.setPen(QPen(QColor(160, 120, 60), std::max(1, bw / 6)));
                p.drawLine(bx + bw * 0.5, by2 + bh * 0.05, bx + bw * 0.5, by2 + bh * 0.45);

                p.setPen(Qt::NoPen);
                p.setBrush(QColor(180, 180, 180));
                p.drawRect(bx + bw * 0.25, by2 + bh * 0.40, bw * 0.5, bh * 0.15);

                p.setBrush(QColor(200, 160, 80));
                QPainterPath br;
                br.moveTo(bx + bw * 0.2, by2 + bh * 0.55);
                br.lineTo(bx + bw * 0.8, by2 + bh * 0.55);
                br.lineTo(bx + bw * 0.65, by2 + bh * 0.95);
                br.lineTo(bx + bw * 0.35, by2 + bh * 0.95);
                br.closeSubpath();
                p.drawPath(br);

                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Paint brush";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.left() - ttw - 4;
                    int tty = r.center().y() - tth / 2;
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(40, 40, 40));
                    p.drawRect(ttx, tty, ttw, tth);
                    p.drawText(ttx + 4, tty + 2 + ttfm.ascent(), tip);
                }
            }

            // Bandpass icon in third button
            if (i == 2) {
                p.setRenderHint(QPainter::Antialiasing, true);
                double bcx = r.x() + r.width() / 2.0;
                double bcy = r.y() + r.height() / 2.0;
                double rad = std::min(r.width(), r.height()) / 2.0 - 2;

                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0, 0, 0));
                p.drawEllipse(QPointF(bcx, bcy), rad * 0.3, rad * 0.3);

                double innerR = rad * 0.3;
                double outerR = rad * 0.6;
                QPainterPath ring;
                ring.addEllipse(QPointF(bcx, bcy), outerR, outerR);
                QPainterPath hole;
                hole.addEllipse(QPointF(bcx, bcy), innerR, innerR);
                ring = ring.subtracted(hole);
                p.setBrush(QColor(200, 200, 255));
                p.drawPath(ring);

                p.setPen(QPen(QColor(100, 100, 200), 1));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(QPointF(bcx, bcy), outerR, outerR);
                p.drawEllipse(QPointF(bcx, bcy), innerR, innerR);

                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Bandpass filter";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.left() - ttw - 4;
                    int tty = r.center().y() - tth / 2;
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(40, 40, 40));
                    p.drawRect(ttx, tty, ttw, tth);
                    p.drawText(ttx + 4, tty + 2 + ttfm.ascent(), tip);
                }
            }

            // Directional filter icon in fourth button
            if (i == 3) {
                p.setRenderHint(QPainter::Antialiasing, true);
                double dcx = r.x() + r.width() / 2.0;
                double dcy = r.y() + r.height() / 2.0;
                double drad = std::min(r.width(), r.height()) / 2.0 - 2;

                auto drawWedge = [&](double a1deg, double a2deg) {
                    double a1 = a1deg * M_PI / 180.0;
                    double a2 = a2deg * M_PI / 180.0;
                    QPainterPath wp;
                    wp.moveTo(dcx, dcy);
                    int steps = 12;
                    for (int s = 1; s <= steps; s++) {
                        double a = a1 + (a2 - a1) * s / steps;
                        wp.lineTo(dcx + drad * std::cos(a), dcy + drad * std::sin(a));
                    }
                    wp.closeSubpath();
                    return wp;
                };
                QPainterPath w1 = drawWedge(-20, 20);
                QPainterPath w2 = drawWedge(160, 200);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(200, 200, 255));
                p.drawPath(w1);
                p.drawPath(w2);
                p.setPen(QPen(QColor(100, 100, 200), 1));
                p.setBrush(Qt::NoBrush);
                p.drawPath(w1);
                p.drawPath(w2);

                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Directional filter";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.left() - ttw - 4;
                    int tty = r.center().y() - tth / 2;
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(40, 40, 40));
                    p.drawRect(ttx, tty, ttw, tth);
                    p.drawText(ttx + 4, tty + 2 + ttfm.ascent(), tip);
                }
            }

            // Lattice filter icon (button 4): hexagonal dot pattern
            if (i == 4) {
                p.setRenderHint(QPainter::Antialiasing, true);
                double cx2 = r.x() + r.width() / 2.0;
                double cy2 = r.y() + r.height() / 2.0;
                double rad = std::min(r.width(), r.height()) * 0.35;
                double dotR = rad * 0.15;

                QColor dotCol(200, 200, 255);
                p.setPen(Qt::NoPen);
                p.setBrush(dotCol);
                // Center dot
                p.drawEllipse(QPointF(cx2, cy2), dotR, dotR);
                // 6 surrounding dots in hexagonal arrangement
                for (int k = 0; k < 6; k++) {
                    double a = k * 60.0 * M_PI / 180.0;
                    p.drawEllipse(QPointF(cx2 + rad * std::cos(a),
                                          cy2 + rad * std::sin(a)), dotR, dotR);
                }
                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Lattice filter";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.left() - ttw - 4;
                    int tty = r.center().y() - tth / 2;
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(40, 40, 40));
                    p.drawRect(ttx, tty, ttw, tth);
                    p.drawText(ttx + 4, tty + 2 + ttfm.ascent(), tip);
                }
            }

            // Rotate Fourier space icon (button 5): curved arrow (same as panel 1 rotate)
            if (i == 5) {
                p.setRenderHint(QPainter::Antialiasing, true);
                double cx2 = r.x() + r.width() / 2.0;
                double cy2 = r.y() + r.height() / 2.0;
                double rad = std::min(r.width(), r.height()) * 0.32;

                p.setPen(QPen(m_ftRotateActive ? QColor(180, 180, 255) : Qt::white,
                              std::max(1, (int)(rad * 0.25))));
                p.setBrush(Qt::NoBrush);
                p.drawArc(QRectF(cx2 - rad, cy2 - rad, rad * 2, rad * 2),
                          30 * 16, 280 * 16);

                double aAngle = -50.0 * M_PI / 180.0;
                double ax = cx2 + rad * std::cos(aAngle);
                double ay = cy2 - rad * std::sin(aAngle);
                double sz = rad * 0.5;
                p.setPen(Qt::NoPen);
                p.setBrush(m_ftRotateActive ? QColor(180, 180, 255) : Qt::white);
                QPainterPath ah;
                ah.moveTo(ax + sz * std::cos(aAngle + 0.3), ay - sz * std::sin(aAngle + 0.3));
                ah.lineTo(ax - sz * 0.4 * std::cos(aAngle - 0.8), ay + sz * 0.4 * std::sin(aAngle - 0.8));
                ah.lineTo(ax + sz * 0.4 * std::cos(aAngle + 1.5), ay - sz * 0.4 * std::sin(aAngle + 1.5));
                ah.closeSubpath();
                p.drawPath(ah);

                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Rotate Fourier space";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.left() - ttw - 4;
                    int tty = r.center().y() - tth / 2;
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(40, 40, 40));
                    p.drawRect(ttx, tty, ttw, tth);
                    p.drawText(ttx + 4, tty + 2 + ttfm.ascent(), tip);
                }
            }
        }
    }

    // ---- Panel 1: loaded image ------------------------------------------------
    int panel1W = cx - 1;
    int panel1H = hy - 1;

    if (!m_image.isNull()) {
        int side1 = static_cast<int>(0.7 * std::min(panel1W, panel1H));
        int imgX = (panel1W - side1) / 2;
        int imgY = (panel1H - side1) / 2;
        QRect frame(imgX, imgY, side1, side1);

        int imgW = m_image.width();
        int imgH = m_image.height();

        // Label (a-j) above center of image
        if (m_activeSlot >= 0) {
            QFont lf; lf.setBold(true); lf.setPixelSize(labelFontMain); p.setFont(lf);
            p.setPen(QColor(255, 255, 0));
            QFontMetrics lfm(lf);
            QString lab = QString(QChar('a' + m_activeSlot));
            p.drawText(frame.x() + (frame.width() - lfm.horizontalAdvance(lab)) / 2,
                       frame.y() - 22, lab);
        }

        drawImageWithFrame(p, frame, m_image, m_zoom[0], imgW, imgH);
        drawAxes(p, frame, m_zoom[0], imgW, imgH, false, m_pixelSize);

        QRect inner = frame.adjusted(2, 2, -2, -2);
        double curVal = 0;
        bool hasCur = sampleValue(inner, m_zoom[0], imgW, imgH, m_imageRawPixels, curVal);
        drawMinMax(p, frame, m_imageMinVal, m_imageMaxVal, curVal, hasCur);
        drawHistogram(p, frame, m_imageRawPixels, m_imageMinVal, m_imageMaxVal, hy - frame.bottom());

        // Pixel size label above top-left corner of image (outside frame)
        {
            QFont pf;
            pf.setPixelSize(11);
            p.setFont(pf);
            p.setPen(Qt::white);
            QFontMetrics pfm(pf);
            QString psLabel = QString("1 pixel = %1 %2")
                                  .arg(m_pixelSize, 0, 'g', 4)
                                  .arg(QString::fromUtf8("\u00C5"));

            // Resolution and pixel-size info below the bottom-right of the frame
            int infoX = frame.right();
            int infoY = frame.bottom() + 4 + 3 * (pfm.height() + 1);

            QString resLabel = QString("%1 x %2 pixels").arg(imgW).arg(imgH);
            p.drawText(infoX - pfm.horizontalAdvance(resLabel), infoY + pfm.ascent(), resLabel);

            infoY += pfm.height() + 1;
            p.drawText(infoX - pfm.horizontalAdvance(psLabel), infoY + pfm.ascent(), psLabel);

            if (!m_imagePath.isEmpty()) {
                infoY += pfm.height() + 1;
                QString fname = QFileInfo(m_imagePath).fileName();
                p.drawText(infoX - pfm.horizontalAdvance(fname), infoY + pfm.ascent(), fname);
            }
        }

        DisplayItem &di = m_dispItems[m_numDispItems++];
        di = { inner, imgW, imgH, 0, &m_imageRawPixels, true };
    } else if (m_activeSlot >= 0) {
        // Empty buffer selected – draw yellow frame with buffer letter
        int side1 = static_cast<int>(0.7 * std::min(panel1W, panel1H));
        int imgX = (panel1W - side1) / 2;
        int imgY = (panel1H - side1) / 2;
        QRect frame(imgX, imgY, side1, side1);

        QFont lf; lf.setBold(true); lf.setPixelSize(labelFontMain); p.setFont(lf);
        p.setPen(QColor(255, 255, 0));
        QFontMetrics lfm(lf);
        QString lab = QString(QChar('a' + m_activeSlot));
        p.drawText(frame.x() + (frame.width() - lfm.horizontalAdvance(lab)) / 2,
                   frame.y() - 22, lab);

        p.setPen(QPen(QColor(255, 255, 0), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(frame);
    }

    // ---- Panel 2: FFT results -------------------------------------------------
    if (m_ftComputed) {
        int panel2X = cx + 2;
        int panel2W = width() - panel2X;
        int panel2H = hy - 1;

        if (m_displayMode == 2 || m_displayMode == 3) {
            int side = static_cast<int>(0.7 * std::min(panel2W, panel2H));
            int fx = panel2X + (panel2W - side) / 2;
            int fy = (panel2H - side) / 2;
            QRect frame(fx, fy, side, side);

            if (m_activeSlot >= 0) {
                QFont af; af.setBold(true); af.setPixelSize(labelFontMain); p.setFont(af);
                p.setPen(QColor(255, 255, 0));
                QFontMetrics afm(af);
                QString ftLab = QString(QChar('A' + m_activeSlot));
                p.drawText(frame.x() + (frame.width() - afm.horizontalAdvance(ftLab)) / 2,
                           frame.y() - 22, ftLab);
            }
            p.setPen(QColor(200, 200, 200));
            QFont lf; lf.setPixelSize(28); p.setFont(lf);
            if (m_displayMode == 3)
                p.drawText(frame.x(), frame.y() - 4, "Powerspectrum");
            else
                p.drawText(frame.x(), frame.y() - 4, "Complex Fourier Transform");

            const QImage &img = (m_displayMode == 3) ? m_powerImg : m_complexImg;
            drawImageWithFrame(p, frame, img, m_zoom[1], m_fftN, m_fftN);
            drawAxes(p, frame, m_zoom[1], m_fftN, m_fftN, true, m_pixelSize);

            QRect inner = frame.adjusted(2, 2, -2, -2);
            double curVal = 0;
            bool hasCur = sampleValue(inner, m_zoom[1], m_fftN, m_fftN, m_powerVals, curVal);

            if (m_displayMode == 2) {
                double curAmp = 0, curPhase = 0;
                bool hasAmp = sampleValue(inner, m_zoom[1], m_fftN, m_fftN, m_ampVals, curAmp);
                bool hasPh  = sampleValue(inner, m_zoom[1], m_fftN, m_fftN, m_phaseVals, curPhase);

                QFont f; f.setPixelSize(11); p.setFont(f); p.setPen(Qt::white);
                QFontMetrics fm2(f);
                QString text;
                if (hasAmp && hasPh)
                    text = QString("Current amplitude: %1     Current phase: %2     Min: %3     Max: %4")
                               .arg(curAmp, 0, 'g', 5).arg(curPhase, 0, 'g', 5)
                               .arg(m_powerMin, 0, 'g', 5).arg(m_powerMax, 0, 'g', 5);
                else
                    text = QString("Min: %1     Max: %2")
                               .arg(m_powerMin, 0, 'g', 5).arg(m_powerMax, 0, 'g', 5);
                int tw = fm2.horizontalAdvance(text);
                p.drawText(frame.right() - tw, frame.top() - 5, text);
            } else {
                drawMinMax(p, frame, m_powerMin, m_powerMax, curVal, hasCur);
            }

            drawHistogram(p, frame, m_powerVals, m_powerMin, m_powerMax, hy - frame.bottom());

            if (m_bandpassActive)
                drawBandpassRing(p, inner, m_zoom[1], m_fftN, m_fftN);
            if (m_directionalActive)
                drawDirectionalWedge(p, inner, m_zoom[1], m_fftN, m_fftN);
            if (m_latticeActive)
                drawLattice(p, inner, m_zoom[1], m_fftN, m_fftN);

            DisplayItem &di = m_dispItems[m_numDispItems++];
            di = { inner, m_fftN, m_fftN, 1, &m_powerVals, true };
        } else {
            int maxSide = std::min((panel2W - 20) / 2, panel2H);
            int side = static_cast<int>(0.80 * maxSide);
            int gapX = 10;
            int totalW = side * 2 + gapX;
            int startX = panel2X + (panel2W - totalW) / 2;
            int fy = (panel2H - side) / 2;

            QRect frame1(startX, fy, side, side);
            QRect frame2(startX + side + gapX, fy, side, side);

            const QImage *img1, *img2;
            const std::vector<double> *vals1, *vals2;
            double min1, max1, min2, max2;
            QString label1, label2;

            if (m_displayMode == 0) {
                img1 = &m_cosImg;  img2 = &m_sinImg;
                vals1 = &m_cosVals; vals2 = &m_sinVals;
                min1 = m_cosMin; max1 = m_cosMax;
                min2 = m_sinMin; max2 = m_sinMax;
                label1 = "Cosinus"; label2 = "Sinus";
            } else {
                img1 = &m_ampImg;  img2 = &m_phaseImg;
                vals1 = &m_ampVals; vals2 = &m_phaseVals;
                min1 = m_ampMin; max1 = m_ampMax;
                min2 = m_phaseMin; max2 = m_phaseMax;
                label1 = "Amplitude"; label2 = "Phase";
            }

            if (m_activeSlot >= 0) {
                QFont af; af.setBold(true); af.setPixelSize(labelFontMain); p.setFont(af);
                p.setPen(QColor(255, 255, 0));
                QFontMetrics afm(af);
                QString ftLab = QString(QChar('A' + m_activeSlot));
                int combinedX = frame1.x();
                int combinedW = frame2.right() - frame1.x();
                p.drawText(combinedX + (combinedW - afm.horizontalAdvance(ftLab)) / 2,
                           frame1.y() - 22, ftLab);
            }

            p.setPen(QColor(200, 200, 200));
            QFont lf;
            lf.setPixelSize(28);
            p.setFont(lf);
            p.drawText(frame1.x(), frame1.y() - 4, label1);
            p.drawText(frame2.x(), frame2.y() - 4, label2);

            drawImageWithFrame(p, frame1, *img1, m_zoom[1], m_fftN, m_fftN);
            drawAxes(p, frame1, m_zoom[1], m_fftN, m_fftN, true, m_pixelSize);
            QRect inner1 = frame1.adjusted(2, 2, -2, -2);
            double curVal1 = 0;
            bool hasCur1 = sampleValue(inner1, m_zoom[1], m_fftN, m_fftN, *vals1, curVal1);
            drawMinMax(p, frame1, min1, max1, curVal1, hasCur1);
            drawHistogram(p, frame1, *vals1, min1, max1, hy - frame1.bottom());

            drawImageWithFrame(p, frame2, *img2, m_zoom[2], m_fftN, m_fftN);
            drawAxes(p, frame2, m_zoom[2], m_fftN, m_fftN, true, m_pixelSize, true);
            QRect inner2 = frame2.adjusted(2, 2, -2, -2);
            double curVal2 = 0;
            bool hasCur2 = sampleValue(inner2, m_zoom[2], m_fftN, m_fftN, *vals2, curVal2);
            drawMinMax(p, frame2, min2, max2, curVal2, hasCur2);
            drawHistogram(p, frame2, *vals2, min2, max2, hy - frame2.bottom());

            if (m_bandpassActive) {
                drawBandpassRing(p, inner1, m_zoom[1], m_fftN, m_fftN);
                drawBandpassRing(p, inner2, m_zoom[2], m_fftN, m_fftN);
            }
            if (m_directionalActive) {
                drawDirectionalWedge(p, inner1, m_zoom[1], m_fftN, m_fftN);
                drawDirectionalWedge(p, inner2, m_zoom[2], m_fftN, m_fftN);
            }
            if (m_latticeActive) {
                drawLattice(p, inner1, m_zoom[1], m_fftN, m_fftN);
                drawLattice(p, inner2, m_zoom[2], m_fftN, m_fftN);
            }

            DisplayItem &d1 = m_dispItems[m_numDispItems++];
            d1 = { inner1, m_fftN, m_fftN, 1, vals1, true };
            DisplayItem &d2 = m_dispItems[m_numDispItems++];
            d2 = { inner2, m_fftN, m_fftN, 2, vals2, true };
        }
    }

    // ---- Rotation drag overlay (red line + angle text) -------------------------
    if ((m_p1Dragging && m_rotateActive) || (m_p2Dragging && m_ftRotateActive)) {
        p.setRenderHint(QPainter::Antialiasing, true);
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (!di.valid) continue;
            bool isP1 = (m_p1Dragging && m_rotateActive && di.zoomIdx == 0);
            bool isP2 = (m_p2Dragging && m_ftRotateActive && di.zoomIdx >= 1);
            if (!isP1 && !isP2) continue;

            QRect sr = di.screenRect;
            double ccx = sr.center().x(), ccy = sr.center().y();
            double radius = std::min(sr.width(), sr.height()) / 2.0;

            // Draw red line from center to edge in direction of current mouse
            double angle2 = std::atan2(m_mousePos.y() - ccy, m_mousePos.x() - ccx);
            double ex = ccx + radius * std::cos(angle2);
            double ey = ccy + radius * std::sin(angle2);
            p.setPen(QPen(Qt::red, 2));
            p.drawLine(QPointF(ccx, ccy), QPointF(ex, ey));

            // Compute rotation angle (difference from drag start)
            const QPoint &dragStart = isP1 ? m_p1DragStart : m_p2DragStart;
            double a1 = std::atan2(dragStart.y() - ccy, dragStart.x() - ccx);
            double angleDeg = (angle2 - a1) * 180.0 / M_PI;
            while (angleDeg > 180.0) angleDeg -= 360.0;
            while (angleDeg < -180.0) angleDeg += 360.0;

            // Draw angle text at half radius
            double tx = ccx + radius * 0.5 * std::cos(angle2);
            double ty = ccy + radius * 0.5 * std::sin(angle2);
            // Offset text perpendicular to the line
            double perpX = -std::sin(angle2) * 15;
            double perpY =  std::cos(angle2) * 15;
            QFont af; af.setBold(true); af.setPixelSize(14); p.setFont(af);
            p.setPen(QColor(255, 255, 0));
            QString angleStr = QString::number(angleDeg, 'f', 1) + QString::fromUtf8("\u00B0");
            p.drawText(QPointF(tx + perpX, ty + perpY), angleStr);
            break;
        }
        p.setRenderHint(QPainter::Antialiasing, false);
    }

    // ---- Bandpass/directional smooth label ------------------------------------
    if (m_bandpassActive || m_directionalActive) {
        QFont sf; sf.setPixelSize(11); p.setFont(sf);
        p.setPen(Qt::white);
        int hy2 = height() - height() / 5;
        int bpX = width() - 250;
        int bpY = hy2 - 90;
        p.drawText(bpX, bpY + 15, "Smooth edge by pixels:");
    }

    // ---- Panel 3: image history (below panel 1) – 2 rows × 8 columns ----------
    {
        int p3x = 0;
        int p3y = hy + 2;
        int p3w = cx - 1;
        int p3h = height() - p3y;

        int cols = 8, rows = 2;
        int margin = 8;
        int availW = p3w - 2 * margin;
        int availH = p3h - 2 * margin;
        int gap = 6;
        int labelH = labelFontHist + 4;
        int sideFromW = (availW - (cols - 1) * gap) / cols;
        int sideFromH = (availH - gap - rows * labelH) / rows;
        int side = std::min(sideFromW, sideFromH);
        if (side < 10) side = 10;

        int totalW = cols * side + (cols - 1) * gap;
        int totalH = rows * (side + labelH) + gap;
        int startX = p3x + (p3w - totalW) / 2;
        int startY = p3y + (p3h - totalH) / 2;

        for (int i = 0; i < HISTORY_SLOTS; i++) {
            int row = i / cols;
            int col = i % cols;
            int sx = startX + col * (side + gap);
            int sy = startY + row * (side + labelH + gap) + labelH;
            QRect r(sx, sy, side, side);
            m_historyRects[i] = r;

            {
                QFont af; af.setBold(true); af.setPixelSize(labelFontHist); p.setFont(af);
                p.setPen(QColor(255, 255, 0));
                QFontMetrics afm(af);
                QString lab = QString(QChar('a' + i));
                p.drawText(r.x() + (r.width() - afm.horizontalAdvance(lab)) / 2,
                           r.y() - 3, lab);
            }

            p.setPen(QPen(QColor(255, 255, 0), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);

            bool isActive = (i == m_activeSlot);
            bool hasImage = isActive ? !m_image.isNull() : m_history[i].occupied;

            if (hasImage) {
                QRect inner = r.adjusted(1, 1, -1, -1);
                if (isActive)
                    p.drawImage(inner, m_image);
                else
                    p.drawImage(inner, m_history[i].image);

                // Horizontal stripe overlay for the active slot
                if (isActive) {
                    p.save();
                    p.setClipRect(inner);
                    int stripeH = std::max(2, inner.height() / 10);
                    for (int sy2 = inner.top(); sy2 < inner.bottom(); sy2 += stripeH * 2) {
                        // Bright stripe (line)
                        int h1 = std::min(stripeH, inner.bottom() - sy2);
                        p.fillRect(inner.left(), sy2, inner.width(), h1,
                                   QColor(255, 255, 255, 179));
                        // Dark stripe (space)
                        if (sy2 + stripeH < inner.bottom()) {
                            int h2 = std::min(stripeH, inner.bottom() - sy2 - stripeH);
                            p.fillRect(inner.left(), sy2 + stripeH, inner.width(), h2,
                                       QColor(0, 0, 0, 179));
                        }
                    }
                    p.restore();
                }
            }
        }
    }

    // ---- Panel 4: power spectrum history (below panel 2) – 2 rows × 8 columns -
    {
        int p4x = cx + 2;
        int p4y = hy + 2;
        int p4w = width() - p4x;
        int p4h = height() - p4y;

        int cols = 8, rows = 2;
        int margin = 8;
        int availW = p4w - 2 * margin;
        int availH = p4h - 2 * margin;
        int gap = 6;
        int labelH = labelFontHist + 4;
        int sideFromW = (availW - (cols - 1) * gap) / cols;
        int sideFromH = (availH - gap - rows * labelH) / rows;
        int side = std::min(sideFromW, sideFromH);
        if (side < 10) side = 10;

        int totalW = cols * side + (cols - 1) * gap;
        int totalH = rows * (side + labelH) + gap;
        int startX = p4x + (p4w - totalW) / 2;
        int startY = p4y + (p4h - totalH) / 2;

        for (int i = 0; i < HISTORY_SLOTS; i++) {
            int row = i / cols;
            int col = i % cols;
            int sx = startX + col * (side + gap);
            int sy = startY + row * (side + labelH + gap) + labelH;
            QRect r(sx, sy, side, side);
            m_powerSpecRects[i] = r;

            {
                QFont af; af.setBold(true); af.setPixelSize(labelFontHist); p.setFont(af);
                p.setPen(QColor(255, 255, 0));
                QFontMetrics afm(af);
                QString lab = QString(QChar('A' + i));
                p.drawText(r.x() + (r.width() - afm.horizontalAdvance(lab)) / 2,
                           r.y() - 3, lab);
            }

            p.setPen(QPen(QColor(255, 255, 0), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);

            bool isActive = (i == m_activeSlot);
            QRect inner = r.adjusted(1, 1, -1, -1);

            if (isActive && m_ftComputed && !m_powerImg.isNull()) {
                p.drawImage(inner, m_powerImg);
            } else if (!isActive && m_history[i].occupied
                       && !m_history[i].powerSpecImg.isNull()) {
                p.drawImage(inner, m_history[i].powerSpecImg);
            }
        }
    }

    // ---- dividers -------------------------------------------------------------
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0));
    p.drawRect(cx - 1, 0, 3, height());
    p.setBrush(QColor(50, 50, 50));
    p.drawRect(0, hy - 1, width(), 2);

    // ---- title bar ------------------------------------------------------------
    {
        QFont titleFont;
        titleFont.setBold(true);
        titleFont.setPixelSize(18);
        p.setFont(titleFont);
        QFontMetrics tfm(titleFont);
        QString title = "Fourier Analyzer";
        int tw = tfm.horizontalAdvance(title);
        int th = tfm.height();
        int pad = 8;
        int tx = (width() - tw) / 2 - pad;
        int ty = 4;
        QRect titleRect(tx, ty, tw + 2 * pad, th + 2 * pad);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(75, 75, 75));
        p.drawRect(titleRect);

        p.setPen(QPen(Qt::white, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(titleRect);

        p.drawText(titleRect, Qt::AlignCenter, title);
    }

    // ---- arrows ---------------------------------------------------------------
    p.setRenderHint(QPainter::Antialiasing, true);

    int arrowW = std::min({width() / 4, hy / 4, 260});
    int arrowH = std::max(arrowW / 5, 20);
    int headW  = arrowH;
    int gap    = arrowH * 2;
    int radius = std::min(6, arrowH / 4);
    int totalH = arrowH * 2 + gap;
    int topY   = (hy - totalH) / 2;

    // Upper arrow: left-to-right ("FT")
    {
        int ax = cx - arrowW / 2;
        int ay = topY;
        QPainterPath path;
        int bodyW = arrowW - headW;
        path.moveTo(ax + radius, ay);
        path.lineTo(ax + bodyW, ay);
        path.lineTo(ax + bodyW, ay - arrowH * 0.15);
        path.lineTo(ax + arrowW, ay + arrowH / 2.0);
        path.lineTo(ax + bodyW, ay + arrowH + arrowH * 0.15);
        path.lineTo(ax + bodyW, ay + arrowH);
        path.lineTo(ax + radius, ay + arrowH);
        path.arcTo(ax, ay + arrowH - 2 * radius, 2 * radius, 2 * radius, -90, -90);
        path.lineTo(ax, ay + radius);
        path.arcTo(ax, ay, 2 * radius, 2 * radius, 180, -90);
        path.closeSubpath();

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(100, 100, 100));
        p.save(); p.translate(2, 2); p.drawPath(path); p.restore();

        QLinearGradient grad(ax, ay, ax, ay + arrowH);
        grad.setColorAt(0.0, QColor(230, 230, 230));
        grad.setColorAt(0.4, QColor(210, 210, 210));
        grad.setColorAt(1.0, QColor(170, 170, 170));
        p.setBrush(grad);
        p.setPen(QPen(QColor(140, 140, 140), 1));
        p.drawPath(path);

        p.setPen(QPen(QColor(255, 255, 255, 120), 1));
        p.drawLine(ax + radius, ay + 1, ax + bodyW - 1, ay + 1);

        if (m_fftProgress >= 0.0 && m_fftProgress <= 1.0) {
            p.save();
            int clipW = ax + (int)(arrowW * m_fftProgress);
            p.setClipRect(ax, ay - arrowH, clipW - ax, arrowH * 3);
            p.setBrush(QColor(40, 100, 220, 180));
            p.setPen(Qt::NoPen);
            p.drawPath(path);
            p.restore();
        }

        QFont font; font.setBold(true); font.setPixelSize(arrowH * 0.55);
        p.setFont(font);
        p.setPen(QColor(30, 30, 30));
        p.drawText(QRect(ax, ay, bodyW, arrowH), Qt::AlignCenter, "FT");
    }

    // Lower arrow: right-to-left ("FT-1")
    {
        int ax = cx - arrowW / 2;
        int ay = topY + arrowH + gap;
        QPainterPath path;
        int bodyX = ax + headW;
        int bodyW = arrowW - headW;
        path.moveTo(ax, ay + arrowH / 2.0);
        path.lineTo(ax + headW, ay - arrowH * 0.15);
        path.lineTo(ax + headW, ay);
        path.lineTo(ax + arrowW - radius, ay);
        path.arcTo(ax + arrowW - 2 * radius, ay, 2 * radius, 2 * radius, 90, -90);
        path.lineTo(ax + arrowW, ay + arrowH - radius);
        path.arcTo(ax + arrowW - 2 * radius, ay + arrowH - 2 * radius, 2 * radius, 2 * radius, 0, -90);
        path.lineTo(ax + headW, ay + arrowH);
        path.lineTo(ax + headW, ay + arrowH + arrowH * 0.15);
        path.closeSubpath();

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(100, 100, 100));
        p.save(); p.translate(2, 2); p.drawPath(path); p.restore();

        QLinearGradient grad(ax, ay, ax, ay + arrowH);
        grad.setColorAt(0.0, QColor(230, 230, 230));
        grad.setColorAt(0.4, QColor(210, 210, 210));
        grad.setColorAt(1.0, QColor(170, 170, 170));
        p.setBrush(grad);
        p.setPen(QPen(QColor(140, 140, 140), 1));
        p.drawPath(path);

        p.setPen(QPen(QColor(255, 255, 255, 120), 1));
        p.drawLine(bodyX + 1, ay + 1, ax + arrowW - radius, ay + 1);

        if (m_iftProgress >= 0.0 && m_iftProgress <= 1.0) {
            p.save();
            int clipX = ax + arrowW - (int)(arrowW * m_iftProgress);
            p.setClipRect(clipX, ay - arrowH, ax + arrowW - clipX, arrowH * 3);
            p.setBrush(QColor(40, 100, 220, 180));
            p.setPen(Qt::NoPen);
            p.drawPath(path);
            p.restore();
        }

        int fontSize = arrowH * 0.55;
        QFont font; font.setBold(true); font.setPixelSize(fontSize);
        p.setFont(font);
        p.setPen(QColor(30, 30, 30));

        QFontMetrics fm(font);
        QString base = "FT";
        int baseW = fm.horizontalAdvance(base);

        QFont superFont; superFont.setBold(true); superFont.setPixelSize(fontSize * 0.6);
        QFontMetrics sfm(superFont);
        QString sup = "-1";
        int supW = sfm.horizontalAdvance(sup);

        int totalTextW = baseW + supW + 1;
        QRect bodyRect(bodyX, ay, bodyW, arrowH);
        int textX = bodyRect.x() + (bodyRect.width() - totalTextW) / 2;
        int textY = bodyRect.y() + (bodyRect.height() + fm.ascent() - fm.descent()) / 2;

        p.setFont(font);
        p.drawText(textX, textY, base);
        p.setFont(superFont);
        p.drawText(textX + baseW + 1, textY - fontSize * 0.3, sup);
    }

    p.setRenderHint(QPainter::Antialiasing, false);
}
