#include "ftwindow_common.h"

// ---------------------------------------------------------------------------
//  Painting
// ---------------------------------------------------------------------------
void FtWindow::drawParamLabel(QPainter &p, const QFontMetrics &fm,
                              int x, int y, const QString &text, const QString &tip,
                              int fieldH)
{
    // Align label baseline with the text baseline inside the adjacent input
    // field of height `fieldH` (line edit / combo box, typically 22 px).
    // Input fields vertically centre their text, so their baseline sits at
    // fieldH/2 + ascent/2 - descent/2 from the widget top — not at
    // fm.ascent() as with a top-anchored drawText.
    int baselineY = y + (fieldH + fm.ascent() - fm.descent()) / 2;
    p.drawText(x, baselineY, text);
    if (!tip.isEmpty()) {
        QRect r(x, baselineY - fm.ascent(), fm.horizontalAdvance(text), fm.height());
        m_paramLabelTips.emplace_back(r, tip);
    }
}

bool FtWindow::p1ToolWindowOpen() const
{
    return m_p1EraserActive || m_p1BrushActive || m_p1TaperActive || m_p1SymmetrizeActive
        || m_binActive || m_padActive || m_copyActive || m_averageActive || m_cropActive || m_peakPickActive || m_extractActive
        || m_gaborActive || m_hessianActive || m_amyloidActive || m_measureActive
        || m_shiftActive || m_rotateActive || m_alignActive
        || m_p1FlipHActive || m_p1FlipVActive || m_p1InvertActive;
}

bool FtWindow::p2ToolWindowOpen() const
{
    return m_bandpassActive || m_directionalActive || m_lineFilterActive || m_brushActive
        || m_eraserActive || m_latticeActive || m_ftCropActive || m_crossSectionActive
        || m_ctfActive || m_ctfFitActive || m_phaseRampActive || m_p2SymmetrizeActive
        || m_ftRotateActive;
}

bool FtWindow::p1FunctionOpen() const { return p1ToolWindowOpen() || m_mathActive; }
bool FtWindow::p2FunctionOpen() const { return p2ToolWindowOpen() || m_ftMathActive; }

bool FtWindow::toolHelpInfo(bool panel2, QString &title, QString &anchor) const
{
    struct Entry { bool active; const char *title; const char *anchor; };
    // Listed in the same order as the drawing chains in paintEvent, so the
    // header always names the tool whose parameters are on screen.
    const Entry p1[] = {
        { m_p1EraserActive,     "Eraser",                 "p1-eraser"            },
        { m_p1BrushActive,      "Paint brush",            "p1-paint-brush"       },
        { m_p1TaperActive,      "Taper edges (Hanning)",  "p1-taper-edges"       },
        { m_p1SymmetrizeActive, "Symmetrize image",       "p1-symmetrize"        },
        { m_binActive,          "Bin image",              "p1-bin"               },
        { m_padActive,          "Pad image",              "p1-pad"               },
        { m_copyActive,         "Copy image buffer",      "p1-copy"              },
        { m_averageActive,      "Average images",         "p1-average"           },
        { m_cropActive,         "Crop image",             "p1-crop"              },
        { m_peakPickActive,     "Peak search",            "p1-peak-search"       },
        { m_extractActive,      "Extract particles",      "p1-extract-particles" },
        { m_gaborActive,        "Gabor filter",           "p1-gabor"             },
        { m_hessianActive,      "Hessian filter",         "p1-hessian"           },
        { m_measureActive,      "Measure",                "p1-measure"           },
        { m_shiftActive,        "Shift image",            "p1-shift"             },
        { m_rotateActive,       "Rotate image",           "p1-rotate"            },
        { m_amyloidActive,      "Amyloid filament",       "p1-amyloid"           },
        { m_alignActive,        "Align to reference",     "p1-align"             },
        { m_p1FlipHActive,      "Flip horizontally",      "p1-flip"              },
        { m_p1FlipVActive,      "Flip vertically",        "p1-flip"              },
        { m_p1InvertActive,     "Invert contrast",        "p1-invert-contrast"   },
    };
    const Entry p2[] = {
        { m_bandpassActive,     "Bandpass filter",           "p2-bandpass"          },
        { m_directionalActive,  "Directional filter",        "p2-directional"       },
        { m_brushActive,        "Paint brush",               "p2-paint-brush"       },
        { m_eraserActive,       "Eraser",                    "p2-eraser"            },
        { m_lineFilterActive,   "Line filter",               "p2-line-filter"       },
        { m_latticeActive,      "Lattice filter",            "p2-lattice-filter"    },
        { m_crossSectionActive, "Cross-section profile",     "p2-cross-section"     },
        { m_p2SymmetrizeActive, "Symmetrize Fourier space",  "p2-symmetrize"        },
        { m_ftCropActive,       "Fourier crop / Fourier pad","p2-fourier-crop-pad"  },
        { m_ctfActive,          "CTF SIM (simulate a CTF)",  "p2-ctf-sim"           },
        { m_ctfFitActive,       "CTF FIT (fit a CTF for an image)", "p2-ctf-fit"    },
        { m_phaseRampActive,    "Phase ramp",                "p2-phase-ramp"        },
        { m_ftRotateActive,     "Rotate Fourier space",      "p2-rotate"            },
    };

    const Entry *entries = panel2 ? p2 : p1;
    const size_t n       = panel2 ? std::size(p2) : std::size(p1);
    for (size_t i = 0; i < n; i++) {
        if (entries[i].active) {
            title  = QString::fromLatin1(entries[i].title);
            anchor = QString::fromLatin1(entries[i].anchor);
            return true;
        }
    }
    return false;
}

// One raised 3D square button carrying a single character, matching the toggle
// buttons next to the histograms. Shared by the "?" help button and the "E"
// exercise button so the two are guaranteed identical in size and styling.
void FtWindow::drawSquareLabelButton(QPainter &p, const QRect &br,
                                     const QString &label, const QString &tip)
{
    p.save();
    p.fillRect(br, QColor(0xdc, 0xdc, 0xdc));
    const QColor lite(0xff, 0xff, 0xff), dark(0x60, 0x60, 0x60);
    p.fillRect(QRect(br.left(),      br.top(),        br.width(), 2), lite);
    p.fillRect(QRect(br.left(),      br.top(),        2, br.height()), lite);
    p.fillRect(QRect(br.right() - 1, br.top(),        2, br.height()), dark);
    p.fillRect(QRect(br.left(),      br.bottom() - 1, br.width(), 2), dark);
    QFont qf; qf.setPixelSize(std::clamp(br.width() * 3 / 4, 9, 18)); qf.setBold(true);
    p.setFont(qf);
    p.setPen(Qt::black);
    p.drawText(br, Qt::AlignCenter, label);
    p.restore();

    m_paramLabelTips.emplace_back(br, tip);
}

QRect FtWindow::drawHelpButton(QPainter &p, const QRect &frame, int marginX,
                               int marginY, int rowH, int maxSize)
{
    int bs = std::clamp(rowH - 6, 14, maxSize);
    QRect br(frame.right() - marginX - bs,
             frame.top() + marginY + (rowH - bs) / 2, bs, bs);
    drawSquareLabelButton(p, br, QStringLiteral("?"), QStringLiteral("Help"));
    return br;
}

// The "E" button sits immediately left of the help button, same size and shape,
// and is drawn only for tools that an exercise actually covers. Returns a null
// rect otherwise, which is also what suppresses its click target.
QRect FtWindow::drawExerciseButton(QPainter &p, const QRect &helpRect,
                                   const QString &anchor)
{
    if (helpRect.isNull() || anchor.isEmpty()) return QRect();
    QRect br = helpRect.translated(-(helpRect.width() + 4), 0);
    drawSquareLabelButton(p, br, QStringLiteral("E"),
                          QStringLiteral("Show the exercise for this function"));
    return br;
}

// The exercise in manual_exercises.html that teaches the tool currently open in
// this panel, or an empty string when no exercise covers it. Only these tools
// get an "E" button.
QString FtWindow::toolExerciseAnchor(bool panel2) const
{
    if (panel2) {
        if (m_brushActive || m_eraserActive)                    return "exercise-0";
        if (m_bandpassActive || m_directionalActive
            || m_lineFilterActive)                              return "exercise-1";
        if (m_latticeActive)                                    return "exercise-4";
        if (m_phaseRampActive)                                  return "exercise-7";
        if (m_ftMathActive)                                     return "exercise-8";
        if (m_ctfFitActive)                                     return "exercise-12";
        if (m_ctfActive)                                        return "exercise-13";
        return QString();
    }
    if (m_p1BrushActive || m_p1EraserActive)                    return "exercise-0";
    if (m_mathActive)                                           return "exercise-6";
    if (m_peakPickActive || m_extractActive)                    return "exercise-9";
    if (m_amyloidActive)                                        return "exercise-10";
    if (m_alignActive)                                          return "exercise-11";
    return QString();
}

QRect FtWindow::drawToolHelpHeader(QPainter &p, const QRect &toolRect, int margin,
                                   int lh, int fontPx, const QString &title)
{
    QFont tf; tf.setPixelSize(fontPx); tf.setBold(true);
    QFontMetrics tfm(tf);

    p.save();
    p.setFont(tf);
    p.setPen(QColor(20, 20, 20));
    int baselineY = toolRect.top() + margin + (lh + tfm.ascent() - tfm.descent()) / 2;
    p.drawText(toolRect.left() + margin, baselineY, title);
    p.restore();

    return drawHelpButton(p, toolRect, margin, margin, lh);
}

void FtWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // The maximized view replaces the whole layout, so it paints on its own
    // and none of the normal panel bookkeeping below applies.
    m_p1MaxRect = QRect();
    m_p2MaxRectA = QRect();
    m_p2MaxRectB = QRect();
    if (m_maxPanel != 0) {
        paintMaximized(p);
        return;
    }

    // Reset painted parameter-label hover rectangles; they are repopulated
    // below as the tool option panels are drawn.
    m_paramLabelTips.clear();

    m_p1ToolRect = QRect();   // cleared each frame, set when a p1 tool dialog draws
    m_p2ToolRect = QRect();   // cleared each frame, set when a p2 tool dialog draws
    m_p1HelpRect = QRect();
    m_p2HelpRect = QRect();
    m_p1MathHelpRect = QRect();
    m_p2MathHelpRect = QRect();
    m_p1ExerciseRect = QRect();
    m_p2ExerciseRect = QRect();
    m_imageHistLockRect = QRect();
    m_markImageCenterRect = QRect();
    m_pixelSizeInfoRect = QRect();
    m_ftHistLockRect = QRect();
    m_maskBtnRect = QRect();

    // Function-button tooltips are stashed here during tool-button rendering
    // and drawn at the very end of this paintEvent, so they appear on top of
    // the image / Fourier display rather than being clipped by them.
    QRect   pendingTipRect;
    QString pendingTipText;

    // Helper to paint a 3D-look toggle button (raised when up, sunken when
    // down). Shared by all panel-1 / panel-2 toggle buttons.
    auto paintToggleButton = [&](const QRect &br, const QString &text, bool down) {
        QFont bf; bf.setPixelSize(11);
        p.save();
        p.fillRect(br, down ? QColor(0xb8, 0xb8, 0xb8) : QColor(0xdc, 0xdc, 0xdc));
        QColor lite(0xff, 0xff, 0xff), dark(0x60, 0x60, 0x60);
        QColor topC   = down ? dark : lite;
        QColor leftC  = down ? dark : lite;
        QColor rightC = down ? lite : dark;
        QColor botC   = down ? lite : dark;
        p.fillRect(QRect(br.left(),       br.top(),         br.width(), 2), topC);
        p.fillRect(QRect(br.left(),       br.top(),         2, br.height()), leftC);
        p.fillRect(QRect(br.right() - 1,  br.top(),         2, br.height()), rightC);
        p.fillRect(QRect(br.left(),       br.bottom() - 1,  br.width(), 2), botC);
        p.setFont(bf);
        p.setPen(Qt::black);
        QRect textRect = br.adjusted(2, 2, -2, -2);
        if (down) textRect.translate(1, 1);
        p.drawText(textRect, Qt::AlignCenter, text);
        p.restore();
    };

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
                           double &outVal,
                           int *outX = nullptr, int *outY = nullptr) -> bool {
        if (!inner.contains(m_mousePos) || vals.empty()) return false;
        QRectF src = zoom.visibleRect(imgW, imgH);
        double relX = (m_mousePos.x() - inner.x()) / (double)inner.width();
        double relY = (m_mousePos.y() - inner.y()) / (double)inner.height();
        int ix = (int)(src.x() + relX * src.width());
        int iy = (int)(src.y() + relY * src.height());
        if (ix < 0 || ix >= imgW || iy < 0 || iy >= imgH) return false;
        outVal = vals[iy * imgW + ix];
        if (outX) *outX = ix;
        if (outY) *outY = iy;
        return true;
    };

    // ---- panel titles ----------------------------------------------------------
    {
        QFont tf;
        tf.setBold(true);
        tf.setPixelSize(28);
        p.setFont(tf);
        p.setPen(Qt::white);
        QFontMetrics tfm(tf);

        QString t1 = "Real Space";
        p.drawText((cx - 1 - tfm.horizontalAdvance(t1)) / 2, 8 + tfm.ascent(), t1);

        QString t2 = "Fourier Space";
        p.drawText(cx + 2 + (width() - cx - 2 - tfm.horizontalAdvance(t2)) / 2, 8 + tfm.ascent(), t2);
    }

    // ---- Tool button columns ----------------------------------
    {
        // Group/slot geometry is computed in layoutToolSlots() (shared with the
        // mouse handler). Each visible tool id draws into the slot it currently
        // occupies: a group face when collapsed, or a popup cell when its group
        // is open. Invisible ids (non-face members of a collapsed group) skip.
        layoutToolSlots();
        int btnSide = m_toolBtnSide;

        // Floating popup panel background, under the member cells drawn below.
        if (m_openMenuPanel == 1 && !m_p1PopupRect.isNull()) {
            p.setPen(QPen(QColor(200, 200, 200), 1));
            p.setBrush(QColor(30, 30, 30));
            p.drawRect(m_p1PopupRect);
        } else if (m_openMenuPanel == 2 && !m_p2PopupRect.isNull()) {
            p.setPen(QPen(QColor(200, 200, 200), 1));
            p.setBrush(QColor(30, 30, 30));
            p.drawRect(m_p2PopupRect);
        }

        // Panel 1: left edge
        for (int i = 0; i < P1_TOOL_BUTTONS; i++) {
            if (!m_p1SlotVisible[i]) continue;
            QRect r = m_p1BtnRects[i];

            p.setPen(QPen(Qt::white, 1));
            if ((i == 0 && m_p1EraserActive) || (i == 1 && m_p1BrushActive) ||
                (i == 2 && m_measureActive) ||
                (i == 5 && m_shiftActive) || (i == 6 && m_rotateActive) ||
                (i == 8 && m_p1TaperActive) || (i == 9 && m_p1SymmetrizeActive) ||
                (i == 10 && m_binActive) || (i == 11 && m_cropActive) ||
                (i == 19 && m_padActive) || (i == 20 && m_copyActive) ||
                (i == 12 && m_gaborActive) || (i == 13 && m_hessianActive) ||
                (i == 14 && m_amyloidActive) || (i == 15 && m_mathActive) ||
                (i == 16 && m_peakPickActive) || (i == 17 && m_extractActive) ||
                (i == 18 && m_alignActive) || (i == 21 && m_averageActive))
                p.setBrush(QColor(60, 60, 60));
            else
                p.setBrush(QColor(0, 0, 0));
            p.drawRect(r);

            // Eraser icon (button 0): pencil eraser tilted at an angle
            if (i == 0) {
                p.setRenderHint(QPainter::Antialiasing, true);
                QRect ir = r.adjusted(3, 3, -3, -3);
                double ix = ir.x(), iy2 = ir.y(), iw = ir.width(), ih = ir.height();

                // Body (wooden pencil shaft) — tilted parallelogram
                QPainterPath body;
                body.moveTo(ix + iw * 0.30, iy2 + ih * 0.08);
                body.lineTo(ix + iw * 0.85, iy2 + ih * 0.08);
                body.lineTo(ix + iw * 0.70, iy2 + ih * 0.50);
                body.lineTo(ix + iw * 0.15, iy2 + ih * 0.50);
                body.closeSubpath();
                p.setPen(QPen(QColor(160, 130, 90), std::max(1, (int)(iw * 0.04))));
                p.setBrush(QColor(210, 185, 140));
                p.drawPath(body);

                // Highlight stripe on body
                QPainterPath stripe;
                stripe.moveTo(ix + iw * 0.33, iy2 + ih * 0.18);
                stripe.lineTo(ix + iw * 0.82, iy2 + ih * 0.18);
                stripe.lineTo(ix + iw * 0.78, iy2 + ih * 0.30);
                stripe.lineTo(ix + iw * 0.29, iy2 + ih * 0.30);
                stripe.closeSubpath();
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(225, 200, 160));
                p.drawPath(stripe);

                // Metal ferrule band
                QPainterPath ferrule;
                ferrule.moveTo(ix + iw * 0.15, iy2 + ih * 0.50);
                ferrule.lineTo(ix + iw * 0.70, iy2 + ih * 0.50);
                ferrule.lineTo(ix + iw * 0.65, iy2 + ih * 0.62);
                ferrule.lineTo(ix + iw * 0.10, iy2 + ih * 0.62);
                ferrule.closeSubpath();
                p.setPen(QPen(QColor(140, 140, 140), std::max(1, (int)(iw * 0.03))));
                p.setBrush(QColor(190, 195, 200));
                p.drawPath(ferrule);

                // Pink eraser tip
                QPainterPath eraser;
                eraser.moveTo(ix + iw * 0.10, iy2 + ih * 0.62);
                eraser.lineTo(ix + iw * 0.65, iy2 + ih * 0.62);
                eraser.lineTo(ix + iw * 0.55, iy2 + ih * 0.92);
                eraser.lineTo(ix + iw * 0.00, iy2 + ih * 0.92);
                eraser.closeSubpath();
                p.setPen(QPen(QColor(180, 70, 80), std::max(1, (int)(iw * 0.04))));
                p.setBrush(QColor(235, 120, 130));
                p.drawPath(eraser);

                // Eraser highlight
                QPainterPath eHighlight;
                eHighlight.moveTo(ix + iw * 0.12, iy2 + ih * 0.66);
                eHighlight.lineTo(ix + iw * 0.50, iy2 + ih * 0.66);
                eHighlight.lineTo(ix + iw * 0.47, iy2 + ih * 0.76);
                eHighlight.lineTo(ix + iw * 0.09, iy2 + ih * 0.76);
                eHighlight.closeSubpath();
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(245, 160, 165));
                p.drawPath(eHighlight);

                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Eraser";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
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
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Measure tool icon (button 2): simple mm ruler
            if (i == 2) {
                p.setRenderHint(QPainter::Antialiasing, true);
                QRect ir = r.adjusted(3, 3, -3, -3);
                double ix = ir.x(), iy = ir.y(), iw = ir.width(), ih = ir.height();

                // Ruler body (light tan rectangle)
                QRectF body(ix + iw * 0.05, iy + ih * 0.30,
                            iw * 0.90, ih * 0.35);
                p.setPen(QPen(QColor(120, 90, 0), std::max(1, (int)(iw * 0.04))));
                p.setBrush(QColor(245, 220, 140));
                p.drawRect(body);

                // Tick marks along the top edge
                p.setPen(QPen(QColor(40, 40, 40), std::max(1, (int)(iw * 0.03))));
                int nTicks = 11;
                for (int k = 0; k < nTicks; k++) {
                    double tx = body.left() + k * body.width() / (nTicks - 1);
                    double tickLen = (k % 5 == 0) ? body.height() * 0.55
                                                  : body.height() * 0.30;
                    p.drawLine(QPointF(tx, body.top()),
                               QPointF(tx, body.top() + tickLen));
                }

                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Measure";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Flip horizontal icon (button 3): double arrow left-right
            if (i == 3) {
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
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Flip vertical icon (button 3): double arrow up-down
            if (i == 4) {
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
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Shift image icon (button 4): arrow pointing right
            if (i == 5) {
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
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Rotate image icon (button 5): curved arrow
            if (i == 6) {
                p.setRenderHint(QPainter::Antialiasing, true);
                double cx2 = r.x() + r.width() / 2.0;
                double cy2 = r.y() + r.height() / 2.0;
                double rad = std::min(r.width(), r.height()) * 0.32;

                p.setPen(QPen(m_rotateActive ? QColor(180, 180, 255) : Qt::white,
                              std::max(1, (int)(rad * 0.25))));
                p.setBrush(Qt::NoBrush);
                p.drawArc(QRectF(cx2 - rad, cy2 - rad, rad * 2, rad * 2),
                          30 * 16, 280 * 16);

                // Arrowhead at end of arc (~310 degrees = -50 degrees)
                double aAngle = 50.0 * M_PI / 180.0;
                double ax = cx2 + rad * std::cos(aAngle);
                double ay = cy2 + rad * std::sin(aAngle);
                double arrowLen = rad * 1.0;
                double arrowHalfW = rad * 0.3;
                QPointF tangent(std::sin(aAngle), -std::cos(aAngle));
                QPointF normal(std::cos(aAngle), std::sin(aAngle));
                QPointF base(ax, ay);
                QPointF tip = base + tangent * arrowLen;
                QPointF left = base + normal * arrowHalfW;
                QPointF right = base - normal * arrowHalfW;
                p.setPen(Qt::NoPen);
                p.setBrush(m_rotateActive ? QColor(180, 180, 255) : Qt::white);
                QPainterPath ah;
                ah.moveTo(tip);
                ah.lineTo(left);
                ah.lineTo(right);
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
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Invert contrast icon (button 6): +/- sign
            if (i == 7) {
                p.setRenderHint(QPainter::Antialiasing, true);
                QRect ir = r.adjusted(3, 3, -3, -3);
                int ix = ir.x(), iy2 = ir.y(), iw = ir.width(), ih = ir.height();
                int voff = ih / 8;  // shift down to visually center
                int lw = std::max(1, iw / 12);
                p.setPen(QPen(Qt::white, lw));
                // Plus sign (top-left)
                int plusCx = ix + iw / 4;
                int plusCy = iy2 + ih / 4 + voff;
                int arm = iw / 7;
                p.drawLine(plusCx - arm, plusCy, plusCx + arm, plusCy);
                p.drawLine(plusCx, plusCy - arm, plusCx, plusCy + arm);
                // Minus sign (bottom-right)
                int minusCx = ix + iw * 3 / 4;
                int minusCy = iy2 + ih * 3 / 4 + voff;
                p.drawLine(minusCx - arm, minusCy, minusCx + arm, minusCy);
                // Slash between
                p.drawLine(ix + iw * 0.60, iy2 + ih * 0.25 + voff, ix + iw * 0.40, iy2 + ih * 0.75 + voff);
                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Invert contrast";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Taper edges icon (button 7): white square ring with black center
            if (i == 8) {
                p.setRenderHint(QPainter::Antialiasing, true);
                QColor col = m_p1TaperActive ? QColor(180, 180, 255) : Qt::white;
                int side = static_cast<int>(btnSide * 0.85);
                int left = r.x() + (r.width() - side) / 2;
                int top = r.y() + (r.height() - side) / 2;
                QRect outer(left, top, side, side);
                int ring = std::max(1, side / 10);
                QRect inner = outer.adjusted(ring, ring, -ring, -ring);

                p.setPen(Qt::NoPen);
                p.setBrush(col);
                p.drawRect(outer);
                p.setBrush(Qt::black);
                p.drawRect(inner);
                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Taper edges";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Symmetrize icon (button 9): white five-fold star on black field
            if (i == 9) {
                p.setRenderHint(QPainter::Antialiasing, true);
                double cx2 = r.x() + r.width() / 2.0;
                double cy2 = r.y() + r.height() / 2.0;
                double outerR = std::min(r.width(), r.height()) * 0.42;
                double innerR = outerR * 0.40;
                QPainterPath star;
                for (int k = 0; k < 10; k++) {
                    double rad = (k % 2 == 0) ? outerR : innerR;
                    // Start with the top point pointing up (angle = -90°)
                    double ang = -M_PI / 2.0 + k * (M_PI / 5.0);
                    double px = cx2 + rad * std::cos(ang);
                    double py = cy2 + rad * std::sin(ang);
                    if (k == 0) star.moveTo(px, py);
                    else        star.lineTo(px, py);
                }
                star.closeSubpath();
                p.setPen(Qt::NoPen);
                p.setBrush(m_p1SymmetrizeActive ? QColor(180, 180, 255) : Qt::white);
                p.drawPath(star);
                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Symmetrize Image";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Bin image icon (button 10): 2x2 grid representing pixel binning
            if (i == 10) {
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
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Crop image icon (button 11): thin white square outline over the
            // lower-right third of an otherwise black field
            if (i == 11) {
                if (m_cropActive) {
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(60, 60, 60));
                    p.drawRect(r);
                }

                QColor col = m_cropActive ? QColor(180, 180, 255) : Qt::white;
                int m = std::max(2, btnSide / 6);
                int sq = std::max(2, (r.width() - 2 * m) * 3 / 5);
                QRect cropSquare(r.right() - m - sq, r.bottom() - m - sq, sq, sq);
                p.setPen(QPen(col, 1));   // thin white line
                p.setBrush(Qt::NoBrush);
                p.drawRect(cropSquare);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Crop image";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Math calculations icon (button 15): Sigma/Sum sign
            if (i == 15) {
                if (m_mathActive) {
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(60, 60, 60));
                    p.drawRect(r);
                }

                p.setRenderHint(QPainter::Antialiasing, true);
                int inset = std::max(3, r.width() / 4);
                QRect ir = r.adjusted(inset, inset, -inset, -inset);
                int sx = ir.x(), sy = ir.y(), sw = ir.width(), sh = ir.height();
                QColor col = m_mathActive ? QColor(180, 180, 255) : Qt::white;

                // Draw Sigma (Σ) symbol
                QPainterPath sigma;
                sigma.moveTo(sx + sw * 0.85, sy + sh * 0.10);  // top-right
                sigma.lineTo(sx + sw * 0.15, sy + sh * 0.10);  // top-left
                sigma.lineTo(sx + sw * 0.50, sy + sh * 0.50);  // center point (chevron)
                sigma.lineTo(sx + sw * 0.15, sy + sh * 0.90);  // bottom-left
                sigma.lineTo(sx + sw * 0.85, sy + sh * 0.90);  // bottom-right

                // Top and bottom horizontal serifs
                sigma.moveTo(sx + sw * 0.85, sy + sh * 0.10);
                sigma.lineTo(sx + sw * 0.85, sy + sh * 0.18);
                sigma.moveTo(sx + sw * 0.85, sy + sh * 0.90);
                sigma.lineTo(sx + sw * 0.85, sy + sh * 0.82);

                p.setPen(QPen(col, std::max(1, sw / 6)));
                p.setBrush(Qt::NoBrush);
                p.drawPath(sigma);

                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Math calculations";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Particle picking icon (button 16): four green plus signs in 2x2 grid
            if (i == 16) {
                if (m_peakPickActive) {
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(60, 60, 60));
                    p.drawRect(r);
                }

                int m = std::max(2, btnSide / 8);
                QRect inner = r.adjusted(m, m, -m, -m);
                int icx = inner.x() + inner.width() / 2;
                int icy = inner.y() + inner.height() / 2;

                // Blue checkmark in center (drawn first, underneath)
                if (!m_peaks.empty()) {
                    p.setRenderHint(QPainter::Antialiasing, true);
                    int checkW = inner.width() * 2 / 5;
                    int checkH = inner.height() / 4;
                    p.setPen(QPen(QColor(100, 180, 255), std::max(1, btnSide / 12)));
                    p.setBrush(Qt::NoBrush);
                    p.drawLine(icx - checkW, icy,
                               icx - checkW / 3, icy + checkH);
                    p.drawLine(icx - checkW / 3, icy + checkH,
                               icx + checkW, icy - checkH / 2);
                    p.setRenderHint(QPainter::Antialiasing, false);
                }

                // Four equally spaced green pluses (drawn on top)
                QColor col = m_peakPickActive ? QColor(100, 255, 100) : QColor(0, 200, 0);
                int armLen = inner.width() / 7;
                int lw = std::max(1, btnSide / 16);
                p.setPen(QPen(col, lw));

                int cx1 = inner.x() + inner.width() / 5;
                int cy1 = inner.y() + inner.height() / 5;
                int cx2 = inner.x() + inner.width() * 4 / 5;
                int cy2 = cy1;
                int cx3 = cx1;
                int cy3 = inner.y() + inner.height() * 4 / 5;
                int cx4 = cx2;
                int cy4 = cy3;

                p.drawLine(cx1 - armLen, cy1, cx1 + armLen, cy1);
                p.drawLine(cx1, cy1 - armLen, cx1, cy1 + armLen);
                p.drawLine(cx2 - armLen, cy2, cx2 + armLen, cy2);
                p.drawLine(cx2, cy2 - armLen, cx2, cy2 + armLen);
                p.drawLine(cx3 - armLen, cy3, cx3 + armLen, cy3);
                p.drawLine(cx3, cy3 - armLen, cx3, cy3 + armLen);
                p.drawLine(cx4 - armLen, cy4, cx4 + armLen, cy4);
                p.drawLine(cx4, cy4 - armLen, cx4, cy4 + armLen);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Peak search";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Extract particles icon (button 17): white smiley face
            if (i == 17) {
                if (m_extractActive) {
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(60, 60, 60));
                    p.drawRect(r);
                }

                p.setRenderHint(QPainter::Antialiasing, true);
                double cx2 = r.x() + r.width() / 2.0;
                double cy2 = r.y() + r.height() / 2.0;
                double rad = std::min(r.width(), r.height()) * 0.35;
                QColor col = m_extractActive ? QColor(180, 180, 255) : Qt::white;

                // Face circle
                p.setPen(QPen(col, std::max(1, (int)(rad * 0.15))));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(QPointF(cx2, cy2), rad, rad);

                // Left eye
                double eyeY = cy2 - rad * 0.25;
                double eyeR = rad * 0.12;
                p.setPen(Qt::NoPen);
                p.setBrush(col);
                p.drawEllipse(QPointF(cx2 - rad * 0.35, eyeY), eyeR, eyeR);
                // Right eye
                p.drawEllipse(QPointF(cx2 + rad * 0.35, eyeY), eyeR, eyeR);

                // Smile arc
                double smileR = rad * 0.55;
                QRectF smileRect(cx2 - smileR, cy2 - smileR * 0.3, smileR * 2, smileR * 1.4);
                p.setPen(QPen(col, std::max(1, (int)(rad * 0.12))));
                p.setBrush(Qt::NoBrush);
                p.drawArc(smileRect, -20 * 16, -140 * 16);

                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Extract particles";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Copy icon (button 20): two numbered white squares, "1" at the top
            // left and "2" over it at the bottom right, overlapping by 30% of a
            // square's width — one buffer duplicated onto another.
            if (i == 20) {
                QRectF ir = QRectF(r).adjusted(2, 2, -2, -2);
                // Two squares offset by 70% of their side span 1.7 sides in all,
                // which is what has to fit inside the icon.
                double s = ir.width() / 1.7;
                double off = s * 0.7;
                QRectF sq1(ir.x(), ir.y(), s, s);
                QRectF sq2(ir.x() + off, ir.y() + off, s, s);

                QFont nf; nf.setBold(true);
                nf.setPixelSize(std::max(6, (int)(s * 0.62)));
                p.setFont(nf);

                p.setPen(QPen(QColor(70, 70, 70), 1));
                p.setBrush(m_copyActive ? QColor(180, 180, 255) : Qt::white);
                p.drawRect(sq1);
                p.setPen(QColor(20, 20, 20));
                p.drawText(sq1, Qt::AlignCenter, "1");

                // Drop shadow, then the second square on top of the first.
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0, 0, 0, 160));
                p.drawRect(sq2.translated(2, 2));
                p.setPen(QPen(QColor(40, 40, 40), 1));
                p.setBrush(m_copyActive ? QColor(180, 180, 255) : Qt::white);
                p.drawRect(sq2);
                p.setPen(QColor(20, 20, 20));
                p.drawText(sq2, Qt::AlignCenter, "2");

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Copy an image buffer to another one";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    pendingTipRect = QRect(r.right() + 4, r.center().y() - tth / 2, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Average icon (button 21): three numbered white squares stepping
            // from the top-left to the bottom-right — several images summed and
            // averaged into one.
            if (i == 21) {
                QRectF ir = QRectF(r).adjusted(2, 2, -2, -2);
                double s   = ir.width() / 2.0;   // three squares stepped by s/2 span 2 sides
                double off = s * 0.5;
                QFont nf; nf.setBold(true);
                nf.setPixelSize(std::max(5, (int)(s * 0.6)));
                p.setFont(nf);
                const char *labels[3] = { "1", "2", "3" };
                for (int k = 0; k < 3; k++) {
                    QRectF sq(ir.x() + off * k, ir.y() + off * k, s, s);
                    if (k > 0) {   // drop shadow so the stacking reads
                        p.setPen(Qt::NoPen);
                        p.setBrush(QColor(0, 0, 0, 160));
                        p.drawRect(sq.translated(2, 2));
                    }
                    p.setPen(QPen(QColor(50, 50, 50), 1));
                    p.setBrush(m_averageActive ? QColor(180, 180, 255) : Qt::white);
                    p.drawRect(sq);
                    p.setPen(QColor(20, 20, 20));
                    p.drawText(sq, Qt::AlignCenter, labels[k]);
                }
                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Average images by summing them up";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    pendingTipRect = QRect(r.right() + 4, r.center().y() - tth / 2, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Pad-image icon (button 19): a small white square centred on the
            // black face, a third of its width — the original image sitting
            // inside the larger canvas it is padded out to.
            if (i == 19) {
                int side = std::max(2, r.width() / 3);
                QRect inner(r.center().x() - side / 2, r.center().y() - side / 2,
                            side, side);
                p.setPen(Qt::NoPen);
                p.setBrush(m_padActive ? QColor(180, 180, 255) : Qt::white);
                p.drawRect(inner);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "pad image to larger or crop to smaller image dimensions";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    pendingTipRect = QRect(r.right() + 4, r.center().y() - tth / 2, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Align-to-reference icon (button 18): a white arrow running from
            // the lower left to the upper right at 30° above the horizontal.
            if (i == 18) {
                p.setRenderHint(QPainter::Antialiasing, true);
                QRectF ir = QRectF(r).adjusted(3, 3, -3, -3);
                double cx2 = ir.center().x(), cy2 = ir.center().y();

                // 30° above horizontal, i.e. up and to the right on a y-down
                // screen. The shaft is as long as fits inside the square.
                const double ang = 30.0 * M_PI / 180.0;
                double ux = std::cos(ang), uy = -std::sin(ang);
                double half = std::min(ir.width() / (2 * std::abs(ux)),
                                       ir.height() / (2 * std::abs(uy)));
                QPointF tail(cx2 - ux * half, cy2 - uy * half);
                QPointF head(cx2 + ux * half, cy2 + uy * half);

                QColor col = m_alignActive ? QColor(180, 180, 255) : Qt::white;
                double lw = std::max(1.5, ir.width() * 0.11);

                // Shaft stops short of the tip so the head is not blunted.
                double headLen = std::max(3.0, ir.width() * 0.34);
                QPointF shaftEnd(head.x() - ux * headLen * 0.75,
                                 head.y() - uy * headLen * 0.75);
                p.setPen(QPen(col, lw, Qt::SolidLine, Qt::FlatCap));
                p.setBrush(Qt::NoBrush);
                p.drawLine(tail, shaftEnd);

                // Solid triangular head.
                double px = -uy, py = ux;                 // unit normal
                double headHalfW = headLen * 0.45;
                QPolygonF tip;
                tip << head
                    << QPointF(head.x() - ux * headLen + px * headHalfW,
                               head.y() - uy * headLen + py * headHalfW)
                    << QPointF(head.x() - ux * headLen - px * headHalfW,
                               head.y() - uy * headLen - py * headHalfW);
                p.setPen(Qt::NoPen);
                p.setBrush(col);
                p.drawPolygon(tip);

                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip2 = "Align image to reference";
                    int ttw = ttfm.horizontalAdvance(tip2) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip2;
                }
            }

            // Hessian filter icon (button 13): letter "H"
            if (i == 13) {
                p.setRenderHint(QPainter::Antialiasing, true);
                QFont gf;
                gf.setBold(true);
                gf.setPixelSize(std::max(10, (int)(btnSide * 0.75)));
                p.setFont(gf);
                p.setPen(QPen(Qt::white, 1));
                p.setBrush(Qt::NoBrush);
                p.drawText(r, Qt::AlignCenter, "H");
                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Hessian filter";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Amyloid filament icon (button 14): letter "A"
            if (i == 14) {
                if (m_amyloidActive) {
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(60, 60, 60));
                    p.drawRect(r);
                }
                p.setRenderHint(QPainter::Antialiasing, true);
                QFont gf;
                gf.setBold(true);
                gf.setPixelSize(std::max(10, (int)(btnSide * 0.75)));
                p.setFont(gf);
                p.setPen(QPen(Qt::white, 1));
                p.setBrush(Qt::NoBrush);
                p.drawText(r, Qt::AlignCenter, "A");
                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Amyloid filament";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Gabor filter icon (button 12): letter "G"
            if (i == 12) {
                p.setRenderHint(QPainter::Antialiasing, true);
                QFont gf;
                gf.setBold(true);
                gf.setPixelSize(std::max(10, (int)(btnSide * 0.75)));
                p.setFont(gf);
                p.setPen(QPen(Qt::white, 1));
                p.setBrush(Qt::NoBrush);
                p.drawText(r, Qt::AlignCenter, "G");
                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Gabor filter";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.right() + 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }
        }

        // Panel 2: right edge
        for (int i = 0; i < P2_TOOL_BUTTONS; i++) {
            if (!m_p2SlotVisible[i]) continue;
            QRect r = m_toolBtnRects[i];

            p.setPen(QPen(Qt::white, 1));
            if ((i == 0 && m_eraserActive) || (i == 1 && m_brushActive) ||
                (i == 2 && m_bandpassActive) || (i == 3 && m_directionalActive) ||
                (i == 4 && m_lineFilterActive) || (i == 5 && m_latticeActive) ||
                (i == 6 && m_ftRotateActive) || (i == 7 && m_crossSectionActive) ||
                (i == 8 && m_p2SymmetrizeActive) ||
                (i == 9 && m_ftCropActive) || (i == 10 && m_phaseRampActive) ||
                (i == 11 && m_ctfActive) || (i == 12 && m_ctfFitActive) ||
                (i == 13 && m_ftMathActive))
                p.setBrush(QColor(60, 60, 60));
            else
                p.setBrush(QColor(0, 0, 0));
            p.drawRect(r);

            // Eraser icon in first button: pencil eraser tilted at an angle
            if (i == 0) {
                p.setRenderHint(QPainter::Antialiasing, true);
                QRect ir = r.adjusted(3, 3, -3, -3);
                double ix = ir.x(), iy = ir.y(), iw = ir.width(), ih = ir.height();

                // Body (wooden pencil shaft)
                QPainterPath body;
                body.moveTo(ix + iw * 0.30, iy + ih * 0.08);
                body.lineTo(ix + iw * 0.85, iy + ih * 0.08);
                body.lineTo(ix + iw * 0.70, iy + ih * 0.50);
                body.lineTo(ix + iw * 0.15, iy + ih * 0.50);
                body.closeSubpath();
                p.setPen(QPen(QColor(160, 130, 90), std::max(1, (int)(iw * 0.04))));
                p.setBrush(QColor(210, 185, 140));
                p.drawPath(body);

                // Highlight stripe on body
                QPainterPath stripe;
                stripe.moveTo(ix + iw * 0.33, iy + ih * 0.18);
                stripe.lineTo(ix + iw * 0.82, iy + ih * 0.18);
                stripe.lineTo(ix + iw * 0.78, iy + ih * 0.30);
                stripe.lineTo(ix + iw * 0.29, iy + ih * 0.30);
                stripe.closeSubpath();
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(225, 200, 160));
                p.drawPath(stripe);

                // Metal ferrule band
                QPainterPath ferrule;
                ferrule.moveTo(ix + iw * 0.15, iy + ih * 0.50);
                ferrule.lineTo(ix + iw * 0.70, iy + ih * 0.50);
                ferrule.lineTo(ix + iw * 0.65, iy + ih * 0.62);
                ferrule.lineTo(ix + iw * 0.10, iy + ih * 0.62);
                ferrule.closeSubpath();
                p.setPen(QPen(QColor(140, 140, 140), std::max(1, (int)(iw * 0.03))));
                p.setBrush(QColor(190, 195, 200));
                p.drawPath(ferrule);

                // Pink eraser tip
                QPainterPath eraser;
                eraser.moveTo(ix + iw * 0.10, iy + ih * 0.62);
                eraser.lineTo(ix + iw * 0.65, iy + ih * 0.62);
                eraser.lineTo(ix + iw * 0.55, iy + ih * 0.92);
                eraser.lineTo(ix + iw * 0.00, iy + ih * 0.92);
                eraser.closeSubpath();
                p.setPen(QPen(QColor(180, 70, 80), std::max(1, (int)(iw * 0.04))));
                p.setBrush(QColor(235, 120, 130));
                p.drawPath(eraser);

                // Eraser highlight
                QPainterPath eHighlight;
                eHighlight.moveTo(ix + iw * 0.12, iy + ih * 0.66);
                eHighlight.lineTo(ix + iw * 0.50, iy + ih * 0.66);
                eHighlight.lineTo(ix + iw * 0.47, iy + ih * 0.76);
                eHighlight.lineTo(ix + iw * 0.09, iy + ih * 0.76);
                eHighlight.closeSubpath();
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(245, 160, 165));
                p.drawPath(eHighlight);

                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Eraser";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.left() - ttw - 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
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
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
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
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
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
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Line filter icon (button 4): 30-degree line spanning the icon
            if (i == 4) {
                p.setRenderHint(QPainter::Antialiasing, true);
                QColor col = m_lineFilterActive ? QColor(180, 180, 255) : QColor(200, 200, 255);
                double angle = 30.0 * M_PI / 180.0;
                double halfLen = std::sqrt(r.width() * r.width() + r.height() * r.height()) / 2.0;
                double cx2 = r.center().x();
                double cy2 = r.center().y();
                double dx = std::cos(angle) * halfLen;
                double dy = std::sin(angle) * halfLen;
                p.setPen(QPen(col, std::max(2, btnSide / 10)));
                p.save();
                p.setClipRect(r.adjusted(1, 1, -1, -1));
                p.drawLine(QPointF(cx2 - dx, cy2 + dy), QPointF(cx2 + dx, cy2 - dy));
                p.restore();
                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Line filter";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.left() - ttw - 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Lattice filter icon (button 5): hexagonal dot pattern
            if (i == 5) {
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
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Rotate Fourier space icon (button 6): curved arrow (same as panel 1 rotate)
            if (i == 6) {
                p.setRenderHint(QPainter::Antialiasing, true);
                double cx2 = r.x() + r.width() / 2.0;
                double cy2 = r.y() + r.height() / 2.0;
                double rad = std::min(r.width(), r.height()) * 0.32;

                p.setPen(QPen(m_ftRotateActive ? QColor(180, 180, 255) : Qt::white,
                              std::max(1, (int)(rad * 0.25))));
                p.setBrush(Qt::NoBrush);
                p.drawArc(QRectF(cx2 - rad, cy2 - rad, rad * 2, rad * 2),
                          30 * 16, 280 * 16);

                double aAngle = 50.0 * M_PI / 180.0;
                double ax = cx2 + rad * std::cos(aAngle);
                double ay = cy2 + rad * std::sin(aAngle);
                double arrowLen = rad * 1.0;
                double arrowHalfW = rad * 0.3;
                QPointF tangent(std::sin(aAngle), -std::cos(aAngle));
                QPointF normal(std::cos(aAngle), std::sin(aAngle));
                QPointF base(ax, ay);
                QPointF tip = base + tangent * arrowLen;
                QPointF left = base + normal * arrowHalfW;
                QPointF right = base - normal * arrowHalfW;
                p.setPen(Qt::NoPen);
                p.setBrush(m_ftRotateActive ? QColor(180, 180, 255) : Qt::white);
                QPainterPath ah;
                ah.moveTo(tip);
                ah.lineTo(left);
                ah.lineTo(right);
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
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Cross-section profile icon (button 7): red horizontal line
            if (i == 7) {
                p.setRenderHint(QPainter::Antialiasing, true);
                QColor col = m_crossSectionActive ? QColor(255, 120, 120) : QColor(220, 60, 60);
                double cy2 = r.y() + r.height() / 2.0;
                p.setPen(QPen(col, std::max(2, btnSide / 8)));
                p.drawLine(r.x() + 2, (int)cy2, r.right() - 2, (int)cy2);
                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Cross-section profile";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.left() - ttw - 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Symmetrize icon (button 8): white five-fold star on black field
            if (i == 8) {
                p.setRenderHint(QPainter::Antialiasing, true);
                double cx2 = r.x() + r.width() / 2.0;
                double cy2 = r.y() + r.height() / 2.0;
                double outerR = std::min(r.width(), r.height()) * 0.42;
                double innerR = outerR * 0.40;
                QPainterPath star;
                for (int k = 0; k < 10; k++) {
                    double rad = (k % 2 == 0) ? outerR : innerR;
                    double ang = -M_PI / 2.0 + k * (M_PI / 5.0);
                    double px = cx2 + rad * std::cos(ang);
                    double py = cy2 + rad * std::sin(ang);
                    if (k == 0) star.moveTo(px, py);
                    else        star.lineTo(px, py);
                }
                star.closeSubpath();
                p.setPen(Qt::NoPen);
                p.setBrush(m_p2SymmetrizeActive ? QColor(180, 180, 255) : Qt::white);
                p.drawPath(star);
                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Symmetrize Fourier space";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.left() - ttw - 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Fourier crop icon (button 9): 2x2 grid (same as panel 1 binning)
            if (i == 9) {
                QColor col = m_ftCropActive ? QColor(180, 180, 255) : Qt::white;
                int m = std::max(2, btnSide / 5);
                QRect inner = r.adjusted(m, m, -m, -m);
                int midX = inner.x() + inner.width() / 2;
                int midY = inner.y() + inner.height() / 2;
                int g = std::max(1, btnSide / 10);
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
                    QString tip = "Fourier crop";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.left() - ttw - 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Phase ramp icon (button 10): grey gradient (dark BL to bright TR)
            // with white "Ramp" text overlaid.
            if (i == 10) {
                QLinearGradient grad(r.left(), r.bottom(), r.right(), r.top());
                grad.setColorAt(0.0, QColor(0, 0, 0));
                grad.setColorAt(1.0, QColor(140, 140, 140));
                p.setPen(Qt::NoPen);
                p.setBrush(grad);
                p.drawRect(r);

                p.setRenderHint(QPainter::Antialiasing, true);
                QFont rf;
                rf.setBold(true);
                rf.setPixelSize(std::max(8, (int)(btnSide * 0.38)));
                p.setFont(rf);
                p.setPen(QPen(Qt::white, 1));
                p.setBrush(Qt::NoBrush);
                p.drawText(r, Qt::AlignCenter, "Ramp");
                p.setRenderHint(QPainter::Antialiasing, false);

                // Re-draw the rect outline so the active highlight border is visible
                p.setPen(QPen(Qt::white, 1));
                p.setBrush(Qt::NoBrush);
                p.drawRect(r);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Phase ramp";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.left() - ttw - 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // Fourier math icon (button 13): Sigma/Sum sign
            if (i == 13) {
                p.setRenderHint(QPainter::Antialiasing, true);
                int inset = std::max(3, btnSide / 4);
                QRect ir = r.adjusted(inset, inset, -inset, -inset);
                int sx = ir.x(), sy = ir.y(), sw = ir.width(), sh = ir.height();
                QColor col = m_ftMathActive ? QColor(180, 180, 255) : Qt::white;

                QPainterPath sigma;
                sigma.moveTo(sx + sw * 0.85, sy + sh * 0.10);
                sigma.lineTo(sx + sw * 0.15, sy + sh * 0.10);
                sigma.lineTo(sx + sw * 0.50, sy + sh * 0.50);
                sigma.lineTo(sx + sw * 0.15, sy + sh * 0.90);
                sigma.lineTo(sx + sw * 0.85, sy + sh * 0.90);
                sigma.moveTo(sx + sw * 0.85, sy + sh * 0.10);
                sigma.lineTo(sx + sw * 0.85, sy + sh * 0.18);
                sigma.moveTo(sx + sw * 0.85, sy + sh * 0.90);
                sigma.lineTo(sx + sw * 0.85, sy + sh * 0.82);

                p.setPen(QPen(col, std::max(1, sw / 6)));
                p.setBrush(Qt::NoBrush);
                p.drawPath(sigma);
                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "Math calculation";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.left() - ttw - 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // CTF SIM icon (button 11): black background with white "CTF"
            // on the first line and "SIM" on the second line
            if (i == 11) {
                p.setRenderHint(QPainter::Antialiasing, true);
                QFont cf;
                cf.setBold(true);
                cf.setPixelSize(std::max(8, (int)(btnSide * 0.36)));
                p.setFont(cf);
                p.setPen(QPen(Qt::white, 1));
                p.setBrush(Qt::NoBrush);
                QRect topHalf(r.left(), r.top(), r.width(), r.height() / 2);
                QRect botHalf(r.left(), r.top() + r.height() / 2, r.width(), r.height() - r.height() / 2);
                p.drawText(topHalf, Qt::AlignHCenter | Qt::AlignBottom, "CTF");
                p.drawText(botHalf, Qt::AlignHCenter | Qt::AlignTop, "SIM");
                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "CTF SIM (contrast transfer function simulation)";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.left() - ttw - 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }

            // CTF FIT icon (button 12): black background with white "CTF"
            // on the first line and "FIT" on the second line
            if (i == 12) {
                p.setRenderHint(QPainter::Antialiasing, true);
                QFont cf;
                cf.setBold(true);
                cf.setPixelSize(std::max(8, (int)(btnSide * 0.36)));
                p.setFont(cf);
                p.setPen(QPen(Qt::white, 1));
                p.setBrush(Qt::NoBrush);
                QRect topHalf(r.left(), r.top(), r.width(), r.height() / 2);
                QRect botHalf(r.left(), r.top() + r.height() / 2, r.width(), r.height() - r.height() / 2);
                p.drawText(topHalf, Qt::AlignHCenter | Qt::AlignBottom, "CTF");
                p.drawText(botHalf, Qt::AlignHCenter | Qt::AlignTop, "FIT");
                p.setRenderHint(QPainter::Antialiasing, false);

                if (r.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = "CTF FIT (fit contrast transfer function to the transform)";
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = r.left() - ttw - 4;
                    int tty = r.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }
        }

        // ---- group extras: submenu markers, open-menu anchor, group tips ----
        auto drawGroupExtras = [&](int panel) {
            const QVector<ToolGroup> &groups = (panel == 1) ? m_p1Groups : m_p2Groups;
            const QRect *groupRects = (panel == 1) ? m_p1GroupRects : m_p2GroupRects;
            for (int g = 0; g < groups.size(); g++) {
                const QRect &G = groupRects[g];
                bool multi = groups[g].members.size() > 1;
                bool open  = (m_openMenuPanel == panel && m_openMenuGroup == g);

                // Collapsed group with a custom text face (e.g. "CTF"): a plain
                // black square with the text, in place of the first member icon.
                if (!open && !groups[g].faceText.isEmpty()) {
                    p.setRenderHint(QPainter::Antialiasing, true);
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(0, 0, 0));
                    p.drawRect(G);
                    QFont cf;
                    cf.setBold(true);
                    cf.setPixelSize(std::max(8, (int)(G.width() * 0.42)));
                    p.setFont(cf);
                    p.setPen(QPen(Qt::white, 1));
                    p.drawText(G, Qt::AlignCenter, groups[g].faceText);
                    p.setRenderHint(QPainter::Antialiasing, false);
                }

                // When a multi-member group is open its face is empty (the
                // representative moved into the popup), so draw a highlighted
                // anchor placeholder in its place.
                if (open && multi) {
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(70, 70, 70));
                    p.drawRect(G);
                }

                // A small corner triangle marks every group that expands.
                if (multi) {
                    p.setRenderHint(QPainter::Antialiasing, false);
                    p.setPen(Qt::NoPen);
                    p.setBrush(QColor(120, 180, 255));
                    int t = std::max(4, G.width() / 4);
                    QPainterPath tri;
                    tri.moveTo(G.right(), G.bottom() - t);
                    tri.lineTo(G.right(), G.bottom());
                    tri.lineTo(G.right() - t, G.bottom());
                    tri.closeSubpath();
                    p.drawPath(tri);
                }

                // Hovering a group face shows the group name (overriding any
                // per-icon tip the representative may have set).
                if (G.contains(m_mousePos)) {
                    QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                    QFontMetrics ttfm(ttf);
                    QString tip = groups[g].name;
                    int ttw = ttfm.horizontalAdvance(tip) + 8;
                    int tth = ttfm.height() + 4;
                    int ttx = (panel == 1) ? (G.right() + 4) : (G.left() - ttw - 4);
                    int tty = G.center().y() - tth / 2;
                    pendingTipRect = QRect(ttx, tty, ttw, tth);
                    pendingTipText = tip;
                }
            }
        };
        drawGroupExtras(1);
        drawGroupExtras(2);

        // Close ("X") button above each panel's function column. layoutToolSlots
        // leaves the rect null unless that panel has a function open.
        auto drawCloseButton = [&](const QRect &r, bool panel1) {
            if (r.isNull()) return;
            p.setPen(QPen(Qt::white, 1));
            p.setBrush(QColor(0, 0, 0));
            p.drawRect(r);

            p.setRenderHint(QPainter::Antialiasing, true);
            QRect ir = r.adjusted(r.width() / 4, r.height() / 4,
                                  -r.width() / 4, -r.height() / 4);
            p.setPen(QPen(Qt::white, std::max(1, r.width() / 10)));
            p.drawLine(ir.topLeft(), ir.bottomRight());
            p.drawLine(ir.topRight(), ir.bottomLeft());
            p.setRenderHint(QPainter::Antialiasing, false);

            if (r.contains(m_mousePos)) {
                QFont ttf; ttf.setPixelSize(11); p.setFont(ttf);
                QFontMetrics ttfm(ttf);
                const QString tip = "Close function";
                int ttw = ttfm.horizontalAdvance(tip) + 8;
                int tth = ttfm.height() + 4;
                int ttx = panel1 ? r.right() + 4 : r.left() - ttw - 4;
                int tty = r.center().y() - tth / 2;
                pendingTipRect = QRect(ttx, tty, ttw, tth);
                pendingTipText = tip;
            }
        };
        drawCloseButton(m_p1CloseRect, true);
        drawCloseButton(m_p2CloseRect, false);

        // While a sub-panel (popup) is open, suppress the mouse-over text on the
        // top-level group squares — it would otherwise overlap and obscure the
        // sub-panel. Tooltips for the popup's own member cells (mouse inside the
        // popup) are kept.
        if (m_openMenuPanel != 0) {
            const QRect &popup = (m_openMenuPanel == 1) ? m_p1PopupRect : m_p2PopupRect;
            if (!popup.contains(m_mousePos))
                pendingTipText.clear();
        }
    }

    // Helper: draw zoom/pan overlay vertically at top-right of a frame, with
    // the maximize icon below it. maxRect (when given) receives the icon's
    // click target for mousePressEvent.
    auto drawZoomPanOverlay = [&](const QRect &frame, const ZoomState &zoom,
                                  QRect *maxRect = nullptr) {
        p.save();
        QFont zf;
        zf.setPixelSize(11);
        p.setFont(zf);
        QFontMetrics zfm(zf);

        QString zoomTxt = QString("Zoom: %1x").arg(zoom.factor, 0, 'f', 1);
        QString panTxt  = QString("Pan: x=%1, y=%2")
                              .arg(zoom.centerX, 0, 'f', 1)
                              .arg(zoom.centerY, 0, 'f', 1);

        int lineH = zfm.height() + 2;
        int maxW  = std::max(zfm.horizontalAdvance(zoomTxt),
                             zfm.horizontalAdvance(panTxt));
        int bx = frame.right() + 4;
        int by = frame.top();

        p.setPen(QColor(180, 180, 180));
        p.drawText(bx, by + zfm.ascent(), zoomTxt);
        p.drawText(bx, by + lineH + zfm.ascent(), panTxt);

        // Maximize icon, set apart below the two text lines.
        if (maxRect) {
            const int iconSide = 32;
            *maxRect = QRect(bx, by + 2 * lineH + 12, iconSide, iconSide);
            drawMaximizeIcon(p, *maxRect);

            if (maxRect->contains(m_mousePos)) {
                QFont ttf; ttf.setPixelSize(11);
                QFontMetrics ttfm(ttf);
                const QString tip = "Show maximized (✕ or ESC to return)";
                int ttw = ttfm.horizontalAdvance(tip) + 8;
                int tth = ttfm.height() + 4;
                // Flip to the icon's left when the tip would run off the
                // window — the panel-2 icons sit against the right edge.
                int ttx = (maxRect->right() + 4 + ttw <= width())
                              ? maxRect->right() + 4
                              : maxRect->left() - ttw - 4;
                pendingTipRect = QRect(ttx, maxRect->center().y() - tth / 2,
                                       ttw, tth);
                pendingTipText = tip;
            }
        }
        Q_UNUSED(maxW);
        p.restore();
    };

    // ---- Panel 1: loaded image ------------------------------------------------
    int panel1W = cx - 1;
    int panel1H = hy - 1;

    // Invert contrast progress overlay
    if (m_invertProgress >= 0.0 && m_invertProgress <= 1.0) {
        int fw = static_cast<int>(panel1W * 0.50);
        int fh = static_cast<int>(panel1H * 0.12);
        int fx = (panel1W - fw) / 2;
        int fy = (panel1H - fh) / 2;
        QRect invertRect(fx, fy, fw, fh);

        drawShadowRect(p, invertRect);

        int progW = static_cast<int>(fw * m_invertProgress);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(180, 210, 255));
        p.drawRect(fx + 1, fy + 1, progW, fh - 2);

        QFont f;
        f.setPixelSize(std::max(14, fh / 3));
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(invertRect, Qt::AlignCenter, "Inverting contrast...");
    }

    // Loading overlay (WASM image fetch in progress)
    if (m_loadingImage) {
        int fw = static_cast<int>(panel1W * 0.50);
        int fh = static_cast<int>(panel1H * 0.12);
        int fx = (panel1W - fw) / 2;
        int fy = (panel1H - fh) / 2;
        QRect loadRect(fx, fy, fw, fh);

        drawShadowRect(p, loadRect);

        QFont f;
        f.setPixelSize(std::max(14, fh / 3));
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(loadRect, Qt::AlignCenter, "Loading image...");
    }

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

        // Draw peak markers as green plus signs
        if (m_peakPickActive && m_peakShowPositions && !m_peaks.empty()) {
            QRect target = frame.adjusted(2, 2, -2, -2);
            QRectF src = m_zoom[0].visibleRect(imgW, imgH);
            p.save();
            p.setClipRect(target);
            int armLen = std::max(3, target.width() / 80);
            int penW = 1;
            p.setPen(QPen(QColor(0, 255, 0), penW));
            for (const auto &pk : m_peaks) {
                // Convert image coords to screen coords
                double sx = target.x() + (pk.x - src.x()) / src.width() * target.width();
                double sy = target.y() + (pk.y - src.y()) / src.height() * target.height();
                int screenX = static_cast<int>(sx);
                int screenY = static_cast<int>(sy);
                p.drawLine(screenX - armLen, screenY, screenX + armLen, screenY);
                p.drawLine(screenX, screenY - armLen, screenX, screenY + armLen);
            }
            p.restore();
        }

        // Draw measurement line overlay
        if (m_measureActive && (m_measureHasLine || m_measurePlacing == 1)) {
            QRect target = frame.adjusted(2, 2, -2, -2);
            QRectF src = m_zoom[0].visibleRect(imgW, imgH);
            p.save();
            p.setClipRect(target);
            p.setRenderHint(QPainter::Antialiasing, true);
            auto imgToScreen = [&](const QPointF &img) -> QPointF {
                double sx = target.x() + (img.x() - src.x()) / src.width()  * target.width();
                double sy = target.y() + (img.y() - src.y()) / src.height() * target.height();
                return QPointF(sx, sy);
            };
            QPointF sp0 = imgToScreen(m_measureP0);
            if (m_measureHasLine) {
                QPointF sp1 = imgToScreen(m_measureP1);
                p.setPen(QPen(QColor(255, 220, 0), 1));
                p.drawLine(sp0, sp1);
                p.setPen(QPen(Qt::white, 1));
                p.setBrush(QColor(255, 220, 0, 200));
                p.drawEllipse(sp0, 3, 3);
                p.drawEllipse(sp1, 3, 3);
            } else if (m_measurePlacing == 1) {
                p.setPen(QPen(Qt::white, 1));
                p.setBrush(QColor(255, 220, 0, 200));
                p.drawEllipse(sp0, 3, 3);
                if (target.contains(m_mousePos)) {
                    p.setPen(QPen(QColor(255, 220, 0, 150), 1, Qt::DashLine));
                    p.drawLine(sp0, QPointF(m_mousePos));
                }
            }
            p.restore();
        }

        // Draw crop selection overlay (50% transparent white fill, white border)
        if (m_cropActive && m_cropHasSelection) {
            QRect target = frame.adjusted(2, 2, -2, -2);
            QRectF src = m_zoom[0].visibleRect(imgW, imgH);
            p.save();
            p.setClipRect(target);
            auto imgToScreen = [&](double ix, double iy) -> QPointF {
                double sx = target.x() + (ix - src.x()) / src.width()  * target.width();
                double sy = target.y() + (iy - src.y()) / src.height() * target.height();
                return QPointF(sx, sy);
            };
            QPointF tl = imgToScreen(m_cropRect.left(), m_cropRect.top());
            QPointF br = imgToScreen(m_cropRect.left() + m_cropRect.width(),
                                     m_cropRect.top()  + m_cropRect.height());
            QRectF sr(tl, br);
            p.setBrush(QColor(255, 255, 255, 128));
            p.setPen(QPen(Qt::white, 1));
            p.drawRect(sr);
            p.restore();
        }

        // Draw amyloid filament overlays (control polylines + draggable points)
        if (m_amyloidActive && (!m_amyloidFilaments.empty() || m_amyloidPlacing == 1)) {
            QRect target = frame.adjusted(2, 2, -2, -2);
            QRectF src = m_zoom[0].visibleRect(imgW, imgH);
            p.save();
            p.setClipRect(target);
            p.setRenderHint(QPainter::Antialiasing, true);

            auto imgToScreen = [&](const QPointF &img) -> QPointF {
                double sx = target.x() + (img.x() - src.x()) / src.width() * target.width();
                double sy = target.y() + (img.y() - src.y()) / src.height() * target.height();
                return QPointF(sx, sy);
            };

            // Draw existing filaments using Catmull-Rom spline curves
            for (const auto &fil : m_amyloidFilaments) {
                if (fil.pts.size() < 2) continue;
                int nPts = (int)fil.pts.size();

                // Only draw spline lines when not yet rendered (avoid visual confusion with FFT)
                if (!m_amyloidRendered) {
                    QPen linePen(QColor(0, 200, 255), 2);
                    p.setPen(linePen);

                    // Build a smooth spline through control points
                    const int stepsPerSeg = 20;
                    QPointF prev = imgToScreen(fil.pts[0]);
                    for (int seg = 0; seg < nPts - 1; seg++) {
                        // Catmull-Rom uses 4 points: p0, p1, p2, p3
                        QPointF p0 = fil.pts[std::max(0, seg - 1)];
                        QPointF p1 = fil.pts[seg];
                        QPointF p2 = fil.pts[seg + 1];
                        QPointF p3 = fil.pts[std::min(nPts - 1, seg + 2)];
                        for (int step = 1; step <= stepsPerSeg; step++) {
                            double t = step / (double)stepsPerSeg;
                            double t2 = t * t, t3 = t2 * t;
                            // Catmull-Rom basis (tau = 0.5)
                            double c0 = -0.5*t3 + t2 - 0.5*t;
                            double c1 =  1.5*t3 - 2.5*t2 + 1.0;
                            double c2 = -1.5*t3 + 2.0*t2 + 0.5*t;
                            double c3 =  0.5*t3 - 0.5*t2;
                            QPointF img(c0*p0.x() + c1*p1.x() + c2*p2.x() + c3*p3.x(),
                                        c0*p0.y() + c1*p1.y() + c2*p2.y() + c3*p3.y());
                            QPointF cur = imgToScreen(img);
                            p.drawLine(prev, cur);
                            prev = cur;
                        }
                    }
                }
                // Control point handles (always visible for editing)
                int cpRad = std::max(3, target.width() / 120);
                for (int j = 0; j < nPts; j++) {
                    QPointF s = imgToScreen(fil.pts[j]);
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(0, 200, 255, 180));
                    p.drawEllipse(s, cpRad, cpRad);
                }
            }

            // Draw in-progress start point with rubber-band line to cursor
            if (m_amyloidPlacing == 1) {
                QPointF sp = imgToScreen(m_amyloidStartPt);
                int cpRad = std::max(3, target.width() / 120);
                p.setPen(QPen(Qt::white, 1));
                p.setBrush(QColor(255, 100, 100, 180));
                p.drawEllipse(sp, cpRad, cpRad);
                // Rubber band to current mouse
                if (target.contains(m_mousePos)) {
                    p.setPen(QPen(QColor(255, 100, 100, 150), 1, Qt::DashLine));
                    p.drawLine(sp, QPointF(m_mousePos));
                }
            }

            p.setRenderHint(QPainter::Antialiasing, false);
            p.restore();
        }

        drawAxes(p, frame, m_zoom[0], imgW, imgH, false, m_pixelSize);
        drawZoomPanOverlay(frame, m_zoom[0], &m_p1MaxRect);

        QRect inner = frame.adjusted(2, 2, -2, -2);
        double curVal = 0;
        int mX = 0, mY = 0;
        bool hasCur = sampleValue(inner, m_zoom[0], imgW, imgH, m_imageRawPixels, curVal, &mX, &mY);
        drawMinMax(p, frame, m_imageMinVal, m_imageMaxVal, curVal, hasCur,
                   hasCur ? QString("Mouse X,Y = %1, %2").arg(mX).arg(mY) : QString());
        drawHistogram(p, frame, m_imageRawPixels, m_imageMinVal, m_imageMaxVal, hy - frame.bottom(),
                      HIST_P1, m_imageDispMin, m_imageDispMax);

        if (!m_histRects[HIST_P1].isNull()) {
            QRect hr = m_histRects[HIST_P1];
            // Stack "mark image center" above "freeze display contrast" to
            // the left of the histogram, both at the same fixed size. These
            // are custom-painted (before any panel-1 tool dialog) so that
            // the dialog overpaints them, leaving them visually behind it.
            QFont bf; bf.setPixelSize(11);
            QFontMetrics bfm(bf);
            const QString markText = "mark image center";
            const QString freezeText = "freeze display contrast";
            int w = std::max(bfm.horizontalAdvance(markText),
                             bfm.horizontalAdvance(freezeText)) + 16;
            int h = bfm.height() + 8;
            const int gap = 4;
            int x = hr.left() - 6 - w;
            int cy = hr.center().y();
            m_markImageCenterRect = QRect(x, cy - h - gap / 2, w, h);
            m_imageHistLockRect   = QRect(x, cy + gap / 2,     w, h);
            paintToggleButton(m_markImageCenterRect, markText, m_imageCenterMarked);
            paintToggleButton(m_imageHistLockRect,   freezeText, m_imageContrastLocked);
        }

        // Pixel size label above top-left corner of image (outside frame)
        {
            QFont pf;
            pf.setPixelSize(11);
            p.setFont(pf);
            p.setPen(Qt::white);
            QFontMetrics pfm(pf);
            // When the file carried no scale we are only assuming 1 px = 1 \u00C5;
            // flag that so the numbers below are not mistaken for a measured size.
            QString psLabel = QString("%1pixel = %2 %3")
                                  .arg(m_pixelSizeAssumed ? "Assumed: 1 " : "1 ")
                                  .arg(m_pixelSize, 0, 'g', 4)
                                  .arg(QString::fromUtf8("\u00C5"));

            // Resolution and pixel-size info below the bottom-right corner of panel 1
            int infoX = panel1W - 4;
            int infoTop = frame.bottom() + 4 + 3 * (pfm.height() + 1);
            int infoY = infoTop;

            QString resLabel = QString("%1 x %2 pixels").arg(imgW).arg(imgH);
            p.drawText(infoX - pfm.horizontalAdvance(resLabel), infoY + pfm.ascent(), resLabel);

            infoY += pfm.height() + 1;
            p.drawText(infoX - pfm.horizontalAdvance(psLabel), infoY + pfm.ascent(), psLabel);

            // The size + pixel-size lines are a double-click target for editing
            // the pixel size (see mouseDoubleClickEvent / onEditPixelSize).
            int infoBlockW = std::max(pfm.horizontalAdvance(resLabel),
                                      pfm.horizontalAdvance(psLabel));
            m_pixelSizeInfoRect = QRect(infoX - infoBlockW, infoTop,
                                        infoBlockW,
                                        (infoY + pfm.height()) - infoTop)
                                      .adjusted(-2, -2, 2, 2);

            if (!m_imagePath.isEmpty()) {
                infoY += pfm.height() + 1;
                QString fname = QFileInfo(m_imagePath).fileName();
                p.drawText(infoX - pfm.horizontalAdvance(fname), infoY + pfm.ascent(), fname);
            }

            // Most recent operation applied to this buffer, one line below the
            // file name. Drawn a touch dimmer so it reads as a status note.
            if (!m_lastOperation.isEmpty()) {
                infoY += pfm.height() + 1;
                p.setPen(QColor(180, 180, 180));
                QString opLabel = QString::fromUtf8("↳ ") + m_lastOperation;
                p.drawText(infoX - pfm.horizontalAdvance(opLabel),
                           infoY + pfm.ascent(), opLabel);
                p.setPen(Qt::white);
            }
        }

        // Red plus sign at image center (mirrors Fourier-space origin cross)
        if (m_imageCenterMarked) {
            QRect target = frame.adjusted(2, 2, -2, -2);
            QRectF src = m_zoom[0].visibleRect(imgW, imgH);
            double originX = imgW / 2.0 + 0.5;
            double originY = imgH / 2.0 + 0.5;
            double sx = target.x() + (originX - src.x()) / src.width()  * target.width();
            double sy = target.y() + (originY - src.y()) / src.height() * target.height();
            double armImg = imgW / 32.0;
            double armScreen = armImg / src.width() * target.width();
            p.save();
            p.setClipRect(target);
            p.setPen(QPen(Qt::red, 1));
            p.drawLine(QPointF(sx - armScreen, sy), QPointF(sx + armScreen, sy));
            p.drawLine(QPointF(sx, sy - armScreen), QPointF(sx, sy + armScreen));
            p.restore();
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

    // Math calculations overlay (draws over panel 1 regardless of image state)
    if (m_mathActive) {
        int side1 = static_cast<int>(0.7 * std::min(panel1W, panel1H));
        int imgX = (panel1W - side1) / 2;
        int imgY = (panel1H - side1) / 2;
        QRect inner = QRect(imgX, imgY, side1, side1).adjusted(2, 2, -2, -2);

        int fw = static_cast<int>(inner.width()  * 0.80);
        int fh = static_cast<int>(inner.height() * 0.30);
        int fx = inner.x() + (inner.width()  - fw) / 2;
        int fy = inner.y() + (inner.height() - fh) / 2;
        QRect mathRect(fx, fy, fw, fh);

        drawShadowRect(p, mathRect);

        // Progress bar: light blue fill from left to right
        if (m_mathProgress >= 0.0 && m_mathProgress <= 1.0) {
            int progW = static_cast<int>(fw * m_mathProgress);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(180, 210, 255));
            p.drawRect(fx + 1, fy + 1, progW, fh - 2);
        }

        // Scale widget sizes relative to frame width
        int fontSize = std::clamp(fw / 30, 12, 32);
        int comboH = fontSize * 2;
        int bufW = fw / 8;
        int eqW  = fw / 16;
        int opW  = fw * 3 / 10;
        int btnW = fw * 3 / 16;
        int btnH = comboH;
        int gap  = fw / 80;

        // Apply scaled stylesheet to all math combos
        auto setStyleSheetIfChanged = [](QWidget *widget, const QString &styleSheet)
        {
            if (widget->styleSheet() != styleSheet)
                widget->setStyleSheet(styleSheet);
        };
        auto setFixedSizeIfChanged = [](QWidget *widget, int w, int h)
        {
            if (widget->size() != QSize(w, h))
                widget->setFixedSize(w, h);
        };

        QString comboSS = QString(
            "QComboBox { background:white; color:black; border:1px solid #888;"
            "  padding: 2px 4px; font-size: %1px; font-weight: bold; }"
            "QComboBox::drop-down { width: %2px; }"
            "QComboBox QAbstractItemView { background:white; color:black;"
            "  selection-background-color:#ccc; min-width: 60px; padding: 4px;"
            "  font-size: %1px; }")
            .arg(fontSize).arg(fontSize);
        setStyleSheetIfChanged(m_mathOutCombo, comboSS);
        setStyleSheetIfChanged(m_mathIn1Combo, comboSS);
        setStyleSheetIfChanged(m_mathOpCombo, comboSS);
        setStyleSheetIfChanged(m_mathIn2Combo, comboSS);

        setFixedSizeIfChanged(m_mathOutCombo, bufW, comboH);
        setFixedSizeIfChanged(m_mathIn1Combo, bufW, comboH);
        setFixedSizeIfChanged(m_mathOpCombo, opW, comboH);
        setFixedSizeIfChanged(m_mathIn2Combo, bufW, comboH);
        setFixedSizeIfChanged(m_mathEqualsLabel, eqW, comboH);
        setStyleSheetIfChanged(
            m_mathEqualsLabel,
            QString("color: black; font-size: %1px; font-weight: bold;").arg(fontSize * 4 / 3));

        setFixedSizeIfChanged(m_mathCancelBtn, btnW, btnH);
        setFixedSizeIfChanged(m_mathComputeBtn, btnW, btnH);
        QString btnSS = QString(
            "QPushButton { background-color: #888; border: 2px outset #aaa;"
            "  color: #eee; padding: 2px; font-size: %1px; font-weight: bold; }").arg(fontSize);
        setStyleSheetIfChanged(m_mathCancelBtn, btnSS);
        setStyleSheetIfChanged(m_mathComputeBtn, btnSS);

        // Position the equation widgets centered in the frame
        int totalEqW = bufW + gap + eqW + gap + bufW + gap + opW + gap + bufW;
        int eqX = fx + (fw - totalEqW) / 2;
        int eqY = fy + fh / 2 - comboH / 2;

        // Title in top-left corner
        int titleBottom;
        {
            int titleFontSize = std::max(14, fontSize * 4 / 3);
            int titleMarginX = std::max(8, fw / 40);
            int titleMarginY = std::max(6, fh / 15);
            QFont tf;
            tf.setPixelSize(titleFontSize);
            tf.setBold(true);
            p.setFont(tf);
            p.setPen(QColor(60, 60, 60));
            QFontMetrics tfm(tf);
            int titleBaseY = fy + titleMarginY + tfm.ascent();
            p.drawText(fx + titleMarginX, titleBaseY, "Image calculation");
            titleBottom = titleBaseY + tfm.descent();
            m_p1MathHelpRect = drawHelpButton(p, mathRect, titleMarginX,
                                              titleMarginY, tfm.height() + 6, 26);
            // The Math window draws its own header rather than going through
            // drawToolHelpHeader, so its "E" button is placed here.
            m_p1ExerciseRect = drawExerciseButton(p, m_p1MathHelpRect,
                                                  toolExerciseAnchor(false));
        }

        // Draw clean equation text centered between title and combo row
        {
            QString opSym = m_mathOpCombo->currentText();
            int opIdx2 = m_mathOpCombo->currentIndex();
            if (opIdx2 == 4) opSym = QString::fromUtf8("\u2731");       // convolute: ✱
            else if (opIdx2 == 5) opSym = QString::fromUtf8("\u229B");  // correlate: ⊛
            QString eqText = QString("%1 = %2 %3 %4")
                .arg(QChar('a' + m_mathOutCombo->currentIndex()))
                .arg(QChar('a' + m_mathIn1Combo->currentIndex()))
                .arg(opSym)
                .arg(QChar('a' + m_mathIn2Combo->currentIndex()));
            int eqFontSize = std::max(16, fontSize * 3 / 2);
            QFont ef("Palatino");
            ef.setPixelSize(eqFontSize);
            p.setFont(ef);
            p.setPen(QColor(40, 40, 40));
            QFontMetrics efm(ef);
            int eqTextW = efm.horizontalAdvance(eqText);
            int gapCenter = titleBottom + (eqY - titleBottom) / 2;
            int eqTextY = gapCenter - efm.height() / 2;
            p.drawText(fx + (fw - eqTextW) / 2, eqTextY + efm.ascent(), eqText);
        }

        m_mathOutCombo->move(eqX, eqY);        eqX += bufW + gap;
        m_mathEqualsLabel->move(eqX, eqY);      eqX += eqW + gap;
        m_mathIn1Combo->move(eqX, eqY);          eqX += bufW + gap;
        m_mathOpCombo->move(eqX, eqY);           eqX += opW + gap;
        m_mathIn2Combo->move(eqX, eqY);

        // Cancel in bottom-left, Compute in bottom-right
        int btnMargin = 8;
        m_mathCancelBtn->move(fx + btnMargin, fy + fh - btnH - btnMargin);
        m_mathComputeBtn->move(fx + fw - btnW - btnMargin, fy + fh - btnH - btnMargin);
    }

    // "Create or Copy an image" popup (launched from top-left "New image" button)
    if (m_newImageActive) {
        int side1 = static_cast<int>(0.7 * std::min(panel1W, panel1H));
        int imgX = (panel1W - side1) / 2;
        int imgY = (panel1H - side1) / 2;
        QRect inner = QRect(imgX, imgY, side1, side1).adjusted(2, 2, -2, -2);

        int fw = static_cast<int>(inner.width()  * 0.85);
        int fh = static_cast<int>(inner.height() * 0.42);
        int fx = inner.x() + (inner.width()  - fw) / 2;
        int fy = inner.y() + (inner.height() - fh) / 2;
        QRect rect(fx, fy, fw, fh);

        drawShadowRect(p, rect);

        // Scale widget sizes relative to frame width (like the math overlay)
        int fontSize     = std::clamp(fw / 30, 12, 32);
        int comboH       = fontSize * 2;
        int srcComboW    = std::max(120, fw / 4);
        int tgtComboW    = std::max(80,  fw / 6);
        int btnW         = std::max(90,  fw / 7);
        int btnH         = comboH;

        auto setStyleSheetIfChanged = [](QWidget *w, const QString &ss) {
            if (w->styleSheet() != ss) w->setStyleSheet(ss);
        };
        auto setFixedSizeIfChanged = [](QWidget *w, int cw, int ch) {
            if (w->size() != QSize(cw, ch)) w->setFixedSize(cw, ch);
        };

        QString comboSS = QString(
            "QComboBox { background:white; color:black; border:1px solid #888;"
            "  padding: 2px 4px; font-size: %1px; font-weight: bold; }"
            "QComboBox::drop-down { width: %2px; }"
            "QComboBox QAbstractItemView { background:white; color:black;"
            "  selection-background-color:#ccc; min-width: 60px; padding: 4px;"
            "  font-size: %1px; }")
            .arg(fontSize).arg(fontSize);
        setStyleSheetIfChanged(m_newImgSrcCombo, comboSS);
        setStyleSheetIfChanged(m_newImgTgtCombo, comboSS);
        setFixedSizeIfChanged(m_newImgSrcCombo, srcComboW, comboH);
        setFixedSizeIfChanged(m_newImgTgtCombo, tgtComboW, comboH);

        QString btnSS = QString(
            "QPushButton { background-color: #888; border: 2px outset #aaa;"
            "  color: #eee; padding: 2px; font-size: %1px; font-weight: bold; }")
            .arg(fontSize);
        setStyleSheetIfChanged(m_newImgCancelBtn, btnSS);
        setStyleSheetIfChanged(m_newImgCreateBtn, btnSS);
        setFixedSizeIfChanged(m_newImgCancelBtn, btnW, btnH);
        setFixedSizeIfChanged(m_newImgCreateBtn, btnW, btnH);

        // Title
        int titleFontSize = std::max(14, fontSize * 4 / 3);
        QFont tf;
        tf.setPixelSize(titleFontSize);
        tf.setBold(true);
        p.setFont(tf);
        p.setPen(QColor(60, 60, 60));
        QFontMetrics tfm(tf);
        int titleMarginX = std::max(10, fw / 40);
        int titleMarginY = std::max(8, fh / 20);
        int titleBaseY = fy + titleMarginY + tfm.ascent();
        p.drawText(fx + titleMarginX, titleBaseY, "Create or Copy an image");

        // Row labels
        QFont lf;
        lf.setPixelSize(fontSize);
        lf.setBold(true);
        p.setFont(lf);
        p.setPen(QColor(40, 40, 40));
        QFontMetrics lfm(lf);

        int padX = std::max(12, fw / 40);
        int labelColW = std::max(lfm.horizontalAdvance("Source image: "),
                                 lfm.horizontalAdvance("Target image: "));

        // ---- Source row: "Source image: [combo]" ----
        int srcRowY = titleBaseY + tfm.descent() + std::max(14, fh / 10);
        p.drawText(fx + padX,
                   srcRowY + comboH / 2 + lfm.ascent() / 2 - 2,
                   "Source image: ");
        m_newImgSrcCombo->move(fx + padX + labelColW + 6, srcRowY);

        // ---- Target row: "Target image: [combo]" ----
        int tgtRowY = srcRowY + comboH + std::max(8, fh / 20);
        p.drawText(fx + padX,
                   tgtRowY + comboH / 2 + lfm.ascent() / 2 - 2,
                   "Target image: ");
        m_newImgTgtCombo->move(fx + padX + labelColW + 6, tgtRowY);

        // ---- Bottom row: Cancel (left), Execute (right) ----
        int btnMargin = std::max(10, fh / 12);
        int btnY = fy + fh - btnH - btnMargin;
        m_newImgCancelBtn->move(fx + btnMargin, btnY);
        m_newImgCreateBtn->move(fx + fw - btnW - btnMargin, btnY);
    }

    // ---- Panel 2: FFT results -------------------------------------------------
    if (m_ftComputed) {
        int panel2X = cx + 2;
        int panel2W = width() - panel2X;
        int panel2H = hy - 1;

        // Helper: draw a small red cross at the Fourier-space origin
        auto drawOriginCross = [&](const QRect &screenRect, const ZoomState &zoom,
                                   int imgW, int imgH) {
            QRectF src = zoom.visibleRect(imgW, imgH);
            double originX = imgW / 2.0 + 0.5;  // center of the central pixel
            double originY = imgH / 2.0 + 0.5;
            // Map image coordinate to screen coordinate
            double sx = screenRect.x() + (originX - src.x()) / src.width()  * screenRect.width();
            double sy = screenRect.y() + (originY - src.y()) / src.height() * screenRect.height();
            // Cross arm length: 1/32 of image mapped to screen
            double armImg = imgW / 32.0;
            double armScreen = armImg / src.width() * screenRect.width();
            p.save();
            p.setClipRect(screenRect);
            p.setPen(QPen(Qt::red, 1));
            p.drawLine(QPointF(sx - armScreen, sy), QPointF(sx + armScreen, sy));
            p.drawLine(QPointF(sx, sy - armScreen), QPointF(sx, sy + armScreen));
            p.restore();
        };

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
            if (m_displayMode == 3) {
                p.drawText(frame.x(), frame.y() - 4, "Powerspectrum");
                QFontMetrics lfm(lf);
                QFont sf; sf.setPixelSize(14); p.setFont(sf);
                p.drawText(frame.x() + lfm.horizontalAdvance("Powerspectrum") + 4,
                           frame.y() - 4, "displayed as log(1 + amp*amp)");
                p.setFont(lf);
            } else {
                p.drawText(frame.x(), frame.y() - 4, "Complex FT");
            }

            const QImage &img = (m_displayMode == 3) ? m_powerImg : m_complexImg;
            drawImageWithFrame(p, frame, img, m_zoom[1], m_fftN, m_fftN);
            drawOriginCross(frame.adjusted(2, 2, -2, -2), m_zoom[1], m_fftN, m_fftN);
            drawAxes(p, frame, m_zoom[1], m_fftN, m_fftN, true, m_pixelSize);
            drawZoomPanOverlay(frame, m_zoom[1], &m_p2MaxRectA);

            QRect inner = frame.adjusted(2, 2, -2, -2);
            double curVal = 0;
            int mX = 0, mY = 0;
            bool hasCur = sampleValue(inner, m_zoom[1], m_fftN, m_fftN, m_powerVals, curVal, &mX, &mY);

            if (m_displayMode == 2) {
                double curAmp = 0, curPhase = 0;
                bool hasAmp = sampleValue(inner, m_zoom[1], m_fftN, m_fftN, m_ampVals, curAmp);
                bool hasPh  = sampleValue(inner, m_zoom[1], m_fftN, m_fftN, m_phaseVals, curPhase);

                QFont f; f.setPixelSize(11); p.setFont(f); p.setPen(Qt::white);
                QFontMetrics fm2(f);
                QString minMaxText = QString("Min: %1     Max: %2")
                               .arg(m_powerMin, 0, 'g', 5).arg(m_powerMax, 0, 'g', 5);
                int mmw = fm2.horizontalAdvance(minMaxText);
                int lineH = fm2.height();
                p.drawText(frame.right() - mmw, frame.top() - 5, minMaxText);
                if (hasAmp && hasPh) {
                    QString curText = QString("Current amplitude: %1     Current phase: %2")
                                   .arg(curAmp, 0, 'g', 5).arg(curPhase, 0, 'g', 5);
                    int cw = fm2.horizontalAdvance(curText);
                    p.drawText(frame.right() - cw, frame.top() - 5 - lineH, curText);
                }
                if (hasCur) {
                    QString mText = QString("Mouse X,Y = %1, %2").arg(mX - m_fftN / 2).arg(mY - m_fftN / 2);
                    int mw = fm2.horizontalAdvance(mText);
                    p.drawText(frame.right() - mw, frame.top() - 5 - 2 * lineH, mText);
                }
            } else {
                QString mouseText;
                if (hasCur) {
                    double dx = mX - m_fftN / 2.0;
                    double dy = mY - m_fftN / 2.0;
                    double rad = std::sqrt(dx * dx + dy * dy);
                    if (rad < 1e-12 || m_pixelSize <= 0) {
                        mouseText = QString("Mouse = (\u221E %1)\u207B\u00B9")
                                        .arg(QString::fromUtf8("\u00C5"));
                    } else {
                        double res = (m_fftN * m_pixelSize) / rad;
                        mouseText = QString("Mouse = (%1 %2)\u207B\u00B9")
                                        .arg(res, 0, 'g', 3)
                                        .arg(QString::fromUtf8("\u00C5"));
                    }
                }
                drawMinMax(p, frame, m_powerMin, m_powerMax, curVal, hasCur, mouseText);
            }

            // In complex mode the histogram controls the brightness range of
            // the coloured display, so show that range; otherwise it controls
            // the power-spectrum image. Until the user actually selects a
            // sub-range in complex mode, pass an empty range so no grey
            // overlay is drawn (the auto default excludes the DC peak and so
            // would otherwise look like a custom selection).
            double powHistDispMin, powHistDispMax;
            if (m_displayMode == 2) {
                if (m_complexRangeCustom) { powHistDispMin = m_complexDispMin; powHistDispMax = m_complexDispMax; }
                else                      { powHistDispMin = powHistDispMax = m_powerMin; }
            } else {
                powHistDispMin = m_powerDispMin; powHistDispMax = m_powerDispMax;
            }
            drawHistogram(p, frame, m_powerVals, m_powerMin, m_powerMax, hy - frame.bottom(),
                          HIST_POWER, powHistDispMin, powHistDispMax);

            // Custom-paint the FT contrast lock and mask-center toggles
            // stacked to the right of the histogram, both same size. They are
            // painted BEFORE the bottom-right tool dialog so the dialog
            // covers them, leaving them visually behind it.
            if (!m_histRects[HIST_POWER].isNull()) {
                QRect hr = m_histRects[HIST_POWER];
                QFont bf; bf.setPixelSize(11);
                QFontMetrics bfm(bf);
                const QString maskText   = "mask center for display";
                const QString freezeText = "freeze display contrast";
                int bw = std::max(bfm.horizontalAdvance(maskText),
                                  bfm.horizontalAdvance(freezeText)) + 16;
                int bh = bfm.height() + 8;
                const int gap = 4;
                int x = hr.right() + 6;
                int cy = hr.center().y();
                if (m_maskBtnVisible)
                    m_maskBtnRect = QRect(x, cy - bh - gap / 2, bw, bh);
                m_ftHistLockRect = QRect(x, cy + gap / 2, bw, bh);
                if (m_maskBtnVisible)
                    paintToggleButton(m_maskBtnRect, maskText, m_maskCenter);
                paintToggleButton(m_ftHistLockRect, freezeText, m_ftContrastLocked);
            }

            // Color wheel for complex FT mode — vertically centred on the
            // power-spectrum histogram, horizontally flush with the left
            // edge of the FT display.
            if (m_displayMode == 2) {
                int availBelow = hy - frame.bottom();
                int histH = std::max(16, availBelow / 3);

                int wheelD = std::min(histH, frame.width() / 6);
                if (wheelD > 8) {
                    int r = wheelD / 2;
                    // Left edge of wheel aligns with left edge of FT frame;
                    // vertical centre matches the histogram's vertical centre.
                    int wcx = frame.left() + r;
                    int wcy = !m_histRects[HIST_POWER].isNull()
                                ? m_histRects[HIST_POWER].center().y()
                                : frame.bottom() + (3 * histH) / 2;

                    QImage wheelImg(wheelD, wheelD, QImage::Format_ARGB32);
                    wheelImg.fill(Qt::transparent);
                    for (int wy = 0; wy < wheelD; wy++) {
                        QRgb *row = reinterpret_cast<QRgb *>(wheelImg.scanLine(wy));
                        for (int wx = 0; wx < wheelD; wx++) {
                            double dx = wx - r + 0.5;
                            double dy = wy - r + 0.5;
                            double dist = std::sqrt(dx * dx + dy * dy);
                            if (dist <= r) {
                                double angle = std::atan2(dy, dx) * 180.0 / M_PI;
                                double hue = angle + 180.0;
                                QColor c = QColor::fromHsvF(hue / 360.0, 1.0, 1.0);
                                row[wx] = c.rgba();
                            }
                        }
                    }
                    p.drawImage(wcx - r, wcy - r, wheelImg);

                    // Phase labels at cardinal directions
                    QFont wf; wf.setPixelSize(9); p.setFont(wf); p.setPen(Qt::white);
                    QFontMetrics wfm(wf);
                    int lr = r + 2;
                    p.drawText(wcx + lr, wcy + wfm.ascent() / 2, QString::fromUtf8("0\u00B0"));
                    QString s180 = QString::fromUtf8("\u00B1180\u00B0");
                    p.drawText(wcx - lr - wfm.horizontalAdvance(s180), wcy + wfm.ascent() / 2, s180);
                    p.drawText(wcx - wfm.horizontalAdvance("90") / 2, wcy + lr + wfm.ascent(), QString::fromUtf8("90\u00B0"));
                    QString sn90 = QString::fromUtf8("-90\u00B0");
                    p.drawText(wcx - wfm.horizontalAdvance(sn90) / 2, wcy - lr, sn90);
                }
            }

            if (m_lineFilterActive)
                drawLineFilter(p, inner, m_zoom[1], m_fftN, m_fftN);
            if (m_bandpassActive)
                drawBandpassRing(p, inner, m_zoom[1], m_fftN, m_fftN);
            if (m_directionalActive)
                drawDirectionalWedge(p, inner, m_zoom[1], m_fftN, m_fftN);
            if (m_latticeActive)
                drawLattice(p, inner, m_zoom[1], m_fftN, m_fftN);
            if (m_crossSectionActive)
                drawCrossSectionLines(p, inner, m_zoom[1], m_fftN, m_fftN);
            if (m_ctfActive)
                drawCtfDirectionLine(p, inner, m_zoom[1], m_fftN, m_fftN);

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
            double dmin1, dmax1, dmin2, dmax2;
            QString label1, label2;

            if (m_displayMode == 0) {
                img1 = &m_cosImg;  img2 = &m_sinImg;
                vals1 = &m_cosVals; vals2 = &m_sinVals;
                min1 = m_cosMin; max1 = m_cosMax;
                min2 = m_sinMin; max2 = m_sinMax;
                dmin1 = m_cosDispMin; dmax1 = m_cosDispMax;
                dmin2 = m_sinDispMin; dmax2 = m_sinDispMax;
                label1 = "Cosinus"; label2 = "Sinus";
            } else {
                img1 = &m_ampImg;  img2 = &m_phaseImg;
                vals1 = &m_ampVals; vals2 = &m_phaseVals;
                min1 = m_ampMin; max1 = m_ampMax;
                min2 = m_phaseMin; max2 = m_phaseMax;
                dmin1 = m_ampDispMin; dmax1 = m_ampDispMax;
                dmin2 = m_phaseDispMin; dmax2 = m_phaseDispMax;
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
            if (m_displayMode == 1) {
                QFontMetrics lfm(lf);
                QFont sf; sf.setPixelSize(14); p.setFont(sf);
                p.drawText(frame1.x() + lfm.horizontalAdvance(label1) + 4,
                           frame1.y() - 4, "displayed as log(1+amp)");
                p.setFont(lf);
            }
            p.drawText(frame2.x(), frame2.y() - 4, label2);

            drawImageWithFrame(p, frame1, *img1, m_zoom[1], m_fftN, m_fftN);
            drawOriginCross(frame1.adjusted(2, 2, -2, -2), m_zoom[1], m_fftN, m_fftN);
            drawAxes(p, frame1, m_zoom[1], m_fftN, m_fftN, true, m_pixelSize);
            // No maximize icon here: the left overlay sits in the narrow gap
            // between the two frames and is clipped by frame2, so the icon
            // would not be clickable. The right-hand overlay carries the icon,
            // and it maximizes both images anyway.
            drawZoomPanOverlay(frame1, m_zoom[1]);
            QRect inner1 = frame1.adjusted(2, 2, -2, -2);
            double curVal1 = 0;
            int mX1 = 0, mY1 = 0;
            bool hasCur1 = sampleValue(inner1, m_zoom[1], m_fftN, m_fftN, *vals1, curVal1, &mX1, &mY1);
            QString mouseText1;
            if (hasCur1) {
                double dx = mX1 - m_fftN / 2.0;
                double dy = mY1 - m_fftN / 2.0;
                double rad = std::sqrt(dx * dx + dy * dy);
                if (rad < 1e-12 || m_pixelSize <= 0) {
                    mouseText1 = QString("Mouse = (\u221E %1)\u207B\u00B9")
                                     .arg(QString::fromUtf8("\u00C5"));
                } else {
                    double res = (m_fftN * m_pixelSize) / rad;
                    mouseText1 = QString("Mouse = (%1 %2)\u207B\u00B9")
                                     .arg(res, 0, 'g', 3)
                                     .arg(QString::fromUtf8("\u00C5"));
                }
            }
            drawMinMax(p, frame1, min1, max1, curVal1, hasCur1, mouseText1);
            drawHistogram(p, frame1, *vals1, min1, max1, hy - frame1.bottom(),
                          HIST_FT_LEFT, dmin1, dmax1);

            drawImageWithFrame(p, frame2, *img2, m_zoom[2], m_fftN, m_fftN);
            drawOriginCross(frame2.adjusted(2, 2, -2, -2), m_zoom[2], m_fftN, m_fftN);
            drawAxes(p, frame2, m_zoom[2], m_fftN, m_fftN, true, m_pixelSize, true);
            drawZoomPanOverlay(frame2, m_zoom[2], &m_p2MaxRectB);
            QRect inner2 = frame2.adjusted(2, 2, -2, -2);
            double curVal2 = 0;
            int mX2 = 0, mY2 = 0;
            bool hasCur2 = sampleValue(inner2, m_zoom[2], m_fftN, m_fftN, *vals2, curVal2, &mX2, &mY2);
            QString mouseText2;
            if (hasCur2) {
                double dx = mX2 - m_fftN / 2.0;
                double dy = mY2 - m_fftN / 2.0;
                double rad = std::sqrt(dx * dx + dy * dy);
                if (rad < 1e-12 || m_pixelSize <= 0) {
                    mouseText2 = QString("Mouse = (\u221E %1)\u207B\u00B9")
                                     .arg(QString::fromUtf8("\u00C5"));
                } else {
                    double freq = rad / (m_fftN * m_pixelSize);
                    double res = 1.0 / freq;
                    mouseText2 = QString("Mouse = (%1 %2)\u207B\u00B9")
                                     .arg(res, 0, 'g', 3)
                                     .arg(QString::fromUtf8("\u00C5"));
                }
            }
            drawMinMax(p, frame2, min2, max2, curVal2, hasCur2, mouseText2);
            drawHistogram(p, frame2, *vals2, min2, max2, hy - frame2.bottom(),
                          HIST_FT_RIGHT, dmin2, dmax2);

            if (m_lineFilterActive) {
                drawLineFilter(p, inner1, m_zoom[1], m_fftN, m_fftN);
                drawLineFilter(p, inner2, m_zoom[2], m_fftN, m_fftN);
            }
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
            if (m_crossSectionActive) {
                drawCrossSectionLines(p, inner1, m_zoom[1], m_fftN, m_fftN);
                drawCrossSectionLines(p, inner2, m_zoom[2], m_fftN, m_fftN);
            }
            if (m_ctfActive) {
                drawCtfDirectionLine(p, inner1, m_zoom[1], m_fftN, m_fftN);
                drawCtfDirectionLine(p, inner2, m_zoom[2], m_fftN, m_fftN);
            }

            DisplayItem &d1 = m_dispItems[m_numDispItems++];
            d1 = { inner1, m_fftN, m_fftN, 1, vals1, true };
            DisplayItem &d2 = m_dispItems[m_numDispItems++];
            d2 = { inner2, m_fftN, m_fftN, 2, vals2, true };
        }
    } else if (m_activeSlot >= 0) {
        // Empty FFT buffer selected – draw yellow frame with uppercase buffer letter
        int panel2X = cx + 2;
        int panel2W = width() - panel2X;
        int panel2H = hy - 1;

        int side = static_cast<int>(0.7 * std::min(panel2W, panel2H));
        int fx = panel2X + (panel2W - side) / 2;
        int fy = (panel2H - side) / 2;
        QRect frame(fx, fy, side, side);

        QFont lf; lf.setBold(true); lf.setPixelSize(labelFontMain); p.setFont(lf);
        p.setPen(QColor(255, 255, 0));
        QFontMetrics lfm(lf);
        QString lab = QString(QChar('A' + m_activeSlot));
        p.drawText(frame.x() + (frame.width() - lfm.horizontalAdvance(lab)) / 2,
                   frame.y() - 22, lab);

        p.setPen(QPen(QColor(255, 255, 0), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(frame);
    }

    // ---- Fourier math overlay (panel 2) -----------------------------------------
    if (m_ftMathActive && m_ftComputed) {
        int panel2X = cx + 2;
        int panel2W = width() - panel2X;
        int panel2H = hy - 1;

        // Match panel 1's math-overlay dimensions so both popups feel identical.
        int side1M = static_cast<int>(0.7 * std::min(panel1W, panel1H));
        int innerM = side1M - 4;
        int fw = static_cast<int>(innerM * 0.80);
        int fh = static_cast<int>(innerM * 0.30);
        int fx = panel2X + (panel2W - fw) / 2;
        int fy = (panel2H - fh) / 2;
        QRect ftMathRect(fx, fy, fw, fh);

        drawShadowRect(p, ftMathRect);

        // Progress bar: light blue fill from left to right
        if (m_ftMathProgress >= 0.0 && m_ftMathProgress <= 1.0) {
            int progW = static_cast<int>(fw * m_ftMathProgress);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(180, 210, 255));
            p.drawRect(fx + 1, fy + 1, progW, fh - 2);
        }

        // Scale widget sizes relative to panel 1's math-overlay frame width
        // so that widgets/fonts in panel 2's math overlay match panel 1's.
        int side1Ref  = static_cast<int>(0.7 * std::min(panel1W, panel1H));
        int innerRefW = side1Ref - 4;
        int fwRef     = static_cast<int>(innerRefW * 0.80);

        int fontSize = std::clamp(fwRef / 30, 12, 32);
        int comboH = fontSize * 2;
        int bufW = fwRef / 8;
        int eqW  = fwRef / 16;
        int opW  = fwRef / 12;   // only single-char +, -, *, /
        int conjW = fwRef * 3 / 10;
        int btnW = fwRef * 3 / 16;
        int btnH2 = comboH;
        int gap  = fwRef / 80;

        QString comboSS = QString(
            "QComboBox { background:white; color:black; border:1px solid #888;"
            "  padding: 2px 4px; font-size: %1px; font-weight: bold; }"
            "QComboBox::drop-down { width: %2px; }"
            "QComboBox QAbstractItemView { background:white; color:black;"
            "  selection-background-color:#ccc; min-width: 60px; padding: 4px;"
            "  font-size: %1px; }")
            .arg(fontSize).arg(fontSize);
        m_ftMathOutCombo->setStyleSheet(comboSS);
        m_ftMathIn1Combo->setStyleSheet(comboSS);
        m_ftMathOpCombo->setStyleSheet(comboSS);
        m_ftMathIn2Combo->setStyleSheet(comboSS);
        m_ftMathConjCombo->setStyleSheet(comboSS);

        m_ftMathOutCombo->setFixedSize(bufW, comboH);
        m_ftMathIn1Combo->setFixedSize(bufW, comboH);
        m_ftMathOpCombo->setFixedSize(opW, comboH);
        m_ftMathIn2Combo->setFixedSize(bufW, comboH);
        m_ftMathConjCombo->setFixedSize(conjW, comboH);
        m_ftMathEqualsLabel->setFixedSize(eqW, comboH);
        m_ftMathEqualsLabel->setStyleSheet(
            QString("color: black; font-size: %1px; font-weight: bold;").arg(fontSize * 4 / 3));

        m_ftMathCancelBtn->setFixedSize(btnW, btnH2);
        m_ftMathComputeBtn->setFixedSize(btnW, btnH2);
        QString btnSS = QString(
            "QPushButton { background-color: #888; border: 2px outset #aaa;"
            "  color: #eee; padding: 2px; font-size: %1px; font-weight: bold; }").arg(fontSize);
        m_ftMathCancelBtn->setStyleSheet(btnSS);
        m_ftMathComputeBtn->setStyleSheet(btnSS);

        // Position the equation widgets centered in the frame
        int totalEqW = bufW + gap + eqW + gap + bufW + gap + opW + gap + bufW + gap + conjW;
        int eqX = fx + (fw - totalEqW) / 2;
        int eqY = fy + fh / 2 - comboH / 2;

        // Title
        int titleBottom2;
        {
            int titleFontSize = std::max(14, fontSize * 4 / 3);
            int titleMarginX = std::max(8, fw / 40);
            int titleMarginY = std::max(6, fh / 15);
            QFont tf;
            tf.setPixelSize(titleFontSize);
            tf.setBold(true);
            p.setFont(tf);
            p.setPen(QColor(60, 60, 60));
            QFontMetrics tfm(tf);
            int titleBaseY = fy + titleMarginY + tfm.ascent();
            p.drawText(fx + titleMarginX, titleBaseY, "Fourier calculation");
            titleBottom2 = titleBaseY + tfm.descent();
            m_p2MathHelpRect = drawHelpButton(p, ftMathRect, titleMarginX,
                                              titleMarginY, tfm.height() + 6, 26);
            // As with the panel-1 Math window, this header is drawn here rather
            // than by drawToolHelpHeader, so its "E" button is placed here too.
            m_p2ExerciseRect = drawExerciseButton(p, m_p2MathHelpRect,
                                                  toolExerciseAnchor(true));
        }

        // Draw clean equation text centered between title and combo row
        {
            QString out  = QString(QChar('A' + m_ftMathOutCombo->currentIndex()));
            QString in1  = QString(QChar('A' + m_ftMathIn1Combo->currentIndex()));
            QString op   = m_ftMathOpCombo->currentText();
            QString in2  = QString(QChar('A' + m_ftMathIn2Combo->currentIndex()));
            QString conj = (m_ftMathConjCombo->currentIndex() == 1) ? "*" : "";
            QString eqText = QString("%1 = %2 %3 %4%5")
                .arg(out).arg(in1).arg(op).arg(in2).arg(conj);
            int eqFontSize = std::max(16, fontSize * 3 / 2);
            QFont ef("Palatino");
            ef.setPixelSize(eqFontSize);
            p.setFont(ef);
            p.setPen(QColor(40, 40, 40));
            QFontMetrics efm(ef);
            int eqTextW = efm.horizontalAdvance(eqText);
            int gapCenter = titleBottom2 + (eqY - titleBottom2) / 2;
            int eqTextY2 = gapCenter - efm.height() / 2;
            p.drawText(fx + (fw - eqTextW) / 2, eqTextY2 + efm.ascent(), eqText);
        }

        m_ftMathOutCombo->move(eqX, eqY);      eqX += bufW + gap;
        m_ftMathEqualsLabel->move(eqX, eqY);    eqX += eqW + gap;
        m_ftMathIn1Combo->move(eqX, eqY);       eqX += bufW + gap;
        m_ftMathOpCombo->move(eqX, eqY);        eqX += opW + gap;
        m_ftMathIn2Combo->move(eqX, eqY);       eqX += bufW + gap;
        m_ftMathConjCombo->move(eqX, eqY);

        // Cancel in bottom-left, Compute in bottom-right
        int btnMargin = 8;
        m_ftMathCancelBtn->move(fx + btnMargin, fy + fh - btnH2 - btnMargin);
        m_ftMathComputeBtn->move(fx + fw - btnW - btnMargin, fy + fh - btnH2 - btnMargin);
    }

    // ---- Shift drag overlay (yellow arrow from grab point to cursor) ------------
    // While dragging with the Shift tool the image itself does not move until the
    // button is released, so draw an arrow that reports how far it will travel.
    if (m_p1Dragging && m_shiftActive && !m_image.isNull()) {
        p.setRenderHint(QPainter::Antialiasing, true);
        const QPointF a = m_p1DragStart;
        const QPointF b = m_mousePos;
        const double dx = b.x() - a.x(), dy = b.y() - a.y();
        const double len = std::hypot(dx, dy);
        if (len > 2.0) {
            const QColor yellow(255, 213, 0);

            // Shaft.
            QPen pen(yellow, 2.5);
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            p.drawLine(a, b);

            // Arrowhead at the cursor end.
            const double ang    = std::atan2(dy, dx);
            const double ah     = std::min(18.0, len);
            const double spread = 0.42;
            QPointF h1(b.x() - ah * std::cos(ang - spread),
                       b.y() - ah * std::sin(ang - spread));
            QPointF h2(b.x() - ah * std::cos(ang + spread),
                       b.y() - ah * std::sin(ang + spread));
            QPainterPath head;
            head.moveTo(b); head.lineTo(h1); head.lineTo(h2); head.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(yellow);
            p.drawPath(head);

            // A small dot marks where the drag began.
            p.drawEllipse(a, 3.0, 3.0);

            // Report the displacement in image pixels next to the cursor.
            for (int i = 0; i < m_numDispItems; i++) {
                const DisplayItem &di = m_dispItems[i];
                if (!di.valid || di.zoomIdx != 0 || di.screenRect.width() <= 0) continue;
                QRectF src = m_zoom[0].visibleRect(di.imgW, di.imgH);
                int shiftX = (int)std::lround(dx / (double)di.screenRect.width()  * src.width());
                int shiftY = (int)std::lround(dy / (double)di.screenRect.height() * src.height());
                QString lbl = QString("Δx = %1, Δy = %2 px").arg(shiftX).arg(shiftY);
                QFont lf; lf.setBold(true); lf.setPixelSize(13); p.setFont(lf);
                QFontMetrics lfm(lf);
                int lw = lfm.horizontalAdvance(lbl), lh2 = lfm.height();
                QPointF lp(b.x() + 12, b.y() - 12);
                QRectF pill(lp.x() - 5, lp.y() - lh2 + lfm.descent() - 3, lw + 10, lh2 + 6);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0, 0, 0, 170));
                p.drawRoundedRect(pill, 4, 4);
                p.setPen(yellow);
                p.drawText(lp, lbl);
                break;
            }
        }
    }

    // ---- Rotation drag overlay (red line + triangle + angle text) ---------------
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

            // Compute rotation angle (difference from drag start)
            const QPoint &dragStart = isP1 ? m_p1DragStart : m_p2DragStart;
            double a1 = std::atan2(dragStart.y() - ccy, dragStart.x() - ccx);
            double angle2 = std::atan2(m_mousePos.y() - ccy, m_mousePos.x() - ccx);
            double angleDeg = (angle2 - a1) * 180.0 / M_PI;
            while (angleDeg > 180.0) angleDeg -= 360.0;
            while (angleDeg < -180.0) angleDeg += 360.0;

            // Draw semi-transparent wedge (pie slice with arc) between initial and current direction
            // Positive angle = counter-clockwise = green; Negative angle = clockwise = blue
            {
                double startEx = ccx + radius * std::cos(a1);
                double startEy = ccy + radius * std::sin(a1);

                QColor fillColor = (angleDeg >= 0)
                    ? QColor(100, 255, 140, 80)    // light green, semi-transparent
                    : QColor(100, 180, 255, 80);   // light blue, semi-transparent
                QColor edgeColor = (angleDeg >= 0)
                    ? QColor(60, 200, 100)          // green, opaque
                    : QColor(60, 130, 220);         // blue, opaque

                // Qt arcTo: start angle is CCW from 3-o'clock in degrees,
                // but screen Y is flipped, so negate the math angles.
                // Span sign: positive = CCW in Qt (screen CW in math).
                double qtStart = -a1 * 180.0 / M_PI;
                double qtSpan  = -angleDeg;  // angleDeg is already the wrapped difference

                QPainterPath wedge;
                wedge.moveTo(ccx, ccy);
                wedge.arcTo(ccx - radius, ccy - radius, radius * 2, radius * 2,
                            qtStart, qtSpan);
                wedge.closeSubpath();
                p.setPen(Qt::NoPen);
                p.setBrush(fillColor);
                p.drawPath(wedge);

                // Draw the initial-direction edge as opaque colored line
                p.setPen(QPen(edgeColor, 2));
                p.drawLine(QPointF(ccx, ccy), QPointF(startEx, startEy));
            }

            // Draw red line from center to edge in direction of current mouse
            double ex = ccx + radius * std::cos(angle2);
            double ey = ccy + radius * std::sin(angle2);
            p.setPen(QPen(Qt::red, 2));
            p.drawLine(QPointF(ccx, ccy), QPointF(ex, ey));

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

    // ---- Tool option rectangles (shadow + white background) --------------------
    {
        double sc2 = std::clamp(hy / 800.0, 0.5, 1.0);
        int fs2 = std::max(9, static_cast<int>(11 * sc2));
        int lh = std::max(16, static_cast<int>(26 * sc2));
        int margin = 8;
        QFont sf; sf.setPixelSize(fs2);
        QFontMetrics fm(sf);

        // Panel 2 tool option rectangles (bottom-right of panel 2)
        const QString ftRotateHint = "Use the mouse to rotate the Fourier transform";
        bool p2Tool = p2ToolWindowOpen();
        if (p2Tool) {
            QString toolTitle, toolAnchor;
            toolHelpInfo(true, toolTitle, toolAnchor);
            int nRows = 0;
            int textW = 0;
            if (m_bandpassActive) {
                nRows = 4;
                int r1 = fm.horizontalAdvance("Smooth edge by pixels: ") + m_smoothEdit->width();
                int r2 = fm.horizontalAdvance("Erase pixels outside of band");
                double halfN = m_fftN / 2.0;
                QString diamStr = QString("Inner d=%1  Outer d=%2")
                    .arg(m_bandInnerR * 2.0 * halfN, 0, 'f', 1)
                    .arg(m_bandOuterR * 2.0 * halfN, 0, 'f', 1);
                int r3 = fm.horizontalAdvance(diamStr);
                int r4 = m_resetBandBtn->width() + 12 + m_applyBandBtn->width();
                textW = std::max({r1, r2, r3, r4});
            } else if (m_directionalActive) {
                nRows = 4;
                int r1 = fm.horizontalAdvance("Smooth edge by pixels: ") + m_smoothEdit->width();
                int r2 = fm.horizontalAdvance("Erase pixels outside of band");
                QString angleStr = QString("Angle 1=%1\u00B0  Angle 2=%2\u00B0")
                    .arg(m_dirAngle1, 0, 'f', 1)
                    .arg(m_dirAngle2, 0, 'f', 1);
                int r3 = fm.horizontalAdvance(angleStr);
                int r4 = m_applyBandBtn->width();
                textW = std::max({r1, r2, r3, r4});
            } else if (m_brushActive) {
                nRows = 2;
                int r1 = fm.horizontalAdvance("Pixel value to enter: ") + m_brushValueEdit->width();
                int r2 = fm.horizontalAdvance("Paint brush Gaussian diameter: ") + m_brushDiameterEdit->width();
                textW = std::max(r1, r2);
            } else if (m_eraserActive) {
                nRows = 1;
                textW = fm.horizontalAdvance("Eraser Gaussian diameter: ") + m_eraserDiameterEdit->width();
            } else if (m_lineFilterActive) {
                nRows = 5;
                int r1 = fm.horizontalAdvance("Width of line: ") + m_lineWidthEdit->width();
                int r2 = fm.horizontalAdvance("Direction of the line: ") + m_lineDirectionEdit->width();
                int rO = fm.horizontalAdvance("Offset of the line: ") + m_lineOffsetEdit->width();
                int r3 = m_lineEraseOutsideBtn->width();
                int r4 = m_applyLineBtn->width();
                textW = std::max({r1, r2, rO, r3, r4});
            } else if (m_latticeActive) {
                nRows = 5;
                int r1 = fm.horizontalAdvance("Smooth edge by pixels: ") + m_latticeSmoothEdit->width();
                int r2 = fm.horizontalAdvance("Diameter of dots: ") + m_latticeDotDiamEdit->width();
                int r3 = fm.horizontalAdvance("Erase pixels outside of lattice");
                int sepW = fm.horizontalAdvance(",  ");
                int gapW = fm.horizontalAdvance("   ");
                int rU = fm.horizontalAdvance("u = ( ") + m_latticeUxEdit->width()
                         + sepW + m_latticeUyEdit->width() + fm.horizontalAdvance(" )");
                int rV = fm.horizontalAdvance("v = ( ") + m_latticeVxEdit->width()
                         + sepW + m_latticeVyEdit->width() + fm.horizontalAdvance(" )");
                int r4 = rU + gapW + rV;
                int r5 = m_latticeApplyBtn->width();
                textW = std::max({r1, r2, r3, r4, r5});
            } else if (m_crossSectionActive) {
                nRows = 2;
                int r1 = fm.horizontalAdvance("Direction of evaluation line [°]: ") + m_crossSectionDirEdit->width();
                int r2 = fm.horizontalAdvance("Integration width [reciprocal pixels]: ") + m_crossSectionWidthEdit->width();
                textW = std::max(r1, r2);
            } else if (m_p2SymmetrizeActive) {
                nRows = 2;
                int r1 = fm.horizontalAdvance("Symmetry to apply: ") + m_p2SymmetryEdit->width();
                int r2 = m_applyP2SymmetryBtn->width();
                textW = std::max(r1, r2);
            } else if (m_ftCropActive) {
                nRows = 3;
                int r1 = m_ftCropCombo->width();
                int r2 = fm.horizontalAdvance("Keep original size");
                int r3 = m_applyFtCropBtn->width() + 12 + m_applyFtPadBtn->width();
                textW = std::max({r1, r2, r3});
            } else if (m_ctfActive) {
                nRows = 7;
                int r0 = fm.horizontalAdvance("Acceleration Voltage (kV): ") + m_ctfVoltageEdit->width()
                         + 12 + fm.horizontalAdvance("Energy spread (eV): ")
                         + m_ctfEnergySpreadEdit->width();
                int r1 = fm.horizontalAdvance("Spherical aberration Cs (mm): ") + m_ctfCsEdit->width()
                         + 12 + fm.horizontalAdvance("Open angle gun (mrad): ")
                         + m_ctfOpenAngleEdit->width();
                int r2 = fm.horizontalAdvance("Defocus (nm): ") + m_ctfDefocusEdit->width()
                         + 12 + fm.horizontalAdvance("Defocus spread (nm): ")
                         + m_ctfDefocusSpreadEdit->width();
                int r3 = fm.horizontalAdvance("Astigmatism (nm): ") + m_ctfAstigEdit->width()
                         + 12 + fm.horizontalAdvance("Astigmatism direction (\u00B0): ")
                         + m_ctfAstigAngleEdit->width();
                int r4 = fm.horizontalAdvance("Amplitude contrast (%): ") + m_ctfAmpContrastEdit->width();
                int r5 = fm.horizontalAdvance("Beamtilt (mrad): ") + m_ctfBeamtiltEdit->width()
                         + 12 + fm.horizontalAdvance("Direction (\u00B0): ")
                         + m_ctfBeamtiltDirEdit->width();
                int r6 = m_ctfCancelBtn->width() + 8 + m_ctfPupilBtn->width()
                         + 8 + m_ctfComplexBtn->width() + 8 + m_ctfRealBtn->width();
                textW = std::max({r0, r1, r2, r3, r4, r5, r6});
            } else if (m_ctfFitActive) {
                nRows = m_ctfFitHasResult ? 8 : 6;
                int r0 = fm.horizontalAdvance("Acceleration Voltage (kV): ") + m_ctfFitVoltageEdit->width();
                int r1 = fm.horizontalAdvance("Spherical aberration Cs (mm): ") + m_ctfFitCsEdit->width();
                int r2 = fm.horizontalAdvance("Input buffer: ") + m_ctfFitInputCombo->width();
                int r3 = fm.horizontalAdvance("Upper resolution limit (Å): ") + m_ctfFitResHiEdit->width();
                int r4 = fm.horizontalAdvance("Lower resolution limit (Å): ") + m_ctfFitResLoEdit->width();
                int r5 = m_ctfFitCancelBtn->width() + 8 + m_ctfFitExecuteBtn->width();
                int r6 = 0, r7 = 0;
                if (m_ctfFitHasResult) {
                    r6 = fm.horizontalAdvance(QString("Fitted defocus: %1 nm")
                             .arg(m_ctfFitResDefocusNM, 0, 'f', 1));
                    r7 = fm.horizontalAdvance(QString("Fitted astigmatism: %1 nm at %2°")
                             .arg(m_ctfFitResAstigNM, 0, 'f', 1)
                             .arg(m_ctfFitResAngleDeg, 0, 'f', 1));
                }
                textW = std::max({r0, r1, r2, r3, r4, r5, r6, r7});
            } else if (m_phaseRampActive) {
                nRows = 4;
                int r0 = fm.horizontalAdvance("Size of FFT to be created: ") + m_phaseRampSizeCombo->width();
                int r1 = fm.horizontalAdvance("Direction of phase ramp (\u00B0): ") + m_phaseRampDirEdit->width();
                int r2 = fm.horizontalAdvance("Phase step of first pixel (\u00B0): ") + m_phaseRampStepEdit->width();
                int r3 = m_phaseRampCancelBtn->width() + 12 + m_phaseRampComputeBtn->width();
                textW = std::max({r0, r1, r2, r3});
            } else if (m_ftRotateActive) {
                nRows = 2;
                textW = std::max(fm.horizontalAdvance(ftRotateHint), m_ftRotateCancelBtn->width());
            }

            // Reserve the header row (function name + "?" help button) on top.
            {
                QFont tf = sf; tf.setBold(true);
                nRows += 1;
                textW = std::max(textW,
                                 QFontMetrics(tf).horizontalAdvance(toolTitle) + 12 + 20);
            }

            int rw = textW * 6 / 5 + 2 * margin;
            int rh = nRows * lh + 2 * margin;
            int rx = width() - rw - margin;
            int ry = hy - rh - margin;
            QRect toolRect(rx, ry, rw, rh);
            m_p2ToolRect = toolRect;   // recorded so mouse handling can detect overlap
            drawShadowRect(p, toolRect);

            // Blue progress fill for apply operations
            if (m_toolProgress >= 0.0 && m_toolProgress <= 1.0) {
                int progW = static_cast<int>(rw * m_toolProgress);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(180, 210, 255));
                p.drawRect(rx + 1, ry + 1, progW, rh - 2);
            }

            m_p2HelpRect = drawToolHelpHeader(p, toolRect, margin, lh, fs2, toolTitle);
            m_p2ExerciseRect = drawExerciseButton(p, m_p2HelpRect, toolExerciseAnchor(true));

            // Draw painted labels inside the rectangle
            p.setFont(sf);
            p.setPen(QColor(60, 60, 60));
            int tx = rx + margin;
            int ty = ry + margin + lh;   // below the header row

            if (m_bandpassActive) {
                drawParamLabel(p, fm, tx, ty, "Smooth edge by pixels:", m_smoothEdit->toolTip());
                m_smoothEdit->move(tx + fm.horizontalAdvance("Smooth edge by pixels: "), ty);
                m_bandEraseOutside->move(tx, ty + lh);
                double halfN = m_fftN / 2.0;
                QString diamStr = QString("Inner d=%1  Outer d=%2")
                    .arg(m_bandInnerR * 2.0 * halfN, 0, 'f', 1)
                    .arg(m_bandOuterR * 2.0 * halfN, 0, 'f', 1);
                drawParamLabel(p, fm, tx, ty + lh * 2, diamStr,
                    "Current bandpass ring diameters, measured in Fourier pixels.\n"
                    "Inner d is the diameter of the inner edge (small numbers = low\n"
                    "frequencies), outer d is the diameter of the outer edge (large\n"
                    "numbers = high frequencies). Drag the ring handles in panel 2\n"
                    "to change these values.");
                int rowRight = rx + rw - margin;
                m_resetBandBtn->move(tx, ty + lh * 3);
                m_applyBandBtn->move(rowRight - m_applyBandBtn->width(), ty + lh * 3);
            } else if (m_directionalActive) {
                drawParamLabel(p, fm, tx, ty, "Smooth edge by pixels:", m_smoothEdit->toolTip());
                m_smoothEdit->move(tx + fm.horizontalAdvance("Smooth edge by pixels: "), ty);
                m_bandEraseOutside->move(tx, ty + lh);
                QString angleStr = QString("Angle 1=%1\u00B0  Angle 2=%2\u00B0")
                    .arg(m_dirAngle1, 0, 'f', 1)
                    .arg(m_dirAngle2, 0, 'f', 1);
                drawParamLabel(p, fm, tx, ty + lh * 2, angleStr,
                    "Current angular limits of the directional wedge, in degrees.\n"
                    "Angle 1 and Angle 2 are the two boundary directions that\n"
                    "define the kept (or erased) Fourier sector. Drag the wedge\n"
                    "edges in panel 2 to change these values.");
                m_applyBandBtn->move(tx, ty + lh * 3);
            } else if (m_brushActive) {
                drawParamLabel(p, fm, tx, ty, "Pixel value to enter:", m_brushValueEdit->toolTip());
                m_brushValueEdit->move(tx + fm.horizontalAdvance("Pixel value to enter: "), ty);
                drawParamLabel(p, fm, tx, ty + lh, "Paint brush Gaussian diameter:", m_brushDiameterEdit->toolTip());
                m_brushDiameterEdit->move(tx + fm.horizontalAdvance("Paint brush Gaussian diameter: "), ty + lh);
            } else if (m_eraserActive) {
                drawParamLabel(p, fm, tx, ty, "Eraser Gaussian diameter:", m_eraserDiameterEdit->toolTip());
                m_eraserDiameterEdit->move(tx + fm.horizontalAdvance("Eraser Gaussian diameter: "), ty);
            } else if (m_lineFilterActive) {
                drawParamLabel(p, fm, tx, ty, "Width of line:", m_lineWidthEdit->toolTip());
                m_lineWidthEdit->move(tx + fm.horizontalAdvance("Width of line: "), ty);
                drawParamLabel(p, fm, tx, ty + lh, "Direction of the line:", m_lineDirectionEdit->toolTip());
                m_lineDirectionEdit->move(tx + fm.horizontalAdvance("Direction of the line: "), ty + lh);
                drawParamLabel(p, fm, tx, ty + lh * 2, "Offset of the line:", m_lineOffsetEdit->toolTip());
                m_lineOffsetEdit->move(tx + fm.horizontalAdvance("Offset of the line: "), ty + lh * 2);
                m_lineEraseOutsideBtn->move(tx, ty + lh * 3);
                m_applyLineBtn->move(tx, ty + lh * 4);
            } else if (m_latticeActive) {
                drawParamLabel(p, fm, tx, ty, "Smooth edge by pixels:", m_latticeSmoothEdit->toolTip());
                m_latticeSmoothEdit->move(tx + fm.horizontalAdvance("Smooth edge by pixels: "), ty);
                drawParamLabel(p, fm, tx, ty + lh, "Diameter of dots:", m_latticeDotDiamEdit->toolTip());
                m_latticeDotDiamEdit->move(tx + fm.horizontalAdvance("Diameter of dots: "), ty + lh);
                m_latticeEraseOutside->move(tx, ty + lh * 2);

                // Vector-edit row: u = ( Ux , Uy )   v = ( Vx , Vy )
                int vy = ty + lh * 3;
                int ex = tx;
                int editYOffset = (lh - m_latticeUxEdit->height()) / 2;
                // Align labels to the widgets' actual top so their baselines
                // match the text inside the vertically-centred edit boxes.
                int labelY = vy + editYOffset;

                drawParamLabel(p, fm, ex, labelY, "u = (", m_latticeUxEdit->toolTip());
                ex += fm.horizontalAdvance("u = ( ");
                m_latticeUxEdit->move(ex, vy + editYOffset);
                ex += m_latticeUxEdit->width();
                drawParamLabel(p, fm, ex, labelY, ", ", m_latticeUxEdit->toolTip());
                ex += fm.horizontalAdvance(", ");
                m_latticeUyEdit->move(ex, vy + editYOffset);
                ex += m_latticeUyEdit->width();
                drawParamLabel(p, fm, ex, labelY, " )", m_latticeUxEdit->toolTip());
                ex += fm.horizontalAdvance(" )") + fm.horizontalAdvance("   ");

                drawParamLabel(p, fm, ex, labelY, "v = (", m_latticeVxEdit->toolTip());
                ex += fm.horizontalAdvance("v = ( ");
                m_latticeVxEdit->move(ex, vy + editYOffset);
                ex += m_latticeVxEdit->width();
                drawParamLabel(p, fm, ex, labelY, ", ", m_latticeVxEdit->toolTip());
                ex += fm.horizontalAdvance(", ");
                m_latticeVyEdit->move(ex, vy + editYOffset);
                ex += m_latticeVyEdit->width();
                drawParamLabel(p, fm, ex, labelY, " )", m_latticeVxEdit->toolTip());

                m_latticeApplyBtn->move(tx, ty + lh * 4);
            } else if (m_crossSectionActive) {
                drawParamLabel(p, fm, tx, ty, "Direction of evaluation line [°]:",
                               m_crossSectionDirEdit->toolTip());
                m_crossSectionDirEdit->move(tx + fm.horizontalAdvance("Direction of evaluation line [°]: "), ty);
                drawParamLabel(p, fm, tx, ty + lh, "Integration width [reciprocal pixels]:",
                               m_crossSectionWidthEdit->toolTip());
                m_crossSectionWidthEdit->move(tx + fm.horizontalAdvance("Integration width [reciprocal pixels]: "), ty + lh);
            } else if (m_p2SymmetrizeActive) {
                drawParamLabel(p, fm, tx, ty, "Symmetry to apply:", m_p2SymmetryEdit->toolTip());
                m_p2SymmetryEdit->move(tx + fm.horizontalAdvance("Symmetry to apply: "), ty);
                m_applyP2SymmetryBtn->move(tx, ty + lh);
            } else if (m_ftCropActive) {
                m_ftCropCombo->move(tx, ty);
                m_ftCropKeepSizeBtn->move(tx, ty + lh);
                // Fourier crop left-aligned, Fourier pad right-aligned
                int rowRight = rx + rw - margin;
                m_applyFtCropBtn->move(tx, ty + lh * 2);
                m_applyFtPadBtn->move(rowRight - m_applyFtPadBtn->width(),
                                      ty + lh * 2);
            } else if (m_ctfActive) {
                // Row 0: Voltage, Energy spread
                drawParamLabel(p, fm, tx, ty, "Acceleration Voltage (kV):", m_ctfVoltageEdit->toolTip());
                int avX = tx + fm.horizontalAdvance("Acceleration Voltage (kV): ");
                m_ctfVoltageEdit->move(avX, ty);
                int esLblX = avX + m_ctfVoltageEdit->width() + 12;
                drawParamLabel(p, fm, esLblX, ty, "Energy spread (eV):", m_ctfEnergySpreadEdit->toolTip());
                m_ctfEnergySpreadEdit->move(esLblX + fm.horizontalAdvance("Energy spread (eV): "), ty);
                // Row 1: Cs, Open angle gun
                drawParamLabel(p, fm, tx, ty + lh, "Spherical aberration Cs (mm):", m_ctfCsEdit->toolTip());
                int csX = tx + fm.horizontalAdvance("Spherical aberration Cs (mm): ");
                m_ctfCsEdit->move(csX, ty + lh);
                int oaLblX = csX + m_ctfCsEdit->width() + 12;
                drawParamLabel(p, fm, oaLblX, ty + lh, "Open angle gun (mrad):", m_ctfOpenAngleEdit->toolTip());
                m_ctfOpenAngleEdit->move(oaLblX + fm.horizontalAdvance("Open angle gun (mrad): "), ty + lh);
                // Row 2: Defocus, Defocus spread
                drawParamLabel(p, fm, tx, ty + lh * 2, "Defocus (nm):", m_ctfDefocusEdit->toolTip());
                int dfX = tx + fm.horizontalAdvance("Defocus (nm): ");
                m_ctfDefocusEdit->move(dfX, ty + lh * 2);
                int dsLblX = dfX + m_ctfDefocusEdit->width() + 12;
                drawParamLabel(p, fm, dsLblX, ty + lh * 2, "Defocus spread (nm):", m_ctfDefocusSpreadEdit->toolTip());
                m_ctfDefocusSpreadEdit->move(dsLblX + fm.horizontalAdvance("Defocus spread (nm): "), ty + lh * 2);
                // Row 3: Astigmatism, Astigmatism direction
                drawParamLabel(p, fm, tx, ty + lh * 3, "Astigmatism (nm):", m_ctfAstigEdit->toolTip());
                int ax1 = tx + fm.horizontalAdvance("Astigmatism (nm): ");
                m_ctfAstigEdit->move(ax1, ty + lh * 3);
                int ax2 = ax1 + m_ctfAstigEdit->width() + 12;
                drawParamLabel(p, fm, ax2, ty + lh * 3, "Astigmatism direction (\u00B0):", m_ctfAstigAngleEdit->toolTip());
                m_ctfAstigAngleEdit->move(ax2 + fm.horizontalAdvance("Astigmatism direction (\u00B0): "), ty + lh * 3);
                // Row 4: Amplitude contrast
                drawParamLabel(p, fm, tx, ty + lh * 4, "Amplitude contrast (%):", m_ctfAmpContrastEdit->toolTip());
                m_ctfAmpContrastEdit->move(tx + fm.horizontalAdvance("Amplitude contrast (%): "), ty + lh * 4);
                // Row 5: Beamtilt, Direction
                drawParamLabel(p, fm, tx, ty + lh * 5, "Beamtilt (mrad):", m_ctfBeamtiltEdit->toolTip());
                int btX = tx + fm.horizontalAdvance("Beamtilt (mrad): ");
                m_ctfBeamtiltEdit->move(btX, ty + lh * 5);
                int btdLblX = btX + m_ctfBeamtiltEdit->width() + 12;
                drawParamLabel(p, fm, btdLblX, ty + lh * 5, "Direction (\u00B0):", m_ctfBeamtiltDirEdit->toolTip());
                m_ctfBeamtiltDirEdit->move(btdLblX + fm.horizontalAdvance("Direction (\u00B0): "), ty + lh * 5);
                // Row 6: Cancel, then the three model buttons packed to the right
                // in the order they are explained in their tooltips.
                m_ctfCancelBtn->move(tx, ty + lh * 6);
                int bRight = rx + rw - margin;
                m_ctfRealBtn->move(bRight - m_ctfRealBtn->width(), ty + lh * 6);
                bRight -= m_ctfRealBtn->width() + 8;
                m_ctfComplexBtn->move(bRight - m_ctfComplexBtn->width(), ty + lh * 6);
                bRight -= m_ctfComplexBtn->width() + 8;
                m_ctfPupilBtn->move(bRight - m_ctfPupilBtn->width(), ty + lh * 6);
            } else if (m_ctfFitActive) {
                // Row 0: Acceleration voltage
                drawParamLabel(p, fm, tx, ty, "Acceleration Voltage (kV):", m_ctfFitVoltageEdit->toolTip());
                m_ctfFitVoltageEdit->move(tx + fm.horizontalAdvance("Acceleration Voltage (kV): "), ty);
                // Row 1: Spherical aberration Cs
                drawParamLabel(p, fm, tx, ty + lh, "Spherical aberration Cs (mm):", m_ctfFitCsEdit->toolTip());
                m_ctfFitCsEdit->move(tx + fm.horizontalAdvance("Spherical aberration Cs (mm): "), ty + lh);
                // Row 2: Input buffer
                drawParamLabel(p, fm, tx, ty + lh * 2, "Input buffer:", m_ctfFitInputCombo->toolTip());
                m_ctfFitInputCombo->move(tx + fm.horizontalAdvance("Input buffer: "), ty + lh * 2);
                // Row 3: Upper resolution limit
                drawParamLabel(p, fm, tx, ty + lh * 3, "Upper resolution limit (Å):", m_ctfFitResHiEdit->toolTip());
                m_ctfFitResHiEdit->move(tx + fm.horizontalAdvance("Upper resolution limit (Å): "), ty + lh * 3);
                // Row 4: Lower resolution limit
                drawParamLabel(p, fm, tx, ty + lh * 4, "Lower resolution limit (Å):", m_ctfFitResLoEdit->toolTip());
                m_ctfFitResLoEdit->move(tx + fm.horizontalAdvance("Lower resolution limit (Å): "), ty + lh * 4);
                // Row 5: Cancel / Execute
                m_ctfFitCancelBtn->move(tx, ty + lh * 5);
                m_ctfFitExecuteBtn->move(rx + rw - margin - m_ctfFitExecuteBtn->width(), ty + lh * 5);
                // Rows 6-7: fitted results (after a successful Execute)
                if (m_ctfFitHasResult) {
                    drawParamLabel(p, fm, tx, ty + lh * 6,
                        QString("Fitted defocus: %1 nm").arg(m_ctfFitResDefocusNM, 0, 'f', 1),
                        QString());
                    drawParamLabel(p, fm, tx, ty + lh * 7,
                        QString("Fitted astigmatism: %1 nm at %2°")
                            .arg(m_ctfFitResAstigNM, 0, 'f', 1)
                            .arg(m_ctfFitResAngleDeg, 0, 'f', 1),
                        QString());
                }
            } else if (m_phaseRampActive) {
                drawParamLabel(p, fm, tx, ty, "Size of FFT to be created:", m_phaseRampSizeCombo->toolTip());
                m_phaseRampSizeCombo->move(tx + fm.horizontalAdvance("Size of FFT to be created: "), ty);
                drawParamLabel(p, fm, tx, ty + lh, "Direction of phase ramp (°):", m_phaseRampDirEdit->toolTip());
                m_phaseRampDirEdit->move(tx + fm.horizontalAdvance("Direction of phase ramp (°): "), ty + lh);
                drawParamLabel(p, fm, tx, ty + lh * 2, "Phase step of first pixel (°):", m_phaseRampStepEdit->toolTip());
                m_phaseRampStepEdit->move(tx + fm.horizontalAdvance("Phase step of first pixel (°): "), ty + lh * 2);
                m_phaseRampCancelBtn->move(tx, ty + lh * 3);
                m_phaseRampComputeBtn->move(rx + rw - margin - m_phaseRampComputeBtn->width(), ty + lh * 3);
            } else if (m_ftRotateActive) {
                p.drawText(tx, ty + fm.ascent(), ftRotateHint);
                m_ftRotateCancelBtn->move(tx, ty + lh);
            }
        }

        // Panel 1 tool option rectangles (bottom-left of panel 1)
        bool p1Tool = p1ToolWindowOpen();
        const QString shiftHint  = "Use the mouse to shift the image";
        const QString rotateHint = "Use the mouse to rotate the image";
        // Spells out what "Resize image" is about to do, so the user can see
        // whether the chosen target pads or crops before committing to it.
        auto padSizeHint = [this]() -> QString {
            if (m_image.isNull()) return QStringLiteral("No image loaded");
            int cur = std::max(m_image.width(), m_image.height());
            int tgt = padTargetSize();
            if (tgt < 1)
                return QString("Enter a size from 1 to %1").arg(kMaxPadSize);
            if (tgt > cur)
                return QString("Pad %1 → %2 px, grey border").arg(cur).arg(tgt);
            if (tgt < cur)
                return QString("Crop %1 → %2 px, centred").arg(cur).arg(tgt);
            return QString("Image is already %1 px").arg(cur);
        };
        if (p1Tool) {
            QString toolTitle, toolAnchor;
            toolHelpInfo(false, toolTitle, toolAnchor);
            int nRows = 0;
            int textW = 0;
            if (m_p1EraserActive) {
                nRows = 1;
                textW = fm.horizontalAdvance("Eraser Gaussian diameter: ") + m_p1EraserDiameterEdit->width();
            } else if (m_p1BrushActive) {
                nRows = 3;
                int r1 = fm.horizontalAdvance("Pixel value to enter: ") + m_p1BrushValueEdit->width();
                int r2 = fm.horizontalAdvance("Paint brush solid diameter: ") + m_p1BrushSolidDiameterEdit->width();
                int r3 = fm.horizontalAdvance("Paint brush Gaussian diameter: ") + m_p1BrushDiameterEdit->width();
                textW = std::max({r1, r2, r3});
            } else if (m_p1TaperActive) {
                nRows = 2;
                int r1 = fm.horizontalAdvance("Hanning width: ") + m_p1TaperWidthEdit->width();
                int r2 = m_applyP1TaperBtn->width();
                textW = std::max(r1, r2);
            } else if (m_p1SymmetrizeActive) {
                nRows = 2;
                int r1 = fm.horizontalAdvance("Symmetry to apply: ") + m_p1SymmetryEdit->width();
                int r2 = m_applyP1SymmetryBtn->width();
                textW = std::max(r1, r2);
            } else if (m_binActive) {
                nRows = 3;
                int r1 = m_binCombo->width();
                int r2 = fm.horizontalAdvance("Keep original image size");
                int r3 = m_applyBinBtn->width();
                textW = std::max({r1, r2, r3});
            } else if (m_copyActive) {
                nRows = 3;
                int labW = fm.horizontalAdvance("Source buffer: ");
                int r0 = labW + m_copySrcCombo->width();
                int r1 = m_copyCancelBtn->width() + 8 + m_copyDuplicateBtn->width();
                textW = std::max(r0, r1);
            } else if (m_averageActive) {
                // header + instruction + two toggle rows + target row + button row
                // (+ an optional result line).
                nRows = m_averageResult.isEmpty() ? 5 : 6;
                int tbs = std::max(16, lh - 4), tg = 4;
                int gridW  = 8 * tbs + 7 * tg;                 // 8 toggles per row
                int chooseW = fm.horizontalAdvance("Choose which images to include in the average");
                int tgtW   = fm.horizontalAdvance("Target buffer: ") + m_averageTargetCombo->width();
                int btnW   = m_averageCancelBtn->width() + 8 + m_averageComputeBtn->width();
                int resW   = m_averageResult.isEmpty() ? 0 : fm.horizontalAdvance(m_averageResult);
                textW = std::max({ gridW, chooseW, tgtW, btnW, resW });
            } else if (m_padActive) {
                nRows = 3;
                int r0 = fm.horizontalAdvance("Target size (pixels): ")
                         + m_padSizeCombo->width() + 8 + m_padCustomEdit->width();
                int r1 = fm.horizontalAdvance(padSizeHint());
                int r2 = m_padCancelBtn->width() + 8 + m_applyPadBtn->width();
                textW = std::max({r0, r1, r2});
            } else if (m_cropActive) {
                nRows = 3;
                int labW = std::max(fm.horizontalAdvance("Top left:  "),
                                    fm.horizontalAdvance("Bottom right:  "));
                int r0 = labW + m_cropTLxEdit->width() + 8 + m_cropTLyEdit->width();
                int r1 = labW + m_cropBRxEdit->width() + 8 + m_cropBRyEdit->width();
                int r2 = m_cropCancelBtn->width() + 8 + m_applyCropBtn->width();
                textW = std::max({r0, r1, r2});
            } else if (m_peakPickActive) {
                nRows = 5;
                // Determine threshold range from selected source buffer
                int srcIdx = m_peakSourceCombo->currentIndex();
                double srcMin = m_imageMinVal, srcMax = m_imageMaxVal;
                if (srcIdx >= 0 && srcIdx < HISTORY_SLOTS && m_history[srcIdx].occupied) {
                    srcMin = m_history[srcIdx].minVal;
                    srcMax = m_history[srcIdx].maxVal;
                }
                double threshVal = srcMin + (srcMax - srcMin)
                                   * m_peakThresholdSlider->value() / 1000.0;
                QString threshStr = QString("Threshold: %1").arg(threshVal, 0, 'g', 5);
                int r0 = fm.horizontalAdvance("Picking source map: ") + m_peakSourceCombo->width()
                         + 8 + m_peakShowPosBtn->width();
                int r1 = fm.horizontalAdvance(threshStr + "  ") + m_peakThresholdSlider->width();
                QString exclStr = QString("Exclusion radius: %1 ").arg(m_peakExclRadiusSlider->value());
                int r2 = fm.horizontalAdvance(exclStr) + m_peakExclRadiusSlider->width();
                QString peakStr = QString("Peaks found: %1").arg(m_peaks.size());
                int r3 = fm.horizontalAdvance(peakStr);
                int r4 = m_peakCancelBtn->width() + 8 + m_peakComputeBtn->width();
                textW = std::max({r0, r1, r2, r3, r4});
            } else if (m_extractActive) {
                if (m_peaks.empty()) {
                    nRows = 1;
                    textW = fm.horizontalAdvance("First prepare a particle position list");
                } else {
                    nRows = 4;
                    int r0 = fm.horizontalAdvance("Source image: ") + m_extractSourceCombo->width();
                    int r1 = fm.horizontalAdvance("Target image: ") + m_extractTargetCombo->width();
                    int r2 = fm.horizontalAdvance("Particle size: ") + m_extractSizeCombo->width();
                    int r3 = m_extractCancelBtn->width() + 8 + m_extractComputeBtn->width();
                    textW = std::max({r0, r1, r2, r3});
                }
            } else if (m_alignActive) {
                // Three selectors laid out side by side: a header row, the
                // pulldowns beneath, then the button row (and an optional result).
                nRows = m_alignResult.isEmpty() ? 3 : 4;
                const int comboW = m_alignSrcCombo->width();
                const int colGap = 14;
                int c0 = std::max(fm.horizontalAdvance("Image source"), comboW);
                int c1 = std::max(fm.horizontalAdvance("Alignment reference"), comboW);
                int c2 = std::max(fm.horizontalAdvance("Output buffer"), comboW);
                int rCols = c0 + c1 + c2 + 2 * colGap;
                int rBtns = m_alignCancelBtn->width() + 8 + m_alignShiftBtn->width()
                            + 8 + m_alignRotBtn->width();
                int rRes  = m_alignResult.isEmpty() ? 0 : fm.horizontalAdvance(m_alignResult);
                textW = std::max({rCols, rBtns, rRes});
            } else if (m_gaborActive) {
                nRows = 5;
                int r0 = fm.horizontalAdvance("Sigma (envelope): ")     + m_gaborSigmaEdit->width();
                int r1 = fm.horizontalAdvance("Wavelength lambda: ")    + m_gaborLambdaEdit->width();
                int r2 = fm.horizontalAdvance("Orientation (deg): ")    + m_gaborThetaEdit->width();
                int r3 = fm.horizontalAdvance("Aspect ratio gamma: ")   + m_gaborGammaEdit->width();
                int r4 = m_gaborCancelBtn->width() + 8 + m_gaborComputeBtn->width();
                textW = std::max({r0, r1, r2, r3, r4});
            } else if (m_hessianActive) {
                nRows = 3;
                int r0 = fm.horizontalAdvance("Sigma (smoothing): ")       + m_hessianSigmaEdit->width();
                int r1 = fm.horizontalAdvance("Polarity (+1/-1): ")        + m_hessianPolarityEdit->width();
                int r2 = m_hessianCancelBtn->width() + 8 + m_hessianComputeBtn->width();
                textW = std::max({r0, r1, r2});
            } else if (m_measureActive) {
                nRows = 3;
                double dx = m_measureP1.x() - m_measureP0.x();
                double dy = m_measureP1.y() - m_measureP0.y();
                double pix = m_measureHasLine ? std::sqrt(dx * dx + dy * dy) : 0.0;
                double ps  = (m_pixelSize > 0.0) ? m_pixelSize : 1.0;
                QString pixStr    = QString("Pixels: %1").arg(pix, 0, 'f', 2);
                QString lenStr    = QString("Length: %1 \u00C5").arg(pix * ps, 0, 'f', 2);
                int r0 = fm.horizontalAdvance(pixStr);
                int r1 = fm.horizontalAdvance(lenStr);
                int r2 = m_measureCancelBtn->width();
                textW = std::max({r0, r1, r2,
                                  fm.horizontalAdvance("Click two points on the image")});
            } else if (m_shiftActive) {
                nRows = 2;
                textW = std::max(fm.horizontalAdvance(shiftHint), m_shiftCancelBtn->width());
            } else if (m_rotateActive) {
                nRows = 2;
                textW = std::max(fm.horizontalAdvance(rotateHint), m_rotateCancelBtn->width());
            } else if (m_amyloidActive) {
                nRows = 8;
                const int colGap = 20;
                int leftSize  = fm.horizontalAdvance("Image size (px): ")   + m_amyloidSizeCombo->width();
                int leftRise  = fm.horizontalAdvance("Helical rise (\u00C5): ") + m_amyloidRiseEdit->width();
                int leftWave  = fm.horizontalAdvance("Waviness wavelength (px): ") + m_amyloidWaveEdit->width();
                int leftCol   = std::max({leftSize, leftRise, leftWave}) + colGap;
                int rRow0 = leftCol + fm.horizontalAdvance("Source map: ")       + m_amyloidMapCombo->width();
                int rRow1 = leftCol + fm.horizontalAdvance("Helical twist (\u00B0): ") + m_amyloidTwistEdit->width();
                int rRow2 = leftCol + fm.horizontalAdvance("Waviness amplitude (px): ") + m_amyloidAmplEdit->width();
                int rPer  = fm.horizontalAdvance("Persistence length (\u00B5m): ")      + m_amyloidPersistEdit->width();
                int rNoise = m_amyloidNoiseBtn->sizeHint().width() + 8 + fm.horizontalAdvance("Sigma: ") + m_amyloidNoiseEdit->width();
                int rSig   = m_amyloidSignalBtn->width();
                QString infoStr = QString("Filaments: %1  Click image to place start & end")
                                      .arg(m_amyloidFilaments.size());
                int rInfo = fm.horizontalAdvance(infoStr);
                int rBtns = m_amyloidCancelBtn->width() + 8 + m_amyloidComputeBtn->width();
                textW = std::max({rRow0, rRow1, rRow2, rPer, rNoise, rSig, rInfo, rBtns});
            } else if (m_p1FlipHActive || m_p1FlipVActive || m_p1InvertActive) {
                // Applied immediately on click and take no parameters, so the
                // window carries nothing but the header row added below.
                nRows = 0;
            }

            // Reserve the header row (function name + "?" help button) on top.
            {
                QFont tf = sf; tf.setBold(true);
                nRows += 1;
                textW = std::max(textW,
                                 QFontMetrics(tf).horizontalAdvance(toolTitle) + 12 + 20);
            }

            int rw = textW * 6 / 5 + 2 * margin;
            int rh = nRows * lh + 2 * margin;
            int rx = margin;
            int ry = hy - rh - margin;
            QRect toolRect(rx, ry, rw, rh);
            m_p1ToolRect = toolRect;
            drawShadowRect(p, toolRect);

            // Blue progress fill for apply operations
            if (m_toolProgress >= 0.0 && m_toolProgress <= 1.0) {
                int progW = static_cast<int>(rw * m_toolProgress);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(180, 210, 255));
                p.drawRect(rx + 1, ry + 1, progW, rh - 2);
            }

            m_p1HelpRect = drawToolHelpHeader(p, toolRect, margin, lh, fs2, toolTitle);
            m_p1ExerciseRect = drawExerciseButton(p, m_p1HelpRect, toolExerciseAnchor(false));

            p.setFont(sf);
            p.setPen(QColor(60, 60, 60));
            int tx = rx + margin;
            int ty = ry + margin + lh;   // below the header row

            if (m_p1EraserActive) {
                drawParamLabel(p, fm, tx, ty, "Eraser Gaussian diameter:", m_p1EraserDiameterEdit->toolTip());
                m_p1EraserDiameterEdit->move(tx + fm.horizontalAdvance("Eraser Gaussian diameter: "), ty);
            } else if (m_p1BrushActive) {
                drawParamLabel(p, fm, tx, ty, "Pixel value to enter:", m_p1BrushValueEdit->toolTip());
                m_p1BrushValueEdit->move(tx + fm.horizontalAdvance("Pixel value to enter: "), ty);
                drawParamLabel(p, fm, tx, ty + lh, "Paint brush solid diameter:", m_p1BrushSolidDiameterEdit->toolTip());
                m_p1BrushSolidDiameterEdit->move(tx + fm.horizontalAdvance("Paint brush solid diameter: "), ty + lh);
                drawParamLabel(p, fm, tx, ty + lh * 2, "Paint brush Gaussian diameter:", m_p1BrushDiameterEdit->toolTip());
                m_p1BrushDiameterEdit->move(tx + fm.horizontalAdvance("Paint brush Gaussian diameter: "), ty + lh * 2);
            } else if (m_p1TaperActive) {
                drawParamLabel(p, fm, tx, ty, "Hanning width:", m_p1TaperWidthEdit->toolTip());
                m_p1TaperWidthEdit->move(tx + fm.horizontalAdvance("Hanning width: "), ty);
                m_applyP1TaperBtn->move(tx, ty + lh);
            } else if (m_p1SymmetrizeActive) {
                drawParamLabel(p, fm, tx, ty, "Symmetry to apply:", m_p1SymmetryEdit->toolTip());
                m_p1SymmetryEdit->move(tx + fm.horizontalAdvance("Symmetry to apply: "), ty);
                m_applyP1SymmetryBtn->move(tx, ty + lh);
            } else if (m_binActive) {
                m_binCombo->move(tx, ty);
                m_binKeepSizeBtn->move(tx, ty + lh);
                m_applyBinBtn->move(tx, ty + lh * 2);
            } else if (m_copyActive) {
                int labW = std::max(fm.horizontalAdvance("Source buffer: "),
                                    fm.horizontalAdvance("Target buffer: "));
                drawParamLabel(p, fm, tx, ty, "Source buffer:", m_copySrcCombo->toolTip(),
                               m_copySrcCombo->height());
                m_copySrcCombo->move(tx + labW, ty);
                drawParamLabel(p, fm, tx, ty + lh, "Target buffer:", m_copyTgtCombo->toolTip(),
                               m_copyTgtCombo->height());
                m_copyTgtCombo->move(tx + labW, ty + lh);
                m_copyCancelBtn->move(tx, ty + lh * 2);
                m_copyDuplicateBtn->move(rx + rw - margin - m_copyDuplicateBtn->width(),
                                         ty + lh * 2);
            } else if (m_averageActive) {
                // Row 0: instruction.
                p.setPen(QColor(60, 60, 60));
                p.drawText(tx, ty + fm.ascent(),
                           "Choose which images to include in the average");

                // Rows 1-2: the a…p include toggles, eight per row. Recorded in
                // m_averageBtnRects for the click handler; a buffer that holds no
                // image is drawn dimmed and cannot be toggled on.
                const int tbs = std::max(16, lh - 4), tg = 4;
                QFont bf; bf.setBold(true); bf.setPixelSize(std::max(9, tbs * 3 / 5));
                for (int k = 0; k < HISTORY_SLOTS; k++) {
                    int col = k % 8, row = k / 8;
                    int bx = tx + col * (tbs + tg);
                    int by = ty + lh + row * lh;
                    QRect br(bx, by, tbs, tbs);
                    m_averageBtnRects[k] = br;

                    const bool has = bufferInUse(k);
                    const bool on  = has && m_averageInclude[k];
                    p.setPen(QPen(QColor(200, 200, 200), 1));
                    p.setBrush(on ? QColor(70, 150, 90)
                                  : (has ? QColor(55, 55, 55) : QColor(38, 38, 38)));
                    p.drawRect(br);
                    p.setFont(bf);
                    p.setPen(has ? Qt::white : QColor(120, 120, 120));
                    p.drawText(br, Qt::AlignCenter, QString(QChar('a' + k)));
                }

                // Row 3: target buffer selector.
                int rowTgt = ty + lh * 3;
                drawParamLabel(p, fm, tx, rowTgt, "Target buffer:",
                               m_averageTargetCombo->toolTip(),
                               m_averageTargetCombo->height());
                m_averageTargetCombo->move(tx + fm.horizontalAdvance("Target buffer: "), rowTgt);

                // Row 4: Cancel (left) and Compute average (right).
                int rowBtn = ty + lh * 4;
                m_averageCancelBtn->move(tx, rowBtn);
                m_averageComputeBtn->move(rx + rw - margin - m_averageComputeBtn->width(), rowBtn);

                // Row 5: status/result line.
                if (!m_averageResult.isEmpty()) {
                    p.setPen(QColor(60, 60, 60));
                    p.drawText(tx, ty + lh * 5 + fm.ascent(), m_averageResult);
                }
            } else if (m_padActive) {
                // Row 0: target selector, with the free-text field beside it
                // (greyed out unless "custom" is chosen).
                drawParamLabel(p, fm, tx, ty, "Target size (pixels):", m_padSizeCombo->toolTip(),
                               m_padSizeCombo->height());
                int cx2 = tx + fm.horizontalAdvance("Target size (pixels): ");
                m_padSizeCombo->move(cx2, ty);
                m_padCustomEdit->move(cx2 + m_padSizeCombo->width() + 8,
                                      ty + (m_padSizeCombo->height() - m_padCustomEdit->height()) / 2);
                // Row 1: what the current selection will actually do.
                p.drawText(tx, ty + lh + fm.ascent(), padSizeHint());
                // Row 2: Cancel left, apply right.
                m_padCancelBtn->move(tx, ty + lh * 2);
                m_applyPadBtn->move(rx + rw - margin - m_applyPadBtn->width(), ty + lh * 2);
            } else if (m_cropActive) {
                int labW = std::max(fm.horizontalAdvance("Top left:  "),
                                    fm.horizontalAdvance("Bottom right:  "));
                drawParamLabel(p, fm, tx, ty, "Top left:", m_cropTLxEdit->toolTip());
                m_cropTLxEdit->move(tx + labW, ty);
                m_cropTLyEdit->move(tx + labW + m_cropTLxEdit->width() + 8, ty);
                drawParamLabel(p, fm, tx, ty + lh, "Bottom right:", m_cropBRxEdit->toolTip());
                m_cropBRxEdit->move(tx + labW, ty + lh);
                m_cropBRyEdit->move(tx + labW + m_cropBRxEdit->width() + 8, ty + lh);
                m_cropCancelBtn->move(tx, ty + lh * 2);
                m_applyCropBtn->move(rx + rw - margin - m_applyCropBtn->width(), ty + lh * 2);
            } else if (m_peakPickActive) {
                // Row 0: source map combo + show/hide button top-right
                drawParamLabel(p, fm, tx, ty, "Picking source map:", m_peakSourceCombo->toolTip(),
                               m_peakSourceCombo->height());
                m_peakSourceCombo->move(tx + fm.horizontalAdvance("Picking source map: "), ty);
                m_peakShowPosBtn->move(rx + rw - margin - m_peakShowPosBtn->width(), ty);
                // Row 1: threshold slider
                int srcIdx = m_peakSourceCombo->currentIndex();
                double srcMin = m_imageMinVal, srcMax = m_imageMaxVal;
                if (srcIdx >= 0 && srcIdx < HISTORY_SLOTS && m_history[srcIdx].occupied) {
                    srcMin = m_history[srcIdx].minVal;
                    srcMax = m_history[srcIdx].maxVal;
                }
                double threshVal = srcMin + (srcMax - srcMin)
                                   * m_peakThresholdSlider->value() / 1000.0;
                QString threshStr = QString("Threshold: %1 ").arg(threshVal, 0, 'g', 5);
                drawParamLabel(p, fm, tx, ty + lh, threshStr, m_peakThresholdSlider->toolTip());
                m_peakThresholdSlider->move(tx + fm.horizontalAdvance(threshStr), ty + lh);
                // Row 2: exclusion radius slider
                QString exclStr2 = QString("Exclusion radius: %1 ").arg(m_peakExclRadiusSlider->value());
                drawParamLabel(p, fm, tx, ty + lh * 2, exclStr2, m_peakExclRadiusSlider->toolTip());
                m_peakExclRadiusSlider->move(tx + fm.horizontalAdvance(exclStr2), ty + lh * 2);
                // Row 3: peaks found
                QString peakStr = QString("Peaks found: %1").arg(m_peaks.size());
                drawParamLabel(p, fm, tx, ty + lh * 3, peakStr,
                    "Number of local maxima that currently pass the threshold\n"
                    "and exclusion-radius filters. Updates live as you adjust\n"
                    "the sliders. Click \"Compute\" to commit these positions\n"
                    "as a particle list for subsequent extraction.");
                // Row 4: Cancel (left) + Compute (right)
                m_peakCancelBtn->move(tx, ty + lh * 4);
                m_peakComputeBtn->move(rx + rw - margin - m_peakComputeBtn->width(), ty + lh * 4);
            } else if (m_extractActive) {
                if (m_peaks.empty()) {
                    p.drawText(tx, ty + fm.ascent(), "First prepare a particle position list");
                } else {
                    drawParamLabel(p, fm, tx, ty, "Source image:", m_extractSourceCombo->toolTip(),
                                   m_extractSourceCombo->height());
                    m_extractSourceCombo->move(tx + fm.horizontalAdvance("Source image: "), ty);
                    drawParamLabel(p, fm, tx, ty + lh, "Target image:", m_extractTargetCombo->toolTip(),
                                   m_extractTargetCombo->height());
                    m_extractTargetCombo->move(tx + fm.horizontalAdvance("Target image: "), ty + lh);
                    drawParamLabel(p, fm, tx, ty + lh * 2, "Particle size:", m_extractSizeCombo->toolTip(),
                                   m_extractSizeCombo->height());
                    m_extractSizeCombo->move(tx + fm.horizontalAdvance("Particle size: "), ty + lh * 2);
                    m_extractCancelBtn->move(tx, ty + lh * 3);
                    m_extractComputeBtn->move(rx + rw - margin - m_extractComputeBtn->width(), ty + lh * 3);
                }
            } else if (m_alignActive) {
                // Three columns side by side: a bold header on top, the pulldown
                // for that buffer role directly beneath it.
                const int comboW = m_alignSrcCombo->width();
                const int colGap = 14;
                int c0 = std::max(fm.horizontalAdvance("Image source"), comboW);
                int c1 = std::max(fm.horizontalAdvance("Alignment reference"), comboW);
                int c2 = std::max(fm.horizontalAdvance("Output buffer"), comboW);
                int x0 = tx;
                int x1 = x0 + c0 + colGap;
                int x2 = x1 + c1 + colGap;

                struct Col { int x; const char *head; QComboBox *cb; };
                const Col cols[3] = {
                    { x0, "Image source",        m_alignSrcCombo },
                    { x1, "Alignment reference",  m_alignRefCombo },
                    { x2, "Output buffer",        m_alignOutCombo },
                };
                QFont hf = sf; hf.setBold(true);
                for (const Col &c : cols) {
                    p.setFont(hf);
                    p.setPen(QColor(60, 60, 60));
                    p.drawText(c.x, ty + fm.ascent(), QString::fromLatin1(c.head));
                    QRect hr(c.x, ty, QFontMetrics(hf).horizontalAdvance(c.head), fm.height());
                    if (!c.cb->toolTip().isEmpty())
                        m_paramLabelTips.emplace_back(hr, c.cb->toolTip());
                    c.cb->move(c.x, ty + lh);
                }
                p.setFont(sf);

                // Cancel on the left, the two alignment actions right-aligned.
                m_alignCancelBtn->move(tx, ty + lh * 2);
                int bRight = rx + rw - margin;
                m_alignRotBtn->move(bRight - m_alignRotBtn->width(), ty + lh * 2);
                bRight -= m_alignRotBtn->width() + 8;
                m_alignShiftBtn->move(bRight - m_alignShiftBtn->width(), ty + lh * 2);
                if (!m_alignResult.isEmpty())
                    p.drawText(tx, ty + lh * 3 + fm.ascent(), m_alignResult);
            } else if (m_gaborActive) {
                drawParamLabel(p, fm, tx, ty, "Sigma (envelope):", m_gaborSigmaEdit->toolTip());
                m_gaborSigmaEdit->move(tx + fm.horizontalAdvance("Sigma (envelope): "), ty);
                drawParamLabel(p, fm, tx, ty + lh, "Wavelength lambda:", m_gaborLambdaEdit->toolTip());
                m_gaborLambdaEdit->move(tx + fm.horizontalAdvance("Wavelength lambda: "), ty + lh);
                drawParamLabel(p, fm, tx, ty + lh * 2, "Orientation (deg):", m_gaborThetaEdit->toolTip());
                m_gaborThetaEdit->move(tx + fm.horizontalAdvance("Orientation (deg): "), ty + lh * 2);
                drawParamLabel(p, fm, tx, ty + lh * 3, "Aspect ratio gamma:", m_gaborGammaEdit->toolTip());
                m_gaborGammaEdit->move(tx + fm.horizontalAdvance("Aspect ratio gamma: "), ty + lh * 3);
                m_gaborCancelBtn->move(tx, ty + lh * 4);
                m_gaborComputeBtn->move(rx + rw - margin - m_gaborComputeBtn->width(), ty + lh * 4);
            } else if (m_hessianActive) {
                drawParamLabel(p, fm, tx, ty, "Sigma (smoothing):", m_hessianSigmaEdit->toolTip());
                m_hessianSigmaEdit->move(tx + fm.horizontalAdvance("Sigma (smoothing): "), ty);
                drawParamLabel(p, fm, tx, ty + lh, "Polarity (+1/-1):", m_hessianPolarityEdit->toolTip());
                m_hessianPolarityEdit->move(tx + fm.horizontalAdvance("Polarity (+1/-1): "), ty + lh);
                m_hessianCancelBtn->move(tx, ty + lh * 2);
                m_hessianComputeBtn->move(rx + rw - margin - m_hessianComputeBtn->width(), ty + lh * 2);
            } else if (m_measureActive) {
                double dx = m_measureP1.x() - m_measureP0.x();
                double dy = m_measureP1.y() - m_measureP0.y();
                double pix = m_measureHasLine ? std::sqrt(dx * dx + dy * dy) : 0.0;
                double ps  = (m_pixelSize > 0.0) ? m_pixelSize : 1.0;
                QString pixStr = QString("Pixels: %1").arg(pix, 0, 'f', 2);
                QString lenStr = QString("Length: %1 \u00C5").arg(pix * ps, 0, 'f', 2);
                p.drawText(tx, ty + fm.ascent(), pixStr);
                p.drawText(tx, ty + lh + fm.ascent(), lenStr);
                m_measureCancelBtn->move(tx, ty + lh * 2);
            } else if (m_shiftActive) {
                p.drawText(tx, ty + fm.ascent(), shiftHint);
                m_shiftCancelBtn->move(tx, ty + lh);
            } else if (m_rotateActive) {
                p.drawText(tx, ty + fm.ascent(), rotateHint);
                m_rotateCancelBtn->move(tx, ty + lh);
            } else if (m_amyloidActive) {
                const int colGap = 20;
                int leftSize = fm.horizontalAdvance("Image size (px): ")   + m_amyloidSizeCombo->width();
                int leftRise = fm.horizontalAdvance("Helical rise (\u00C5): ") + m_amyloidRiseEdit->width();
                int leftWave = fm.horizontalAdvance("Waviness wavelength (px): ") + m_amyloidWaveEdit->width();
                int col2X = tx + std::max({leftSize, leftRise, leftWave}) + colGap;
                // Row 0: Image size | Source map
                drawParamLabel(p, fm, tx, ty, "Image size (px):", m_amyloidSizeCombo->toolTip());
                m_amyloidSizeCombo->move(tx + fm.horizontalAdvance("Image size (px): "), ty);
                drawParamLabel(p, fm, col2X, ty, "Source map:", m_amyloidMapCombo->toolTip());
                m_amyloidMapCombo->move(col2X + fm.horizontalAdvance("Source map: "), ty);
                // Row 1: Helical rise | Helical twist
                drawParamLabel(p, fm, tx, ty + lh, "Helical rise (\u00C5):", m_amyloidRiseEdit->toolTip());
                m_amyloidRiseEdit->move(tx + fm.horizontalAdvance("Helical rise (\u00C5): "), ty + lh);
                drawParamLabel(p, fm, col2X, ty + lh, "Helical twist (\u00B0):", m_amyloidTwistEdit->toolTip());
                m_amyloidTwistEdit->move(col2X + fm.horizontalAdvance("Helical twist (\u00B0): "), ty + lh);
                // Row 2: Waviness wavelength | Waviness amplitude
                drawParamLabel(p, fm, tx, ty + lh * 2, "Waviness wavelength (px):", m_amyloidWaveEdit->toolTip());
                m_amyloidWaveEdit->move(tx + fm.horizontalAdvance("Waviness wavelength (px): "), ty + lh * 2);
                drawParamLabel(p, fm, col2X, ty + lh * 2, "Waviness amplitude (px):", m_amyloidAmplEdit->toolTip());
                m_amyloidAmplEdit->move(col2X + fm.horizontalAdvance("Waviness amplitude (px): "), ty + lh * 2);
                // Row 3: Persistence length
                drawParamLabel(p, fm, tx, ty + lh * 3, "Persistence length (\u00B5m):", m_amyloidPersistEdit->toolTip());
                m_amyloidPersistEdit->move(tx + fm.horizontalAdvance("Persistence length (\u00B5m): "), ty + lh * 3);
                // Row 4: Noise checkbox + Sigma edit
                m_amyloidNoiseBtn->move(tx, ty + lh * 4);
                int noiseLblX = tx + m_amyloidNoiseBtn->sizeHint().width() + 8;
                drawParamLabel(p, fm, noiseLblX, ty + lh * 4, "Sigma:", m_amyloidNoiseEdit->toolTip());
                m_amyloidNoiseEdit->move(noiseLblX + fm.horizontalAdvance("Sigma: "), ty + lh * 4);
                // Row 5: Signal polarity button
                m_amyloidSignalBtn->move(tx, ty + lh * 5);
                // Row 6: Filament info
                QString infoStr;
                if (m_amyloidPlacing == 1)
                    infoStr = QString("Filaments: %1  Click to place end point").arg(m_amyloidFilaments.size());
                else
                    infoStr = QString("Filaments: %1  Click image to place start & end").arg(m_amyloidFilaments.size());
                p.drawText(tx, ty + lh * 6 + fm.ascent(), infoStr);
                // Row 7: Cancel + Compute
                m_amyloidCancelBtn->move(tx, ty + lh * 7);
                m_amyloidComputeBtn->move(rx + rw - margin - m_amyloidComputeBtn->width(), ty + lh * 7);
            }
        }
    }

    // ---- Panel 3: image history (below panel 1) – 2 rows × 8 columns ----------
    {
        int p3x = 0;
        int p3y = hy + 2;
        // Narrowed by half the centre gutter: the Reload / Save / Delete buttons
        // are placed there by resizeEvent, so nothing may be drawn under them.
        int p3w = cx - 1 - historyButtonGutter() / 2;
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

            bool isActive = (i == m_activeSlot);
            // Slot remembered from the last session but deliberately not loaded
            // at startup (see restoreHistory): drawn as a dashed placeholder,
            // filled in as soon as the user clicks it. isLoading is the same
            // slot once its background read has started.
            bool isDeferred = !m_history[i].occupied && m_history[i].deferred;
            bool isLoading  = !m_history[i].occupied && m_history[i].loading;

            if (isActive) {
                p.setPen(QPen(QColor(120, 180, 255), 8));
            } else if (isDeferred || isLoading) {
                p.setPen(QPen(QColor(160, 160, 60), 1, Qt::DashLine));
            } else {
                p.setPen(QPen(QColor(255, 255, 0), 1));
            }
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);

            bool hasImage = isActive ? !m_image.isNull() : m_history[i].occupied;

            if (hasImage) {
                QRect inner = r.adjusted(1, 1, -1, -1);
                if (isActive)
                    p.drawImage(inner, m_image);
                else
                    p.drawImage(inner, m_history[i].image);
            } else if (isDeferred || isLoading) {
                QFont df; df.setPixelSize(std::max(8, labelFontHist - 1)); p.setFont(df);
                p.setPen(QColor(190, 190, 90));
                p.drawText(r, Qt::AlignCenter,
                           isLoading ? QStringLiteral("loading…")
                                     : QStringLiteral("click\nto load"));
            }
        }
    }

    // ---- Panel 4: power spectrum history (below panel 2) – 2 rows × 8 columns -
    {
        int p4x = cx + 2 + historyButtonGutter() / 2;   // clear of the centre buttons
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

            bool isActive = (i == m_activeSlot);
            bool isDeferred = !m_history[i].occupied
                              && (m_history[i].deferred || m_history[i].loading);

            if (isActive) {
                p.setPen(QPen(QColor(120, 180, 255), 8));
            } else if (isDeferred) {
                p.setPen(QPen(QColor(160, 160, 60), 1, Qt::DashLine));
            } else {
                p.setPen(QPen(QColor(255, 255, 0), 1));
            }
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);

            QRect inner = r.adjusted(1, 1, -1, -1);

            if (isActive && m_ftComputed && !m_powerImg.isNull()) {
                p.drawImage(inner, m_powerImg);
            } else if (!isActive && m_history[i].occupied
                       && !m_history[i].powerSpecImg.isNull()) {
                p.drawImage(inner, m_history[i].powerSpecImg);
            }
        }
    }

    // ---- Cross-section profile overlay in panel 4 --------------------------------
    if (m_crossSectionActive && m_ftComputed && !m_crossSectionProfile.empty()) {
        int p4x = cx + 2 + historyButtonGutter() / 2;   // clear of the centre buttons
        int p4y = hy + 2;
        int p4w = width() - p4x;
        int p4h = height() - p4y;

        int rw = static_cast<int>(p4w * 0.80);
        int rh = static_cast<int>(p4h * 0.80);
        int rx = p4x + (p4w - rw) / 2;
        int ry = p4y + (p4h - rh) / 2;
        QRect profileRect(rx, ry, rw, rh);
        drawShadowRect(p, profileRect);

        // Plot the profile
        int plotMarginL = 50, plotMarginR = 15, plotMarginT = 25, plotMarginB = 35;
        int plotX = rx + plotMarginL;
        int plotY = ry + plotMarginT;
        int plotW = rw - plotMarginL - plotMarginR;
        int plotH = rh - plotMarginT - plotMarginB;

        if (plotW > 10 && plotH > 10 && m_crossSectionProfile.size() > 1
            && m_crossSectionValid.size() == m_crossSectionProfile.size()) {
            int nPts = (int)m_crossSectionProfile.size();

            // Compute min/max only from valid bins
            double profMin = 1e30, profMax = -1e30;
            for (int j = 0; j < nPts; j++) {
                if (!m_crossSectionValid[j]) continue;
                profMin = std::min(profMin, m_crossSectionProfile[j]);
                profMax = std::max(profMax, m_crossSectionProfile[j]);
            }
            if (profMax <= profMin) profMax = profMin + 1.0;

            // Draw plot area background
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(245, 245, 245));
            p.drawRect(plotX, plotY, plotW, plotH);

            // Draw grid lines
            p.setPen(QPen(QColor(210, 210, 210), 1));
            for (int g = 1; g < 4; g++) {
                int gy = plotY + g * plotH / 4;
                p.drawLine(plotX, gy, plotX + plotW, gy);
            }

            // Draw profile curve as one continuous line: skip invalid bins
            // (e.g. with a very narrow integration width) but keep the pen
            // down so gaps are bridged by a straight line.
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(QColor(40, 100, 220), 2));
            p.setBrush(Qt::NoBrush);
            bool started = false;
            QPainterPath curve;
            for (int j = 0; j < nPts; j++) {
                if (!m_crossSectionValid[j]) continue;
                double xp = plotX + (double)j / (nPts - 1) * plotW;
                double yp = plotY + plotH - (m_crossSectionProfile[j] - profMin) / (profMax - profMin) * plotH;
                if (!started) {
                    curve.moveTo(xp, yp);
                    started = true;
                } else {
                    curve.lineTo(xp, yp);
                }
            }
            if (started) p.drawPath(curve);
            p.setRenderHint(QPainter::Antialiasing, false);

            // Mark center of Fourier transform
            if (m_crossSectionCenter >= 0 && m_crossSectionCenter < nPts) {
                double centerXp = plotX + (double)m_crossSectionCenter / (nPts - 1) * plotW;
                p.setPen(QPen(QColor(200, 60, 60), 1, Qt::DashLine));
                p.drawLine((int)centerXp, plotY, (int)centerXp, plotY + plotH);

                QFont cf; cf.setPixelSize(9); p.setFont(cf);
                p.setPen(QColor(200, 60, 60));
                p.drawText((int)centerXp + 3, plotY + 10, "center");
            }

            // Dotted vertical lines at ±0.5 reciprocal pixels
            {
                double halfN = m_fftN / 2.0;
                double maxProj = halfN * std::sqrt(2.0);
                double projMin = -maxProj;
                double freqs[2] = { -0.5, 0.5 };
                p.setPen(QPen(QColor(120, 120, 120), 1, Qt::DotLine));
                for (double freq : freqs) {
                    double projDist = freq * m_fftN;
                    int idx = (int)std::round(projDist - projMin);
                    if (idx >= 0 && idx < nPts) {
                        double xp = plotX + (double)idx / (nPts - 1) * plotW;
                        p.drawLine((int)xp, plotY, (int)xp, plotY + plotH);
                    }
                }
            }

            // Draw axes
            p.setPen(QPen(QColor(60, 60, 60), 1));
            p.drawLine(plotX, plotY + plotH, plotX + plotW, plotY + plotH);  // X axis
            p.drawLine(plotX, plotY, plotX, plotY + plotH);                  // Y axis

            // X axis labels – always ±sqrt(2)/2 ≈ ±0.707
            {
                QFont af; af.setPixelSize(10); p.setFont(af);
                QFontMetrics afm(af);
                p.setPen(QColor(60, 60, 60));

                double dispFreq = std::sqrt(2.0) / 2.0;  // always ±0.707
                QString lbl0 = QString::number(-dispFreq, 'f', 3);
                QString lblM = "0";
                QString lbl1 = QString::number(dispFreq, 'f', 3);

                p.drawText(plotX, plotY + plotH + afm.ascent() + 3, lbl0);
                if (m_crossSectionCenter >= 0 && m_crossSectionCenter < nPts) {
                    double centerXp = plotX + (double)m_crossSectionCenter / (nPts - 1) * plotW;
                    p.drawText((int)centerXp - afm.horizontalAdvance(lblM) / 2,
                               plotY + plotH + afm.ascent() + 3, lblM);
                }
                p.drawText(plotX + plotW - afm.horizontalAdvance(lbl1),
                           plotY + plotH + afm.ascent() + 3, lbl1);

                // X axis title
                QString xTitle = "reciprocal pixels";
                p.drawText(plotX + (plotW - afm.horizontalAdvance(xTitle)) / 2,
                           plotY + plotH + afm.ascent() + 15, xTitle);
            }

            // Y axis labels
            {
                QFont af; af.setPixelSize(10); p.setFont(af);
                QFontMetrics afm(af);
                p.setPen(QColor(60, 60, 60));
                QString yMax = QString::number(profMax, 'g', 3);
                QString yMin = QString::number(profMin, 'g', 3);
                p.drawText(plotX - afm.horizontalAdvance(yMax) - 4, plotY + afm.ascent(), yMax);
                p.drawText(plotX - afm.horizontalAdvance(yMin) - 4, plotY + plotH, yMin);
            }

            // Title
            {
                QFont tf; tf.setBold(true); tf.setPixelSize(12); p.setFont(tf);
                p.setPen(QColor(40, 40, 40));
                p.drawText(rx + 8, ry + 16, "Amplitude profile");
            }

            // Direction annotation in the top-right corner
            {
                QFont df; df.setBold(true); df.setPixelSize(12); p.setFont(df);
                QFontMetrics dfm(df);
                double dispDeg = -m_crossSectionAngle;
                while (dispDeg <= -180.0) dispDeg += 360.0;
                while (dispDeg > 180.0)   dispDeg -= 360.0;
                QString dirStr = QString("Direction: %1°").arg(dispDeg, 0, 'f', 1);
                p.setPen(QColor(220, 50, 50));
                p.drawText(rx + rw - dfm.horizontalAdvance(dirStr) - 8, ry + 16, dirStr);
            }
        }
    }

    // ---- Cross-section phase profile overlay in panel 3 -------------------------
    if (m_crossSectionActive && m_ftComputed && !m_crossSectionPhaseProfile.empty()
        && m_crossSectionValid.size() == m_crossSectionPhaseProfile.size()) {
        int p3x = 0;
        int p3y = hy + 2;
        // Narrowed by half the centre gutter: the Reload / Save / Delete buttons
        // are placed there by resizeEvent, so nothing may be drawn under them.
        int p3w = cx - 1 - historyButtonGutter() / 2;
        int p3h = height() - p3y;

        int rw = static_cast<int>(p3w * 0.80);
        int rh = static_cast<int>(p3h * 0.80);
        int rx = p3x + (p3w - rw) / 2;
        int ry = p3y + (p3h - rh) / 2;
        QRect profileRect(rx, ry, rw, rh);
        drawShadowRect(p, profileRect);

        int plotMarginL = 50, plotMarginR = 15, plotMarginT = 25, plotMarginB = 35;
        int plotX = rx + plotMarginL;
        int plotY = ry + plotMarginT;
        int plotW = rw - plotMarginL - plotMarginR;
        int plotH = rh - plotMarginT - plotMarginB;

        if (plotW > 10 && plotH > 10 && m_crossSectionPhaseProfile.size() > 1) {
            int nPts = (int)m_crossSectionPhaseProfile.size();

            // Plot background
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(245, 245, 245));
            p.drawRect(plotX, plotY, plotW, plotH);

            // Grid lines at ±π/2 (phase range [−π,π])
            p.setPen(QPen(QColor(210, 210, 210), 1));
            for (int g = 1; g < 4; g++) {
                int gy = plotY + g * plotH / 4;
                p.drawLine(plotX, gy, plotX + plotW, gy);
            }

            // Zero line (phase = 0) through the middle.
            int zeroY = plotY + plotH / 2;
            p.setPen(QPen(QColor(160, 160, 160), 1));
            p.drawLine(plotX, zeroY, plotX + plotW, zeroY);

            // Phase curve (arg ∈ [−π,π]; 0 at the middle, +π top, −π bottom),
            // drawn as one continuous line: skip invalid bins (e.g. with a
            // very narrow integration width) but keep the pen down so gaps are
            // bridged by a straight line.
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(QColor(150, 40, 200), 2));
            p.setBrush(Qt::NoBrush);
            bool started = false;
            QPainterPath curve;
            for (int j = 0; j < nPts; j++) {
                if (!m_crossSectionValid[j]) continue;
                double xp = plotX + (double)j / (nPts - 1) * plotW;
                double v = std::clamp(m_crossSectionPhaseProfile[j], -M_PI, M_PI);
                double yp = zeroY - (v / M_PI) * (plotH / 2.0);
                if (!started) { curve.moveTo(xp, yp); started = true; }
                else curve.lineTo(xp, yp);
            }
            if (started) p.drawPath(curve);
            p.setRenderHint(QPainter::Antialiasing, false);

            // Mark center of Fourier transform
            if (m_crossSectionCenter >= 0 && m_crossSectionCenter < nPts) {
                double centerXp = plotX + (double)m_crossSectionCenter / (nPts - 1) * plotW;
                p.setPen(QPen(QColor(200, 60, 60), 1, Qt::DashLine));
                p.drawLine((int)centerXp, plotY, (int)centerXp, plotY + plotH);
                QFont cf; cf.setPixelSize(9); p.setFont(cf);
                p.setPen(QColor(200, 60, 60));
                p.drawText((int)centerXp + 3, plotY + 10, "center");
            }

            // Dotted vertical lines at ±0.5 reciprocal pixels
            {
                double halfN = m_fftN / 2.0;
                double maxProj = halfN * std::sqrt(2.0);
                double projMin = -maxProj;
                double freqs[2] = { -0.5, 0.5 };
                p.setPen(QPen(QColor(120, 120, 120), 1, Qt::DotLine));
                for (double freq : freqs) {
                    double projDist = freq * m_fftN;
                    int idx = (int)std::round(projDist - projMin);
                    if (idx >= 0 && idx < nPts) {
                        double xp = plotX + (double)idx / (nPts - 1) * plotW;
                        p.drawLine((int)xp, plotY, (int)xp, plotY + plotH);
                    }
                }
            }

            // Axes
            p.setPen(QPen(QColor(60, 60, 60), 1));
            p.drawLine(plotX, plotY + plotH, plotX + plotW, plotY + plotH);
            p.drawLine(plotX, plotY, plotX, plotY + plotH);

            // Y axis labels (phase range [−180°,180°])
            {
                QFont af; af.setPixelSize(10); p.setFont(af);
                QFontMetrics afm(af);
                p.setPen(QColor(60, 60, 60));
                QString l180  = "180°";
                QString l90   = "90°";
                QString l0    = "0°";
                QString lm90  = "-90°";
                QString lm180 = "-180°";
                int q1 = plotY + plotH / 4;
                int q3 = plotY + 3 * plotH / 4;
                p.drawText(plotX - afm.horizontalAdvance(l180)  - 4, plotY + afm.ascent(),    l180);
                p.drawText(plotX - afm.horizontalAdvance(l90)   - 4, q1 + afm.ascent() / 2,   l90);
                p.drawText(plotX - afm.horizontalAdvance(l0)    - 4, zeroY + afm.ascent() / 2, l0);
                p.drawText(plotX - afm.horizontalAdvance(lm90)  - 4, q3 + afm.ascent() / 2,   lm90);
                p.drawText(plotX - afm.horizontalAdvance(lm180) - 4, plotY + plotH,           lm180);
            }

            // X axis labels – always ±sqrt(2)/2 ≈ ±0.707 reciprocal pixels
            {
                QFont af; af.setPixelSize(10); p.setFont(af);
                QFontMetrics afm(af);
                p.setPen(QColor(60, 60, 60));

                double dispFreq = std::sqrt(2.0) / 2.0;
                QString lbl0 = QString::number(-dispFreq, 'f', 3);
                QString lblM = "0";
                QString lbl1 = QString::number(dispFreq, 'f', 3);

                p.drawText(plotX, plotY + plotH + afm.ascent() + 3, lbl0);
                if (m_crossSectionCenter >= 0 && m_crossSectionCenter < nPts) {
                    double centerXp = plotX + (double)m_crossSectionCenter / (nPts - 1) * plotW;
                    p.drawText((int)centerXp - afm.horizontalAdvance(lblM) / 2,
                               plotY + plotH + afm.ascent() + 3, lblM);
                }
                p.drawText(plotX + plotW - afm.horizontalAdvance(lbl1),
                           plotY + plotH + afm.ascent() + 3, lbl1);

                QString xTitle = "reciprocal pixels";
                p.drawText(plotX + (plotW - afm.horizontalAdvance(xTitle)) / 2,
                           plotY + plotH + afm.ascent() + 15, xTitle);
            }

            // Title
            {
                QFont tf; tf.setBold(true); tf.setPixelSize(12); p.setFont(tf);
                p.setPen(QColor(40, 40, 40));
                p.drawText(rx + 8, ry + 16, "Phase profile");
            }

            // Direction annotation in the top-right corner
            {
                QFont df; df.setBold(true); df.setPixelSize(12); p.setFont(df);
                QFontMetrics dfm(df);
                double dispDeg = -m_crossSectionAngle;
                while (dispDeg <= -180.0) dispDeg += 360.0;
                while (dispDeg > 180.0)   dispDeg -= 360.0;
                QString dirStr = QString("Direction: %1°").arg(dispDeg, 0, 'f', 1);
                p.setPen(QColor(220, 50, 50));
                p.drawText(rx + rw - dfm.horizontalAdvance(dirStr) - 8, ry + 16, dirStr);
            }
        }
    }

    // ---- Alignment diagnostics overlay in panel 4 -------------------------------
    // Gated on the tool being open, so it vanishes by every route that closes
    // the tool, not only via its Cancel button.
    if (m_alignActive)
        drawAlignOverlay(p);

    // ---- CTF 1D profile overlay in panel 4 --------------------------------------
    if (m_ctfActive && !m_ctfProfile.empty()) {
        int p4x = cx + 2 + historyButtonGutter() / 2;   // clear of the centre buttons
        int p4y = hy + 2;
        int p4w = width() - p4x;
        int p4h = height() - p4y;

        int rw = static_cast<int>(p4w * 0.80);
        int rh = static_cast<int>(p4h * 0.80);
        int rx = p4x + (p4w - rw) / 2;
        int ry = p4y + (p4h - rh) / 2;
        QRect profileRect(rx, ry, rw, rh);
        drawShadowRect(p, profileRect);

        int plotMarginL = 50, plotMarginR = 15, plotMarginT = 25, plotMarginB = 35;
        int plotX = rx + plotMarginL;
        int plotY = ry + plotMarginT;
        int plotW = rw - plotMarginL - plotMarginR;
        int plotH = rh - plotMarginT - plotMarginB;

        if (plotW > 10 && plotH > 10 && m_ctfProfile.size() > 1) {
            int nPts = (int)m_ctfProfile.size();

            // Plot background
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(245, 245, 245));
            p.drawRect(plotX, plotY, plotW, plotH);

            // Grid lines at 0.25, 0.5, 0.75 (amplitude range is [0,1])
            p.setPen(QPen(QColor(210, 210, 210), 1));
            for (int g = 1; g < 4; g++) {
                int gy = plotY + g * plotH / 4;
                p.drawLine(plotX, gy, plotX + plotW, gy);
            }

            // The profile runs from q = 0 to the diagonal (corner) frequency, but
            // along the red-line direction θ the CTF in panel 2 only exists out to
            // where the ray leaves the square grid, at radial distance
            //   r_edge(θ) = (N/2) / max(|cosθ|, |sinθ|).
            // As a fraction of the full diagonal r_max = (N/2)·√2 this is
            //   frac = 1 / (√2 · max(|cosθ|, |sinθ|)),
            // = 1/√2 ≈ 0.707 along the axes (q up to Nyquist) and 1 along the
            // diagonal. Plot the curve only up to that point; leave the rest empty.
            double thetaRad = m_ctfAngleDeg * M_PI / 180.0;
            double mAxis = std::max(std::abs(std::cos(thetaRad)),
                                    std::abs(std::sin(thetaRad)));
            double validFrac = (mAxis > 1e-9)
                                 ? std::min(1.0, 1.0 / (std::sqrt(2.0) * mAxis))
                                 : 1.0;

            // Profile curve (amplitude, [0,1]; 0 at the bottom axis, 1 at the top)
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(QColor(40, 100, 220), 2));
            p.setBrush(Qt::NoBrush);
            QPainterPath curve;
            bool started = false;
            for (int j = 0; j < nPts; j++) {
                double frac = (double)j / (nPts - 1);
                if (frac > validFrac) break;
                double xp = plotX + frac * plotW;
                double v = std::clamp(m_ctfProfile[j], 0.0, 1.0);
                double yp = plotY + plotH - v * plotH;
                if (!started) { curve.moveTo(xp, yp); started = true; }
                else curve.lineTo(xp, yp);
            }
            p.drawPath(curve);
            p.setRenderHint(QPainter::Antialiasing, false);

            // Axes
            p.setPen(QPen(QColor(60, 60, 60), 1));
            p.drawLine(plotX, plotY + plotH, plotX + plotW, plotY + plotH);
            p.drawLine(plotX, plotY, plotX, plotY + plotH);

            // Y axis labels (amplitude range [0,1])
            {
                QFont af; af.setPixelSize(10); p.setFont(af);
                QFontMetrics afm(af);
                p.setPen(QColor(60, 60, 60));
                QString yMax = "1";
                QString yMid = "0.5";
                QString yMin = "0";
                int midY = plotY + plotH / 2;
                p.drawText(plotX - afm.horizontalAdvance(yMax) - 4, plotY + afm.ascent(), yMax);
                p.drawText(plotX - afm.horizontalAdvance(yMid) - 4, midY + afm.ascent() / 2, yMid);
                p.drawText(plotX - afm.horizontalAdvance(yMin) - 4, plotY + plotH, yMin);
            }

            // X axis label: spatial frequency range in 1/Å
            {
                QFont af; af.setPixelSize(10); p.setFont(af);
                QFontMetrics afm(af);
                double maxQ = (m_fftN > 0 && m_pixelSize > 0)
                                ? std::sqrt(2.0) / (2.0 * m_pixelSize) : 0.0;

                // Dotted vertical line at q = 0.5 (1/Å)
                if (maxQ > 0.5) {
                    double xp = plotX + (0.5 / maxQ) * plotW;
                    p.setPen(QPen(QColor(120, 120, 120), 1, Qt::DotLine));
                    p.drawLine((int)xp, plotY, (int)xp, plotY + plotH);
                }

                p.setPen(QColor(60, 60, 60));
                QString lbl0 = "0";
                QString lbl1 = QString::number(maxQ, 'f', 3);
                p.drawText(plotX, plotY + plotH + afm.ascent() + 3, lbl0);
                p.drawText(plotX + plotW - afm.horizontalAdvance(lbl1),
                           plotY + plotH + afm.ascent() + 3, lbl1);

                // Tick labels at 0.25 and 0.5 (1/Å)
                double ticks[2] = { 0.25, 0.5 };
                for (double t : ticks) {
                    if (maxQ <= t) continue;
                    double xp = plotX + (t / maxQ) * plotW;
                    QString lbl = QString::number(t, 'f', 2);
                    p.drawLine((int)xp, plotY + plotH, (int)xp, plotY + plotH + 3);
                    p.drawText((int)xp - afm.horizontalAdvance(lbl) / 2,
                               plotY + plotH + afm.ascent() + 3, lbl);
                }

                QString xTitle = "spatial frequency (1/\u00C5)";
                p.drawText(plotX + (plotW - afm.horizontalAdvance(xTitle)) / 2,
                           plotY + plotH + afm.ascent() + 15, xTitle);
            }

            // Title
            {
                QFont tf; tf.setBold(true); tf.setPixelSize(12); p.setFont(tf);
                p.setPen(QColor(40, 40, 40));
                p.drawText(rx + 8, ry + 16, "CTF Amplitude Profile");
            }

            // Direction + defocus annotation in the top-right corner
            {
                QFont df; df.setBold(true); df.setPixelSize(12); p.setFont(df);
                QFontMetrics dfm(df);

                // Defocus valid in the selected direction:
                //   Δf(θ) = Δf_avg + Δf_A · cos(2(θ − α))
                bool okD = false, okA = false, okAA = false;
                double defocusNM     = m_ctfDefocusEdit->text().toDouble(&okD);
                double astigNM       = m_ctfAstigEdit->text().toDouble(&okA);
                double astigAngleDeg = m_ctfAstigAngleEdit->text().toDouble(&okAA);
                if (!okD)  defocusNM = 1000.0;
                if (!okA)  astigNM   = 0.0;
                if (!okAA) astigAngleDeg = 0.0;
                double profRad = m_ctfAngleDeg * M_PI / 180.0;
                double astigRad = astigAngleDeg * M_PI / 180.0;
                double dfLocalNM = defocusNM + astigNM * std::cos(2.0 * (profRad - astigRad));

                QString dirStr = QString("Direction: %1\u00B0   Defocus: %2 nm")
                                     .arg(m_ctfAngleDeg, 0, 'f', 1)
                                     .arg(dfLocalNM, 0, 'f', 1);
                p.setPen(QColor(220, 50, 50));
                p.drawText(rx + rw - dfm.horizontalAdvance(dirStr) - 8,
                           ry + 16, dirStr);
            }
        }
    }

    // ---- CTF 1D phase profile overlay in panel 3 --------------------------------
    if (m_ctfActive && !m_ctfPhaseProfile.empty()) {
        int p3x = 0;
        int p3y = hy + 2;
        // Narrowed by half the centre gutter: the Reload / Save / Delete buttons
        // are placed there by resizeEvent, so nothing may be drawn under them.
        int p3w = cx - 1 - historyButtonGutter() / 2;
        int p3h = height() - p3y;

        int rw = static_cast<int>(p3w * 0.80);
        int rh = static_cast<int>(p3h * 0.80);
        int rx = p3x + (p3w - rw) / 2;
        int ry = p3y + (p3h - rh) / 2;
        QRect profileRect(rx, ry, rw, rh);
        drawShadowRect(p, profileRect);

        int plotMarginL = 50, plotMarginR = 15, plotMarginT = 25, plotMarginB = 35;
        int plotX = rx + plotMarginL;
        int plotY = ry + plotMarginT;
        int plotW = rw - plotMarginL - plotMarginR;
        int plotH = rh - plotMarginT - plotMarginB;

        if (plotW > 10 && plotH > 10 && m_ctfPhaseProfile.size() > 1) {
            int nPts = (int)m_ctfPhaseProfile.size();

            // Plot background
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(245, 245, 245));
            p.drawRect(plotX, plotY, plotW, plotH);

            // Grid lines at ±π/2 (phase range is [−π,π])
            p.setPen(QPen(QColor(210, 210, 210), 1));
            for (int g = 1; g < 4; g++) {
                int gy = plotY + g * plotH / 4;
                p.drawLine(plotX, gy, plotX + plotW, gy);
            }

            // Zero line (phase = 0) through the middle.
            int zeroY = plotY + plotH / 2;
            p.setPen(QPen(QColor(160, 160, 160), 1));
            p.drawLine(plotX, zeroY, plotX + plotW, zeroY);

            // Same valid-frequency cutoff as the amplitude profile: along the
            // red-line direction θ the CTF in panel 2 only exists out to where the
            // ray leaves the square grid, i.e. a fraction
            //   frac = 1 / (√2 · max(|cosθ|, |sinθ|))
            // of the full diagonal. Plot the curve only that far; rest stays empty.
            double thetaRad = m_ctfAngleDeg * M_PI / 180.0;
            double mAxis = std::max(std::abs(std::cos(thetaRad)),
                                    std::abs(std::sin(thetaRad)));
            double validFrac = (mAxis > 1e-9)
                                 ? std::min(1.0, 1.0 / (std::sqrt(2.0) * mAxis))
                                 : 1.0;

            // Phase curve (arg C ∈ [−π,π]; 0 at the middle, +π top, −π bottom).
            // The curve is drawn continuously: where the phase wraps at ±π the
            // pen stays down, so the wrap shows up as a (near-)vertical connector.
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(QColor(150, 40, 200), 2));
            p.setBrush(Qt::NoBrush);
            QPainterPath curve;
            bool started = false;
            for (int j = 0; j < nPts; j++) {
                double frac = (double)j / (nPts - 1);
                if (frac > validFrac) break;
                double xp = plotX + frac * plotW;
                double v = std::clamp(m_ctfPhaseProfile[j], -M_PI, M_PI);
                double yp = zeroY - (v / M_PI) * (plotH / 2.0);
                if (!started) { curve.moveTo(xp, yp); started = true; }
                else curve.lineTo(xp, yp);
            }
            p.drawPath(curve);
            p.setRenderHint(QPainter::Antialiasing, false);

            // Axes
            p.setPen(QPen(QColor(60, 60, 60), 1));
            p.drawLine(plotX, plotY + plotH, plotX + plotW, plotY + plotH);
            p.drawLine(plotX, plotY, plotX, plotY + plotH);

            // Y axis labels (phase range [−180°,180°])
            {
                QFont af; af.setPixelSize(10); p.setFont(af);
                QFontMetrics afm(af);
                p.setPen(QColor(60, 60, 60));
                QString l180  = "180°";
                QString l90   = "90°";
                QString l0    = "0°";
                QString lm90  = "-90°";
                QString lm180 = "-180°";
                int q1 = plotY + plotH / 4;
                int q3 = plotY + 3 * plotH / 4;
                p.drawText(plotX - afm.horizontalAdvance(l180)  - 4, plotY + afm.ascent(),    l180);
                p.drawText(plotX - afm.horizontalAdvance(l90)   - 4, q1 + afm.ascent() / 2,   l90);
                p.drawText(plotX - afm.horizontalAdvance(l0)    - 4, zeroY + afm.ascent() / 2, l0);
                p.drawText(plotX - afm.horizontalAdvance(lm90)  - 4, q3 + afm.ascent() / 2,   lm90);
                p.drawText(plotX - afm.horizontalAdvance(lm180) - 4, plotY + plotH,           lm180);
            }

            // X axis label: spatial frequency range in 1/Å
            {
                QFont af; af.setPixelSize(10); p.setFont(af);
                QFontMetrics afm(af);
                double maxQ = (m_fftN > 0 && m_pixelSize > 0)
                                ? std::sqrt(2.0) / (2.0 * m_pixelSize) : 0.0;

                // Dotted vertical line at q = 0.5 (1/Å)
                if (maxQ > 0.5) {
                    double xp = plotX + (0.5 / maxQ) * plotW;
                    p.setPen(QPen(QColor(120, 120, 120), 1, Qt::DotLine));
                    p.drawLine((int)xp, plotY, (int)xp, plotY + plotH);
                }

                p.setPen(QColor(60, 60, 60));
                QString lbl0 = "0";
                QString lbl1 = QString::number(maxQ, 'f', 3);
                p.drawText(plotX, plotY + plotH + afm.ascent() + 3, lbl0);
                p.drawText(plotX + plotW - afm.horizontalAdvance(lbl1),
                           plotY + plotH + afm.ascent() + 3, lbl1);

                // Tick labels at 0.25 and 0.5 (1/Å)
                double ticks[2] = { 0.25, 0.5 };
                for (double t : ticks) {
                    if (maxQ <= t) continue;
                    double xp = plotX + (t / maxQ) * plotW;
                    QString lbl = QString::number(t, 'f', 2);
                    p.drawLine((int)xp, plotY + plotH, (int)xp, plotY + plotH + 3);
                    p.drawText((int)xp - afm.horizontalAdvance(lbl) / 2,
                               plotY + plotH + afm.ascent() + 3, lbl);
                }

                QString xTitle = "spatial frequency (1/Å)";
                p.drawText(plotX + (plotW - afm.horizontalAdvance(xTitle)) / 2,
                           plotY + plotH + afm.ascent() + 15, xTitle);
            }

            // Title
            {
                QFont tf; tf.setBold(true); tf.setPixelSize(12); p.setFont(tf);
                p.setPen(QColor(40, 40, 40));
                p.drawText(rx + 8, ry + 16, "CTF Phase Profile");
            }

            // Direction + defocus annotation in the top-right corner
            {
                QFont df; df.setBold(true); df.setPixelSize(12); p.setFont(df);
                QFontMetrics dfm(df);

                bool okD = false, okA = false, okAA = false;
                double defocusNM     = m_ctfDefocusEdit->text().toDouble(&okD);
                double astigNM       = m_ctfAstigEdit->text().toDouble(&okA);
                double astigAngleDeg = m_ctfAstigAngleEdit->text().toDouble(&okAA);
                if (!okD)  defocusNM = 1000.0;
                if (!okA)  astigNM   = 0.0;
                if (!okAA) astigAngleDeg = 0.0;
                double profRad = m_ctfAngleDeg * M_PI / 180.0;
                double astigRad = astigAngleDeg * M_PI / 180.0;
                double dfLocalNM = defocusNM + astigNM * std::cos(2.0 * (profRad - astigRad));

                QString dirStr = QString("Direction: %1°   Defocus: %2 nm")
                                     .arg(m_ctfAngleDeg, 0, 'f', 1)
                                     .arg(dfLocalNM, 0, 'f', 1);
                p.setPen(QColor(220, 50, 50));
                p.drawText(rx + rw - dfm.horizontalAdvance(dirStr) - 8,
                           ry + 16, dirStr);
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
    // When embedded in another Qt application (i.e. FtWindow is not a
    // top-level window), the host provides its own window chrome, so skip
    // drawing the internal "Fourier Analyzer" title. The "Manual" button is
    // still drawn (at the top, in place of the title) so users can access the
    // manual in any build mode.
    {
        QFont titleFont;
        titleFont.setBold(true);
        titleFont.setPixelSize(18);
        p.setFont(titleFont);
        QFontMetrics tfm(titleFont);
        // Size the boxes from the longer of the two labels so the Manual
        // button has a consistent width whether or not the title is shown.
        QString refLabel = "Fourier Analyzer";
        int tw = tfm.horizontalAdvance(refLabel);
        int th = tfm.height();
        int pad = 8;
        int tx = (width() - tw) / 2 - pad;
        int ty = 4;
        int boxW = tw + 2 * pad;
        int boxH = th + 2 * pad;

        QRect manualRect;
        if (isWindow()) {
            QRect titleRect(tx, ty, boxW, boxH);
            m_titleRect = titleRect;

            p.setPen(Qt::NoPen);
            p.setBrush(QColor(75, 75, 75));
            p.drawRect(titleRect);

            p.setPen(QPen(Qt::white, 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(titleRect);

            p.drawText(titleRect, Qt::AlignCenter, "Fourier Analyzer");

            // Manual button sits just below the title with a small gap.
            manualRect = QRect(titleRect.x(), titleRect.bottom() + 8,
                               titleRect.width(), titleRect.height());
        } else {
            m_titleRect = QRect();
            manualRect = QRect(tx, ty, boxW, boxH);
        }

        m_manualRect = manualRect;

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(75, 75, 75));
        p.drawRect(manualRect);

        p.setPen(QPen(Qt::white, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(manualRect);

        p.drawText(manualRect, Qt::AlignCenter, "Help");
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
            // Build blue fill polygon manually (no clipping/intersection needed)
            double progX = ax + arrowW * m_fftProgress;
            QPainterPath bluePath;
            // Left edge (rounded corners)
            bluePath.moveTo(ax + radius, ay);
            if (progX <= ax + radius) {
                // Progress within left rounded corner — just a sliver
                bluePath.lineTo(progX, ay);
                bluePath.lineTo(progX, ay + arrowH);
                bluePath.lineTo(ax + radius, ay + arrowH);
            } else if (progX <= ax + bodyW) {
                // Progress within body rectangle
                bluePath.lineTo(progX, ay);
                bluePath.lineTo(progX, ay + arrowH);
                bluePath.lineTo(ax + radius, ay + arrowH);
            } else {
                // Progress extends into arrowhead
                bluePath.lineTo(ax + bodyW, ay);
                double headFrac = (progX - (ax + bodyW)) / headW;
                double tipY = ay + arrowH / 2.0;
                double topEdge = ay - arrowH * 0.15;
                double botEdge = ay + arrowH + arrowH * 0.15;
                double yt = topEdge + (tipY - topEdge) * headFrac;
                double yb = botEdge + (tipY - botEdge) * headFrac;
                bluePath.lineTo(ax + bodyW, topEdge);
                bluePath.lineTo(progX, yt);
                bluePath.lineTo(progX, yb);
                bluePath.lineTo(ax + bodyW, botEdge);
                bluePath.lineTo(ax + bodyW, ay + arrowH);
                bluePath.lineTo(ax + radius, ay + arrowH);
            }
            bluePath.arcTo(ax, ay + arrowH - 2 * radius, 2 * radius, 2 * radius, -90, -90);
            bluePath.lineTo(ax, ay + radius);
            bluePath.arcTo(ax, ay, 2 * radius, 2 * radius, 180, -90);
            bluePath.closeSubpath();
            p.setBrush(QColor(40, 100, 220, 180));
            p.setPen(Qt::NoPen);
            p.drawPath(bluePath);
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
            // Build blue fill polygon manually, filling from right to left
            double progX = ax + arrowW - arrowW * m_iftProgress;  // left edge of fill
            QPainterPath bluePath;
            // Right edge (rounded corners)
            bluePath.moveTo(ax + arrowW - radius, ay);
            if (progX >= ax + arrowW - radius) {
                // Progress within right rounded corner — just a sliver
                bluePath.lineTo(ax + arrowW - radius, ay + arrowH);
                bluePath.lineTo(progX, ay + arrowH);
                bluePath.lineTo(progX, ay);
            } else if (progX >= ax + headW) {
                // Progress within body rectangle
                bluePath.lineTo(ax + arrowW - radius, ay + arrowH);
                bluePath.lineTo(progX, ay + arrowH);
                bluePath.lineTo(progX, ay);
            } else {
                // Progress extends into arrowhead (left side)
                bluePath.lineTo(ax + arrowW - radius, ay + arrowH);
                bluePath.lineTo(ax + headW, ay + arrowH);
                double headFrac = ((ax + headW) - progX) / headW;
                double tipY = ay + arrowH / 2.0;
                double topEdge = ay - arrowH * 0.15;
                double botEdge = ay + arrowH + arrowH * 0.15;
                double yb = botEdge + (tipY - botEdge) * headFrac;
                double yt = topEdge + (tipY - topEdge) * headFrac;
                bluePath.lineTo(ax + headW, botEdge);
                bluePath.lineTo(progX, yb);
                bluePath.lineTo(progX, yt);
                bluePath.lineTo(ax + headW, topEdge);
                bluePath.lineTo(ax + headW, ay);
            }
            bluePath.arcTo(ax + arrowW - 2 * radius, ay, 2 * radius, 2 * radius, 90, -90);
            bluePath.lineTo(ax + arrowW, ay + arrowH - radius);
            bluePath.arcTo(ax + arrowW - 2 * radius, ay + arrowH - 2 * radius, 2 * radius, 2 * radius, 0, -90);
            // Close back to start — go along bottom then up right side
            bluePath.closeSubpath();
            p.setBrush(QColor(40, 100, 220, 180));
            p.setPen(Qt::NoPen);
            p.drawPath(bluePath);
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

    // Render the function-button tooltip last so it always appears on top of
    // the image / Fourier display, rather than being painted over by them.
    if (!pendingTipText.isEmpty()) {
        QFont ttf; ttf.setPixelSize(11);
        p.setFont(ttf);
        QFontMetrics ttfm(ttf);
        p.setPen(QPen(Qt::white, 1));
        p.setBrush(QColor(40, 40, 40));
        p.drawRect(pendingTipRect);
        p.drawText(pendingTipRect.x() + 4,
                   pendingTipRect.y() + 2 + ttfm.ascent(),
                   pendingTipText);
    }
}

// ---------------------------------------------------------------------------
//  Draw cross-section lines on Fourier transform panel
// ---------------------------------------------------------------------------
void FtWindow::drawCrossSectionLines(QPainter &p, const QRect &screenRect,
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

    double angle = m_crossSectionAngle * M_PI / 180.0;
    double dirX = std::cos(angle);
    double dirY = std::sin(angle);
    double normX = -std::sin(angle);
    double normY =  std::cos(angle);

    bool okW = false;
    double widthRecip = m_crossSectionWidthEdit->text().toDouble(&okW);
    if (!okW || widthRecip < 1.0) widthRecip = 1.0;   // minimum 1 reciprocal pixel
    double separation = widthRecip / 2.0;  // half-width of the band in reciprocal pixels
    double widthPx1 = separation * scaleX;
    double widthPx2 = separation * scaleY;
    double widthPx = std::min(widthPx1, widthPx2);

    auto clipLine = [&](double shiftNorm) {
        double x0 = scrCx + shiftNorm * normX;
        double y0 = scrCy + shiftNorm * normY;
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

    p.save();
    p.setClipRect(screenRect);
    p.setRenderHint(QPainter::Antialiasing, true);

    QLineF upper = clipLine(widthPx);
    QLineF lower = clipLine(-widthPx);

    p.setPen(QPen(Qt::red, 2));
    if (upper.length() > 0) p.drawLine(upper);
    if (lower.length() > 0) p.drawLine(lower);

    p.restore();
}

// ---------------------------------------------------------------------------
//  Compute cross-section profile by integrating power spectrum between lines
// ---------------------------------------------------------------------------
void FtWindow::computeCrossSectionProfile()
{
    if (!m_ftComputed || m_powerVals.empty() || m_fftN == 0) {
        m_crossSectionProfile.clear();
        m_crossSectionPhaseProfile.clear();
        m_crossSectionValid.clear();
        return;
    }

    int N = m_fftN;
    double halfN = N / 2.0;
    double imgCenter = halfN + 0.5;
    double angle = m_crossSectionAngle * M_PI / 180.0;
    double dirX = std::cos(angle);
    double dirY = std::sin(angle);
    double normX = -std::sin(angle);
    double normY =  std::cos(angle);
    bool okW = false;
    double widthRecip = m_crossSectionWidthEdit->text().toDouble(&okW);
    if (!okW || widthRecip < 1.0) widthRecip = 1.0;   // minimum 1 reciprocal pixel
    double separation = widthRecip / 2.0;  // half-width of the band in reciprocal pixels

    // Always use the maximum diagonal extent: halfN * sqrt(2)
    double maxProj = halfN * std::sqrt(2.0);
    double projMin = -maxProj;
    double projMax =  maxProj;

    // Profile length: one bin per pixel of projection
    int profileLen = (int)std::ceil(projMax - projMin) + 1;
    if (profileLen < 2) profileLen = 2;

    std::vector<double> profile(profileLen, 0.0);
    std::vector<double> sumRe(profileLen, 0.0);   // complex accumulator for phase
    std::vector<double> sumIm(profileLen, 0.0);
    std::vector<int> counts(profileLen, 0);

    // Center index maps to projection distance 0
    int centerIdx = (int)std::round(-projMin);
    if (centerIdx < 0) centerIdx = 0;
    if (centerIdx >= profileLen) centerIdx = profileLen - 1;

    bool haveComplex = (m_cosVals.size() == (size_t)N * N
                        && m_sinVals.size() == (size_t)N * N);

    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            double dx = x - imgCenter + 0.5;
            double dy = y - imgCenter + 0.5;

            // Distance from the center line (perpendicular)
            double perpDist = dx * normX + dy * normY;
            if (std::abs(perpDist) > separation) continue;

            // Projection along line direction
            double projDist = dx * dirX + dy * dirY;

            // Map projection to profile index
            int idx = (int)std::round(projDist - projMin);
            if (idx < 0 || idx >= profileLen) continue;

            profile[idx] += m_powerVals[y * N + x];
            if (haveComplex) {
                sumRe[idx] += m_cosVals[y * N + x];   // real part of FT
                sumIm[idx] += m_sinVals[y * N + x];   // imag part of FT
            }
            counts[idx]++;
        }
    }

    // Average amplitude where we have counts; phase = arg of complex sum.
    std::vector<double> phase(profileLen, 0.0);
    std::vector<bool> valid(profileLen, false);
    for (int i = 0; i < profileLen; i++) {
        if (counts[i] > 0) {
            profile[i] /= counts[i];
            if (haveComplex) phase[i] = std::atan2(sumIm[i], sumRe[i]);
            valid[i] = true;
        }
    }

    m_crossSectionProfile = std::move(profile);
    m_crossSectionPhaseProfile = std::move(phase);
    m_crossSectionValid = std::move(valid);
    m_crossSectionCenter = centerIdx;
    m_crossSectionProjMin = projMin;
    m_crossSectionProjMax = projMax;
}

// ---------------------------------------------------------------------------
//  Refresh the direction edit from the internal (screen-convention) angle.
//  The displayed value uses the math convention (CCW from +x, y-up), so it is
//  the negation of m_crossSectionAngle. Signals are blocked to avoid feeding
//  the textChanged handler back into a recompute.
// ---------------------------------------------------------------------------
void FtWindow::syncCrossSectionDirEdit()
{
    if (!m_crossSectionDirEdit) return;
    double disp = -m_crossSectionAngle;
    while (disp <= -180.0) disp += 360.0;
    while (disp > 180.0)   disp -= 360.0;
    QSignalBlocker block(m_crossSectionDirEdit);
    m_crossSectionDirEdit->setText(QString::number(disp, 'f', 1));
}

// ---------------------------------------------------------------------------
//  Draw the red CTF direction line on the Fourier-transform display
// ---------------------------------------------------------------------------
void FtWindow::drawCtfDirectionLine(QPainter &p, const QRect &screenRect,
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

    // Direction angle is measured CCW from +x in math convention; screen y
    // points downward, so flip the y component for drawing.
    double a = m_ctfAngleDeg * M_PI / 180.0;
    double dirX =  std::cos(a);
    double dirY = -std::sin(a);

    // Extend the ray out to the edge of the FFT square (image coords ±N/2).
    double halfN = N / 2.0;
    double tX = (dirX > 0) ?  halfN / dirX : (dirX < 0 ? -halfN / dirX : 1e18);
    double tY = (dirY > 0) ?  halfN / dirY : (dirY < 0 ? -halfN / dirY : 1e18);
    double tEdge = std::min(tX, tY);
    double endXimg = dirX * tEdge;
    double endYimg = dirY * tEdge;

    double endScrX = scrCx + endXimg * scaleX;
    double endScrY = scrCy + endYimg * scaleY;

    p.save();
    p.setClipRect(screenRect);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(220, 50, 50), 2));
    p.drawLine(QPointF(scrCx, scrCy), QPointF(endScrX, endScrY));
    // Small handle at the endpoint to make it clearly draggable.
    p.setBrush(QColor(220, 50, 50));
    p.drawEllipse(QPointF(endScrX, endScrY), 4.0, 4.0);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.restore();
}

// ---------------------------------------------------------------------------
//  Maximized (display-only) image view
// ---------------------------------------------------------------------------
// Entered by clicking the maximize icon under a panel's Zoom/Pan overlay. The
// chosen image fills the whole widget, covering the normal layout. Zoom and
// pan keep working (wheel, pinch, drag) because the maximized frames are
// registered as DisplayItems just like the normal panels; nothing else is.
// ESC returns to the normal layout.
void FtWindow::enterMaximized(int panel)
{
    if (panel == 1 && m_image.isNull())   return;
    if (panel == 2 && !m_ftComputed)      return;
    if (m_maxPanel == panel)              return;

    // Child widgets are real QWidgets and would keep painting over the
    // maximized image, so hide the visible ones and remember which they were.
    m_maxHiddenWidgets.clear();
    for (QObject *o : children()) {
        auto *w = qobject_cast<QWidget *>(o);
        if (w && w->isVisible()) {
            m_maxHiddenWidgets.append(w);
            w->hide();
        }
    }

    m_maxPanel = panel;

    // Take the top-level window truly fullscreen so no title bar remains — the
    // maximized view is meant to fill the whole screen with the image.
#ifdef __EMSCRIPTEN__
    installFullscreenSync();
    m_maxDidFullScreen = EM_ASM_INT({
        var fs = document.fullscreenElement ||
                 document.webkitFullscreenElement ||
                 document.webkitCurrentFullScreenElement;
        if (fs) return 0;   // already fullscreen — leave it be
        var target = document.getElementById('screen') || document.documentElement;
        var req = target.requestFullscreen ||
                  target.webkitRequestFullscreen ||
                  target.webkitRequestFullScreen;
        if (!req) return 0;
        try {
            var p = req.call(target);
            if (p && p.then) p.then(null, function(e) {});
        } catch (e) { return 0; }
        return 1;
    });
#else
    m_maxDidFullScreen = false;
    if (QWidget *top = window()) {
        if (!top->isFullScreen()) {
            m_maxPrevWindowState = top->windowState();
            m_maxDidFullScreen = true;
            top->showFullScreen();
        }
    }
#endif

    setFocus(Qt::OtherFocusReason);   // so ESC reaches keyPressEvent
    update();
}

void FtWindow::exitMaximized()
{
    if (m_maxPanel == 0) return;
    m_maxPanel = 0;
    m_maxCloseRect = QRect();

    // Restore the window if it was us that took it fullscreen on the way in.
    if (m_maxDidFullScreen) {
        m_maxDidFullScreen = false;
#ifdef __EMSCRIPTEN__
        EM_ASM({
            var exit = document.exitFullscreen ||
                       document.webkitExitFullscreen ||
                       document.webkitCancelFullScreen;
            if (exit) exit.call(document);
        });
#else
        if (QWidget *top = window())
            top->setWindowState(m_maxPrevWindowState);
#endif
    }

    for (const QPointer<QWidget> &w : m_maxHiddenWidgets)
        if (w) w->show();
    m_maxHiddenWidgets.clear();
    update();
}

QRectF FtWindow::itemSrcRect(const DisplayItem &di) const
{
    const ZoomState &z = m_zoom[di.zoomIdx];
    if (m_maxPanel == 0 || di.screenRect.height() <= 0)
        return z.visibleRect(di.imgW, di.imgH);
    return z.visibleRect(di.imgW, di.imgH,
                         di.screenRect.width() / (double)di.screenRect.height());
}

void FtWindow::paintMaximized(QPainter &p)
{
    m_numDispItems = 0;
    p.fillRect(rect(), Qt::black);

    // Fill `area` with the image and register it so the zoom/pan handlers in
    // ftwindow_mouse.cpp find it. The item owns the whole area rather than a
    // square inside it: unzoomed, the image is letterboxed as before, but a
    // zoomed image widens into the full area instead of staying boxed in.
    auto place = [&](const QRect &area, const QImage &img, int zoomIdx,
                     int imgW, int imgH, const std::vector<double> *vals) {
        if (img.isNull() || imgW <= 0 || imgH <= 0) return;
        if (m_numDispItems >= MAX_DISP) return;
        if (area.width() <= 0 || area.height() <= 0) return;

        DisplayItem &di = m_dispItems[m_numDispItems++];
        di = { area, imgW, imgH, zoomIdx, vals, true };

        // The source rect may reach past the image (that surplus is what
        // becomes the black letterbox), so draw only the part that exists and
        // map it to the matching slice of the area.
        QRectF src = itemSrcRect(di);
        QRectF have = src.intersected(QRectF(0, 0, imgW, imgH));
        if (have.isEmpty()) return;
        double sx = area.width()  / src.width();
        double sy = area.height() / src.height();
        QRectF target(area.x() + (have.x() - src.x()) * sx,
                      area.y() + (have.y() - src.y()) * sy,
                      have.width() * sx, have.height() * sy);
        p.drawImage(target, img, have);
    };

    const int margin = 8;
    QRect area = rect().adjusted(margin, margin, -margin, -margin);

    if (m_maxPanel == 1) {
        place(area, m_image, 0, m_image.width(), m_image.height(),
              &m_imageRawPixels);
    } else if (m_displayMode == 2 || m_displayMode == 3) {
        const QImage &img = (m_displayMode == 3) ? m_powerImg : m_complexImg;
        place(area, img, 1, m_fftN, m_fftN, &m_powerVals);
    } else {
        // Two-image Fourier displays (cos/sin, amplitude/phase) occupy the
        // left and right halves, separated by a black divider down the middle.
        const QImage *img1 = (m_displayMode == 0) ? &m_cosImg  : &m_ampImg;
        const QImage *img2 = (m_displayMode == 0) ? &m_sinImg  : &m_phaseImg;
        const std::vector<double> *vals1 =
            (m_displayMode == 0) ? &m_cosVals : &m_ampVals;
        const std::vector<double> *vals2 =
            (m_displayMode == 0) ? &m_sinVals : &m_phaseVals;

        const int divider = 4;
        int halfW = (area.width() - divider) / 2;
        QRect left(area.x(), area.y(), halfW, area.height());
        QRect right(area.x() + halfW + divider, area.y(), halfW, area.height());

        place(left,  *img1, 1, m_fftN, m_fftN, vals1);
        place(right, *img2, 2, m_fftN, m_fftN, vals2);

        p.fillRect(QRect(area.x() + halfW, rect().top(), divider, height()),
                   Qt::black);
    }

    // Scale bar, bottom-right corner. Its length is a "nice" round physical
    // distance spanning roughly 1/14 of the screen width (kept within about
    // 1/20..1/10). White bar and label sit over a faint black shadow so they
    // stay legible on a bright image. Panel 1 is real space (nm); the Fourier
    // panel is reciprocal space (1/Å). Pixel size is stored in Ångström.
    if (m_numDispItems > 0 && m_pixelSize > 0.0) {
        const DisplayItem &di = m_dispItems[m_numDispItems - 1];
        QRectF src = itemSrcRect(di);
        if (src.width() > 0.0 && di.screenRect.width() > 0 && di.imgW > 0) {
            const double imgPxPerScreenPx = src.width() / di.screenRect.width();
            const double targetScreenLen  = width() / 14.0;
            const bool   reciprocal       = (m_maxPanel != 1);

            // Physical length one screen pixel spans, and the unit to print.
            double  physPerScreenPx;
            QString unit;
            if (!reciprocal) {
                // Real space: pixel size in Å, reported in nm (1 nm = 10 Å).
                physPerScreenPx = imgPxPerScreenPx * m_pixelSize / 10.0;
                unit = "nm";
            } else {
                // Reciprocal space: one FFT pixel = 1/(N·pixelSize) in 1/Å.
                physPerScreenPx = imgPxPerScreenPx / (di.imgW * m_pixelSize);
                unit = QString::fromUtf8("1/Å");
            }

            const double rawPhys = physPerScreenPx * targetScreenLen;
            if (rawPhys > 0.0 && std::isfinite(rawPhys)) {
                // Round to the nearest 1 / 2 / 5 × 10^k.
                double e    = std::floor(std::log10(rawPhys));
                double base = std::pow(10.0, e);
                double f    = rawPhys / base;
                double nf   = (f < 1.5) ? 1.0 : (f < 3.5) ? 2.0 : (f < 7.5) ? 5.0 : 10.0;
                double nicePhys = nf * base;
                double barLen   = nicePhys / physPerScreenPx;   // screen pixels

                if (barLen > 4.0 && barLen < width()) {
                    const int iBarLen = (int)std::lround(barLen);
                    const int barH = std::max(4, height() / 200);
                    const int pad  = std::max(14, width() / 90);
                    const int bx   = width()  - pad - iBarLen;
                    const int by   = height() - pad - barH;

                    const QString label =
                        QString("%1 %2").arg(QString::number(nicePhys, 'g', 3), unit);
                    QFont sf; sf.setPixelSize(std::max(12, height() / 45)); sf.setBold(true);
                    p.setFont(sf);
                    QFontMetrics sfm(sf);
                    const int tw = sfm.horizontalAdvance(label);
                    const int tx = bx + iBarLen - tw;   // label right-aligned to the bar
                    const int ty = by - 6;              // baseline just above the bar

                    p.save();
                    // Faint black shadow underlaid behind the bar (a soft halo).
                    p.setRenderHint(QPainter::Antialiasing, false);
                    p.setPen(Qt::NoPen);
                    p.setBrush(QColor(0, 0, 0, 130));
                    p.drawRect(bx - 2, by - 2, iBarLen + 4, barH + 4);
                    // Shadow for the text (drawn slightly offset in four directions).
                    p.setRenderHint(QPainter::Antialiasing, true);
                    p.setPen(QColor(0, 0, 0, 150));
                    for (const QPoint &d : { QPoint(1, 1), QPoint(-1, 1),
                                             QPoint(1, -1), QPoint(-1, -1) })
                        p.drawText(tx + d.x(), ty + d.y(), label);
                    // White bar and label on top.
                    p.setRenderHint(QPainter::Antialiasing, false);
                    p.fillRect(QRect(bx, by, iBarLen, barH), Qt::white);
                    p.setRenderHint(QPainter::Antialiasing, true);
                    p.setPen(Qt::white);
                    p.drawText(tx, ty, label);

                    // When the pixel size is only assumed, the whole scale is
                    // conditional — note that under the bar so it is not read as
                    // a calibrated distance.
                    if (m_pixelSizeAssumed) {
                        const QString note = QString::fromUtf8("(if 1px = 1Å)");
                        QFont nf; nf.setPixelSize(std::max(10, height() / 65));
                        p.setFont(nf);
                        QFontMetrics nfm(nf);
                        const int ntw = nfm.horizontalAdvance(note);
                        const int ntx = bx + iBarLen - ntw;          // right-aligned to bar
                        const int nty = by + barH + 2 + nfm.ascent(); // just below the bar
                        p.setPen(QColor(0, 0, 0, 150));
                        for (const QPoint &d : { QPoint(1, 1), QPoint(-1, 1),
                                                 QPoint(1, -1), QPoint(-1, -1) })
                            p.drawText(ntx + d.x(), nty + d.y(), note);
                        p.setPen(Qt::white);
                        p.drawText(ntx, nty, note);
                    }
                    p.restore();
                }
            }
        }
    }

    // Close button, top right. This is the only way out on a touch device —
    // iPad has no ESC key — so it is sized as a comfortable tap target and
    // drawn over its own backing so it stays legible on top of any image.
    const int closeSide = 44;
    m_maxCloseRect = QRect(width() - closeSide - margin, margin,
                           closeSide, closeSide);
    {
        const bool hover = m_maxCloseRect.contains(m_mousePos);
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(200, 200, 200), 1));
        p.setBrush(QColor(0, 0, 0, hover ? 220 : 160));
        p.drawRoundedRect(m_maxCloseRect, 6, 6);

        QRect x = m_maxCloseRect.adjusted(13, 13, -13, -13);
        p.setPen(QPen(hover ? Qt::white : QColor(210, 210, 210), 2.5));
        p.drawLine(x.topLeft(), x.bottomRight());
        p.drawLine(x.topRight(), x.bottomLeft());
        p.restore();
    }

    // Hint along the bottom. It sits on its own dark pill: the image may run
    // all the way to the edge, and plain grey text on top of it is unreadable.
    {
        QFont hf; hf.setPixelSize(13); p.setFont(hf);
        QFontMetrics hfm(hf);
        const QString hint =
            "Tap ✕ (top right) or press ESC to leave the maximized view";
        int tw = hfm.horizontalAdvance(hint);
        int th = hfm.height();
        QRect pill((width() - tw) / 2 - 10, height() - th - 18,
                   tw + 20, th + 8);
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 170));
        p.drawRoundedRect(pill, 5, 5);
        p.setPen(QColor(215, 215, 215));
        p.drawText(pill, Qt::AlignCenter, hint);
        p.restore();
    }
}
