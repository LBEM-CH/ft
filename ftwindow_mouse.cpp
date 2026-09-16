#include "ftwindow_common.h"
#include "helptheme.h"

#include <QHBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QTextDocument>

// QImage::mirrored() was deprecated in Qt 6.9 in favour of flipped(). The
// native desktop kit is newer, but the WebAssembly kit is still on Qt 6.8
// (which has no flipped()), so wrap both behind a version check.
static QImage flipImage(const QImage &img, Qt::Orientations dir)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    return img.flipped(dir);
#else
    return img.mirrored(dir.testFlag(Qt::Horizontal), dir.testFlag(Qt::Vertical));
#endif
}

// ---------------------------------------------------------------------------
//  Grouped tool buttons
// ---------------------------------------------------------------------------
//  The many individual tool squares along each panel edge are organised into a
//  small number of group squares. A single-member group activates its tool
//  directly; a multi-member group opens a floating popup grid of its members.
//  Tool ids are the original per-function indices (see the icon-drawing loops
//  in ftwindow_paint.cpp); the group lists below map ids into groups.
void FtWindow::buildToolGroups()
{
    // The trailing `true` marks a group as advanced-only: it is dropped below
    // when the user level is Basic, so it takes no slot in the column and none
    // of its tools can be reached.
    m_p1Groups = {
        { "Edit",        {0, 1, 8, 25}, {} }, // eraser, paint brush, taper edges, threshold
        { "Measure",     {2},           {} },
        { "Transform",   {3, 4, 5, 6, 22, 7, 9}, {} }, // flip H/V, shift, rotate, shear, invert, symmetrize
        { "Redimension", {10, 19, 11},  {} }, // bin, pad, crop
        { "Filter",      {12, 13, 23},  {}, true }, // gabor, hessian, hough
        { "Amyloid",     {14},          {}, true },
        { "Math",        {15, 21},      {} }, // math calculations, average images
        { "Particles",   {16, 17, 24},  {}, true }, // peak, extract, average tiles
        // Single-member group: the group face is what the user hovers, so the
        // group name is the tooltip that shows (it overrides the icon's own).
        { "Align image to reference", {18}, {}, true },
    };
    m_p2Groups = {
        { "Edit",                  {0, 1},      {} }, // eraser, paint brush
        { "Cross-section profile", {7},         {} },
        { "Filter",                {2, 3, 4, 5}, {} }, // bandpass, directional, line, lattice
        { "Transform",             {6, 8},      {} }, // rotate, symmetrize
        { "Redimension",           {9},         {} }, // Fourier crop / pad
        { "Ramp",                  {10},        {} }, // phase ramp
        { "CTF",                   {11, 12}, "CTF", true }, // CTF SIM, CTF FIT (face = "CTF")
        { "Math",                  {13},        {} },
    };

    if (!m_advancedLevel) {
        auto dropAdvanced = [](QVector<ToolGroup> &groups) {
            groups.erase(std::remove_if(groups.begin(), groups.end(),
                                        [](const ToolGroup &g) { return g.advanced; }),
                         groups.end());
        };
        dropAdvanced(m_p1Groups);
        dropAdvanced(m_p2Groups);
    }
}

// Compute, for every tool id, the on-screen rect of the slot it occupies and
// whether it is currently visible. Runs each paint; the mouse handler reads the
// results (paint always precedes any click).
void FtWindow::layoutToolSlots()
{
    int hy = height() - height() / 5;
    int gap = 2;
    // Half again the size the squares used to be — the icons in them are drawn
    // relative to the square, so they grow with it.
    int btnSide = std::max(width() * 5 / 400, 20) * 3 / 2;
    // …but never taller than the column of groups has room for. At this size a
    // short window would otherwise push the squares off the top of the panel,
    // taking the close button — half a square above them again — with them. The
    // longer of the two panels' group lists sets the limit, since both columns
    // share this one size, and the +3 leaves the close button its space.
    const int nGroupsMax = std::max(m_p1Groups.size(), m_p2Groups.size());
    if (nGroupsMax > 0) {
        int fits = (hy - (nGroupsMax - 1) * gap) / (nGroupsMax + 3);
        btnSide = std::max(8, std::min(btnSide, fits));
    }
    int offset = btnSide / 2;
    m_toolBtnSide = btnSide;
    m_toolBtnGap = gap;
    m_toolBtnOffset = offset;

    // The group squares themselves, plus the close button. Split out from the
    // member placement below because the hover preview has to know where the
    // squares are before the members can be placed: which group is previewed
    // depends on which square the mouse is over.
    auto layoutGroupFaces = [&](int panel) {
        const QVector<ToolGroup> &groups = (panel == 1) ? m_p1Groups : m_p2Groups;
        QRect *groupRects = (panel == 1) ? m_p1GroupRects : m_p2GroupRects;
        int nG = groups.size();
        int totalH = nG * btnSide + (nG - 1) * gap;
        int startY = (hy - totalH) / 2;
        int gx = (panel == 1) ? offset : (width() - btnSide - offset);

        for (int g = 0; g < nG; g++)
            groupRects[g] = QRect(gx, startY + g * (btnSide + gap), btnSide, btnSide);

        // Close button: same size as a function button, sitting half a square
        // above the column. Only offered while this panel has a function open.
        bool funcOpen = (panel == 1) ? p1FunctionOpen() : p2FunctionOpen();
        QRect &closeRect = (panel == 1) ? m_p1CloseRect : m_p2CloseRect;
        closeRect = funcOpen ? QRect(gx, startY - btnSide - btnSide / 2, btnSide, btnSide)
                             : QRect();
    };
    layoutGroupFaces(1);
    layoutGroupFaces(2);

    // Reads the popup rect from the previous layout to tell whether the pointer
    // is resting on the previewed row, so the rects are only cleared afterwards.
    updateGroupHoverPreview();
    m_p1PopupRect = QRect();
    m_p2PopupRect = QRect();

    auto layoutPanel = [&](int panel) {
        const QVector<ToolGroup> &groups = (panel == 1) ? m_p1Groups : m_p2Groups;
        const QRect *groupRects = (panel == 1) ? m_p1GroupRects : m_p2GroupRects;
        QRect *slotRects  = (panel == 1) ? m_p1BtnRects   : m_toolBtnRects;
        bool  *slotVis    = (panel == 1) ? m_p1SlotVisible : m_p2SlotVisible;
        int nTools = (panel == 1) ? P1_TOOL_BUTTONS : P2_TOOL_BUTTONS;
        for (int i = 0; i < nTools; i++) { slotVis[i] = false; slotRects[i] = QRect(); }

        for (int g = 0; g < groups.size(); g++) {
            const QRect &G = groupRects[g];
            const QVector<int> &mem = groups[g].members;

            bool open    = (m_openMenuPanel  == panel && m_openMenuGroup  == g);
            bool preview = (m_hoverMenuPanel == panel && m_hoverMenuGroup == g);
            if ((open || preview) && mem.size() > 1) {
                // Members fan out into a single floating vertical column immediately
                // beside the group square and level with it, so the first member's
                // icon touches the square: reaching it is one short sideways move,
                // with no diagonal and no gap to fall out of. The square itself is
                // left empty (drawn as a highlighted anchor in paint), and the
                // group's mouse-over text is written over that first icon rather
                // than between the two — see groupTipRect(). The text therefore
                // costs the column no room, which is what lets it sit this high;
                // hanging it below the text pushed the icons clear of the square
                // on a short window, where they could not be reached at all.
                //
                // One column wide, and deliberately so: the icons are painted
                // before the image displays are, so anything reaching past the
                // narrow gutter beside the button column disappears behind panel
                // 1 / panel 2. A horizontal row of six would.
                int n = mem.size();
                int panelW = btnSide;
                int panelH = n * btnSide + (n - 1) * gap;
                int px = (panel == 1) ? (G.right() + 1) : (G.left() - 1 - panelW);
                int py = G.top();
                // Keep the column on screen. If it will not fit below the square,
                // it grows upwards from the square's foot instead of being pushed
                // off it — sliding it up the window would break the very adjacency
                // the layout is built around. Only a very short window gets here.
                if (py + panelH > height()) py = G.bottom() + 1 - panelH;
                if (py < 0) py = 0;
                if (px + panelW > width()) px = width() - panelW;
                if (px < 0) px = 0;
                QRect popup(px, py, panelW, panelH);
                if (panel == 1) m_p1PopupRect = popup; else m_p2PopupRect = popup;
                for (int k = 0; k < n; k++) {
                    QRect cell(px, py + k * (btnSide + gap), btnSide, btnSide);
                    slotRects[mem[k]] = cell;
                    slotVis[mem[k]] = true;
                }
            } else if (groups[g].faceText.isEmpty()) {
                slotRects[mem[0]] = G;   // collapsed: face shows first member
                slotVis[mem[0]] = true;
            }
            // else: collapsed group with a custom text face — no member icon is
            // drawn here; the face is rendered in paint (drawGroupExtras).
        }
    };
    layoutPanel(1);
    layoutPanel(2);
}

QRect FtWindow::groupTipRect(int panel, int g) const
{
    const QVector<ToolGroup> &groups = (panel == 1) ? m_p1Groups : m_p2Groups;
    if (g < 0 || g >= groups.size()) return QRect();
    const QRect &G = (panel == 1) ? m_p1GroupRects[g] : m_p2GroupRects[g];
    if (G.isNull()) return QRect();
    QFont ttf; ttf.setPixelSize(11);
    QFontMetrics ttfm(ttf);
    int ttw = ttfm.horizontalAdvance(groups[g].name) + 8;
    int tth = ttfm.height() + 4;

    // While this group's members are previewed, the text is written over the
    // first member's icon instead of beside the group square. That way it takes
    // no room of its own, which is what lets the column sit tight against the
    // square: put the text between them and the icons are pushed away from the
    // square by its whole height, far enough on a short window that the pointer
    // cannot get across. The icon it covers is the one the pointer is on its way
    // to; arriving there replaces this text with that tool's own.
    const QRect &popup = (panel == 1) ? m_p1PopupRect : m_p2PopupRect;
    if (m_hoverMenuPanel == panel && m_hoverMenuGroup == g && !popup.isNull()) {
        QRect first(popup.left(), popup.top(), m_toolBtnSide, m_toolBtnSide);
        int ttx = (panel == 1) ? first.left() : (first.right() + 1 - ttw);
        int tty = first.center().y() - tth / 2;
        return QRect(ttx, tty, ttw, tth);
    }

    int ttx = (panel == 1) ? (G.right() + 4) : (G.left() - ttw - 4);
    int tty = G.center().y() - tth / 2;
    return QRect(ttx, tty, ttw, tth);
}

// Which group, if any, is showing its contents on hover. Called from
// layoutToolSlots() on every paint, and mouse moves repaint, so the preview
// follows the pointer without any state of its own to keep in step.
void FtWindow::updateGroupHoverPreview()
{
    // A clicked-open menu owns the space beside the column while it is up; a
    // second popup there would only fight with it.
    if (m_openMenuPanel != 0) {
        m_hoverMenuPanel = 0;
        m_hoverMenuGroup = -1;
        return;
    }

    // A group square under the pointer wins, so that running down the column
    // hands the preview from one group to the next.
    for (int panel = 1; panel <= 2; panel++) {
        const QVector<ToolGroup> &groups = (panel == 1) ? m_p1Groups : m_p2Groups;
        const QRect *groupRects = (panel == 1) ? m_p1GroupRects : m_p2GroupRects;
        for (int g = 0; g < groups.size(); g++) {
            // A one-member group has nothing hidden to reveal: its square
            // already shows the only icon it has.
            if (groups[g].members.size() < 2) continue;
            if (!groupRects[g].contains(m_mousePos)) continue;
            m_hoverMenuPanel = panel;
            m_hoverMenuGroup = g;
            return;
        }
    }

    // Otherwise hold the current preview open while the pointer is on the icons
    // themselves: they sit outside the square that summoned them, and without
    // this they would vanish from under the pointer on the way over. The column
    // is laid out flush against the square, so there is nothing in between to
    // fall through.
    if (m_hoverMenuPanel != 0) {
        const QRect &popup = (m_hoverMenuPanel == 1) ? m_p1PopupRect : m_p2PopupRect;
        if (popup.contains(m_mousePos)) return;
    }

    m_hoverMenuPanel = 0;
    m_hoverMenuGroup = -1;
}

// A shear drag works like grabbing the image and pulling that one point
// sideways: the angle returned is the one that puts the grabbed point under the
// cursor. Which axis slides follows the direction dragged in — mostly sideways
// shears the rows, mostly up or down shears the columns.
bool FtWindow::shearFromDrag(const DisplayItem &di, const QPoint &from,
                             const QPoint &to, double &angleDeg,
                             bool &vertical) const
{
    if (!di.valid || di.screenRect.width() <= 0 || di.screenRect.height() <= 0
        || di.imgW <= 0 || di.imgH <= 0)
        return false;

    const QRectF src = m_zoom[0].visibleRect(di.imgW, di.imgH);
    auto imgX = [&](int sx) {
        return src.x() + (sx - di.screenRect.x())
               / (double)di.screenRect.width() * src.width();
    };
    auto imgY = [&](int sy) {
        return src.y() + (sy - di.screenRect.y())
               / (double)di.screenRect.height() * src.height();
    };

    const double gx = imgX(from.x()), gy = imgY(from.y());
    const double dx = imgX(to.x()) - gx, dy = imgY(to.y()) - gy;
    if (std::hypot(dx, dy) < 1.0) return false;   // a click, not a drag

    vertical = (std::abs(dy) > std::abs(dx));

    // Distance of the grabbed point from the centre line is the lever the drag
    // works on, so grabbing near the edge shears gently and grabbing further in
    // shears harder. On the line itself the lever would vanish and any drag at
    // all would ask for 90°, so it is held at a minimum: a drag started inside
    // that band behaves as though it had grabbed at the band's edge, on the side
    // it did start on.
    double lever = vertical ? (gx - di.imgW / 2.0) : (gy - di.imgH / 2.0);
    const double minLever = 0.15 * (vertical ? di.imgW : di.imgH);
    if (std::abs(lever) < minLever) lever = (lever < 0.0) ? -minLever : minLever;

    angleDeg = std::atan(-(vertical ? dy : dx) / lever) * 180.0 / M_PI;
    angleDeg = std::clamp(angleDeg, -SHEAR_MAX_DEG, SHEAR_MAX_DEG);
    return std::abs(angleDeg) >= 0.05;
}

void FtWindow::deactivateAllP1Tools()
{
    m_p1EraserActive = false; m_p1BrushActive = false;
    m_shiftActive = false; m_rotateActive = false; m_shearActive = false;
    m_p1TaperActive = false; m_p1SymmetrizeActive = false; m_threshActive = false;
    m_binActive = false; m_mathActive = false; m_padActive = false;
    m_copyActive = false; m_averageActive = false;
    m_cropActive = false; m_cropDragging = false; m_cropMoving = false; m_cropHasSelection = false;
    m_peakPickActive = false; m_extractActive = false; m_tileAvgActive = false;
    m_alignActive = false; clearAlignDiagnostics();
    m_gaborActive = false; m_hessianActive = false; m_houghActive = false;
    m_amyloidActive = false; m_amyloidPlacing = 0;
    m_measureActive = false; m_measurePlacing = 0; m_measureHasLine = false;
    m_p1FlipHActive = false; m_p1FlipVActive = false; m_p1InvertActive = false;
}

void FtWindow::showP1ToolWidgets()
{
    m_p1EraserDiameterEdit->setVisible(m_p1EraserActive);
    m_p1BrushValueEdit->setVisible(m_p1BrushActive);
    m_p1BrushSolidDiameterEdit->setVisible(m_p1BrushActive);
    m_p1BrushDiameterEdit->setVisible(m_p1BrushActive);
    m_p1TaperWidthEdit->setVisible(m_p1TaperActive);
    m_applyP1TaperBtn->setVisible(m_p1TaperActive);
    m_p1SymmetryEdit->setVisible(m_p1SymmetrizeActive);
    m_applyP1SymmetryBtn->setVisible(m_p1SymmetrizeActive);
    m_threshModeCombo->setVisible(m_threshActive);
    m_threshMinEdit->setVisible(m_threshActive);
    m_threshMaxEdit->setVisible(m_threshActive);
    m_threshCancelBtn->setVisible(m_threshActive);
    m_threshComputeBtn->setVisible(m_threshActive);
    m_copySrcCombo->setVisible(m_copyActive);
    m_copyTgtCombo->setVisible(m_copyActive);
    m_copyCancelBtn->setVisible(m_copyActive);
    m_copyDuplicateBtn->setVisible(m_copyActive);
    m_averageTargetCombo->setVisible(m_averageActive);
    m_averageCancelBtn->setVisible(m_averageActive);
    m_averageComputeBtn->setVisible(m_averageActive);
    m_padSizeCombo->setVisible(m_padActive);
    m_padCustomEdit->setVisible(m_padActive);
    m_padCancelBtn->setVisible(m_padActive);
    m_applyPadBtn->setVisible(m_padActive);
    m_binCombo->setVisible(m_binActive);
    m_binKeepSizeBtn->setVisible(m_binActive);
    m_applyBinBtn->setVisible(m_binActive);
    m_cropTLxEdit->setVisible(m_cropActive);
    m_cropTLyEdit->setVisible(m_cropActive);
    m_cropBRxEdit->setVisible(m_cropActive);
    m_cropBRyEdit->setVisible(m_cropActive);
    m_cropCancelBtn->setVisible(m_cropActive);
    m_applyCropBtn->setVisible(m_cropActive);
    m_mathOutCombo->setVisible(m_mathActive);
    m_mathEqualsLabel->setVisible(m_mathActive);
    m_mathIn1Combo->setVisible(m_mathActive);
    m_mathOpCombo->setVisible(m_mathActive);
    m_mathIn2Combo->setVisible(m_mathActive);
    m_mathCancelBtn->setVisible(m_mathActive);
    m_mathComputeBtn->setVisible(m_mathActive);
    m_peakSourceCombo->setVisible(m_peakPickActive);
    m_peakThresholdSlider->setVisible(m_peakPickActive);
    m_peakThresholdLabel->setVisible(m_peakPickActive);
    m_peakExclLabel->setVisible(m_peakPickActive);
    m_peakExclRadiusSlider->setVisible(m_peakPickActive);
    m_peakCancelBtn->setVisible(m_peakPickActive);
    m_peakComputeBtn->setVisible(m_peakPickActive);
    m_peakShowPosBtn->setVisible(m_peakPickActive);
    bool showExtract = m_extractActive && !m_peaks.empty();
    m_extractSourceCombo->setVisible(showExtract);
    m_extractTargetCombo->setVisible(showExtract);
    m_extractSizeCombo->setVisible(showExtract);
    m_extractCancelBtn->setVisible(showExtract);
    m_extractComputeBtn->setVisible(showExtract);
    m_tileAvgSourceCombo->setVisible(m_tileAvgActive);
    m_tileAvgTargetCombo->setVisible(m_tileAvgActive);
    m_tileAvgSizeCombo->setVisible(m_tileAvgActive);
    m_tileAvgCancelBtn->setVisible(m_tileAvgActive);
    m_tileAvgComputeBtn->setVisible(m_tileAvgActive);
    m_gaborSigmaEdit->setVisible(m_gaborActive);
    m_gaborLambdaEdit->setVisible(m_gaborActive);
    m_gaborThetaEdit->setVisible(m_gaborActive);
    m_gaborGammaEdit->setVisible(m_gaborActive);
    m_gaborCancelBtn->setVisible(m_gaborActive);
    m_gaborComputeBtn->setVisible(m_gaborActive);
    m_hessianSigmaEdit->setVisible(m_hessianActive);
    m_hessianPolarityEdit->setVisible(m_hessianActive);
    m_hessianCancelBtn->setVisible(m_hessianActive);
    m_hessianComputeBtn->setVisible(m_hessianActive);
    m_houghSourceCombo->setVisible(m_houghActive);
    m_houghTargetCombo->setVisible(m_houghActive);
    m_houghElementCombo->setVisible(m_houghActive);
    m_houghRadiusSlider->setVisible(houghShowsRadiusSlider());
    m_houghInverseBtn->setVisible(m_houghActive);
    m_houghCancelBtn->setVisible(m_houghActive);
    m_houghComputeBtn->setVisible(m_houghActive);
    m_amyloidRiseEdit->setVisible(m_amyloidActive);
    m_amyloidTwistEdit->setVisible(m_amyloidActive);
    m_amyloidMapCombo->setVisible(m_amyloidActive);
    m_amyloidSizeCombo->setVisible(m_amyloidActive);
    m_amyloidNoiseBtn->setVisible(m_amyloidActive);
    m_amyloidNoiseEdit->setVisible(m_amyloidActive);
    m_amyloidPersistEdit->setVisible(m_amyloidActive);
    m_amyloidWaveEdit->setVisible(m_amyloidActive);
    m_amyloidAmplEdit->setVisible(m_amyloidActive);
    m_amyloidSignalBtn->setVisible(m_amyloidActive);
    m_amyloidCancelBtn->setVisible(m_amyloidActive);
    m_amyloidComputeBtn->setVisible(m_amyloidActive);
    m_measureCancelBtn->setVisible(m_measureActive);
    m_shiftCancelBtn->setVisible(m_shiftActive);
    m_rotateCancelBtn->setVisible(m_rotateActive);
    m_shearAngleEdit->setVisible(m_shearActive);
    m_shearAxisCombo->setVisible(m_shearActive);
    m_shearCancelBtn->setVisible(m_shearActive);
    m_applyShearBtn->setVisible(m_shearActive);
    m_alignSrcCombo->setVisible(m_alignActive);
    m_alignRefCombo->setVisible(m_alignActive);
    m_alignOutCombo->setVisible(m_alignActive);
    m_alignCancelBtn->setVisible(m_alignActive);
    m_alignShiftBtn->setVisible(m_alignActive);
    m_alignRotBtn->setVisible(m_alignActive);
    m_alignFullBtn->setVisible(m_alignActive);
    m_alignTilesBtn->setVisible(m_alignActive);
    m_alignTileSizeCombo->setVisible(m_alignActive);
}

void FtWindow::closeP1Function()
{
    deactivateAllP1Tools();
    m_p1Dragging = false;
    m_toolDragging = false;
    showP1ToolWidgets();
    update();
}

void FtWindow::closeP2Function()
{
    deactivateAllP2Tools();
    m_p2Dragging = false;
    m_toolDragging = false;
    // Mirrors the cross-section tool's own deactivation cleanup.
    m_crossSectionProfile.clear();
    m_crossSectionPhaseProfile.clear();
    showP2ToolWidgets();
    update();
}

void FtWindow::onFtRotateCancel()
{
    m_ftRotateActive = false;
    m_p2Dragging = false;
    m_ftRotateCancelBtn->hide();
    update();
}

void FtWindow::activateP1Tool(int toolId)
{
    switch (toolId) {
    case 0: { bool was = m_p1EraserActive; deactivateAllP1Tools(); m_p1EraserActive = !was; break; }
    case 1: {
        bool was = m_p1BrushActive; deactivateAllP1Tools(); m_p1BrushActive = !was;
        if (m_p1BrushActive && !m_image.isNull()) {
            double defVal = m_imageMaxVal > 0 ? m_imageMaxVal : 1.0;
            m_p1BrushValueEdit->setText(QString::number(defVal, 'g', 5));
        }
        break;
    }
    case 2: {
        bool was = m_measureActive; deactivateAllP1Tools(); m_measureActive = !was;
        if (!m_measureActive) { m_measurePlacing = 0; m_measureHasLine = false; }
        break;
    }
    // Flip / invert act on every click (so two clicks still undo each other);
    // the flag only toggles the parameter window carrying the help button.
    case 3: {
        if (m_image.isNull()) return;
        bool was = m_p1FlipHActive;
        deactivateAllP1Tools(); storeUndoSnapshot(tr("Flipped horizontally"));
        m_image = flipImage(m_image, Qt::Horizontal);
        extractImageData(); if (m_ftComputed) computeFFT();
        m_p1FlipHActive = !was;
        break;
    }
    case 4: {
        if (m_image.isNull()) return;
        bool was = m_p1FlipVActive;
        deactivateAllP1Tools(); storeUndoSnapshot(tr("Flipped vertically"));
        m_image = flipImage(m_image, Qt::Vertical);
        extractImageData(); if (m_ftComputed) computeFFT();
        m_p1FlipVActive = !was;
        break;
    }
    case 5: { bool was = m_shiftActive; deactivateAllP1Tools(); m_shiftActive = !was; break; }
    case 6: { bool was = m_rotateActive; deactivateAllP1Tools(); m_rotateActive = !was; break; }
    case 22: { bool was = m_shearActive; deactivateAllP1Tools(); m_shearActive = !was; break; }
    case 7: {
        if (m_image.isNull()) return;
        bool was = m_p1InvertActive;
        deactivateAllP1Tools(); showP1ToolWidgets(); onInvertContrast();
        m_p1InvertActive = !was;
        update();
        return;
    }
    case 8: { bool was = m_p1TaperActive; deactivateAllP1Tools(); m_p1TaperActive = !was; break; }
    case 25: {
        bool was = m_threshActive; deactivateAllP1Tools(); m_threshActive = !was;
        // Open on whatever the histogram under panel 1 currently has marked.
        if (m_threshActive) syncThresholdEdits();
        break;
    }
    case 9: { bool was = m_p1SymmetrizeActive; deactivateAllP1Tools(); m_p1SymmetrizeActive = !was; break; }
    case 10: { bool was = m_binActive; deactivateAllP1Tools(); m_binActive = !was; break; }
    case 20: {
        bool was = m_copyActive; deactivateAllP1Tools(); m_copyActive = !was;
        if (m_copyActive) syncCopyCombos();
        break;
    }
    case 21: {
        bool was = m_averageActive; deactivateAllP1Tools(); m_averageActive = !was;
        if (m_averageActive) {
            m_averageResult.clear();
            // Seed the include set with every buffer that holds an image, and
            // aim the output at the active buffer.
            for (int i = 0; i < HISTORY_SLOTS; i++)
                m_averageInclude[i] = bufferInUse(i);
            if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS)
                m_averageTargetCombo->setCurrentIndex(m_activeSlot);
        }
        break;
    }
    case 19: {
        bool was = m_padActive; deactivateAllP1Tools(); m_padActive = !was;
        if (m_padActive) syncPadSizeCombo();
        break;
    }
    case 11: {
        bool was = m_cropActive; deactivateAllP1Tools(); m_cropActive = !was;
        if (m_cropActive) { m_cropRect = QRect(); m_cropHasSelection = false; syncCropEdits(); }
        break;
    }
    case 12: { bool was = m_gaborActive; deactivateAllP1Tools(); m_gaborActive = !was; break; }
    case 13: { bool was = m_hessianActive; deactivateAllP1Tools(); m_hessianActive = !was; break; }
    case 23: {
        bool was = m_houghActive; deactivateAllP1Tools(); m_houghActive = !was;
        // Both open on the buffer being looked at, so the transform replaces
        // its own input unless the user points the output somewhere else.
        if (m_houghActive) {
            m_houghAutoRadius = 0;   // nothing found yet in this session of the tool
            if (m_activeSlot >= 0) {
                m_houghSourceCombo->setCurrentIndex(m_activeSlot);
                m_houghTargetCombo->setCurrentIndex(m_activeSlot);
            }
        }
        break;
    }
    case 14: {
        bool was = m_amyloidActive; deactivateAllP1Tools(); m_amyloidActive = !was;
        if (!m_amyloidActive) { m_amyloidPlacing = 0; }
        else if (m_activeSlot < 0 || m_image.isNull()) {
            int sz = m_amyloidSizeCombo->currentText().toInt();
            if (sz <= 0) sz = 1024;
            onCreateImageSized(sz);
        }
        break;
    }
    case 15: { bool was = m_mathActive; deactivateAllP1Tools(); m_mathActive = !was; break; }
    case 16: {
        bool was = m_peakPickActive; deactivateAllP1Tools(); m_peakPickActive = !was;
        if (m_peakPickActive) {
            // Seeding the slider is not the user moving it, so it must not set
            // the automatic search going — opening the tool should not compute.
            { QSignalBlocker b(m_peakThresholdSlider);
              m_peakThresholdSlider->setValue(750); }
            if (m_activeSlot >= 0) m_peakSourceCombo->setCurrentIndex(m_activeSlot);
        }
        break;
    }
    case 17: {
        bool was = m_extractActive; deactivateAllP1Tools(); m_extractActive = !was;
        if (m_extractActive && m_activeSlot >= 0)
            m_extractSourceCombo->setCurrentIndex(m_activeSlot);
        break;
    }
    case 24: {
        bool was = m_tileAvgActive; deactivateAllP1Tools(); m_tileAvgActive = !was;
        if (m_tileAvgActive) {
            if (m_activeSlot >= 0)
                m_tileAvgSourceCombo->setCurrentIndex(m_activeSlot);
            // The tile size follows the box size Extract particles last used —
            // both pulldowns offer the same two sizes, and the two functions are
            // normally run on the same features.
            m_tileAvgSizeCombo->setCurrentIndex(m_extractSizeCombo->currentIndex());
        }
        break;
    }
    case 18: {
        bool was = m_alignActive; deactivateAllP1Tools(); m_alignActive = !was;
        if (m_alignActive) {
            m_alignResult.clear();
            // Same tile size the other two tile functions default to.
            m_alignTileSizeCombo->setCurrentIndex(m_extractSizeCombo->currentIndex());
            int src = (m_activeSlot >= 0) ? m_activeSlot : 0;
            alignSeedSourceAndOutput(src);
            // The reference is sticky: restore whichever buffer was last used as
            // a reference and never retarget it automatically. On first use fall
            // back to another occupied buffer, else buffer a.
            if (m_alignRefSlot < 0 || m_alignRefSlot >= HISTORY_SLOTS) {
                int alt = -1;
                for (int i = 0; i < HISTORY_SLOTS; i++)
                    if (i != src && m_history[i].occupied) { alt = i; break; }
                m_alignRefSlot = (alt >= 0) ? alt : 0;
            }
            {   // Setting it manually here should not itself be treated as a
                // fresh user pick, so block the change handler.
                QSignalBlocker b(m_alignRefCombo);
                m_alignRefCombo->setCurrentIndex(m_alignRefSlot);
            }
            syncAlignCombos();
        }
        break;
    }
    default: return;
    }
    showP1ToolWidgets();
    update();
}

void FtWindow::deactivateAllP2Tools()
{
    m_eraserActive = false; m_brushActive = false;
    m_bandpassActive = false; m_directionalActive = false;
    m_lineFilterActive = false;
    m_latticeActive = false; m_ftRotateActive = false;
    m_crossSectionActive = false;
    m_p2SymmetrizeActive = false;
    m_ftCropActive = false; m_ftMathActive = false;
    m_ctfActive = false;
    m_ctfFitActive = false;
    m_phaseRampActive = false;
}

void FtWindow::showP2ToolWidgets()
{
    bool showFilter = m_bandpassActive || m_directionalActive;
    m_smoothEdit->setVisible(showFilter);
    m_bandEraseOutside->setVisible(showFilter);
    m_applyBandBtn->setVisible(showFilter);
    m_resetBandBtn->setVisible(m_bandpassActive);

    m_brushValueEdit->setVisible(m_brushActive);
    m_brushDiameterEdit->setVisible(m_brushActive);

    m_eraserDiameterEdit->setVisible(m_eraserActive);

    m_lineWidthEdit->setVisible(m_lineFilterActive);
    m_lineDirectionEdit->setVisible(m_lineFilterActive);
    m_lineOffsetEdit->setVisible(m_lineFilterActive);
    m_lineEraseOutsideBtn->setVisible(m_lineFilterActive);
    m_applyLineBtn->setVisible(m_lineFilterActive);

    m_latticeSmoothEdit->setVisible(m_latticeActive);
    m_latticeDotDiamEdit->setVisible(m_latticeActive);
    m_latticeUxEdit->setVisible(m_latticeActive);
    m_latticeUyEdit->setVisible(m_latticeActive);
    m_latticeVxEdit->setVisible(m_latticeActive);
    m_latticeVyEdit->setVisible(m_latticeActive);
    if (m_latticeActive) syncLatticeVectorEdits();
    m_latticeEraseOutside->setVisible(m_latticeActive);
    m_latticeApplyBtn->setVisible(m_latticeActive);

    m_ftRotateCancelBtn->setVisible(m_ftRotateActive);

    m_crossSectionDirEdit->setVisible(m_crossSectionActive);
    m_crossSectionWidthEdit->setVisible(m_crossSectionActive);

    m_p2SymmetryEdit->setVisible(m_p2SymmetrizeActive);
    m_applyP2SymmetryBtn->setVisible(m_p2SymmetrizeActive);

    m_ftCropCombo->setVisible(m_ftCropActive);
    m_ftCropKeepSizeBtn->setVisible(m_ftCropActive);
    m_applyFtCropBtn->setVisible(m_ftCropActive);
    m_applyFtPadBtn->setVisible(m_ftCropActive);

    m_ftMathOutCombo->setVisible(m_ftMathActive);
    m_ftMathEqualsLabel->setVisible(m_ftMathActive);
    m_ftMathIn1Combo->setVisible(m_ftMathActive);
    m_ftMathOpCombo->setVisible(m_ftMathActive);
    m_ftMathIn2Combo->setVisible(m_ftMathActive);
    m_ftMathConjCombo->setVisible(m_ftMathActive);
    m_ftMathCancelBtn->setVisible(m_ftMathActive);
    m_ftMathComputeBtn->setVisible(m_ftMathActive);

    m_ctfVoltageEdit->setVisible(m_ctfActive);
    m_ctfEnergySpreadEdit->setVisible(m_ctfActive);
    m_ctfDefocusSpreadEdit->setVisible(m_ctfActive);
    m_ctfOpenAngleEdit->setVisible(m_ctfActive);
    m_ctfCsEdit->setVisible(m_ctfActive);
    m_ctfDefocusEdit->setVisible(m_ctfActive);
    m_ctfAstigEdit->setVisible(m_ctfActive);
    m_ctfAstigAngleEdit->setVisible(m_ctfActive);
    m_ctfAmpContrastEdit->setVisible(m_ctfActive);
    m_ctfBeamtiltEdit->setVisible(m_ctfActive);
    m_ctfBeamtiltDirEdit->setVisible(m_ctfActive);
    m_ctfCancelBtn->setVisible(m_ctfActive);
    m_ctfPupilBtn->setVisible(m_ctfActive);
    m_ctfComplexBtn->setVisible(m_ctfActive);
    m_ctfRealBtn->setVisible(m_ctfActive);

    m_ctfFitVoltageEdit->setVisible(m_ctfFitActive);
    m_ctfFitCsEdit->setVisible(m_ctfFitActive);
    m_ctfFitInputCombo->setVisible(m_ctfFitActive);
    m_ctfFitResHiEdit->setVisible(m_ctfFitActive);
    m_ctfFitResLoEdit->setVisible(m_ctfFitActive);
    m_ctfFitCancelBtn->setVisible(m_ctfFitActive);
    m_ctfFitExecuteBtn->setVisible(m_ctfFitActive);

    m_phaseRampSizeCombo->setVisible(m_phaseRampActive);
    m_phaseRampDirEdit->setVisible(m_phaseRampActive);
    m_phaseRampStepEdit->setVisible(m_phaseRampActive);
    m_phaseRampCancelBtn->setVisible(m_phaseRampActive);
    m_phaseRampComputeBtn->setVisible(m_phaseRampActive);
}

void FtWindow::activateP2Tool(int toolId)
{
    switch (toolId) {
    case 0: { bool was = m_eraserActive; deactivateAllP2Tools(); m_eraserActive = !was; break; }
    case 1: {
        bool was = m_brushActive; deactivateAllP2Tools(); m_brushActive = !was;
        if (m_brushActive && m_ftComputed) {
            double bv = brushValue();
            m_brushValueEdit->setText(bv > 0 ? QString::number(bv, 'g', 5) : "1");
        }
        break;
    }
    case 2: { bool was = m_bandpassActive; deactivateAllP2Tools(); m_bandpassActive = !was; break; }
    case 3: { bool was = m_directionalActive; deactivateAllP2Tools(); m_directionalActive = !was; break; }
    case 4: { bool was = m_lineFilterActive; deactivateAllP2Tools(); m_lineFilterActive = !was; break; }
    case 5: { bool was = m_latticeActive; deactivateAllP2Tools(); m_latticeActive = !was; break; }
    case 6: { bool was = m_ftRotateActive; deactivateAllP2Tools(); m_ftRotateActive = !was; break; }
    case 7: {
        bool was = m_crossSectionActive; deactivateAllP2Tools(); m_crossSectionActive = !was;
        if (m_crossSectionActive && m_ftComputed) {
            syncCrossSectionDirEdit();
            computeCrossSectionProfile();
        } else {
            m_crossSectionProfile.clear();
            m_crossSectionPhaseProfile.clear();
        }
        break;
    }
    case 8: { bool was = m_p2SymmetrizeActive; deactivateAllP2Tools(); m_p2SymmetrizeActive = !was; break; }
    case 9: { bool was = m_ftCropActive; deactivateAllP2Tools(); m_ftCropActive = !was; break; }
    case 10: { bool was = m_phaseRampActive; deactivateAllP2Tools(); m_phaseRampActive = !was; break; }
    case 11: {
        bool was = m_ctfActive; deactivateAllP2Tools(); m_ctfActive = !was;
        m_ctfProfile.clear();
        m_ctfPhaseProfile.clear();
        break;
    }
    case 12: {
        bool was = m_ctfFitActive; deactivateAllP2Tools(); m_ctfFitActive = !was;
        if (m_ctfFitActive) {
            m_ctfFitHasResult = false;
            if (m_activeSlot >= 0 && m_ctfFitInputCombo)
                m_ctfFitInputCombo->setCurrentIndex(m_activeSlot);
            // Seed the fit band from the image's own Nyquist resolution, so the
            // defaults follow the pixel size instead of being fixed at 3/30 Å.
            updateCtfFitResolutionDefaults();
        }
        break;
    }
    case 13: { bool was = m_ftMathActive; deactivateAllP2Tools(); m_ftMathActive = !was; break; }
    default: return;
    }
    showP2ToolWidgets();
    update();
}

// ---------------------------------------------------------------------------
//  Mouse
// ---------------------------------------------------------------------------
// The manual is hosted beside the app rather than inside it — /ft-manual/ next
// to /ft/ — so its pages can be corrected and re-indexed without re-deploying
// the WASM build. Every link out of the app goes through this one base.
static const QString kManualBase = QStringLiteral("https://lbem-status.epfl.ch/ft-manual/");

void FtWindow::openManualAnchor(bool panel2, const QString &anchor)
{
    const QString page = panel2 ? "manual_panel2.html" : "manual_panel1.html";
    QDesktopServices::openUrl(QUrl(kManualBase + page + "#" + anchor));
}

void FtWindow::openExercise(const QString &anchor)
{
    if (anchor.isEmpty()) return;
    QDesktopServices::openUrl(QUrl(kManualBase + "manual_exercises.html#" + anchor));
}

void FtWindow::openToolHelp(bool panel2)
{
    QString title, anchor;
    if (!toolHelpInfo(panel2, title, anchor)) return;
    // The Copy tool is launched from the central button strip, not the panel-1
    // tool row, and is documented in the main manual's GUI-layout section rather
    // than on the panel-1 tool page.
    if (!panel2 && m_copyActive) {
        QDesktopServices::openUrl(QUrl(kManualBase + "manual.html#gui"));
        return;
    }
    openManualAnchor(panel2, anchor);
}

// ---------------------------------------------------------------------------
//  "Find in manual" snippet search
// ---------------------------------------------------------------------------
// The Help dialog's "Find in manual" searches every manual page, not just
// manual.html, and lists each occurrence as a clickable snippet. The pages are
// downloaded rather than bundled so the search always sees the manual as
// currently published (see kManualBase above).
namespace {
struct ManualPage { const char *file; const char *title; };
struct ManualHit {
    QString url;      // page URL + #:~:text= directive targeting this occurrence
    QString snippet;  // rich-text context line, occurrence in bold
};
} // namespace

static const ManualPage kManualPages[] = {
    { "manual.html",           "Main manual" },
    { "manual_panel1.html",    "Panel 1 — real-space tools" },
    { "manual_panel2.html",    "Panel 2 — Fourier tools" },
    { "manual_exercises.html", "Exercises" },
};

// Downloaded page HTML, kept for the whole session: the pages total ~300 kB
// (the figures live in separate files the search never fetches) and repeated
// searches shouldn't re-fetch them every time. A failed download is not
// cached, so the next search retries it.
static QHash<QString, QString> s_manualPageCache;

// Reduce a page to the plain-text blocks the browser renders, one string per
// block. A text fragment cannot match across a block boundary, so snippets and
// their prefix/suffix context must be built within a single block.
static QStringList manualTextBlocks(QString html)
{
    // Content the browser doesn't render as text goes first, or styles,
    // scripts and embedded image data would turn up as search hits.
    html.remove(QRegularExpression(QStringLiteral("(?is)<head\\b.*?</head>")));
    html.remove(QRegularExpression(QStringLiteral("(?is)<script\\b.*?</script>")));
    html.remove(QRegularExpression(QStringLiteral("(?is)<style\\b.*?</style>")));
    html.remove(QRegularExpression(QStringLiteral("(?is)<svg\\b.*?</svg>")));
    html.remove(QRegularExpression(QStringLiteral("(?is)<img\\b[^>]*>")));
    // QTextDocument supplies the full entity table and block segmentation:
    // toPlainText() yields one line per block and folds &nbsp; to a plain
    // space, which matches how the browsers' fragment matchers compare text.
    QTextDocument doc;
    doc.setHtml(html);
    QStringList blocks;
    const QStringList lines = doc.toPlainText().split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        QString t = line;
        t.replace(QChar(0xFFFC), QLatin1Char(' '));   // object-replacement chars
        t = t.simplified();
        if (!t.isEmpty()) blocks << t;
    }
    return blocks;
}

// Percent-encode one part of a text-fragment directive. '-' separates the
// prefix/suffix from the match text, so it must not survive as a literal;
// the other delimiters (',' and '&') are outside the default unreserved set
// and get encoded anyway.
static QString fragmentEncode(const QString &s)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s, QByteArray(), "-"));
}

// Every occurrence of `query` in one manual page. The URL pins down WHICH
// occurrence via context words (#:~:text=prefix-,match,-suffix), so the
// browser highlights the chosen one instead of the page's first.
static QVector<ManualHit> findManualMatches(const QString &pageFile,
                                            const QString &pageHtml,
                                            const QString &query)
{
    // Context sizes in words: enough around the match in the URL to identify
    // the occurrence uniquely, a bit more in the visible snippet so the hits
    // can be told apart at a glance.
    const int kUrlContext = 4, kSnippetContext = 8;

    QVector<ManualHit> hits;
    QSet<QString> seen;
    const QStringList blocks = manualTextBlocks(pageHtml);
    for (qsizetype b = 0; b < blocks.size(); ++b) {
        const QString &block = blocks.at(b);
        int idx, from = 0;
        while ((idx = int(block.indexOf(query, from, Qt::CaseInsensitive))) >= 0) {
            from = idx + int(query.size());
            // Fragment matches must start and end on word boundaries, so a
            // query that hits mid-word is widened to whole words ("math" →
            // "mathematics").
            int start = idx, end = idx + int(query.size());
            while (start > 0 && block.at(start - 1) != QLatin1Char(' ')) --start;
            while (end < block.size() && block.at(end) != QLatin1Char(' ')) ++end;
            const QString match = block.mid(start, end - start);
            const QStringList before =
                block.left(start).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            const QStringList after =
                block.mid(end).split(QLatin1Char(' '), Qt::SkipEmptyParts);

            QStringList pre = before.mid(qMax<qsizetype>(0, before.size() - kUrlContext));
            QStringList suf = after.mid(0, kUrlContext);
            // A match filling its whole block (e.g. a bare "Math" heading) has
            // no same-block context, and "#:~:text=Math" alone would highlight
            // the page's FIRST "Math" instead. The fragment matcher's text walk
            // crosses block boundaries as whitespace, so borrow the context
            // words from the neighbouring blocks.
            if (pre.isEmpty() && b > 0) {
                const QStringList prev =
                    blocks.at(b - 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
                pre = prev.mid(qMax<qsizetype>(0, prev.size() - kUrlContext));
            }
            if (suf.isEmpty() && b + 1 < blocks.size()) {
                const QStringList next =
                    blocks.at(b + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
                suf = next.mid(0, kUrlContext);
            }
            QString url = kManualBase + pageFile + QStringLiteral("#:~:text=");
            if (!pre.isEmpty())
                url += fragmentEncode(pre.join(QLatin1Char(' '))) + QStringLiteral("-,");
            url += fragmentEncode(match);
            if (!suf.isEmpty())
                url += QStringLiteral(",-") + fragmentEncode(suf.join(QLatin1Char(' ')));

            // Same text in the same context: the browser could not tell the
            // occurrences apart either, so one entry stands for all of them.
            if (seen.contains(url)) continue;
            seen.insert(url);

            const QStringList dpre = before.mid(qMax<qsizetype>(0, before.size() - kSnippetContext));
            const QStringList dsuf = after.mid(0, kSnippetContext);
            QString snip;
            if (before.size() > dpre.size()) snip += QStringLiteral("… ");
            if (!dpre.isEmpty()) snip += dpre.join(QLatin1Char(' ')).toHtmlEscaped() + QLatin1Char(' ');
            snip += QStringLiteral("<b>") + match.toHtmlEscaped() + QStringLiteral("</b>");
            if (!dsuf.isEmpty()) snip += QLatin1Char(' ') + dsuf.join(QLatin1Char(' ')).toHtmlEscaped();
            if (after.size() > dsuf.size()) snip += QStringLiteral(" …");
            hits.append({url, snip});
        }
    }
    return hits;
}

// Render the finished search into the Help dialog's results pane. Every page
// that downloaded is searched; ones that didn't are reported, so a network
// failure can't silently pose as "no matches".
static void showManualSearchResults(QTextBrowser *browser, const QString &query,
                                    const QStringList &fetchErrors,
                                    const HelpTheme &t)
{
    const int kMaxPerPage = 25;
    QString out;
    qsizetype total = 0;
    for (const ManualPage &page : kManualPages) {
        const auto it = s_manualPageCache.constFind(QLatin1String(page.file));
        if (it == s_manualPageCache.constEnd()) continue;
        const QVector<ManualHit> hits =
            findManualMatches(QLatin1String(page.file), it.value(), query);
        if (hits.isEmpty()) continue;
        total += hits.size();
        out += QStringLiteral("<h4 style=\"margin:10px 0 2px 0; color:%1;\">").arg(t.fg)
             + QString::fromUtf8(page.title).toHtmlEscaped()
             + QStringLiteral(" <span style=\"color:%1;\">— %2 match%3</span></h4>")
                   .arg(t.dim).arg(hits.size()).arg(hits.size() == 1 ? "" : "es");
        const int shown = int(qMin<qsizetype>(hits.size(), kMaxPerPage));
        // Half a line of air below each entry, so multi-line snippets read as
        // one finding each instead of running together into a wall of text.
        for (int k = 0; k < shown; ++k)
            out += QStringLiteral("<p style=\"margin:0 0 8px 14px;\"><a href=\"")
                 + hits[k].url + QStringLiteral("\">") + hits[k].snippet
                 + QStringLiteral("</a></p>");
        if (hits.size() > shown)
            out += QStringLiteral("<p style=\"margin:0 0 8px 14px; color:%1;\">"
                                  "… %2 further matches not listed — try a more "
                                  "specific phrase</p>").arg(t.dim).arg(hits.size() - shown);
    }

    QString head;
    if (total == 0)
        head = QStringLiteral("<p style=\"color:%1;\">No matches for “%2” in the "
                              "manual. Try a shorter keyword, or the Search Google "
                              "button.</p>").arg(t.fg, query.toHtmlEscaped());
    else
        head = QStringLiteral("<p style=\"color:%1;\">%2 match%3 for “%4” — click "
                              "one to open it in the browser:</p>")
                   .arg(t.muted).arg(total).arg(total == 1 ? "" : "es").arg(query.toHtmlEscaped());
    for (const QString &err : fetchErrors)
        out += QStringLiteral("<p style=\"color:%1;\">Could not load %2</p>")
                   .arg(t.dark ? QStringLiteral("#ff9999") : QStringLiteral("#c62828"),
                        err.toHtmlEscaped());
    browser->setHtml(head + out);
}

// Entry point from the Help dialog: make sure all manual pages are downloaded,
// then search them and fill `browser` with clickable snippet links.
static void runManualSearch(const QString &rawQuery, QTextBrowser *browser,
                            const HelpTheme &t)
{
    const QString query = rawQuery.simplified();
    if (query.isEmpty()) return;
    browser->show();
    browser->setHtml(QStringLiteral("<p style=\"color:%1;\">Searching the manual…</p>")
                         .arg(t.muted));

    // Every call bumps the pane's generation, and a download finishing for an
    // older one may still cache its page but must not paint: the user (or a
    // theme change re-running the search) has asked for something newer since.
    const int generation = browser->property("searchGeneration").toInt() + 1;
    browser->setProperty("searchGeneration", generation);

    QStringList missing;
    for (const ManualPage &page : kManualPages)
        if (!s_manualPageCache.contains(QLatin1String(page.file)))
            missing << QLatin1String(page.file);
    if (missing.isEmpty()) {
        showManualSearchResults(browser, query, {}, t);
        return;
    }

    // One-shot manager for the missing pages; owned by the results pane so an
    // early dialog close tears the replies down with it.
    auto *nam = new QNetworkAccessManager(browser);
    nam->setTransferTimeout(10000);
    struct Fetch { int pending; QStringList errors; };
    auto st = std::make_shared<Fetch>();
    st->pending = int(missing.size());
    for (const QString &file : missing) {
        QNetworkReply *reply = nam->get(QNetworkRequest(QUrl(kManualBase + file)));
        QObject::connect(reply, &QNetworkReply::finished, browser,
                         [nam, reply, file, query, browser, st, t, generation]() {
            if (reply->error() == QNetworkReply::NoError)
                s_manualPageCache.insert(file, QString::fromUtf8(reply->readAll()));
            else
                st->errors << file + QStringLiteral(" — ") + reply->errorString();
            reply->deleteLater();
            if (--st->pending == 0) {
                nam->deleteLater();
                if (browser->property("searchGeneration").toInt() == generation)
                    showManualSearchResults(browser, query, st->errors, t);
            }
        });
    }
}

// Make buffer `i` the one on display, saving whatever is on screen back to
// its own slot first. Shared by clicking a history thumbnail and by the
// arrow keys in the maximized view, so both routes leave exactly the same
// state behind.
void FtWindow::activateHistorySlot(int i)
{
    if (i < 0 || i >= HISTORY_SLOTS) return;

    // With the Align tool open, moving to a buffer retargets what it works
    // on: that buffer becomes both the image that moves and the buffer the
    // result lands in, so the tool keeps acting on what is being looked at
    // instead of on whichever buffer happened to be shown when it opened.
    // The reference is left as it is — that one is chosen once and stays
    // put. Done before the early-outs below, so it applies even when the
    // buffer is already the active one (there the display does not change
    // but the pulldowns may still be pointing somewhere else). It lives
    // here rather than at the call site so that every route to another
    // buffer — a thumbnail click, an arrow key in the maximized view —
    // leaves the same tool state behind.
    if (m_alignActive) {
        alignSeedSourceAndOutput(i);
        syncAlignCombos();      // repaints the parameter window
    }

    // A slot the startup restore skipped (large image, or file on a
    // network volume) still holds its path. Reaching it is the moment we
    // pay for the load — including when it is already the active slot,
    // which is why the "already active" early-out comes after this test.
    const bool needsDiskLoad = !m_history[i].occupied && m_history[i].deferred;

    if (i == m_activeSlot && !needsDiskLoad) return;   // already active
    if (m_history[i].loading && i == m_activeSlot) return;   // still reading

    // Save current active image back to its slot, caching its forward
    // FFT so returning here won't recompute it. The power-spectrum
    // thumbnail is derived from that cached FFT (no extra transform).
    if (m_activeSlot >= 0 && !m_image.isNull()) {
        HistoryEntry &cur = m_history[m_activeSlot];
        cur.image        = m_image;
        cur.path         = m_imagePath;
        cur.rawPixels    = m_imageRawPixels;
        cur.minVal       = m_imageMinVal;
        cur.maxVal       = m_imageMaxVal;
        cur.pixelSize    = m_pixelSize;
        cur.pixelSizeAssumed = m_pixelSizeAssumed;
        cur.lastOperation = m_lastOperation;
        cur.ftComputed   = m_ftComputed;
        if (m_ftComputed) {
            cur.fftData      = m_fftData;
            cur.fftN         = m_fftN;
            cur.fftOrigW     = m_origW;
            cur.fftOrigH     = m_origH;
            cur.ftInverseOutput = m_ftInverseOutput;
            cur.powerSpecImg = powerSpecFromCurrentFFT();
        } else {
            cur.fftData.clear();
            cur.fftData.shrink_to_fit();
            cur.powerSpecImg = computePowerSpecMasked(m_image);
        }
        cur.occupied     = true;
        cur.deferred     = false;
    }

    // Activate the clicked slot
    m_activeSlot = i;

    // Realise a deferred slot now that its data is actually wanted. The
    // read runs on a worker thread — these are exactly the images that
    // were too big to load at startup, so blocking here would freeze the
    // window and leave the user unable to delete the buffer they just
    // discovered they do not want. The slot fills in when it arrives.
    if (needsDiskLoad)
        startSlotLoad(i);

    if (m_history[i].occupied) {
        // Load occupied slot into panel 1
        m_image          = m_history[i].image;
        m_imagePath      = m_history[i].path;
        m_imageRawPixels = m_history[i].rawPixels;
        m_imageMinVal    = m_history[i].minVal;
        m_imageMaxVal    = m_history[i].maxVal;
        m_imageDispMin   = m_history[i].minVal;
        m_imageDispMax   = m_history[i].maxVal;
        m_pixelSize      = m_history[i].pixelSize;
        m_pixelSizeAssumed = m_history[i].pixelSizeAssumed;
        m_lastOperation  = m_history[i].lastOperation;
    } else {
        // Empty slot – clear panel 1
        m_image          = QImage();
        m_imagePath.clear();
        m_imageRawPixels.clear();
        m_imageMinVal    = 0;
        m_imageMaxVal    = 0;
        m_imageDispMin   = 0;
        m_imageDispMax   = 0;
        m_pixelSize      = 1.0;
        m_pixelSizeAssumed = false;
        m_lastOperation.clear();
    }

    m_modeBtn->setText(modeLabel());

    if (m_history[i].occupied && m_history[i].ftComputed
        && !m_history[i].fftData.empty()) {
        // Cached FFT: restore it and rebuild only the display images
        // (parallelized) instead of recomputing the forward transform.
        m_fftData    = m_history[i].fftData;
        m_fftN       = m_history[i].fftN;
        m_origW      = m_history[i].fftOrigW;
        m_origH      = m_history[i].fftOrigH;
        m_ftInverseOutput = m_history[i].ftInverseOutput;
        m_ftComputed = true;
        recomputeDisplayImages();
        m_modeBtn->show();
        m_maskBtnVisible = true;
    } else {
        m_ftComputed = false;
        m_modeBtn->hide();
        m_maskBtnVisible = false;
        if (!m_image.isNull())
            computeFFT(true);
    }

    saveHistory();
#ifndef __EMSCRIPTEN__
    QSettings settings("ft", "ft");
    settings.setValue("lastFile", m_imagePath);
    settings.setValue("activeSlot", m_activeSlot);
#endif
    update();
}

void FtWindow::mousePressEvent(QMouseEvent *event)
{
    // Maximized view: display only. The one interaction still offered is
    // starting a pan drag on a zoomed image; everything else is suppressed.
    if (m_maxPanel != 0) {
        if (m_maxCloseRect.contains(event->pos())) {
            exitMaximized();
            return;
        }
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (!di.valid || di.zoomIdx < 0) continue;
            if (!di.screenRect.contains(event->pos())) continue;
            if (m_zoom[di.zoomIdx].factor <= 1.0) break;
            if (di.zoomIdx == 0) { m_p1PanDragging = true; m_p1PanStart = event->pos(); }
            else                 { m_p2PanDragging = true; m_p2PanStart = event->pos(); }
            break;
        }
        return;
    }

    // Maximize icon under a panel's Zoom/Pan overlay. Both panel-2 icons open
    // the same view, which shows whichever images panel 2 currently displays.
    if (!m_p1MaxRect.isNull() && m_p1MaxRect.contains(event->pos())) {
        enterMaximized(1);
        return;
    }
    if ((!m_p2MaxRectA.isNull() && m_p2MaxRectA.contains(event->pos())) ||
        (!m_p2MaxRectB.isNull() && m_p2MaxRectB.contains(event->pos()))) {
        enterMaximized(2);
        return;
    }

    // If the "New image" popup is open, any click outside its child widgets
    // (which intercept their own clicks) dismisses the popup before the click
    // proceeds to its normal handler.
    if (m_newImageActive) onNewImageCancel();

    // If a tool-group popup is open, it floats over the image, so it must claim
    // the click before any image-area handler. Clicking a member cell activates
    // that tool; clicking anywhere else just dismisses the popup.
    if (m_openMenuPanel != 0) {
        int panel = m_openMenuPanel;
        const QVector<ToolGroup> &groups = (panel == 1) ? m_p1Groups : m_p2Groups;
        const QRect *slotRects = (panel == 1) ? m_p1BtnRects : m_toolBtnRects;
        int hitTool = -1;
        if (m_openMenuGroup >= 0 && m_openMenuGroup < groups.size()) {
            for (int id : groups[m_openMenuGroup].members) {
                if (slotRects[id].contains(event->pos())) { hitTool = id; break; }
            }
        }
        m_openMenuPanel = 0;
        m_openMenuGroup = -1;
        if (hitTool >= 0) {
            if (panel == 1) activateP1Tool(hitTool);
            else            activateP2Tool(hitTool);
        } else {
            update();   // just close the popup
        }
        return;
    }

    // The hover preview floats over the image in the same way, so it too has to
    // claim a click on one of its cells before any image-area handler sees it.
    // Clicking a previewed icon starts that function directly — having looked
    // inside the group, there is no reason to make the user open it first.
    if (m_hoverMenuPanel != 0) {
        int panel = m_hoverMenuPanel;
        const QVector<ToolGroup> &groups = (panel == 1) ? m_p1Groups : m_p2Groups;
        const QRect *slotRects = (panel == 1) ? m_p1BtnRects : m_toolBtnRects;
        int hitTool = -1;
        if (m_hoverMenuGroup >= 0 && m_hoverMenuGroup < groups.size()) {
            for (int id : groups[m_hoverMenuGroup].members) {
                if (slotRects[id].contains(event->pos())) { hitTool = id; break; }
            }
        }
        if (hitTool >= 0) {
            // Drop the preview: the pointer is left standing where the row was,
            // and it must not keep the row alive over the function just opened.
            m_hoverMenuPanel = 0;
            m_hoverMenuGroup = -1;
            m_p1PopupRect = QRect();
            m_p2PopupRect = QRect();
            if (panel == 1) activateP1Tool(hitTool);
            else            activateP2Tool(hitTool);
            return;
        }
        // A click anywhere else is not the preview's business — it falls through
        // to the normal handlers, and the row closes on its own as soon as the
        // pointer leaves it.
    }

    // "?" help button in a tool parameter window – open that function's entry
    // in the panel's manual page. Checked before the image-area handlers so a
    // click on the button never reaches the tool underneath.
    if (!m_p1HelpRect.isNull() && m_p1HelpRect.contains(event->pos())) {
        openToolHelp(false);
        return;
    }
    if (!m_p1ExerciseRect.isNull() && m_p1ExerciseRect.contains(event->pos())) {
        openExercise(toolExerciseAnchor(false));
        return;
    }
    if (!m_p2ExerciseRect.isNull() && m_p2ExerciseRect.contains(event->pos())) {
        openExercise(toolExerciseAnchor(true));
        return;
    }
    if (!m_p2HelpRect.isNull() && m_p2HelpRect.contains(event->pos())) {
        openToolHelp(true);
        return;
    }
    // "X" close button above a panel's function buttons.
    if (!m_p1CloseRect.isNull() && m_p1CloseRect.contains(event->pos())) {
        closeP1Function();
        return;
    }
    if (!m_p2CloseRect.isNull() && m_p2CloseRect.contains(event->pos())) {
        closeP2Function();
        return;
    }
    // Average tool: click an a…p tile to include/exclude that buffer from the
    // average. Buffers that hold no image are inert.
    if (m_averageActive) {
        for (int k = 0; k < HISTORY_SLOTS; k++) {
            if (!m_averageBtnRects[k].isNull()
                && m_averageBtnRects[k].contains(event->pos())) {
                if (bufferInUse(k)) {
                    m_averageInclude[k] = !m_averageInclude[k];
                    m_averageResult.clear();
                    update();
                }
                return;
            }
        }
    }
    if (!m_p1MathHelpRect.isNull() && m_p1MathHelpRect.contains(event->pos())) {
        openManualAnchor(false, "p1-math");
        return;
    }
    if (!m_p2MathHelpRect.isNull() && m_p2MathHelpRect.contains(event->pos())) {
        openManualAnchor(true, "p2-math");
        return;
    }

    // Title bar click – show About dialog
    if (!m_titleRect.isNull() && m_titleRect.contains(event->pos())) {
        auto *about = new QMessageBox(this);
        about->setAttribute(Qt::WA_DeleteOnClose);
        about->setWindowTitle("Fourier Analyzer");
        about->setTextFormat(Qt::RichText);
        about->setTextInteractionFlags(Qt::TextBrowserInteraction);
        about->setIconPixmap(QApplication::windowIcon().pixmap(64, 64));
        about->setText(
            "<h3>Fourier Analyzer</h3>"
            "<p>Created by the Stahlberg Lab, EPFL, Lausanne, Switzerland in Spring 2026</p>"
            "<p>This software is provided free of charge for non-commercial use. For commercial use, please contact the authors.</p>"
            "<p>Wen-Lu Chung: <a href=\"mailto:wen-lu.chung@epfl.ch\">wen-lu.chung@epfl.ch</a></p>"
            "<p>Henning Stahlberg: <a href=\"mailto:henning.stahlberg@epfl.ch\">henning.stahlberg@epfl.ch</a></p>");
        about->open();
        return;
    }

    // "Help" click – show manual link plus a question box that searches the
    // online manual. Two ways to search (hybrid):
    //   * "Find in manual" downloads all manual pages and lists every
    //     occurrence of the query as a clickable snippet right in the dialog;
    //     a click opens that page in the browser with the occurrence
    //     highlighted via a Text Fragment. Works immediately, no
    //     search-engine indexing required.
    //   * "Search Google" runs a site-restricted Google query. Only returns
    //     hits once Google has crawled/indexed the manual pages.
    if (!m_manualRect.isNull() && m_manualRect.contains(event->pos())) {
        const QString manualUrl = kManualBase + "manual.html";

        auto *dlg = new QDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowTitle("Help");

        // The banner spans the full dialog width, so the outer layout carries
        // no margins; the content below gets its own margined layout. All the
        // colours come later, from applyLook() — see helptheme.h.
        auto *outer = new QVBoxLayout(dlg);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        // Banner: title on the left; Increase Font / Decrease Font / Dark
        // mode in the top right corner. Blue in the light look, matching the
        // manual pages' header.
        auto *banner = new QWidget(dlg);
        banner->setObjectName(QStringLiteral("helpBanner"));
        auto *bannerLayout = new QHBoxLayout(banner);
        bannerLayout->setContentsMargins(12, 6, 8, 6);
        auto *bannerTitle = new QLabel(QStringLiteral("Fourier Analyzer — Help"), banner);
        bannerLayout->addWidget(bannerTitle);
        bannerLayout->addStretch(1);
        auto *fontMinusBtn = new QPushButton(QStringLiteral("A−"), banner);
        fontMinusBtn->setToolTip("Decrease font size");
        auto *fontPlusBtn = new QPushButton(QStringLiteral("A+"), banner);
        fontPlusBtn->setToolTip("Increase font size");
        auto *darkBtn = new QPushButton("Dark mode", banner);
        darkBtn->setCheckable(true);
        darkBtn->setChecked(helpDialogDark());
        darkBtn->setToolTip("Dark background with bright text; off, the dialog "
                            "matches the manual pages' light look.");
        for (QPushButton *b : { fontMinusBtn, fontPlusBtn, darkBtn }) {
            b->setCursor(Qt::PointingHandCursor);
            b->setFocusPolicy(Qt::NoFocus);
            bannerLayout->addWidget(b);
        }
        outer->addWidget(banner);

        auto *content = new QWidget(dlg);
        auto *layout = new QVBoxLayout(content);
        outer->addWidget(content, 1);

        auto *intro = new QLabel(dlg);
        intro->setTextFormat(Qt::RichText);
        intro->setTextInteractionFlags(Qt::TextBrowserInteraction);
        intro->setOpenExternalLinks(true);
        layout->addWidget(intro);

        auto *qLabel = new QLabel(
            "Ask a question about the Fourier Analyzer\n"
            "(searches the online manual):", dlg);
        layout->addWidget(qLabel);

        auto *edit = new QLineEdit(dlg);
        edit->setPlaceholderText("e.g. convolution theorem");
        layout->addWidget(edit);
        edit->setFocus();

        // Results pane for "Find in manual": hidden until the first search so
        // the dialog opens compact.
        auto *results = new QTextBrowser(dlg);
        results->setOpenLinks(false);   // handled below: the links leave the app
        results->setMinimumHeight(240);
        results->hide();
        layout->addWidget(results, 1);
        connect(results, &QTextBrowser::anchorClicked, dlg,
                [](const QUrl &url) { QDesktopServices::openUrl(url); });

        auto *buttons = new QDialogButtonBox(dlg);
        auto *findBtn   = buttons->addButton("Find in manual", QDialogButtonBox::ActionRole);
        auto *googleBtn = buttons->addButton("Search Google (indexing still not done...)",  QDialogButtonBox::ActionRole);

        // AI mode: the same question box, answered by a local model reading the
        // manual sections retrieval picked, instead of listed as occurrences to
        // open one by one. Desktop only -- the WebAssembly build has no local
        // model, so there the button does not exist and literal search stays the
        // only route. `aiBtn` is declared outside the guard so the submit lambda
        // below is identical in both builds; in WebAssembly it stays null.
        QPushButton *aiBtn = nullptr;
        QPushButton *askBtn = nullptr;
#ifndef __EMSCRIPTEN__
        aiBtn = buttons->addButton("AI mode", QDialogButtonBox::ActionRole);
        aiBtn->setCheckable(true);
        aiBtn->setToolTip("Ask in your own words and have a local model answer "
                          "from the manual, instead of listing keyword matches.");

        // AI mode's own submit button. "Find in manual" keeps its name and its
        // job in both modes -- switching to AI must add a way to ask, never take
        // the literal search away.
        askBtn = buttons->addButton("Ask", QDialogButtonBox::ActionRole);
        askBtn->hide();

        // QTextBrowser's HTML subset has no <details>, so the model's reasoning
        // is folded by a button rather than by markup.
        m_aiThinkBtn = buttons->addButton("Show reasoning", QDialogButtonBox::ActionRole);
        m_aiThinkBtn->hide();
        connect(m_aiThinkBtn, &QPushButton::clicked, dlg, [this]() {
            m_aiShowThink = !m_aiShowThink;
            aiRender();
        });
#endif
        buttons->addButton(QDialogButtonBox::Close);

        // Inside a QDialog a QPushButton is autoDefault by default, so Enter in
        // the question box would fire returnPressed *and* click whichever button
        // currently holds default -- submitting the same question twice. The
        // Enter key is wired explicitly below; no button should claim it.
        for (QAbstractButton *b : buttons->buttons())
            if (auto *pb = qobject_cast<QPushButton *>(b)) {
                pb->setAutoDefault(false);
                pb->setDefault(false);
            }

        layout->addWidget(buttons);

        // ------------------------------------------------------------------
        // Appearance: apply the current look (dark or manual-page light) and
        // font size to every part of the dialog, and re-render whatever the
        // results pane is showing so its inline colours match. The same
        // scheme as the Help dialog of the 4d application, which embeds this
        // program — see helptheme.h.
        // ------------------------------------------------------------------
        auto lastQuery = std::make_shared<QString>();
        auto applyLook = [this, dlg, banner, bannerTitle, fontMinusBtn, fontPlusBtn,
                          darkBtn, intro, qLabel, edit, results, buttons, aiBtn,
                          manualUrl, lastQuery]() {
            const HelpTheme t = helpTheme(helpDialogDark());
            const int base = 13 + helpDialogFontDelta();

            dlg->setStyleSheet(QStringLiteral("QDialog { background:%1; }").arg(t.windowBg));
            // ID selector: colours exactly this widget, and never its children
            // the way a bare declaration would cascade.
            banner->setStyleSheet(QStringLiteral("QWidget#helpBanner { background:%1; }")
                                      .arg(t.banner));
            bannerTitle->setStyleSheet(
                QStringLiteral("color:%1; font-size:%2px; font-weight:600; background:transparent;")
                    .arg(t.bannerFg).arg(base + 3));
            const QString bannerButtonCss = QStringLiteral(
                "QPushButton { background:transparent; border:1px solid %1; border-radius:3px;"
                " color:%2; padding:1px 8px; font-size:%3px; }"
                "QPushButton:checked { background:rgba(255,255,255,0.25); }")
                                                .arg(t.dark ? QStringLiteral("#666666")
                                                            : QStringLiteral("#7d9ce0"),
                                                     t.bannerFg)
                                                .arg(base - 1);
            fontMinusBtn->setStyleSheet(bannerButtonCss);
            fontPlusBtn->setStyleSheet(bannerButtonCss);
            darkBtn->setStyleSheet(bannerButtonCss);

            intro->setText(QStringLiteral(
                "<h3 style=\"color:%1;\">Fourier Analyzer</h3>"
                "<p style=\"color:%1;\">For instructions and exercises, visit the manual:</p>"
                "<p><a style=\"color:%2;\" href=\"%3\">%3</a></p>")
                               .arg(t.fg, t.link, manualUrl));
            intro->setStyleSheet(QStringLiteral("font-size:%1px; background:transparent;").arg(base));
            qLabel->setStyleSheet(QStringLiteral("color:%1; font-size:%2px; background:transparent;")
                                      .arg(t.fg).arg(base));
            edit->setStyleSheet(QStringLiteral("background:%1; color:%2; border:1px solid %3;"
                                               " padding:2px; font-size:%4px;")
                                    .arg(t.paneBg, t.fg, t.border).arg(base));
            results->setStyleSheet(QStringLiteral("QTextBrowser { background:%1; color:%2;"
                                                  " border:1px solid %3; font-size:%4px; }")
                                       .arg(t.paneBg, t.fg, t.border).arg(base));
            results->document()->setDefaultStyleSheet(
                QStringLiteral("a { color:%1; text-decoration:none; } code { background:%2; }")
                    .arg(t.link, t.codeBg));
            buttons->setStyleSheet(QStringLiteral(
                "QPushButton { background-color:%1; border:2px outset %2; color:%3;"
                " padding:2px 12px; font-size:%4px; }"
                "QPushButton:checked { background-color:%5; border:2px inset %2; }")
                                       .arg(t.buttonBg, t.buttonBorder, t.buttonFg)
                                       .arg(base)
                                       .arg(t.buttonCheckedBg));

            // The pane's content carries inline colours from the previous
            // look, so repaint it: the AI reply from its render, a literal
            // search by running it again (the pages are cached).
            bool aiShown = false;
#ifndef __EMSCRIPTEN__
            m_aiDark = t.dark;
            if (aiBtn && aiBtn->isChecked()) {
                aiRender();
                aiShown = true;
            }
#endif
            if (!aiShown && !lastQuery->isEmpty() && results->isVisible())
                runManualSearch(*lastQuery, results, t);
        };
        applyLook();

        connect(darkBtn, &QPushButton::toggled, dlg, [applyLook](bool on) {
            setHelpDialogDark(on);
            applyLook();
        });
        auto stepFont = [applyLook](int step) {
            // Clamped: -3 keeps the smallest text legible, +12 the dialog usable.
            setHelpDialogFontDelta(qBound(-3, helpDialogFontDelta() + step, 12));
            applyLook();
        };
        connect(fontPlusBtn, &QPushButton::clicked, dlg, [stepFont]() { stepFont(+1); });
        connect(fontMinusBtn, &QPushButton::clicked, dlg, [stepFont]() { stepFont(-1); });

        // Search every manual page and list the hits in the results pane.
        auto findInManual = [dlg, edit, results, lastQuery]() {
            if (edit->text().trimmed().isEmpty()) return;
            *lastQuery = edit->text();
            runManualSearch(edit->text(), results, helpTheme(helpDialogDark()));
            // First search: give the freshly shown results pane vertical room.
            // The width is left alone — it tracks the main window (90% at
            // open) or whatever the user has resized it to since.
            if (dlg->height() < 560)
                dlg->resize(dlg->width(), 560);
        };

        // Site-restricted Google query. Restricting to the manual's own path
        // covers manual.html *and* manual_exercises.html, manual_panel1.html, …
        // while keeping the app itself out of the results.
        auto searchGoogle = [edit]() {
            const QString question = edit->text().trimmed();
            if (question.isEmpty()) return;
            const QString query =
                question + " site:lbem-status.epfl.ch/ft-manual";
            QUrl url("https://www.google.com/search");
            QUrlQuery uq;
            uq.addQueryItem("q", query);
            url.setQuery(uq);
            QDesktopServices::openUrl(url);
        };

        // Put the question to the local model. Reachable from the Ask button in
        // AI mode, and from Enter. In the WebAssembly build there is no worker,
        // so the body compiles away and nothing can call it: aiBtn stays null.
        auto askAi = [this, dlg, edit, results]() {
            if (edit->text().trimmed().isEmpty()) return;
#ifndef __EMSCRIPTEN__
            m_aiOut = results;              // QPointer: cleared when dlg closes
            results->show();
            aiAsk(edit->text());
            if (dlg->height() < 560)
                dlg->resize(dlg->width(), 560);
#endif
        };

        // Enter follows whichever mode is on. The buttons do not -- each one
        // always does the single thing its label says.
        auto submit = [aiBtn, askAi, findInManual]() {
            if (aiBtn && aiBtn->isChecked()) askAi();
            else                             findInManual();
        };

#ifndef __EMSCRIPTEN__
        // Switching mode re-labels the question box, offers the Ask button, and
        // starts the helper loading its models straight away so the wait
        // overlaps with the user still typing. "Find in manual" is deliberately
        // left alone: both routes stay available in either mode.
        connect(aiBtn, &QPushButton::toggled, dlg,
                [this, edit, qLabel, results, askBtn](bool on) {
            edit->setPlaceholderText(on ? "e.g. how do I do CTF correction?"
                                        : "e.g. convolution theorem");
            qLabel->setText(on ? "Ask a question about the Fourier Analyzer\n"
                                 "(a local model answers from the manual, or use "
                                 "Find in manual for literal matches):"
                               : "Ask a question about the Fourier Analyzer\n"
                                 "(searches the online manual):");
            askBtn->setVisible(on);
            if (m_aiThinkBtn && !on)
                m_aiThinkBtn->hide();
            if (on) {
                // This dialog has not asked anything yet. The reply fields
                // belong to the window and outlive it, so drop the previous
                // answer rather than repainting it into a fresh dialog.
                aiResetReply();
                m_aiOut = results;
                results->show();
                aiEnsureStarted();
                aiRender();
            }
        });
        connect(askBtn, &QPushButton::clicked, dlg, askAi);
#endif

        connect(findBtn,   &QPushButton::clicked, dlg, findInManual);
        connect(googleBtn, &QPushButton::clicked, dlg, searchGoogle);
        connect(edit, &QLineEdit::returnPressed, dlg, submit);
        connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

        // Open at 90% of the window's current width — the manual-search result
        // snippets read best wide — but cap it at 1024 px so the dialog stays
        // readable on very large screens. Height starts at its natural compact
        // size; the first search grows it (see findInManual).
        dlg->resize(qMin(int(width() * 0.9), 1024), dlg->sizeHint().height());
        dlg->open();
        // Land the keyboard focus in the search box so a keyword can be typed
        // straight away. setFocus() before show (above) is not reliably
        // honoured on every platform once the window appears — the link-enabled
        // intro label is also in the focus chain — so re-assert it one event
        // loop turn after the dialog is up.
        QTimer::singleShot(0, edit, [edit]() { edit->setFocus(); });
        return;
    }

    // Check history / power-spectrum slot clicks (panels 3 & 4) – activate clicked slot
    int clickedSlot = -1;
    for (int i = 0; i < HISTORY_SLOTS; i++) {
        if (m_historyRects[i].contains(event->pos())
            || m_powerSpecRects[i].contains(event->pos())) {
            clickedSlot = i;
            break;
        }
    }
    if (clickedSlot >= 0) {
        activateHistorySlot(clickedSlot);
        return;
    }

    // Custom-painted toggle buttons next to the histograms. Each sits behind
    // its panel's tool dialog, so a click only fires when the click lands on
    // the visible (uncovered) portion of the button.
    if (!m_imageHistLockRect.isNull() && m_imageHistLockRect.contains(event->pos())
        && !m_p1ToolRect.contains(event->pos())) {
        m_imageContrastLocked = !m_imageContrastLocked;
        update();
        return;
    }
    if (!m_markImageCenterRect.isNull() && m_markImageCenterRect.contains(event->pos())
        && !m_p1ToolRect.contains(event->pos())) {
        m_imageCenterMarked = !m_imageCenterMarked;
        update();
        return;
    }
    if (!m_ftHistLockRect.isNull() && m_ftHistLockRect.contains(event->pos())
        && !m_p2ToolRect.contains(event->pos())) {
        m_ftContrastLocked = !m_ftContrastLocked;
        update();
        return;
    }
    if (!m_maskBtnRect.isNull() && m_maskBtnRect.contains(event->pos())
        && !m_p2ToolRect.contains(event->pos())) {
        onToggleMask(!m_maskCenter);
        return;
    }

    // Check histogram clicks – start drag for display range adjustment
    for (int h = 0; h < NUM_HISTS; h++) {
        if (!m_histRects[h].isNull() && m_histRects[h].contains(event->pos())) {
            m_histDragging = true;
            m_histDragTarget = h;
            m_histDragStartX = event->pos().x();
            return;
        }
    }

    // Check panel 1 tool group clicks (left edge). A single-member group
    // activates its tool directly; a multi-member group opens its popup grid.
    for (int g = 0; g < m_p1Groups.size(); g++) {
        if (!m_p1GroupRects[g].contains(event->pos())) continue;
        const ToolGroup &grp = m_p1Groups[g];
        if (grp.members.size() == 1) {
            activateP1Tool(grp.members[0]);
        } else {
            m_openMenuPanel = 1;
            m_openMenuGroup = g;
            update();
        }
        return;
    }

    // Measure: click two points on panel 1 image
    if (m_measureActive && !m_image.isNull()) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (di.valid && di.zoomIdx == 0 && di.screenRect.contains(event->pos())) {
                QRect target = di.screenRect.adjusted(2, 2, -2, -2);
                QRectF src = m_zoom[0].visibleRect(m_image.width(), m_image.height());
                double imgX = src.x() + (event->pos().x() - target.x()) * src.width()  / target.width();
                double imgY = src.y() + (event->pos().y() - target.y()) * src.height() / target.height();
                if (event->button() == Qt::RightButton) {
                    m_measurePlacing = 0;
                    m_measureHasLine = false;
                    update();
                    return;
                }
                if (m_measurePlacing == 0) {
                    m_measureP0 = QPointF(imgX, imgY);
                    m_measurePlacing = 1;
                    m_measureHasLine = false;
                } else {
                    m_measureP1 = QPointF(imgX, imgY);
                    m_measurePlacing = 0;
                    m_measureHasLine = true;
                }
                update();
                return;
            }
        }
    }

    // Amyloid filament: click on panel 1 image to place/drag control points
    if (m_amyloidActive && !m_image.isNull()) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (di.valid && di.zoomIdx == 0 && di.screenRect.contains(event->pos())) {
                QRect target = di.screenRect.adjusted(2, 2, -2, -2);
                QRectF src = m_zoom[0].visibleRect(m_image.width(), m_image.height());

                // Convert screen to image coords
                double imgX = src.x() + (event->pos().x() - target.x()) * src.width() / target.width();
                double imgY = src.y() + (event->pos().y() - target.y()) * src.height() / target.height();

                double hitR = src.width() / target.width() * 8; // 8 screen pixels

                // Right mouse button: delete the filament under the cursor.
                // Clicking near any control point or segment of a filament
                // removes that whole filament. If currently placing a start
                // point, the right click cancels the placement instead.
                if (event->button() == Qt::RightButton) {
                    if (m_amyloidPlacing == 1) {
                        m_amyloidPlacing = 0;
                        m_amyloidRendered = false;
                        update();
                        return;
                    }
                    for (int fi = (int)m_amyloidFilaments.size() - 1; fi >= 0; fi--) {
                        const auto &fil = m_amyloidFilaments[fi];
                        bool hit = false;
                        for (const auto &pt : fil.pts) {
                            double dx = pt.x() - imgX, dy = pt.y() - imgY;
                            if (dx * dx + dy * dy < hitR * hitR) { hit = true; break; }
                        }
                        if (!hit) {
                            for (int pi = 0; pi + 1 < (int)fil.pts.size(); pi++) {
                                QPointF a = fil.pts[pi], b = fil.pts[pi + 1];
                                double abx = b.x() - a.x(), aby = b.y() - a.y();
                                double len2 = abx * abx + aby * aby;
                                if (len2 < 1e-6) continue;
                                double t = ((imgX - a.x()) * abx + (imgY - a.y()) * aby) / len2;
                                t = std::max(0.0, std::min(1.0, t));
                                double px = a.x() + t * abx, py = a.y() + t * aby;
                                double d2 = (imgX - px) * (imgX - px) + (imgY - py) * (imgY - py);
                                if (d2 < hitR * hitR) { hit = true; break; }
                            }
                        }
                        if (hit) {
                            m_amyloidFilaments.erase(m_amyloidFilaments.begin() + fi);
                            m_amyloidDragFil = -1;
                            m_amyloidDragPt = -1;
                            m_amyloidRendered = false;
                            update();
                            return;
                        }
                    }
                    return;
                }

                // Check if clicking near an existing control point (for dragging)
                for (int fi = (int)m_amyloidFilaments.size() - 1; fi >= 0; fi--) {
                    auto &fil = m_amyloidFilaments[fi];
                    for (int pi = 0; pi < (int)fil.pts.size(); pi++) {
                        double dx = fil.pts[pi].x() - imgX;
                        double dy = fil.pts[pi].y() - imgY;
                        if (dx * dx + dy * dy < hitR * hitR) {
                            m_amyloidDragFil = fi;
                            m_amyloidDragPt = pi;
                            m_amyloidRendered = false;
                            return;
                        }
                    }
                }

                // Check if clicking near a segment to insert a new control point
                for (int fi = (int)m_amyloidFilaments.size() - 1; fi >= 0; fi--) {
                    auto &fil = m_amyloidFilaments[fi];
                    for (int pi = 0; pi + 1 < (int)fil.pts.size(); pi++) {
                        QPointF a = fil.pts[pi], b = fil.pts[pi + 1];
                        // Distance from point to segment
                        double abx = b.x() - a.x(), aby = b.y() - a.y();
                        double len2 = abx * abx + aby * aby;
                        if (len2 < 1e-6) continue;
                        double t = ((imgX - a.x()) * abx + (imgY - a.y()) * aby) / len2;
                        if (t < 0.05 || t > 0.95) continue;
                        double px = a.x() + t * abx, py = a.y() + t * aby;
                        double d2 = (imgX - px) * (imgX - px) + (imgY - py) * (imgY - py);
                        if (d2 < hitR * hitR) {
                            // Insert new control point and start dragging it
                            fil.pts.insert(fil.pts.begin() + pi + 1, QPointF(imgX, imgY));
                            m_amyloidDragFil = fi;
                            m_amyloidDragPt = pi + 1;
                            m_amyloidRendered = false;
                            update();
                            return;
                        }
                    }
                }

                // Placing new filament points
                if (m_amyloidPlacing == 0) {
                    m_amyloidStartPt = QPointF(imgX, imgY);
                    m_amyloidPlacing = 1;
                    m_amyloidRendered = false;
                    update();
                    return;
                } else if (m_amyloidPlacing == 1) {
                    AmyloidFilament fil;
                    fil.pts.push_back(m_amyloidStartPt);
                    fil.pts.push_back(QPointF(imgX, imgY));
                    m_amyloidFilaments.push_back(fil);
                    m_amyloidPlacing = 0;
                    m_amyloidRendered = false;
                    update();
                    return;
                }
            }
        }
    }

    // Crop: click-drag a square selection on panel 1 image
    if (m_cropActive && !m_image.isNull()) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (di.valid && di.zoomIdx == 0 && di.screenRect.contains(event->pos())) {
                if (event->button() == Qt::RightButton) {
                    m_cropDragging = false;
                    m_cropHasSelection = false;
                    update();
                    return;
                }
                QRect target = di.screenRect.adjusted(2, 2, -2, -2);
                QRectF src = m_zoom[0].visibleRect(m_image.width(), m_image.height());
                double imgX = src.x() + (event->pos().x() - target.x()) * src.width()  / target.width();
                double imgY = src.y() + (event->pos().y() - target.y()) * src.height() / target.height();
                imgX = std::clamp(imgX, 0.0, (double)m_image.width());
                imgY = std::clamp(imgY, 0.0, (double)m_image.height());

                // Click inside an existing square grabs it for repositioning;
                // otherwise begin a fresh square selection.
                bool insideSel = m_cropHasSelection &&
                    imgX >= m_cropRect.left() && imgX <= m_cropRect.left() + m_cropRect.width() &&
                    imgY >= m_cropRect.top()  && imgY <= m_cropRect.top()  + m_cropRect.height();
                if (insideSel) {
                    m_cropGrabOffset = QPointF(imgX - m_cropRect.left(), imgY - m_cropRect.top());
                    m_cropMoving = true;
                    return;
                }

                m_cropAnchor = QPointF(imgX, imgY);
                m_cropRect = QRect((int)std::lround(imgX), (int)std::lround(imgY), 0, 0);
                m_cropDragging = true;
                m_cropHasSelection = true;
                syncCropEdits();
                update();
                return;
            }
        }
    }

    // Panel 1 eraser/brush: start drag on panel 1 image
    if ((m_p1EraserActive || m_p1BrushActive) && !m_image.isNull()) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (di.valid && di.zoomIdx == 0 && di.screenRect.contains(event->pos())) {
                storeUndoSnapshot(m_p1EraserActive ? tr("Erased region")
                                                   : tr("Brush edit"));
                if (m_p1EraserActive) p1EraserApply(event->pos());
                else                  p1BrushApply(event->pos());
                m_p1ToolDragging = true;
                return;
            }
        }
    }

    // Shift/rotate/shear: start drag on panel 1 image
    if ((m_shiftActive || m_rotateActive || m_shearActive) && !m_image.isNull()
        && !m_p1ToolRect.contains(event->pos())) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (di.valid && di.zoomIdx == 0 && di.screenRect.contains(event->pos())) {
                // Shear snapshots inside applyShear() instead, so that a drag
                // too small to shear anything leaves no undo step behind.
                if (m_shiftActive)       storeUndoSnapshot(tr("Shifted image"));
                else if (m_rotateActive) storeUndoSnapshot(tr("Rotated image"));
                m_p1Dragging = true;
                m_p1DragStart = event->pos();
                return;
            }
        }
    }

    // Pan zoomed panel 1 image (no tool active, zoom > 1)
    if (!m_p1EraserActive && !m_p1BrushActive && !m_shiftActive && !m_rotateActive
        && !m_shearActive
        && !m_image.isNull() && m_zoom[0].factor > 1.0) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (di.valid && di.zoomIdx == 0 && di.screenRect.contains(event->pos())) {
                m_p1PanDragging = true;
                m_p1PanStart = event->pos();
                return;
            }
        }
    }

    // Check panel 2 tool group clicks (right edge).
    for (int g = 0; g < m_p2Groups.size(); g++) {
        if (!m_p2GroupRects[g].contains(event->pos())) continue;
        const ToolGroup &grp = m_p2Groups[g];
        if (grp.members.size() == 1) {
            activateP2Tool(grp.members[0]);
        } else {
            m_openMenuPanel = 2;
            m_openMenuGroup = g;
            update();
        }
        return;
    }

    if (m_lineFilterActive && m_ftComputed && m_fftN > 0) {
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
            if (imgX >= imgCenter) {
                bool ok = false;
                double angleDeg = m_lineDirectionEdit->text().toDouble(&ok);
                if (!ok) angleDeg = 0.0;
                double angle = angleDeg * M_PI / 180.0;
                double nx = -std::sin(angle);
                double ny =  std::cos(angle);
                m_lineOffset = (imgX - imgCenter) * nx + (imgY - imgCenter) * ny;
                m_lineOffsetEdit->setText(QString::number(m_lineOffset, 'f', 2));
                m_lineDragging = 1;
            } else {
                double angleDeg = std::atan2(imgY - imgCenter, imgX - imgCenter)
                                  * 180.0 / M_PI;
                while (angleDeg > 90.0)  angleDeg -= 180.0;
                while (angleDeg <= -90.0) angleDeg += 180.0;
                m_lineDirectionEdit->setText(QString::number(angleDeg, 'f', 2));
                m_lineDragging = 2;
            }
            m_toolDragging = true;
            update();
            return;
        }
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

    // FT rotate: start drag on panel 2 FFT. The tool window overlaps the FFT,
    // so clicks that land on it must not begin a rotation.
    if (m_ftRotateActive && m_ftComputed
        && !m_p2ToolRect.contains(event->pos())) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (di.valid && di.zoomIdx >= 1 && di.screenRect.contains(event->pos())) {
                storeUndoSnapshot(tr("Rotated in Fourier space"));
                m_p2Dragging = true;
                m_p2DragStart = event->pos();
                return;
            }
        }
    }

    // CTF direction line: start drag on panel 2 FFT to rotate line
    if (m_ctfActive && m_ftComputed) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (di.valid && di.zoomIdx >= 1 && di.screenRect.contains(event->pos())) {
                m_ctfDragging = true;
                m_toolDragging = true;
                double ccx = di.screenRect.center().x();
                double ccy = di.screenRect.center().y();
                m_ctfAngleDeg = std::atan2(-(event->pos().y() - ccy),
                                             event->pos().x() - ccx) * 180.0 / M_PI;
                computeCtfProfile1D();
                update();
                return;
            }
        }
    }

    // Cross-section: start drag on panel 2 FFT to rotate lines
    if (m_crossSectionActive && m_ftComputed) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (di.valid && di.zoomIdx >= 1 && di.screenRect.contains(event->pos())) {
                m_crossSectionDragging = true;
                m_toolDragging = true;
                // Compute initial angle from mouse position relative to center
                double ccx = di.screenRect.center().x();
                double ccy = di.screenRect.center().y();
                m_crossSectionAngle = std::atan2(event->pos().y() - ccy,
                                                  event->pos().x() - ccx) * 180.0 / M_PI;
                syncCrossSectionDirEdit();
                computeCrossSectionProfile();
                update();
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
                storeUndoSnapshot(m_eraserActive ? tr("Erased in Fourier space")
                                                 : tr("Painted in Fourier space"));
                if (m_eraserActive) eraserApply(event->pos());
                else                brushApply(event->pos());
                m_toolDragging = true;
                return;
            }
        }
    }

    // Pan zoomed panel 2 image (no tool dragging, zoom > 1)
    if (!m_eraserActive && !m_brushActive && !m_ftRotateActive
        && !m_bandpassActive && !m_directionalActive && !m_lineFilterActive
        && !m_latticeActive && !m_crossSectionActive
        && m_ftComputed) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (di.valid && di.zoomIdx >= 1
                && m_zoom[di.zoomIdx].factor > 1.0
                && di.screenRect.contains(event->pos())) {
                m_p2PanDragging = true;
                m_p2PanStart = event->pos();
                return;
            }
        }
    }

    // FT / FT⁻¹ arrows. Ignore clicks while a transform is still animating
    // (the WASM-animated variants run asynchronously, so a second click could
    // otherwise race the in-flight one).
    bool transformBusy = (m_fftProgress >= 0.0) || (m_iftProgress >= 0.0);

    // FT arrow
    if (!m_image.isNull() && !transformBusy) {
        QRect arrowRect = upperArrowBounds();
        if (arrowRect.contains(event->pos())) {
            computeFFTAnimated();
            update();
            return;
        }
    }

    // FT⁻¹ arrow
    if (m_ftComputed && !transformBusy) {
        QRect iftRect = lowerArrowBounds();
        if (iftRect.contains(event->pos())) {
            computeInverseFFTAnimated();
            update();
            return;
        }
    }
}

void FtWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_maxPanel != 0) {
        m_p1PanDragging = false;
        m_p2PanDragging = false;
        return;
    }

    if (m_histDragging) {
        m_histDragging = false;
        int h = m_histDragTarget;
        m_histDragTarget = -1;
        if (h >= 0 && h < NUM_HISTS && !m_histRects[h].isNull()) {
            QRect hr = m_histRects[h];
            // Map start and end X positions to value range
            double globalMin = 0, globalMax = 0;
            switch (h) {
            case HIST_P1:       globalMin = m_imageMinVal; globalMax = m_imageMaxVal; break;
            case HIST_POWER:    globalMin = m_powerMin;    globalMax = m_powerMax;    break;
            case HIST_FT_LEFT:
                if (m_displayMode == 0) { globalMin = m_cosMin;   globalMax = m_cosMax; }
                else                    { globalMin = m_ampMin;   globalMax = m_ampMax; }
                break;
            case HIST_FT_RIGHT:
                if (m_displayMode == 0) { globalMin = m_sinMin;   globalMax = m_sinMax; }
                else                    { globalMin = m_phaseMin; globalMax = m_phaseMax; }
                break;
            }
            double range = globalMax - globalMin;
            if (range <= 0) range = 1;

            int x1 = m_histDragStartX;
            int x2 = event->pos().x();
            if (x1 > x2) std::swap(x1, x2);
            // Only apply if there was meaningful movement (> 3 pixels)
            if (x2 - x1 > 3) {
                double frac1 = std::clamp((x1 - hr.x()) / (double)hr.width(), 0.0, 1.0);
                double frac2 = std::clamp((x2 - hr.x()) / (double)hr.width(), 0.0, 1.0);
                double newMin = globalMin + frac1 * range;
                double newMax = globalMin + frac2 * range;

                switch (h) {
                case HIST_P1:
                    m_imageDispMin = newMin; m_imageDispMax = newMax;
                    rebuildImageWithLUT();
                    // The threshold tool works on this very selection, so its
                    // fields follow it rather than only seeding when it opens.
                    syncThresholdEdits();
                    break;
                case HIST_POWER:
                    if (m_displayMode == 2) {
                        m_complexDispMin = newMin; m_complexDispMax = newMax;
                        m_complexRangeCustom = true;
                        buildComplexImage();
                    } else {
                        m_powerDispMin = newMin; m_powerDispMax = newMax;
                        rebuildFTImageWithLUT(HIST_POWER);
                    }
                    break;
                case HIST_FT_LEFT:
                    if (m_displayMode == 0) { m_cosDispMin = newMin; m_cosDispMax = newMax; }
                    else                    { m_ampDispMin = newMin; m_ampDispMax = newMax; }
                    rebuildFTImageWithLUT(HIST_FT_LEFT);
                    break;
                case HIST_FT_RIGHT:
                    if (m_displayMode == 0) { m_sinDispMin = newMin; m_sinDispMax = newMax; }
                    else                    { m_phaseDispMin = newMin; m_phaseDispMax = newMax; }
                    rebuildFTImageWithLUT(HIST_FT_RIGHT);
                    break;
                }
                update();
            } else {
                switch (h) {
                case HIST_P1:
                    m_imageDispMin = m_imageMinVal;
                    m_imageDispMax = m_imageMaxVal;
                    rebuildImageWithLUT();
                    syncThresholdEdits();
                    break;
                case HIST_POWER:
                    if (m_displayMode == 2) {
                        resetComplexDisplayRange();
                        buildComplexImage();
                    } else {
                        m_powerDispMin = m_powerMin;
                        m_powerDispMax = m_powerMax;
                        rebuildFTImageWithLUT(HIST_POWER);
                    }
                    break;
                case HIST_FT_LEFT:
                    if (m_displayMode == 0) { m_cosDispMin = m_cosMin; m_cosDispMax = m_cosMax; }
                    else                    { m_ampDispMin = m_ampMin; m_ampDispMax = m_ampMax; }
                    rebuildFTImageWithLUT(HIST_FT_LEFT);
                    break;
                case HIST_FT_RIGHT:
                    if (m_displayMode == 0) { m_sinDispMin = m_sinMin; m_sinDispMax = m_sinMax; }
                    else                    { m_phaseDispMin = m_phaseMin; m_phaseDispMax = m_phaseMax; }
                    rebuildFTImageWithLUT(HIST_FT_RIGHT);
                    break;
                }
                update();
            }
        }
        return;
    }

    if (m_amyloidDragFil >= 0) {
        m_amyloidDragFil = -1;
        m_amyloidDragPt = -1;
        update();
    }

    if (m_cropDragging || m_cropMoving) {
        m_cropDragging = false;
        m_cropMoving = false;
        if (m_cropRect.width() < 2 || m_cropRect.height() < 2)
            m_cropHasSelection = false;
        syncCropEdits();
        update();
    }

    if (m_p1ToolDragging) {
        m_p1ToolDragging = false;
        if (m_ftComputed) {
            computeFFT();
            update();
        }
    }

    if (m_p1PanDragging) {
        m_p1PanDragging = false;
    }

    if (m_p2PanDragging) {
        m_p2PanDragging = false;
    }

    if (m_toolDragging) {
        bool wasPainting = (m_eraserActive || m_brushActive)
                           && m_bandDragging == 0 && m_dirDragging == 0
                           && m_latticeDragging == 0
                           && !m_crossSectionDragging;
        bool wasCrossSection = m_crossSectionDragging;
        m_toolDragging = false;
        m_bandDragging = 0;
        m_dirDragging = 0;
        m_latticeDragging = 0;
        m_lineDragging = 0;
        m_crossSectionDragging = false;
        m_ctfDragging = false;
        if (wasCrossSection && m_ftComputed) {
            computeCrossSectionProfile();
            update();
        }
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

            // Real-space rotation center = center of original image in N×N grid
            double rcx = (m_origW - 1) / 2.0;
            double rcy = (m_origH - 1) / 2.0;

            // Un-shift to standard DFT layout (DC at [0,0])
            std::vector<Complex> freq(m_fftData);
            fftShift(freq, N);

            // Rotate in Fourier space with phase correction for off-center rotation.
            // For rotation around (rcx, rcy) instead of (0,0), the DFT shift theorem
            // gives: G(u,v) = F(u',v') · exp(j·2π·((u'-u)·rcx + (v'-v)·rcy)/N)
            // where (u',v') is the nearest source frequency from inverse rotation.
            std::vector<Complex> rotated(N * N, Complex(0, 0));
            for (int v = 0; v < N; v++) {
                for (int u = 0; u < N; u++) {
                    double us = (u <= halfN) ? (double)u : (double)(u - N);
                    double vs = (v <= halfN) ? (double)v : (double)(v - N);

                    // Inverse-rotate to find source signed frequency
                    double uSrcF = us * cosA - vs * sinA;
                    double vSrcF = us * sinA + vs * cosA;

                    // Nearest-neighbor (signed integer source frequency)
                    int uSrcI = (int)std::round(uSrcF);
                    int vSrcI = (int)std::round(vSrcF);

                    // Wrap to array indices
                    int su = ((uSrcI % N) + N) % N;
                    int sv = ((vSrcI % N) + N) % N;

                    // Phase correction for off-center rotation
                    double du = (double)uSrcI - us;
                    double dv = (double)vSrcI - vs;
                    double phase = -2.0 * M_PI * (du * rcx + dv * rcy) / N;
                    Complex phasor(std::cos(phase), std::sin(phase));

                    rotated[v * N + u] = freq[sv * N + su] * phasor;
                }
            }

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

            if (m_shearActive) {
                // Shear runs on the raw pixel values rather than on the 8-bit
                // display image, so it takes the whole path itself — undo
                // snapshot, FFT, history slot — and must not fall through to
                // the extractImageData() below, which would re-derive the raw
                // values from the quantised image and lose the dynamic range.
                double angleDeg = 0.0;
                bool vertical = false;
                if (shearFromDrag(di, m_p1DragStart, event->pos(), angleDeg, vertical)) {
                    m_shearAngleEdit->setText(QString::number(angleDeg, 'f', 2));
                    m_shearAxisCombo->setCurrentIndex(vertical ? 1 : 0);
                    if (ensureCalcHeadroom(tr("shear the image")))
                        applyShear(angleDeg, vertical);
                }
                update();
                break;
            }

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
    const QPoint prevMouse = m_mousePos;
    m_mousePos = event->pos();

    // Maximized view: only pan dragging is live. Skipping the handlers below
    // also keeps them off the hit rects left over from the last normal paint.
    if (m_maxPanel != 0) {
        // Repaint just when the close button's hover state flips, rather than
        // on every move — the maximized image can be an expensive redraw.
        if (m_maxCloseRect.contains(prevMouse) != m_maxCloseRect.contains(m_mousePos))
            update();
        if (!m_p1PanDragging && !m_p2PanDragging) return;
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (!di.valid || di.zoomIdx < 0) continue;
            if (m_p1PanDragging != (di.zoomIdx == 0)) continue;

            ZoomState &z = m_zoom[di.zoomIdx];
            QRectF src = itemSrcRect(di);
            QPoint &start = (di.zoomIdx == 0) ? m_p1PanStart : m_p2PanStart;
            double dxImg = (event->pos().x() - start.x())
                           / (double)di.screenRect.width() * src.width();
            double dyImg = (event->pos().y() - start.y())
                           / (double)di.screenRect.height() * src.height();
            z.centerX -= dxImg;
            z.centerY -= dyImg;
            z.centerX = ZoomState::clampCenter(z.centerX, src.width(),  di.imgW);
            z.centerY = ZoomState::clampCenter(z.centerY, src.height(), di.imgH);

            // Keep the two Fourier images locked together, as in normal layout.
            if ((m_displayMode == 0 || m_displayMode == 1) &&
                (di.zoomIdx == 1 || di.zoomIdx == 2)) {
                m_zoom[(di.zoomIdx == 1) ? 2 : 1] = z;
            }

            start = event->pos();
            update();
            break;
        }
        return;
    }

    // Check if mouse is over a histogram – show tooltip
    {
        bool overHist = false;
        for (int h = 0; h < NUM_HISTS; h++) {
            if (!m_histRects[h].isNull() && m_histRects[h].contains(event->pos())) {
                overHist = true;
                break;
            }
        }
        // Check if mouse is over a painted parameter-label rectangle
        QString paramTip;
        for (const auto &e : m_paramLabelTips) {
            if (e.first.contains(event->pos())) {
                paramTip = e.second;
                break;
            }
        }
        auto overRect = [&](const QRect &r, bool p2) {
            const QRect &dlg = p2 ? m_p2ToolRect : m_p1ToolRect;
            return !r.isNull() && r.contains(event->pos()) && !dlg.contains(event->pos());
        };
        QString lockTipImg =
            "Lock the display contrast range for the real-space image.\n"
            "When checked, the contrast is preserved across Compute\n"
            "operations. Uncheck for auto-contrast (full dynamic range).";
        QString lockTipFt =
            "Lock the display contrast range for all Fourier-space images.\n"
            "When checked, the contrast is preserved across Compute\n"
            "operations. Uncheck for auto-contrast (full dynamic range).";
        if (overHist)
            setToolTip("Click to adjust display contrast");
        else if (overRect(m_imageHistLockRect, false))
            setToolTip(lockTipImg);
        else if (overRect(m_markImageCenterRect, false))
            setToolTip("Mark the geometric center of the loaded image with a red cross.");
        else if (overRect(m_pixelSizeInfoRect, false))
            setToolTip("Double-click here to edit the pixel size");
        else if (overRect(m_ftHistLockRect, true))
            setToolTip(lockTipFt);
        else if (overRect(m_maskBtnRect, true))
            setToolTip("Mask out the central pixels of the Fourier transform display so\n"
                       "the bright DC component does not dominate the contrast.");
        else if (!m_image.isNull() && upperArrowBounds().contains(event->pos()))
            setToolTip("Compute Fourier Transform");
        else if (m_ftComputed && lowerArrowBounds().contains(event->pos()))
            setToolTip("Compute Inverse Fourier Transform");
        else if (!paramTip.isEmpty())
            setToolTip(paramTip);
        else if (!m_histDragging)
            setToolTip(QString());
    }

    if (m_histDragging) {
        update();
        return;
    }

    // Amyloid control point dragging
    if (m_amyloidDragFil >= 0 && m_amyloidDragPt >= 0 && !m_image.isNull()) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (!di.valid || di.zoomIdx != 0) continue;
            QRect target = di.screenRect.adjusted(2, 2, -2, -2);
            QRectF src = m_zoom[0].visibleRect(m_image.width(), m_image.height());
            double imgX = src.x() + (event->pos().x() - target.x()) * src.width() / target.width();
            double imgY = src.y() + (event->pos().y() - target.y()) * src.height() / target.height();
            imgX = std::max(0.0, std::min((double)m_image.width() - 1, imgX));
            imgY = std::max(0.0, std::min((double)m_image.height() - 1, imgY));
            auto &fil = m_amyloidFilaments[m_amyloidDragFil];
            fil.pts[m_amyloidDragPt] = QPointF(imgX, imgY);
            update();
            return;
        }
    }

    // Crop selection move: drag an existing square to a new position
    if (m_cropMoving && !m_image.isNull()) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (!di.valid || di.zoomIdx != 0) continue;
            QRect target = di.screenRect.adjusted(2, 2, -2, -2);
            QRectF src = m_zoom[0].visibleRect(m_image.width(), m_image.height());
            double curX = src.x() + (event->pos().x() - target.x()) * src.width()  / target.width();
            double curY = src.y() + (event->pos().y() - target.y()) * src.height() / target.height();
            int side = m_cropRect.width();
            int W = m_image.width(), H = m_image.height();
            int left = (int)std::lround(curX - m_cropGrabOffset.x());
            int top  = (int)std::lround(curY - m_cropGrabOffset.y());
            left = std::clamp(left, 0, std::max(0, W - side));
            top  = std::clamp(top,  0, std::max(0, H - side));
            m_cropRect = QRect(left, top, side, side);
            syncCropEdits();
            update();
            return;
        }
    }

    // Crop selection drag: keep the region square and inside the image
    if (m_cropDragging && !m_image.isNull()) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (!di.valid || di.zoomIdx != 0) continue;
            QRect target = di.screenRect.adjusted(2, 2, -2, -2);
            QRectF src = m_zoom[0].visibleRect(m_image.width(), m_image.height());
            double curX = src.x() + (event->pos().x() - target.x()) * src.width()  / target.width();
            double curY = src.y() + (event->pos().y() - target.y()) * src.height() / target.height();
            double W = m_image.width(), H = m_image.height();
            double x0 = m_cropAnchor.x(), y0 = m_cropAnchor.y();
            double dx = curX - x0, dy = curY - y0;
            double signX = (dx < 0) ? -1.0 : 1.0;
            double signY = (dy < 0) ? -1.0 : 1.0;
            double side = std::max(std::abs(dx), std::abs(dy));
            double maxSideX = (signX > 0) ? (W - x0) : x0;
            double maxSideY = (signY > 0) ? (H - y0) : y0;
            side = std::min({side, maxSideX, maxSideY});
            double left = (signX > 0) ? x0 : x0 - side;
            double top  = (signY > 0) ? y0 : y0 - side;
            m_cropRect = QRect((int)std::lround(left), (int)std::lround(top),
                               (int)std::lround(side), (int)std::lround(side));
            m_cropHasSelection = true;
            syncCropEdits();
            update();
            return;
        }
    }

    if (m_p1ToolDragging && !m_image.isNull()) {
        if (m_p1EraserActive) p1EraserApply(event->pos());
        else if (m_p1BrushActive) p1BrushApply(event->pos());
        return;
    }

    if (m_p1PanDragging && !m_image.isNull()) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (!di.valid || di.zoomIdx != 0) continue;
            ZoomState &z = m_zoom[0];
            QRectF src = z.visibleRect(di.imgW, di.imgH);
            double dxImg = (event->pos().x() - m_p1PanStart.x())
                           / (double)di.screenRect.width() * src.width();
            double dyImg = (event->pos().y() - m_p1PanStart.y())
                           / (double)di.screenRect.height() * src.height();
            z.centerX -= dxImg;
            z.centerY -= dyImg;
            double hw = src.width() / 2.0, hh = src.height() / 2.0;
            z.centerX = std::clamp(z.centerX, hw, (double)di.imgW - hw);
            z.centerY = std::clamp(z.centerY, hh, (double)di.imgH - hh);
            m_p1PanStart = event->pos();
            update();
            break;
        }
        return;
    }

    if (m_p2PanDragging && m_ftComputed) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (!di.valid || di.zoomIdx < 1) continue;
            ZoomState &z = m_zoom[di.zoomIdx];
            QRectF src = z.visibleRect(di.imgW, di.imgH);
            double dxImg = (event->pos().x() - m_p2PanStart.x())
                           / (double)di.screenRect.width() * src.width();
            double dyImg = (event->pos().y() - m_p2PanStart.y())
                           / (double)di.screenRect.height() * src.height();
            z.centerX -= dxImg;
            z.centerY -= dyImg;
            double hw = src.width() / 2.0, hh = src.height() / 2.0;
            z.centerX = std::clamp(z.centerX, hw, (double)di.imgW - hw);
            z.centerY = std::clamp(z.centerY, hh, (double)di.imgH - hh);

            // Sync both FT panels if in cos/sin or amp/phase mode
            if ((m_displayMode == 0 || m_displayMode == 1) &&
                (di.zoomIdx == 1 || di.zoomIdx == 2)) {
                int other = (di.zoomIdx == 1) ? 2 : 1;
                m_zoom[other] = z;
            }

            m_p2PanStart = event->pos();
            update();
            break;
        }
        return;
    }

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
        if (m_lineDragging && m_fftN > 0) {
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
                if (m_lineDragging == 1) {
                    bool ok = false;
                    double angleDeg = m_lineDirectionEdit->text().toDouble(&ok);
                    if (!ok) angleDeg = 0.0;
                    double angle = angleDeg * M_PI / 180.0;
                    double nx = -std::sin(angle);
                    double ny =  std::cos(angle);
                    m_lineOffset = (imgX - imgCenter) * nx + (imgY - imgCenter) * ny;
                    m_lineOffsetEdit->setText(QString::number(m_lineOffset, 'f', 2));
                } else {
                    double angleDeg = std::atan2(imgY - imgCenter, imgX - imgCenter)
                                      * 180.0 / M_PI;
                    while (angleDeg > 90.0)   angleDeg -= 180.0;
                    while (angleDeg <= -90.0) angleDeg += 180.0;
                    m_lineDirectionEdit->setText(QString::number(angleDeg, 'f', 2));
                }
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
                // Round to 0.1 pixel precision for fine control
                double vx = std::round((imgX - imgCenter) * 10.0) / 10.0;
                double vy = std::round((imgY - imgCenter) * 10.0) / 10.0;
                if (m_latticeDragging == 1) {
                    m_latticeUx = vx;
                    m_latticeUy = vy;
                } else {
                    m_latticeVx = vx;
                    m_latticeVy = vy;
                }
                syncLatticeVectorEdits();
                update();
                break;
            }
            return;
        }
        if (m_crossSectionDragging) {
            for (int i = 0; i < m_numDispItems; i++) {
                const DisplayItem &di = m_dispItems[i];
                if (!di.valid || di.zoomIdx < 1) continue;
                double ccx = di.screenRect.center().x();
                double ccy = di.screenRect.center().y();
                m_crossSectionAngle = std::atan2(event->pos().y() - ccy,
                                                  event->pos().x() - ccx) * 180.0 / M_PI;
                syncCrossSectionDirEdit();
                if (m_ftComputed) computeCrossSectionProfile();
                update();
                break;
            }
            return;
        }
        if (m_ctfDragging) {
            for (int i = 0; i < m_numDispItems; i++) {
                const DisplayItem &di = m_dispItems[i];
                if (!di.valid || di.zoomIdx < 1) continue;
                double ccx = di.screenRect.center().x();
                double ccy = di.screenRect.center().y();
                m_ctfAngleDeg = std::atan2(-(event->pos().y() - ccy),
                                             event->pos().x() - ccx) * 180.0 / M_PI;
                computeCtfProfile1D();
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
//  Double-click – reset histogram display range to global min/max
// ---------------------------------------------------------------------------
void FtWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_maxPanel != 0) return;   // display-only view

    // Double-click the panel-1 size / pixel-size info to edit the pixel size.
    if (!m_pixelSizeInfoRect.isNull() && m_pixelSizeInfoRect.contains(event->pos())
        && !m_image.isNull()) {
        onEditPixelSize();
        return;
    }

    for (int h = 0; h < NUM_HISTS; h++) {
        if (!m_histRects[h].isNull() && m_histRects[h].contains(event->pos())) {
            switch (h) {
            case HIST_P1:
                m_imageDispMin = m_imageMinVal;
                m_imageDispMax = m_imageMaxVal;
                rebuildImageWithLUT();
                break;
            case HIST_POWER:
                if (m_displayMode == 2) {
                    resetComplexDisplayRange();
                    buildComplexImage();
                } else {
                    m_powerDispMin = m_powerMin;
                    m_powerDispMax = m_powerMax;
                    rebuildFTImageWithLUT(HIST_POWER);
                }
                break;
            case HIST_FT_LEFT:
                if (m_displayMode == 0) { m_cosDispMin = m_cosMin; m_cosDispMax = m_cosMax; }
                else                    { m_ampDispMin = m_ampMin; m_ampDispMax = m_ampMax; }
                rebuildFTImageWithLUT(HIST_FT_LEFT);
                break;
            case HIST_FT_RIGHT:
                if (m_displayMode == 0) { m_sinDispMin = m_sinMin; m_sinDispMax = m_sinMax; }
                else                    { m_phaseDispMin = m_phaseMin; m_phaseDispMax = m_phaseMax; }
                rebuildFTImageWithLUT(HIST_FT_RIGHT);
                break;
            }
            update();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

// ---------------------------------------------------------------------------
//  Wheel – zoom
// ---------------------------------------------------------------------------
// ESC is the documented way out of the maximized image view.
// Walk to the next buffer that holds an image, wrapping round the sixteen. A
// deferred slot counts: it holds a picture, it has just not been read off disk
// yet, and activating it is what pays for that.
bool FtWindow::stepToAdjacentBuffer(int dir)
{
    if (m_activeSlot < 0 || dir == 0) return false;
    for (int n = 1; n < HISTORY_SLOTS; n++) {
        const int cand = ((m_activeSlot + dir * n) % HISTORY_SLOTS + HISTORY_SLOTS)
                         % HISTORY_SLOTS;
        if (!bufferInUse(cand) && !m_history[cand].deferred) continue;
        activateHistorySlot(cand);
        // Switching a buffer goes through code that shows the display-mode
        // button; the maximized view carries no chrome, so put it away again
        // and let exitMaximized() work out what it should be on the way back.
        if (m_maxPanel != 0) m_modeBtn->hide();
        flashMaximizedSlotLetter();
        return true;
    }
    return false;
}

void FtWindow::flashMaximizedSlotLetter()
{
    m_maxSlotFlash = m_activeSlot;
    if (!m_maxSlotFlashTimer) {
        m_maxSlotFlashTimer = new QTimer(this);
        m_maxSlotFlashTimer->setSingleShot(true);
        connect(m_maxSlotFlashTimer, &QTimer::timeout, this, [this]() {
            m_maxSlotFlash = -1;
            update();
        });
    }
    m_maxSlotFlashTimer->start(1000);   // restarts on every press
    update();
}

void FtWindow::keyPressEvent(QKeyEvent *event)
{
    if (m_maxPanel != 0) {
        if (event->key() == Qt::Key_Escape) {
            exitMaximized();
            event->accept();
            return;
        }
        // The arrow keys page through the buffers while the view is up. Left
        // and Up go back, Right and Down go forward — there is one list, not
        // two, so either axis walks it.
        const int k = event->key();
        if (k == Qt::Key_Left || k == Qt::Key_Up
            || k == Qt::Key_Right || k == Qt::Key_Down) {
            const int dir = (k == Qt::Key_Left || k == Qt::Key_Up) ? -1 : +1;
            stepToAdjacentBuffer(dir);
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void FtWindow::wheelEvent(QWheelEvent *event)
{
    QPoint pos = event->position().toPoint();

    // Trackpad scroll events have phases (Begin/Update/End/Momentum);
    // mouse wheel events have NoScrollPhase.
    bool isTrackpad = (event->phase() != Qt::NoScrollPhase);

    for (int i = 0; i < m_numDispItems; i++) {
        const DisplayItem &di = m_dispItems[i];
        if (!di.valid || di.zoomIdx < 0) continue;
        if (!di.screenRect.contains(pos)) continue;

        ZoomState &z = m_zoom[di.zoomIdx];

        if (isTrackpad && z.factor > 1.0) {
            // Trackpad two-finger scroll while zoomed → pan
            QRectF src = itemSrcRect(di);
            // Use pixelDelta if available, otherwise fall back to angleDelta
            double dx, dy;
            if (!event->pixelDelta().isNull()) {
                dx = event->pixelDelta().x();
                dy = event->pixelDelta().y();
            } else {
                dx = event->angleDelta().x() * 0.5;
                dy = event->angleDelta().y() * 0.5;
            }
            double dxImg = -dx / (double)di.screenRect.width() * src.width();
            double dyImg = -dy / (double)di.screenRect.height() * src.height();

            z.centerX += dxImg;
            z.centerY += dyImg;
            z.centerX = ZoomState::clampCenter(z.centerX, src.width(),  di.imgW);
            z.centerY = ZoomState::clampCenter(z.centerY, src.height(), di.imgH);
        } else if (isTrackpad) {
            // Trackpad but not zoomed in — ignore (pinch handles zoom)
            event->accept();
            return;
        } else {
            // Mouse scroll wheel → zoom
            double step = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
            double newFactor = z.factor * step;
            if (newFactor < 1.0) newFactor = 1.0;
            if (newFactor > 64.0) newFactor = 64.0;

            if (newFactor <= 1.0) {
                z.reset(di.imgW, di.imgH);
            } else {
                QRectF src = itemSrcRect(di);
                double relX = (pos.x() - di.screenRect.x()) / (double)di.screenRect.width();
                double relY = (pos.y() - di.screenRect.y()) / (double)di.screenRect.height();
                double imgX = src.x() + relX * src.width();
                double imgY = src.y() + relY * src.height();

                // Keep the image point under the cursor pinned while the view
                // shrinks by step; the new view keeps the item's proportions.
                z.factor = newFactor;
                QRectF newSrc = itemSrcRect(di);
                z.centerX = imgX + (0.5 - relX) * newSrc.width();
                z.centerY = imgY + (0.5 - relY) * newSrc.height();

                z.centerX = ZoomState::clampCenter(z.centerX, newSrc.width(),  di.imgW);
                z.centerY = ZoomState::clampCenter(z.centerY, newSrc.height(), di.imgH);
            }
        }

        // Sync both FT panels if in cos/sin or amp/phase mode
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

// Apply a multiplicative zoom step at a given screen position.
// Shared by macOS native gesture events and cross-platform pinch gestures
// (the latter drives pinch-to-zoom on iPad / WASM).
bool FtWindow::applyPinchZoom(const QPoint &pos, double step)
{
    for (int i = 0; i < m_numDispItems; i++) {
        const DisplayItem &di = m_dispItems[i];
        if (!di.valid || di.zoomIdx < 0) continue;
        if (!di.screenRect.contains(pos)) continue;

        ZoomState &z = m_zoom[di.zoomIdx];

        double newFactor = z.factor * step;
        if (newFactor < 1.0) newFactor = 1.0;
        if (newFactor > 64.0) newFactor = 64.0;

        if (newFactor <= 1.0) {
            z.reset(di.imgW, di.imgH);
        } else {
            QRectF src = itemSrcRect(di);
            double relX = (pos.x() - di.screenRect.x()) / (double)di.screenRect.width();
            double relY = (pos.y() - di.screenRect.y()) / (double)di.screenRect.height();
            double imgX = src.x() + relX * src.width();
            double imgY = src.y() + relY * src.height();

            z.factor = newFactor;
            QRectF newSrc = itemSrcRect(di);
            z.centerX = imgX + (0.5 - relX) * newSrc.width();
            z.centerY = imgY + (0.5 - relY) * newSrc.height();

            z.centerX = ZoomState::clampCenter(z.centerX, newSrc.width(),  di.imgW);
            z.centerY = ZoomState::clampCenter(z.centerY, newSrc.height(), di.imgH);
        }

        if ((m_displayMode == 0 || m_displayMode == 1) &&
            (di.zoomIdx == 1 || di.zoomIdx == 2)) {
            int other = (di.zoomIdx == 1) ? 2 : 1;
            m_zoom[other] = m_zoom[di.zoomIdx];
        }

        update();
        return true;
    }
    return false;
}

// Pinch-to-zoom: macOS native gesture events + cross-platform QPinchGesture
// (the latter covers iPad / touch devices under WASM).
bool FtWindow::event(QEvent *event)
{
    if (event->type() == QEvent::NativeGesture) {
        auto *ge = static_cast<QNativeGestureEvent *>(event);
        if (ge->gestureType() == Qt::ZoomNativeGesture) {
            if (applyPinchZoom(ge->position().toPoint(), 1.0 + ge->value())) {
                event->accept();
                return true;
            }
        }
    } else if (event->type() == QEvent::Gesture) {
        auto *ge = static_cast<QGestureEvent *>(event);
        if (auto *pinch = static_cast<QPinchGesture *>(ge->gesture(Qt::PinchGesture))) {
            // Only zoom while the pinch is actively updating. On some backends
            // (notably the WebAssembly one) the finishing/cancel event reports
            // a collapsed scale factor, which would otherwise multiply the zoom
            // back down to its lowest level when the user lifts their fingers.
            Qt::GestureState st = pinch->state();
            if (st == Qt::GestureFinished || st == Qt::GestureCanceled) {
                event->accept();
                return true;
            }
            if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged) {
                double step = pinch->scaleFactor();
                if (step > 0.0 && std::isfinite(step)) {
                    QPoint pos = mapFromGlobal(pinch->centerPoint().toPoint());
                    if (applyPinchZoom(pos, step)) {
                        event->accept();
                        return true;
                    }
                }
            }
        }
    }
    return QWidget::event(event);
}
