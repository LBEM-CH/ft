#include "ftwindow_common.h"

// ---------------------------------------------------------------------------
//  Mouse
// ---------------------------------------------------------------------------
void FtWindow::mousePressEvent(QMouseEvent *event)
{
    // Check history slot clicks (panel 3) – activate clicked slot
    for (int i = 0; i < HISTORY_SLOTS; i++) {
        if (m_historyRects[i].contains(event->pos())) {
            if (i == m_activeSlot) return;   // already active

            // Save current active image back to its slot
            if (m_activeSlot >= 0 && !m_image.isNull()) {
                m_history[m_activeSlot].image        = m_image;
                m_history[m_activeSlot].path         = m_imagePath;
                m_history[m_activeSlot].rawPixels    = m_imageRawPixels;
                m_history[m_activeSlot].minVal       = m_imageMinVal;
                m_history[m_activeSlot].maxVal       = m_imageMaxVal;
                m_history[m_activeSlot].pixelSize    = m_pixelSize;
                m_history[m_activeSlot].powerSpecImg = computePowerSpecMasked(m_image);
                m_history[m_activeSlot].occupied     = true;
            }

            // Activate the clicked slot
            m_activeSlot = i;

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
            }

            m_ftComputed = false;
            m_modeBtn->setText(modeLabel());
            m_modeBtn->hide();
            m_maskBtn->hide();

            if (!m_image.isNull()) {
                m_zoom[0].reset(m_image.width(), m_image.height());
                computeFFT();
            }

            saveHistory();
#ifndef __EMSCRIPTEN__
            QSettings settings("ft", "ft");
            settings.setValue("lastFile", m_imagePath);
            settings.setValue("activeSlot", m_activeSlot);
#endif
            update();
            return;
        }
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

    // Check panel 1 tool button clicks (left edge)
    auto deactivateAllP1Tools = [&]() {
        m_p1EraserActive = false; m_p1BrushActive = false;
        m_shiftActive = false; m_rotateActive = false;
        m_p1TaperActive = false; m_binActive = false; m_mathActive = false;
        m_peakPickActive = false; m_extractActive = false;
        m_gaborActive = false; m_hessianActive = false;
    };
    auto showP1ToolWidgets = [&]() {
        m_p1EraserDiameterEdit->setVisible(m_p1EraserActive);
        m_p1BrushValueEdit->setVisible(m_p1BrushActive);
        m_p1BrushDiameterEdit->setVisible(m_p1BrushActive);
        m_p1TaperWidthEdit->setVisible(m_p1TaperActive);
        m_applyP1TaperBtn->setVisible(m_p1TaperActive);
        m_binCombo->setVisible(m_binActive);
        m_binKeepSizeBtn->setVisible(m_binActive);
        m_applyBinBtn->setVisible(m_binActive);
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
    };
    if (m_p1BtnRects[0].contains(event->pos())) {
        bool was = m_p1EraserActive; deactivateAllP1Tools();
        m_p1EraserActive = !was; showP1ToolWidgets(); update(); return;
    }
    if (m_p1BtnRects[1].contains(event->pos())) {
        bool was = m_p1BrushActive; deactivateAllP1Tools();
        m_p1BrushActive = !was;
        if (m_p1BrushActive && !m_image.isNull()) {
            double defVal = m_imageMaxVal > 0 ? m_imageMaxVal : 1.0;
            m_p1BrushValueEdit->setText(QString::number(defVal, 'g', 5));
        }
        showP1ToolWidgets(); update(); return;
    }
    if (m_p1BtnRects[2].contains(event->pos()) && !m_image.isNull()) {
        deactivateAllP1Tools(); showP1ToolWidgets();
        storeUndoSnapshot();
        m_image = m_image.mirrored(true, false);
        extractImageData();
        if (m_ftComputed) computeFFT();
        update();
        return;
    }
    if (m_p1BtnRects[3].contains(event->pos()) && !m_image.isNull()) {
        deactivateAllP1Tools(); showP1ToolWidgets();
        storeUndoSnapshot();
        m_image = m_image.mirrored(false, true);
        extractImageData();
        if (m_ftComputed) computeFFT();
        update();
        return;
    }
    if (m_p1BtnRects[4].contains(event->pos())) {
        bool was = m_shiftActive; deactivateAllP1Tools();
        m_shiftActive = !was; showP1ToolWidgets(); update(); return;
    }
    if (m_p1BtnRects[5].contains(event->pos())) {
        bool was = m_rotateActive; deactivateAllP1Tools();
        m_rotateActive = !was; showP1ToolWidgets(); update(); return;
    }
    if (m_p1BtnRects[6].contains(event->pos()) && !m_image.isNull()) {
        deactivateAllP1Tools(); showP1ToolWidgets();
        onInvertContrast();
        return;
    }
    if (m_p1BtnRects[7].contains(event->pos())) {
        bool was = m_p1TaperActive; deactivateAllP1Tools();
        m_p1TaperActive = !was; showP1ToolWidgets(); update(); return;
    }
    if (m_p1BtnRects[8].contains(event->pos())) {
        bool was = m_binActive; deactivateAllP1Tools();
        m_binActive = !was; showP1ToolWidgets(); update(); return;
    }
    if (m_p1BtnRects[9].contains(event->pos())) {
        bool was = m_mathActive; deactivateAllP1Tools();
        m_mathActive = !was; showP1ToolWidgets(); update(); return;
    }
    if (m_p1BtnRects[10].contains(event->pos())) {
        bool was = m_peakPickActive; deactivateAllP1Tools();
        m_peakPickActive = !was;
        if (m_peakPickActive) {
            m_peakThresholdSlider->setValue(750);
            if (m_activeSlot >= 0)
                m_peakSourceCombo->setCurrentIndex(m_activeSlot);
        }
        showP1ToolWidgets(); update(); return;
    }
    if (m_p1BtnRects[11].contains(event->pos())) {
        bool was = m_extractActive; deactivateAllP1Tools();
        m_extractActive = !was;
        if (m_extractActive && m_activeSlot >= 0)
            m_extractSourceCombo->setCurrentIndex(m_activeSlot);
        showP1ToolWidgets(); update(); return;
    }
    if (m_p1BtnRects[12].contains(event->pos())) {
        bool was = m_gaborActive; deactivateAllP1Tools();
        m_gaborActive = !was; showP1ToolWidgets(); update(); return;
    }
    if (m_p1BtnRects[13].contains(event->pos())) {
        bool was = m_hessianActive; deactivateAllP1Tools();
        m_hessianActive = !was; showP1ToolWidgets(); update(); return;
    }

    // Panel 1 eraser/brush: start drag on panel 1 image
    if ((m_p1EraserActive || m_p1BrushActive) && !m_image.isNull()) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (di.valid && di.zoomIdx == 0 && di.screenRect.contains(event->pos())) {
                storeUndoSnapshot();
                if (m_p1EraserActive) p1EraserApply(event->pos());
                else                  p1BrushApply(event->pos());
                m_p1ToolDragging = true;
                return;
            }
        }
    }

    // Shift/rotate: start drag on panel 1 image
    if ((m_shiftActive || m_rotateActive) && !m_image.isNull()) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (di.valid && di.zoomIdx == 0 && di.screenRect.contains(event->pos())) {
                storeUndoSnapshot();
                m_p1Dragging = true;
                m_p1DragStart = event->pos();
                return;
            }
        }
    }

    // Pan zoomed panel 1 image (no tool active, zoom > 1)
    if (!m_p1EraserActive && !m_p1BrushActive && !m_shiftActive && !m_rotateActive
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

    // Check tool button clicks (panel 2 right edge)
    auto deactivateAllTools = [&]() {
        m_eraserActive = false; m_brushActive = false;
        m_bandpassActive = false; m_directionalActive = false;
        m_lineFilterActive = false;
        m_latticeActive = false; m_ftRotateActive = false;
        m_crossSectionActive = false;
        m_ftCropActive = false; m_ftMathActive = false;
    };
    auto showToolWidgets = [&]() {
        bool showFilter = m_bandpassActive || m_directionalActive;
        m_smoothEdit->setVisible(showFilter);
        m_bandEraseOutside->setVisible(showFilter);
        m_applyBandBtn->setVisible(showFilter);

        m_brushValueEdit->setVisible(m_brushActive);
        m_brushDiameterEdit->setVisible(m_brushActive);

        m_eraserDiameterEdit->setVisible(m_eraserActive);

        m_lineWidthEdit->setVisible(m_lineFilterActive);
        m_lineDirectionEdit->setVisible(m_lineFilterActive);
        m_lineEraseOutsideBtn->setVisible(m_lineFilterActive);
        m_applyLineBtn->setVisible(m_lineFilterActive);

        m_latticeSmoothEdit->setVisible(m_latticeActive);
        m_latticeDotDiamEdit->setVisible(m_latticeActive);
        m_latticeEraseOutside->setVisible(m_latticeActive);
        m_latticeApplyBtn->setVisible(m_latticeActive);

        m_crossSectionWidthEdit->setVisible(m_crossSectionActive);

        m_ftCropCombo->setVisible(m_ftCropActive);
        m_ftCropKeepSizeBtn->setVisible(m_ftCropActive);
        m_applyFtCropBtn->setVisible(m_ftCropActive);

        m_ftMathOutCombo->setVisible(m_ftMathActive);
        m_ftMathEqualsLabel->setVisible(m_ftMathActive);
        m_ftMathIn1Combo->setVisible(m_ftMathActive);
        m_ftMathOpCombo->setVisible(m_ftMathActive);
        m_ftMathIn2Combo->setVisible(m_ftMathActive);
        m_ftMathConjCombo->setVisible(m_ftMathActive);
        m_ftMathCancelBtn->setVisible(m_ftMathActive);
        m_ftMathComputeBtn->setVisible(m_ftMathActive);
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
        bool was = m_lineFilterActive; deactivateAllTools();
        m_lineFilterActive = !was; showToolWidgets(); update(); return;
    }
    if (m_toolBtnRects[5].contains(event->pos())) {
        bool was = m_latticeActive; deactivateAllTools();
        m_latticeActive = !was; showToolWidgets(); update(); return;
    }
    if (m_toolBtnRects[6].contains(event->pos())) {
        bool was = m_ftRotateActive; deactivateAllTools();
        m_ftRotateActive = !was; showToolWidgets(); update(); return;
    }
    if (m_toolBtnRects[7].contains(event->pos())) {
        bool was = m_crossSectionActive; deactivateAllTools();
        m_crossSectionActive = !was;
        if (m_crossSectionActive && m_ftComputed) {
            computeCrossSectionProfile();
        } else {
            m_crossSectionProfile.clear();
        }
        showToolWidgets(); update(); return;
    }
    if (m_toolBtnRects[8].contains(event->pos())) {
        bool was = m_ftCropActive; deactivateAllTools();
        m_ftCropActive = !was; showToolWidgets(); update(); return;
    }
    if (m_toolBtnRects[9].contains(event->pos())) {
        bool was = m_ftMathActive; deactivateAllTools();
        m_ftMathActive = !was; showToolWidgets(); update(); return;
    }

    if (m_lineFilterActive && m_ftComputed && m_fftN > 0) {
        double halfN = m_fftN / 2.0;
        double imgCenter = halfN + 0.5;
        bool ok = false;
        double angleDeg = m_lineDirectionEdit->text().toDouble(&ok);
        if (!ok) angleDeg = 0.0;
        double angle = angleDeg * M_PI / 180.0;
        double nx = -std::sin(angle);
        double ny =  std::cos(angle);

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
            m_lineOffset = (imgX - imgCenter) * nx + (imgY - imgCenter) * ny;
            m_lineDragging = true;
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

    // FT rotate: start drag on panel 2 FFT
    if (m_ftRotateActive && m_ftComputed) {
        for (int i = 0; i < m_numDispItems; i++) {
            const DisplayItem &di = m_dispItems[i];
            if (di.valid && di.zoomIdx >= 1 && di.screenRect.contains(event->pos())) {
                storeUndoSnapshot();
                m_p2Dragging = true;
                m_p2DragStart = event->pos();
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
                storeUndoSnapshot();
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
                    break;
                case HIST_POWER:
                    m_powerDispMin = newMin; m_powerDispMax = newMax;
                    rebuildFTImageWithLUT(HIST_POWER);
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
                    break;
                case HIST_POWER:
                    m_powerDispMin = m_powerMin;
                    m_powerDispMax = m_powerMax;
                    rebuildFTImageWithLUT(HIST_POWER);
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
        m_lineDragging = false;
        m_crossSectionDragging = false;
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

    // Check if mouse is over a histogram – show tooltip
    {
        bool overHist = false;
        for (int h = 0; h < NUM_HISTS; h++) {
            if (!m_histRects[h].isNull() && m_histRects[h].contains(event->pos())) {
                overHist = true;
                break;
            }
        }
        if (overHist)
            setToolTip("Click to adjust display parameters");
        else if (!m_histDragging)
            setToolTip(QString());
    }

    if (m_histDragging) {
        update();
        return;
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
            bool ok = false;
            double angleDeg = m_lineDirectionEdit->text().toDouble(&ok);
            if (!ok) angleDeg = 0.0;
            double angle = angleDeg * M_PI / 180.0;
            double nx = -std::sin(angle);
            double ny =  std::cos(angle);
            for (int i = 0; i < m_numDispItems; i++) {
                const DisplayItem &di = m_dispItems[i];
                if (!di.valid || di.zoomIdx < 1) continue;
                ZoomState &z = m_zoom[di.zoomIdx];
                QRectF src = z.visibleRect(di.imgW, di.imgH);
                double imgX = src.x() + (event->pos().x() - di.screenRect.x())
                              / (double)di.screenRect.width() * src.width();
                double imgY = src.y() + (event->pos().y() - di.screenRect.y())
                              / (double)di.screenRect.height() * src.height();
                m_lineOffset = (imgX - imgCenter) * nx + (imgY - imgCenter) * ny;
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
    for (int h = 0; h < NUM_HISTS; h++) {
        if (!m_histRects[h].isNull() && m_histRects[h].contains(event->pos())) {
            switch (h) {
            case HIST_P1:
                m_imageDispMin = m_imageMinVal;
                m_imageDispMax = m_imageMaxVal;
                rebuildImageWithLUT();
                break;
            case HIST_POWER:
                m_powerDispMin = m_powerMin;
                m_powerDispMax = m_powerMax;
                rebuildFTImageWithLUT(HIST_POWER);
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
