#include "ftwindow.h"
#include "mrcloader.h"
#include "fft.h"

#include <QApplication>
#include <QScreen>
#include <QPainter>
#include <QPainterPath>
#include <QFileDialog>
#include <QSettings>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFont>
#include <QFontMetrics>
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <thread>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
//  Constructor
// ---------------------------------------------------------------------------
FtWindow::FtWindow(QWidget *parent) : QWidget(parent)
{
    setWindowTitle("ft");

    setMouseTracking(true);

    QScreen *screen = QApplication::primaryScreen();
    QRect available = screen->availableGeometry();
    setGeometry(available);

    // Load button
    m_loadBtn = new QPushButton("Load image", this);
    m_loadBtn->setFixedSize(100, 30);
    connect(m_loadBtn, &QPushButton::clicked, this, &FtWindow::onLoadImage);

    // Mode cycle button
    m_modeBtn = new QPushButton(modeLabel(), this);
    m_modeBtn->setFixedSize(180, 30);
    connect(m_modeBtn, &QPushButton::clicked, this, &FtWindow::onCycleMode);
    m_modeBtn->hide();

    // Mask-center toggle button
    m_maskBtn = new QPushButton("mask center for display", this);
    m_maskBtn->setFixedSize(180, 30);
    m_maskBtn->setCheckable(true);
    m_maskBtn->setStyleSheet(
        "QPushButton        { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }"
        "QPushButton:checked { background-color: #444; border: 2px inset #333; color: #ccc; }");
    connect(m_maskBtn, &QPushButton::toggled, this, &FtWindow::onToggleMask);
    m_maskBtn->hide();

    // Restore history first (so loadImageFile doesn't push empty into history)
    restoreHistory();

    // Restore last file
    QSettings settings("ft", "ft");
    QString lastFile = settings.value("lastFile").toString();
    if (!lastFile.isEmpty() && QFile::exists(lastFile))
        loadImageFile(lastFile);
}

// ---------------------------------------------------------------------------
//  Layout
// ---------------------------------------------------------------------------
void FtWindow::resizeEvent(QResizeEvent *)
{
    m_loadBtn->move(8, 8);
    m_modeBtn->move(width() - m_modeBtn->width() - 8, 8);
    m_maskBtn->move(width() - m_maskBtn->width() - 8, 8 + m_modeBtn->height() + 4);
}

// ---------------------------------------------------------------------------
//  Mouse
// ---------------------------------------------------------------------------
void FtWindow::mousePressEvent(QMouseEvent *event)
{
    // Check history slot clicks (panel 3)
    for (int i = 0; i < HISTORY_SLOTS; i++) {
        if (m_history[i].occupied && m_historyRects[i].contains(event->pos())) {
            // Take the clicked entry out
            HistoryEntry clicked = std::move(m_history[i]);

            // Remove slot i: shift slots i+1.. left to close the gap
            for (int j = i; j < HISTORY_SLOTS - 1; j++)
                m_history[j] = std::move(m_history[j + 1]);
            m_history[HISTORY_SLOTS - 1].occupied = false;

            // Now push current panel 1 image into slot 0, shifting everything right
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

            // Load clicked entry into panel 1
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

    // Check tool button clicks (panel 2 right edge)
    if (m_toolBtnRects[0].contains(event->pos())) {
        m_eraserActive = !m_eraserActive;
        if (m_eraserActive) m_brushActive = false;
        update();
        return;
    }
    if (m_toolBtnRects[1].contains(event->pos())) {
        m_brushActive = !m_brushActive;
        if (m_brushActive) m_eraserActive = false;
        update();
        return;
    }

    // Eraser / Brush: click on FT image, start drag
    if ((m_eraserActive || m_brushActive) && m_ftComputed) {
        if (m_eraserActive) eraserApply(event->pos());
        else                brushApply(event->pos());
        m_toolDragging = true;
        return;
    }

    if (m_image.isNull()) return;
    QRect arrowRect = upperArrowBounds();
    if (arrowRect.contains(event->pos())) {
        computeFFT();
        update();
    }
}

void FtWindow::mouseReleaseEvent(QMouseEvent *)
{
    if (m_toolDragging) {
        m_toolDragging = false;
    }
}

void FtWindow::mouseMoveEvent(QMouseEvent *event)
{
    m_mousePos = event->pos();

    // Tool drag: apply along the trace
    if (m_toolDragging && m_ftComputed) {
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
        double oldFactor = z.factor;

        // scroll up = zoom in, down = zoom out
        double step = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        double newFactor = oldFactor * step;
        if (newFactor < 1.0) newFactor = 1.0;
        if (newFactor > 64.0) newFactor = 64.0;

        if (newFactor <= 1.0) {
            z.reset(di.imgW, di.imgH);
        } else {
            // keep image point under mouse fixed
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

            // clamp
            double hw = newVW / 2.0, hh = newVH / 2.0;
            z.centerX = std::clamp(z.centerX, hw, di.imgW - hw);
            z.centerY = std::clamp(z.centerY, hh, di.imgH - hh);
        }

        // In modes 0/1 (side-by-side), sync both FT zoom states
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
    int labelFontHist = std::clamp(std::min(cx, height() - hy) / 4, 10, 36);

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
        int btnSide = width() * 5 / 400;
        int gap = 2;
        int totalH = 8 * btnSide + 7 * gap;
        int startY = (hy - totalH) / 2;

        int offset = btnSide / 2;

        // Panel 1: left edge
        for (int i = 0; i < 8; i++) {
            int by = startY + i * (btnSide + gap);
            QRect r(offset, by, btnSide, btnSide);
            p.setPen(QPen(Qt::white, 1));
            p.setBrush(QColor(0, 0, 0));
            p.drawRect(r);
        }

        // Panel 2: right edge
        for (int i = 0; i < 8; i++) {
            int by = startY + i * (btnSide + gap);
            QRect r(width() - btnSide - offset, by, btnSide, btnSide);
            m_toolBtnRects[i] = r;

            p.setPen(QPen(Qt::white, 1));
            if ((i == 0 && m_eraserActive) || (i == 1 && m_brushActive))
                p.setBrush(QColor(60, 60, 60));
            else
                p.setBrush(QColor(0, 0, 0));
            p.drawRect(r);

            // Eraser icon in first button
            if (i == 0) {
                p.setRenderHint(QPainter::Antialiasing, true);
                QRect ir = r.adjusted(3, 3, -3, -3);
                int ix = ir.x(), iy = ir.y(), iw = ir.width(), ih = ir.height();

                // eraser body (tilted rectangle)
                QPainterPath ep;
                ep.moveTo(ix + iw * 0.2, iy + ih * 0.1);
                ep.lineTo(ix + iw * 0.9, iy + ih * 0.1);
                ep.lineTo(ix + iw * 0.8, iy + ih * 0.9);
                ep.lineTo(ix + iw * 0.1, iy + ih * 0.9);
                ep.closeSubpath();
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(200, 180, 160));
                p.drawPath(ep);

                // rubber tip (bottom portion)
                QPainterPath tp;
                tp.moveTo(ix + iw * 0.1, iy + ih * 0.9);
                tp.lineTo(ix + iw * 0.15, iy + ih * 0.55);
                tp.lineTo(ix + iw * 0.85, iy + ih * 0.55);
                tp.lineTo(ix + iw * 0.8, iy + ih * 0.9);
                tp.closeSubpath();
                p.setBrush(QColor(230, 100, 100));
                p.drawPath(tp);

                // divider line
                p.setPen(QPen(QColor(120, 80, 60), std::max(1, iw / 10)));
                p.drawLine(ix + iw * 0.15, iy + ih * 0.55, ix + iw * 0.85, iy + ih * 0.55);

                p.setRenderHint(QPainter::Antialiasing, false);

                // Tooltip on hover
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

                // handle
                p.setPen(QPen(QColor(160, 120, 60), std::max(1, bw / 6)));
                p.drawLine(bx + bw * 0.5, by2 + bh * 0.05, bx + bw * 0.5, by2 + bh * 0.45);

                // ferrule
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(180, 180, 180));
                p.drawRect(bx + bw * 0.25, by2 + bh * 0.40, bw * 0.5, bh * 0.15);

                // bristles
                p.setBrush(QColor(200, 160, 80));
                QPainterPath br;
                br.moveTo(bx + bw * 0.2, by2 + bh * 0.55);
                br.lineTo(bx + bw * 0.8, by2 + bh * 0.55);
                br.lineTo(bx + bw * 0.65, by2 + bh * 0.95);
                br.lineTo(bx + bw * 0.35, by2 + bh * 0.95);
                br.closeSubpath();
                p.drawPath(br);

                p.setRenderHint(QPainter::Antialiasing, false);

                // Tooltip on hover
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

        // Label "a" above center of image
        {
            QFont lf; lf.setBold(true); lf.setPixelSize(labelFontMain); p.setFont(lf);
            p.setPen(QColor(255, 255, 0));
            QFontMetrics lfm(lf);
            QString lab = "a";
            p.drawText(frame.x() + (frame.width() - lfm.horizontalAdvance(lab)) / 2,
                       frame.y() - 6, lab);
        }

        drawImageWithFrame(p, frame, m_image, m_zoom[0], imgW, imgH);
        drawAxes(p, frame, m_zoom[0], imgW, imgH, false, m_pixelSize);

        QRect inner = frame.adjusted(2, 2, -2, -2);
        double curVal = 0;
        bool hasCur = sampleValue(inner, m_zoom[0], imgW, imgH, m_imageRawPixels, curVal);
        drawMinMax(p, frame, m_imageMinVal, m_imageMaxVal, curVal, hasCur);
        drawHistogram(p, frame, m_imageRawPixels, m_imageMinVal, m_imageMaxVal, hy - frame.bottom());

        // Pixel size label to the right of the histogram
        {
            int avail = hy - frame.bottom();
            int histW = frame.width() / 2;
            int histH = std::max(16, avail / 3);
            int histX = frame.x() + (frame.width() - histW) / 2;
            int histY = frame.bottom() + histH;
            QFont pf;
            pf.setPixelSize(11);
            p.setFont(pf);
            p.setPen(Qt::white);
            QString psLabel = QString("1 pixel = %1 %2")
                                  .arg(m_pixelSize, 0, 'g', 4)
                                  .arg(QString::fromUtf8("\u00C5"));
            p.drawText(histX + histW + 8, histY + histH / 2 + QFontMetrics(pf).ascent() / 2, psLabel);
        }

        DisplayItem &di = m_dispItems[m_numDispItems++];
        di = { inner, imgW, imgH, 0, &m_imageRawPixels, true };
    }

    // ---- Panel 2: FFT results -------------------------------------------------
    if (m_ftComputed) {
        int panel2X = cx + 2;
        int panel2W = width() - panel2X;
        int panel2H = hy - 1;

        if (m_displayMode == 2 || m_displayMode == 3) {
            // Single square display (power spectrum or complex FT)
            int side = static_cast<int>(0.7 * std::min(panel2W, panel2H));
            int fx = panel2X + (panel2W - side) / 2;
            int fy = (panel2H - side) / 2;
            QRect frame(fx, fy, side, side);

            // Label "A" above center, title above top-left
            {
                QFont af; af.setBold(true); af.setPixelSize(labelFontMain); p.setFont(af);
                p.setPen(QColor(255, 255, 0));
                QFontMetrics afm(af);
                p.drawText(frame.x() + (frame.width() - afm.horizontalAdvance("A")) / 2,
                           frame.y() - 22, "A");
            }
            p.setPen(QColor(200, 200, 200));
            QFont lf; lf.setPixelSize(14); p.setFont(lf);
            if (m_displayMode == 2)
                p.drawText(frame.x(), frame.y() - 4, "Powerspectrum");
            else
                p.drawText(frame.x(), frame.y() - 4, "Complex Fourier transform");

            const QImage &img = (m_displayMode == 2) ? m_powerImg : m_complexImg;
            drawImageWithFrame(p, frame, img, m_zoom[1], m_fftN, m_fftN);
            drawAxes(p, frame, m_zoom[1], m_fftN, m_fftN, true, m_pixelSize);

            QRect inner = frame.adjusted(2, 2, -2, -2);
            double curVal = 0;
            bool hasCur = sampleValue(inner, m_zoom[1], m_fftN, m_fftN, m_powerVals, curVal);

            if (m_displayMode == 3) {
                // Show separate amplitude and phase readout
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

            DisplayItem &di = m_dispItems[m_numDispItems++];
            di = { inner, m_fftN, m_fftN, 1, &m_powerVals, true };
        } else {
            // Mode 0 or 1: two images side by side
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
                label1 = "cosinus"; label2 = "sinus";
            } else {
                img1 = &m_ampImg;  img2 = &m_phaseImg;
                vals1 = &m_ampVals; vals2 = &m_phaseVals;
                min1 = m_ampMin; max1 = m_ampMax;
                min2 = m_phaseMin; max2 = m_phaseMax;
                label1 = "amplitude"; label2 = "phase";
            }

            // Label "A" above center of both frames combined
            {
                QFont af; af.setBold(true); af.setPixelSize(labelFontMain); p.setFont(af);
                p.setPen(QColor(255, 255, 0));
                QFontMetrics afm(af);
                int combinedX = frame1.x();
                int combinedW = frame2.right() - frame1.x();
                p.drawText(combinedX + (combinedW - afm.horizontalAdvance("A")) / 2,
                           frame1.y() - 22, "A");
            }

            // Labels above frames
            p.setPen(QColor(200, 200, 200));
            QFont lf;
            lf.setPixelSize(14);
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

            DisplayItem &d1 = m_dispItems[m_numDispItems++];
            d1 = { inner1, m_fftN, m_fftN, 1, vals1, true };
            DisplayItem &d2 = m_dispItems[m_numDispItems++];
            d2 = { inner2, m_fftN, m_fftN, 2, vals2, true };
        }
    }

    // ---- Panel 3: image history (below panel 1) --------------------------------
    {
        int p3x = 0;
        int p3y = hy + 2;
        int p3w = cx - 1;
        int p3h = height() - p3y;

        int margin = 8;
        int availW = p3w - 2 * margin;
        int availH = p3h - 2 * margin;
        int gap = 6;
        int side = std::min(availH, (availW - (HISTORY_SLOTS - 1) * gap) / HISTORY_SLOTS);
        if (side < 10) side = 10;

        int totalW = HISTORY_SLOTS * side + (HISTORY_SLOTS - 1) * gap;
        int startX = p3x + (p3w - totalW) / 2;
        int startY = p3y + (p3h - side) / 2;

        for (int i = 0; i < HISTORY_SLOTS; i++) {
            int sx = startX + i * (side + gap);
            QRect r(sx, startY, side, side);
            m_historyRects[i] = r;

            // Label "b", "c", ... above center
            {
                QFont af; af.setBold(true); af.setPixelSize(labelFontHist); p.setFont(af);
                p.setPen(QColor(255, 255, 0));
                QFontMetrics afm(af);
                QString lab = QString(QChar('b' + i));
                p.drawText(r.x() + (r.width() - afm.horizontalAdvance(lab)) / 2,
                           r.y() - 3, lab);
            }

            // Yellow border
            p.setPen(QPen(QColor(255, 255, 0), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);

            if (m_history[i].occupied) {
                QRect inner = r.adjusted(1, 1, -1, -1);
                p.drawImage(inner, m_history[i].image);
            }
        }
    }

    // ---- Panel 4: power spectrum history (below panel 2) -----------------------
    {
        int p4x = cx + 2;
        int p4y = hy + 2;
        int p4w = width() - p4x;
        int p4h = height() - p4y;

        int margin = 8;
        int availW = p4w - 2 * margin;
        int availH = p4h - 2 * margin;
        int gap = 6;
        int side = std::min(availH, (availW - (HISTORY_SLOTS - 1) * gap) / HISTORY_SLOTS);
        if (side < 10) side = 10;

        int totalW = HISTORY_SLOTS * side + (HISTORY_SLOTS - 1) * gap;
        int startX = p4x + (p4w - totalW) / 2;
        int startY = p4y + (p4h - side) / 2;

        for (int i = 0; i < HISTORY_SLOTS; i++) {
            int sx = startX + i * (side + gap);
            QRect r(sx, startY, side, side);
            m_powerSpecRects[i] = r;

            // Label "B", "C", ... above center
            {
                QFont af; af.setBold(true); af.setPixelSize(labelFontHist); p.setFont(af);
                p.setPen(QColor(255, 255, 0));
                QFontMetrics afm(af);
                QString lab = QString(QChar('B' + i));
                p.drawText(r.x() + (r.width() - afm.horizontalAdvance(lab)) / 2,
                           r.y() - 3, lab);
            }

            p.setPen(QPen(QColor(255, 255, 0), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);

            if (m_history[i].occupied && !m_history[i].powerSpecImg.isNull()) {
                QRect inner = r.adjusted(1, 1, -1, -1);
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

        // Blue progress overlay
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

    // Lower arrow: right-to-left ("FT⁻¹")
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

// ---------------------------------------------------------------------------
//  Drawing helpers
// ---------------------------------------------------------------------------
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

    // --- helper: format the second-row (physical) label for one tick -----------
    auto physLabel = [&](double val, bool isReciprocal) -> QString {
        if (isReciprocal) {
            // val is in reciprocal pixels; convert to reciprocal Angstrom
            // spatial freq = val / (N * pixelSize)
            double freq = val / (imgW * pixelSize);
            if (std::abs(freq) < 1e-12)
                return QString::fromUtf8("(\u221E)\u207B\u00B9");  // (∞)⁻¹
            // display as (X Å)⁻¹  where X = 1/|freq|
            double res = 1.0 / std::abs(freq);
            return QString("(%1 %2)%3")
                       .arg(res, 0, 'g', 3)
                       .arg(QString::fromUtf8("\u00C5"))
                       .arg(QString::fromUtf8("\u207B\u00B9"));  // ⁻¹
        } else {
            // val is pixel index; convert to Angstrom
            double ang = val * pixelSize;
            return QString::number(ang, 'g', 4) + QString::fromUtf8(" \u00C5");
        }
    };

    // --- horizontal axis (below image) -----------------------------------------
    int axisY1 = frame.bottom() + 4;                 // row 1: pixel units
    int axisY2 = axisY1 + lineH + 1;                 // row 2: physical units
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

    // --- vertical axis ---------------------------------------------------------
    for (int i = 0; i < numTicks; i++) {
        double frac = i / (double)(numTicks - 1);
        double val  = yStart + frac * (yEnd - yStart);
        int sy = frame.top() + (int)(frac * frame.height());

        QString lab1 = QString::number((int)std::round(val));
        QString lab2 = physLabel(val, reciprocal);
        int lw1 = fm.horizontalAdvance(lab1);
        int lw2 = fm.horizontalAdvance(lab2);

        // choose Y position: flush top / bottom for first / last tick
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
            p.drawText(x0 + (maxLw - lw1), ty1, lab1);   // right-align row 1
            p.drawText(x0 + (maxLw - lw2), ty2, lab2);   // right-align row 2
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

    QString text;
    if (hasCur)
        text = QString("Current: %1     Min: %2     Max: %3")
                   .arg(curVal, 0, 'g', 5)
                   .arg(minVal, 0, 'g', 5)
                   .arg(maxVal, 0, 'g', 5);
    else
        text = QString("Min: %1     Max: %2")
                   .arg(minVal, 0, 'g', 5)
                   .arg(maxVal, 0, 'g', 5);

    QFontMetrics fm(f);
    int tw = fm.horizontalAdvance(text);
    p.drawText(frame.right() - tw, frame.top() - 5, text);
}

void FtWindow::drawHistogram(QPainter &p, const QRect &frame,
                              const std::vector<double> &vals,
                              double minVal, double maxVal,
                              int availableBelow)
{
    if (vals.empty()) return;

    // histogram takes about 1/3 of available space, with equal gap above
    int histH = std::max(16, availableBelow / 3);
    int histW = frame.width() / 2;
    int histX = frame.x() + (frame.width() - histW) / 2;
    int histY = frame.bottom() + histH;  // gap = histH above the histogram

    // compute bins
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

    // border
    p.setPen(QPen(QColor(200, 200, 200), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(histX, histY, histW, histH);

    // draw bars
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

    // labels: min at bottom-left, max at bottom-right
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
}

// ---------------------------------------------------------------------------
//  Slots
// ---------------------------------------------------------------------------
void FtWindow::onLoadImage()
{
    QSettings settings("ft", "ft");
    QString lastDir = QFileInfo(settings.value("lastFile").toString()).absolutePath();

    QString path = QFileDialog::getOpenFileName(
        this, "Load image", lastDir,
        "Images (*.tif *.tiff *.jpg *.jpeg *.png *.mrc *.MRC)");

    if (path.isEmpty()) return;
    loadImageFile(path);
}

void FtWindow::onCycleMode()
{
    m_displayMode = (m_displayMode + 1) % 4;
    m_modeBtn->setText(modeLabel());
    update();
}

void FtWindow::onToggleMask(bool checked)
{
    m_maskCenter = checked;
    recomputeDisplayImages();
    update();
}

// ---------------------------------------------------------------------------
//  Loading
// ---------------------------------------------------------------------------
void FtWindow::loadImageFile(const QString &path)
{
    qDebug() << "Loading image:" << path;

    // Push current image into history (shift right, slot 0 = most recent)
    if (!m_image.isNull()) {
        for (int i = HISTORY_SLOTS - 1; i > 0; i--)
            m_history[i] = std::move(m_history[i - 1]);
        m_history[0].image     = m_image;
        m_history[0].path      = m_imagePath;
        m_history[0].rawPixels    = m_imageRawPixels;
        m_history[0].minVal       = m_imageMinVal;
        m_history[0].maxVal       = m_imageMaxVal;
        m_history[0].pixelSize    = m_pixelSize;
        m_history[0].powerSpecImg = computePowerSpecMasked(m_image);
        m_history[0].occupied     = true;
        saveHistory();
    }

    if (path.endsWith(".mrc", Qt::CaseInsensitive)) {
        MrcResult r = loadMrc(path);
        m_image = r.image;
        m_imageRawPixels = std::move(r.rawPixels);
        m_imageMinVal = r.minVal;
        m_imageMaxVal = r.maxVal;
        m_pixelSize = r.pixelSize;

        if (m_image.isNull()) {
            qDebug() << "MRC load FAILED – image is null";
        } else {
            qDebug() << "MRC load OK –" << m_image.width() << "x" << m_image.height();
        }
    } else {
        m_image = QImage(path);
        m_pixelSize = 1.0;
        if (m_image.isNull()) {
            qDebug() << "Image load FAILED for:" << path;
        } else {
            qDebug() << "Image loaded:" << m_image.width() << "x" << m_image.height()
                     << "format:" << m_image.format();
            extractImageData();
        }
    }

    m_imagePath = path;

    QSettings settings("ft", "ft");
    settings.setValue("lastFile", path);

    m_ftComputed = false;
    m_displayMode = 2;  // default to powerspectrum
    m_modeBtn->setText(modeLabel());
    m_modeBtn->hide();
    m_maskBtn->hide();
    m_maskBtn->setChecked(false);
    m_maskCenter = false;

    // reset zoom
    if (!m_image.isNull()) {
        m_zoom[0].reset(m_image.width(), m_image.height());
        computeFFT();
    }

    update();
}

void FtWindow::extractImageData()
{
    QImage gray = m_image.convertToFormat(QImage::Format_Grayscale8);
    int w = gray.width(), h = gray.height();
    m_imageRawPixels.resize((size_t)w * h);
    for (int y = 0; y < h; y++) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < w; x++)
            m_imageRawPixels[y * w + x] = row[x];
    }
    m_imageMinVal = *std::min_element(m_imageRawPixels.begin(), m_imageRawPixels.end());
    m_imageMaxVal = *std::max_element(m_imageRawPixels.begin(), m_imageRawPixels.end());
}

// ---------------------------------------------------------------------------
//  FFT
// ---------------------------------------------------------------------------
void FtWindow::computeFFT()
{
    if (m_image.isNull()) return;

    QImage gray = m_image.convertToFormat(QImage::Format_Grayscale8);
    int w = gray.width();
    int h = gray.height();
    int N = nextPow2(std::max(w, h));
    m_fftN = N;

    std::vector<Complex> data(N * N, Complex(0.0, 0.0));
    for (int y = 0; y < h; y++) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < w; x++)
            data[y * N + x] = Complex(row[x], 0.0);
    }

    // 2D FFT with animated progress, parallelised in batches
    m_fftProgress = 0.0;
    update();
    QApplication::processEvents();

    {
        int nThreads = (int)std::thread::hardware_concurrency();
        if (nThreads < 1) nThreads = 1;
        int batchSize = nThreads * 16;   // rows/cols per progress step

        // Row-wise FFT (0% – 50%)
        for (int b = 0; b < N; b += batchSize) {
            int bEnd = std::min(b + batchSize, N);
            std::vector<std::thread> threads;
            int perThread = ((bEnd - b) + nThreads - 1) / nThreads;
            for (int t = 0; t < nThreads; t++) {
                int y0 = b + t * perThread;
                int y1 = std::min(y0 + perThread, bEnd);
                if (y0 < y1)
                    threads.emplace_back([&data, N, y0, y1]() {
                        std::vector<Complex> row(N);
                        for (int y = y0; y < y1; y++) {
                            for (int x = 0; x < N; x++) row[x] = data[y * N + x];
                            fft1d(row, false);
                            for (int x = 0; x < N; x++) data[y * N + x] = row[x];
                        }
                    });
            }
            for (auto &t : threads) t.join();
            m_fftProgress = 0.5 * bEnd / N;
            update();
            QApplication::processEvents();
        }

        // Column-wise FFT (50% – 100%)
        for (int b = 0; b < N; b += batchSize) {
            int bEnd = std::min(b + batchSize, N);
            std::vector<std::thread> threads;
            int perThread = ((bEnd - b) + nThreads - 1) / nThreads;
            for (int t = 0; t < nThreads; t++) {
                int x0 = b + t * perThread;
                int x1 = std::min(x0 + perThread, bEnd);
                if (x0 < x1)
                    threads.emplace_back([&data, N, x0, x1]() {
                        std::vector<Complex> col(N);
                        for (int x = x0; x < x1; x++) {
                            for (int y = 0; y < N; y++) col[y] = data[y * N + x];
                            fft1d(col, false);
                            for (int y = 0; y < N; y++) data[y * N + x] = col[y];
                        }
                    });
            }
            for (auto &t : threads) t.join();
            m_fftProgress = 0.5 + 0.5 * bEnd / N;
            update();
            QApplication::processEvents();
        }
    }

    fftShift(data, N);

    m_fftData = data;   // keep for mask toggle

    recomputeDisplayImages();

    m_fftProgress = -1;
    m_ftComputed = true;
    m_modeBtn->show();
    m_maskBtn->show();

    // reset FT zoom states
    m_zoom[1].reset(N, N);
    m_zoom[2].reset(N, N);
}

void FtWindow::recomputeDisplayImages()
{
    int N = m_fftN;
    int total = N * N;
    if (total == 0) return;

    std::vector<Complex> data = m_fftData;

    if (m_maskCenter) {
        int half = N / 2;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int x = half + dx;
                int y = half + dy;
                if (x >= 0 && x < N && y >= 0 && y < N)
                    data[y * N + x] = Complex(0, 0);
            }
        }
    }

    m_cosVals.resize(total);
    m_sinVals.resize(total);
    m_ampVals.resize(total);
    m_phaseVals.resize(total);
    m_powerVals.resize(total);

    for (int i = 0; i < total; i++) {
        m_cosVals[i]   = data[i].real();
        m_sinVals[i]   = data[i].imag();
        double amp     = std::abs(data[i]);
        m_ampVals[i]   = std::log(1.0 + amp);
        m_phaseVals[i] = std::arg(data[i]) * 180.0 / M_PI;
        m_powerVals[i] = std::log(1.0 + amp * amp);
    }

    m_cosImg   = floatToImage(m_cosVals,   N);
    m_sinImg   = floatToImage(m_sinVals,   N);
    m_ampImg   = floatToImage(m_ampVals,   N);
    m_phaseImg = floatToImage(m_phaseVals, N);
    m_powerImg = floatToImage(m_powerVals, N);

    // Complex FT image: brightness = power, hue = phase
    {
        double pMin = *std::min_element(m_powerVals.begin(), m_powerVals.end());
        double pMax = *std::max_element(m_powerVals.begin(), m_powerVals.end());
        double pScale = (pMax > pMin) ? 1.0 / (pMax - pMin) : 1.0;

        m_complexImg = QImage(N, N, QImage::Format_RGB32);
        for (int y = 0; y < N; y++) {
            QRgb *row = reinterpret_cast<QRgb *>(m_complexImg.scanLine(y));
            for (int x = 0; x < N; x++) {
                int idx = y * N + x;
                double val = std::clamp((m_powerVals[idx] - pMin) * pScale, 0.0, 1.0);
                // phase in [-180, 180] degrees -> hue in [0, 360)
                double hue = m_phaseVals[idx] + 180.0;
                QColor c = QColor::fromHsvF(hue / 360.0, 1.0, val);
                row[x] = c.rgb();
            }
        }
    }

    auto mm = [](const std::vector<double> &v) {
        return std::make_pair(*std::min_element(v.begin(), v.end()),
                              *std::max_element(v.begin(), v.end()));
    };
    std::tie(m_cosMin,   m_cosMax)   = mm(m_cosVals);
    std::tie(m_sinMin,   m_sinMax)   = mm(m_sinVals);
    std::tie(m_ampMin,   m_ampMax)   = mm(m_ampVals);
    std::tie(m_phaseMin, m_phaseMax) = mm(m_phaseVals);
    std::tie(m_powerMin, m_powerMax) = mm(m_powerVals);
}

// ---------------------------------------------------------------------------
//  Utility
// ---------------------------------------------------------------------------
QRect FtWindow::upperArrowBounds() const
{
    int cx = width() / 2;
    int hy = height() - height() / 5;
    int arrowW = std::min({width() / 4, hy / 4, 260});
    int arrowH = std::max(arrowW / 5, 20);
    int gap    = arrowH * 2;
    int totalH = arrowH * 2 + gap;
    int topY   = (hy - totalH) / 2;
    int ax = cx - arrowW / 2;
    return QRect(ax, topY - arrowH * 0.15, arrowW, arrowH * 1.3);
}

QString FtWindow::modeLabel() const
{
    switch (m_displayMode) {
    case 0: return "sinus and cosinus";
    case 1: return "amplitude and phase";
    case 2: return "powerspectrum";
    case 3: return "complex Fourier transform";
    }
    return "";
}

QImage FtWindow::computePowerSpecMasked(const QImage &img)
{
    if (img.isNull()) return {};

    QImage gray = img.convertToFormat(QImage::Format_Grayscale8);
    int w = gray.width(), h = gray.height();
    int N = nextPow2(std::max(w, h));

    std::vector<Complex> data(N * N, Complex(0, 0));
    for (int y = 0; y < h; y++) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < w; x++)
            data[y * N + x] = Complex(row[x], 0.0);
    }

    fft2d(data, N, false);
    fftShift(data, N);

    // mask center 3x3
    int half = N / 2;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int px = half + dx, py = half + dy;
            if (px >= 0 && px < N && py >= 0 && py < N)
                data[py * N + px] = Complex(0, 0);
        }

    // power spectrum
    int total = N * N;
    std::vector<double> power(total);
    for (int i = 0; i < total; i++) {
        double a = std::abs(data[i]);
        power[i] = std::log(1.0 + a * a);
    }

    return floatToImage(power, N);
}

void FtWindow::saveHistory()
{
    QSettings settings("ft", "ft");
    for (int i = 0; i < HISTORY_SLOTS; i++) {
        QString key = QString("history/%1").arg(i);
        if (m_history[i].occupied)
            settings.setValue(key, m_history[i].path);
        else
            settings.remove(key);
    }
}

void FtWindow::restoreHistory()
{
    QSettings settings("ft", "ft");
    for (int i = 0; i < HISTORY_SLOTS; i++) {
        QString key = QString("history/%1").arg(i);
        QString path = settings.value(key).toString();
        if (path.isEmpty() || !QFile::exists(path)) {
            m_history[i].occupied = false;
            continue;
        }

        qDebug() << "Restoring history slot" << i << ":" << path;

        QImage img;
        std::vector<double> rawPixels;
        double minVal = 0, maxVal = 0, pixelSize = 1.0;

        if (path.endsWith(".mrc", Qt::CaseInsensitive)) {
            MrcResult r = loadMrc(path);
            img       = r.image;
            rawPixels = std::move(r.rawPixels);
            minVal    = r.minVal;
            maxVal    = r.maxVal;
            pixelSize = r.pixelSize;
        } else {
            img = QImage(path);
            if (!img.isNull()) {
                QImage gray = img.convertToFormat(QImage::Format_Grayscale8);
                int w = gray.width(), h = gray.height();
                rawPixels.resize((size_t)w * h);
                for (int y = 0; y < h; y++) {
                    const uchar *row = gray.constScanLine(y);
                    for (int x = 0; x < w; x++)
                        rawPixels[y * w + x] = row[x];
                }
                minVal = *std::min_element(rawPixels.begin(), rawPixels.end());
                maxVal = *std::max_element(rawPixels.begin(), rawPixels.end());
            }
        }

        if (img.isNull()) {
            m_history[i].occupied = false;
            continue;
        }

        m_history[i].image        = img;
        m_history[i].path         = path;
        m_history[i].rawPixels    = std::move(rawPixels);
        m_history[i].minVal       = minVal;
        m_history[i].maxVal       = maxVal;
        m_history[i].pixelSize    = pixelSize;
        m_history[i].powerSpecImg = computePowerSpecMasked(img);
        m_history[i].occupied     = true;
    }
}

void FtWindow::eraserApply(QPoint pos)
{
    if (!m_ftComputed) return;

    for (int i = 0; i < m_numDispItems; i++) {
        const DisplayItem &di = m_dispItems[i];
        if (!di.valid || di.zoomIdx < 1) continue;
        if (!di.screenRect.contains(pos)) continue;

        ZoomState &z = m_zoom[di.zoomIdx];
        QRectF src = z.visibleRect(di.imgW, di.imgH);
        double relX = (pos.x() - di.screenRect.x()) / (double)di.screenRect.width();
        double relY = (pos.y() - di.screenRect.y()) / (double)di.screenRect.height();
        int ix = (int)(src.x() + relX * src.width());
        int iy = (int)(src.y() + relY * src.height());

        if (ix >= 0 && ix < m_fftN && iy >= 0 && iy < m_fftN) {
            m_fftData[iy * m_fftN + ix] = Complex(0, 0);
            // Friedel mate
            int fx = (m_fftN - ix) % m_fftN;
            int fy = (m_fftN - iy) % m_fftN;
            m_fftData[fy * m_fftN + fx] = Complex(0, 0);
            recomputeDisplayImages();
            update();
        }
        return;
    }
}

double FtWindow::brushValue() const
{
    if (!m_ftComputed || m_fftN == 0) return 1.0;
    int half = m_fftN / 2;
    double maxAmp = 0;
    for (int y = 0; y < m_fftN; y++) {
        for (int x = 0; x < m_fftN; x++) {
            if (std::abs(x - half) <= 1 && std::abs(y - half) <= 1)
                continue;  // skip center 3x3
            double a = std::abs(m_fftData[y * m_fftN + x]);
            if (a > maxAmp) maxAmp = a;
        }
    }
    return maxAmp;
}

void FtWindow::brushApply(QPoint pos)
{
    if (!m_ftComputed) return;

    for (int i = 0; i < m_numDispItems; i++) {
        const DisplayItem &di = m_dispItems[i];
        if (!di.valid || di.zoomIdx < 1) continue;
        if (!di.screenRect.contains(pos)) continue;

        ZoomState &z = m_zoom[di.zoomIdx];
        QRectF src = z.visibleRect(di.imgW, di.imgH);
        double relX = (pos.x() - di.screenRect.x()) / (double)di.screenRect.width();
        double relY = (pos.y() - di.screenRect.y()) / (double)di.screenRect.height();
        int ix = (int)(src.x() + relX * src.width());
        int iy = (int)(src.y() + relY * src.height());

        if (ix >= 0 && ix < m_fftN && iy >= 0 && iy < m_fftN) {
            double val = brushValue();
            m_fftData[iy * m_fftN + ix] = Complex(val, 0);
            // Friedel mate (complex conjugate)
            int fx = (m_fftN - ix) % m_fftN;
            int fy = (m_fftN - iy) % m_fftN;
            m_fftData[fy * m_fftN + fx] = Complex(val, 0);
            recomputeDisplayImages();
            update();
        }
        return;
    }
}
