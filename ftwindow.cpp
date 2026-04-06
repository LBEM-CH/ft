#include "ftwindow_common.h"

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
    new QShortcut(QKeySequence::Open, this, SLOT(onLoadImage()));

    // Save button
    m_saveBtn = new QPushButton("Save image", this);
    m_saveBtn->setFixedSize(100, 30);
    connect(m_saveBtn, &QPushButton::clicked, this, &FtWindow::onSaveImage);

    // Create image button
    m_createBtn = new QPushButton("Create image", this);
    m_createBtn->setFixedSize(100, 30);
    connect(m_createBtn, &QPushButton::clicked, this, &FtWindow::onCreateImage);

    // Reload button
    m_reloadBtn = new QPushButton("Reload image", this);
    m_reloadBtn->setFixedSize(100, 30);
    connect(m_reloadBtn, &QPushButton::clicked, this, &FtWindow::onReloadImage);

    // Undo / Redo button
    m_undoBtn = new QPushButton("Undo last action", this);
    m_undoBtn->setFixedSize(140, 30);
    connect(m_undoBtn, &QPushButton::clicked, this, &FtWindow::onUndoRedo);
    updateUndoButton();

    // Mode cycle button
    m_modeBtn = new QPushButton(modeLabel(), this);
    m_modeBtn->setFixedSize(180, 30);
    connect(m_modeBtn, &QPushButton::clicked, this, &FtWindow::onCycleMode);
    m_modeBtn->hide();

    // Mask-center toggle checkbox
    m_maskBtn = new QCheckBox("mask center for display", this);
    m_maskBtn->setStyleSheet("color: white;");
    connect(m_maskBtn, &QCheckBox::toggled, this, &FtWindow::onToggleMask);
    m_maskBtn->hide();

    // Bandpass filter widgets (hidden until bandpass mode active)
    m_smoothEdit = new QLineEdit("0", this);
    m_smoothEdit->setFixedSize(40, 22);
    m_smoothEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_smoothEdit->hide();

    m_bandEraseOutside = new QCheckBox("Erase pixels outside of band", this);
    m_bandEraseOutside->setStyleSheet("color: white;");
    m_bandEraseOutside->setChecked(true);
    m_bandEraseOutside->hide();

    m_applyBandBtn = new QPushButton("Apply filter", this);
    m_applyBandBtn->setFixedSize(100, 26);
    connect(m_applyBandBtn, &QPushButton::clicked, this, [this]() {
        if (m_bandpassActive) onApplyBandpass();
        else if (m_directionalActive) onApplyDirectional();
    });
    m_applyBandBtn->hide();

    // Line filter widgets (hidden until line filter mode active)
    m_lineWidthEdit = new QLineEdit("10", this);
    m_lineWidthEdit->setFixedSize(40, 22);
    m_lineWidthEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_lineWidthEdit->hide();

    m_lineDirectionEdit = new QLineEdit("0", this);
    m_lineDirectionEdit->setFixedSize(50, 22);
    m_lineDirectionEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_lineDirectionEdit->hide();

    m_lineEraseOutsideBtn = new QCheckBox("Erase pixels outside of line", this);
    m_lineEraseOutsideBtn->setStyleSheet("color: white;");
    m_lineEraseOutsideBtn->setChecked(true);
    m_lineEraseOutsideBtn->hide();

    m_applyLineBtn = new QPushButton("Apply filter", this);
    m_applyLineBtn->setFixedSize(100, 26);
    connect(m_applyLineBtn, &QPushButton::clicked, this, &FtWindow::onApplyLineFilter);
    m_applyLineBtn->hide();

    // Brush parameter widgets
    m_brushValueLabel = new QLabel("Pixel value to enter:", this);
    m_brushValueLabel->setStyleSheet("color: white;");
    m_brushValueLabel->hide();
    m_brushValueEdit = new QLineEdit("0", this);
    m_brushValueEdit->setFixedSize(60, 22);
    m_brushValueEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_brushValueEdit->hide();
    m_brushDiamLabel = new QLabel("Paint brush Gaussian diameter:", this);
    m_brushDiamLabel->setStyleSheet("color: white;");
    m_brushDiamLabel->hide();
    m_brushDiameterEdit = new QLineEdit("0", this);
    m_brushDiameterEdit->setFixedSize(40, 22);
    m_brushDiameterEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_brushDiameterEdit->hide();

    // Eraser parameter widgets
    m_eraserDiamLabel = new QLabel("Eraser Gaussian diameter:", this);
    m_eraserDiamLabel->setStyleSheet("color: white;");
    m_eraserDiamLabel->hide();
    m_eraserDiameterEdit = new QLineEdit("0", this);
    m_eraserDiameterEdit->setFixedSize(40, 22);
    m_eraserDiameterEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_eraserDiameterEdit->hide();

    // Lattice filter widgets (hidden until lattice mode active)
    m_latticeSmoothLabel = new QLabel("Smooth edge by pixels:", this);
    m_latticeSmoothLabel->setStyleSheet("color: white;");
    m_latticeSmoothLabel->hide();
    m_latticeSmoothEdit = new QLineEdit("0", this);
    m_latticeSmoothEdit->setFixedSize(40, 22);
    m_latticeSmoothEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_latticeSmoothEdit->hide();

    m_latticeDotDiamLabel = new QLabel("Diameter of dots:", this);
    m_latticeDotDiamLabel->setStyleSheet("color: white;");
    m_latticeDotDiamLabel->hide();
    m_latticeDotDiamEdit = new QLineEdit("3", this);
    m_latticeDotDiamEdit->setFixedSize(40, 22);
    m_latticeDotDiamEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_latticeDotDiamEdit->hide();

    m_latticeEraseOutside = new QCheckBox("Erase pixels outside of lattice", this);
    m_latticeEraseOutside->setStyleSheet("color: white;");
    m_latticeEraseOutside->setChecked(true);
    m_latticeEraseOutside->hide();

    m_latticeApplyBtn = new QPushButton("Apply filter", this);
    m_latticeApplyBtn->setFixedSize(100, 26);
    connect(m_latticeApplyBtn, &QPushButton::clicked, this, &FtWindow::onApplyLattice);
    m_latticeApplyBtn->hide();

    // Fourier math widgets (hidden until Fourier math mode active)
    {
        auto ftMathComboStyle = [](QComboBox *cb) {
            cb->setStyleSheet(
                "QComboBox { background:white; color:black; border:1px solid #888;"
                "  padding: 2px 8px; font-size: 26px; font-weight: bold; }"
                "QComboBox::drop-down { width: 28px; }"
                "QComboBox QAbstractItemView { background:white; color:black;"
                "  selection-background-color:#ccc; min-width: 80px; padding: 4px;"
                "  font-size: 26px; }"
            );
        };
        m_ftMathOutCombo = new QComboBox(this);
        for (int i = 0; i < HISTORY_SLOTS; i++)
            m_ftMathOutCombo->addItem(QString(QChar('A' + i)));
        m_ftMathOutCombo->setFixedSize(100, 56);
        ftMathComboStyle(m_ftMathOutCombo);
        m_ftMathOutCombo->hide();

        m_ftMathEqualsLabel = new QLabel("=", this);
        m_ftMathEqualsLabel->setStyleSheet("color: black; font-size: 36px; font-weight: bold;");
        m_ftMathEqualsLabel->setFixedSize(40, 56);
        m_ftMathEqualsLabel->setAlignment(Qt::AlignCenter);
        m_ftMathEqualsLabel->hide();

        m_ftMathIn1Combo = new QComboBox(this);
        for (int i = 0; i < HISTORY_SLOTS; i++)
            m_ftMathIn1Combo->addItem(QString(QChar('A' + i)));
        m_ftMathIn1Combo->setFixedSize(100, 56);
        ftMathComboStyle(m_ftMathIn1Combo);
        m_ftMathIn1Combo->hide();

        m_ftMathOpCombo = new QComboBox(this);
        m_ftMathOpCombo->addItem("+");
        m_ftMathOpCombo->addItem("\u2212");   // minus sign
        m_ftMathOpCombo->addItem("\u00D7");   // multiplication sign
        m_ftMathOpCombo->addItem("\u00F7");   // division sign
        m_ftMathOpCombo->setFixedSize(100, 56);
        ftMathComboStyle(m_ftMathOpCombo);
        m_ftMathOpCombo->hide();

        m_ftMathIn2Combo = new QComboBox(this);
        for (int i = 0; i < HISTORY_SLOTS; i++)
            m_ftMathIn2Combo->addItem(QString(QChar('A' + i)));
        m_ftMathIn2Combo->setFixedSize(100, 56);
        ftMathComboStyle(m_ftMathIn2Combo);
        m_ftMathIn2Combo->hide();

        m_ftMathConjCombo = new QComboBox(this);
        m_ftMathConjCombo->addItem(" ");
        m_ftMathConjCombo->addItem("* (complex conjugate)");
        m_ftMathConjCombo->setFixedSize(260, 56);
        ftMathComboStyle(m_ftMathConjCombo);
        m_ftMathConjCombo->hide();

        m_ftMathCancelBtn = new QPushButton("Cancel", this);
        m_ftMathCancelBtn->setFixedSize(80, 28);
        m_ftMathCancelBtn->setStyleSheet(
            "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
        connect(m_ftMathCancelBtn, &QPushButton::clicked, this, &FtWindow::onFtMathCancel);
        m_ftMathCancelBtn->hide();

        m_ftMathComputeBtn = new QPushButton("Compute", this);
        m_ftMathComputeBtn->setFixedSize(80, 28);
        m_ftMathComputeBtn->setStyleSheet(
            "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
        connect(m_ftMathComputeBtn, &QPushButton::clicked, this, &FtWindow::onFtMathCompute);
        m_ftMathComputeBtn->hide();
    }

    // Fourier crop widgets (hidden until Fourier crop mode active)
    m_ftCropCombo = new QComboBox(this);
    for (int i = 2; i <= 8; i++)
        m_ftCropCombo->addItem(QString::number(i), i);
    m_ftCropCombo->setFixedSize(70, 28);
    m_ftCropCombo->setStyleSheet(
        "QComboBox { background:#222; color:white; border:1px solid #888;"
        "  padding: 2px 8px; }"
        "QComboBox::drop-down { width: 20px; }"
        "QComboBox QAbstractItemView { background:#222; color:white;"
        "  selection-background-color:#555; min-width: 60px; padding: 4px; }"
    );
    m_ftCropCombo->hide();

    m_ftCropKeepSizeBtn = new QCheckBox("Keep original Fourier transform size", this);
    m_ftCropKeepSizeBtn->setStyleSheet("color: white;");
    m_ftCropKeepSizeBtn->setChecked(true);
    m_ftCropKeepSizeBtn->hide();

    m_applyFtCropBtn = new QPushButton("Apply Fourier crop", this);
    m_applyFtCropBtn->setFixedSize(140, 26);
    connect(m_applyFtCropBtn, &QPushButton::clicked, this, &FtWindow::onApplyFtCrop);
    m_applyFtCropBtn->hide();

    // Panel 1 eraser parameter widgets
    m_p1EraserDiamLabel = new QLabel("Eraser Gaussian diameter:", this);
    m_p1EraserDiamLabel->setStyleSheet("color: white;");
    m_p1EraserDiamLabel->hide();
    m_p1EraserDiameterEdit = new QLineEdit("5", this);
    m_p1EraserDiameterEdit->setFixedSize(40, 22);
    m_p1EraserDiameterEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_p1EraserDiameterEdit->hide();

    // Panel 1 brush parameter widgets
    m_p1BrushValueLabel = new QLabel("Pixel value to enter:", this);
    m_p1BrushValueLabel->setStyleSheet("color: white;");
    m_p1BrushValueLabel->hide();
    m_p1BrushValueEdit = new QLineEdit("1", this);
    m_p1BrushValueEdit->setFixedSize(60, 22);
    m_p1BrushValueEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_p1BrushValueEdit->hide();
    m_p1BrushDiamLabel = new QLabel("Paint brush Gaussian diameter:", this);
    m_p1BrushDiamLabel->setStyleSheet("color: white;");
    m_p1BrushDiamLabel->hide();
    m_p1BrushDiameterEdit = new QLineEdit("5", this);
    m_p1BrushDiameterEdit->setFixedSize(40, 22);
    m_p1BrushDiameterEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_p1BrushDiameterEdit->hide();

    // Panel 1 taper widgets
    m_p1TaperWidthLabel = new QLabel("Hanning width:", this);
    m_p1TaperWidthLabel->setStyleSheet("color: white;");
    m_p1TaperWidthLabel->hide();
    m_p1TaperWidthEdit = new QLineEdit("32", this);
    m_p1TaperWidthEdit->setFixedSize(50, 22);
    m_p1TaperWidthEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_p1TaperWidthEdit->hide();
    m_applyP1TaperBtn = new QPushButton("Apply edge taper", this);
    m_applyP1TaperBtn->setFixedSize(130, 26);
    connect(m_applyP1TaperBtn, &QPushButton::clicked, this, &FtWindow::onApplyEdgeTaper);
    m_applyP1TaperBtn->hide();

    // Math calculation widgets (hidden until math button is active)
    auto mathComboStyle = [](QComboBox *cb) {
        cb->setStyleSheet(
            "QComboBox { background:white; color:black; border:1px solid #888;"
            "  padding: 2px 8px; font-size: 26px; font-weight: bold; }"
            "QComboBox::drop-down { width: 28px; }"
            "QComboBox QAbstractItemView { background:white; color:black;"
            "  selection-background-color:#ccc; min-width: 80px; padding: 4px;"
            "  font-size: 26px; }"
        );
    };
    m_mathOutCombo = new QComboBox(this);
    for (int i = 0; i < HISTORY_SLOTS; i++)
        m_mathOutCombo->addItem(QString(QChar('a' + i)));
    m_mathOutCombo->setFixedSize(100, 56);
    mathComboStyle(m_mathOutCombo);
    m_mathOutCombo->hide();

    m_mathEqualsLabel = new QLabel("=", this);
    m_mathEqualsLabel->setStyleSheet("color: black; font-size: 36px; font-weight: bold;");
    m_mathEqualsLabel->setFixedSize(40, 56);
    m_mathEqualsLabel->setAlignment(Qt::AlignCenter);
    m_mathEqualsLabel->hide();

    m_mathIn1Combo = new QComboBox(this);
    for (int i = 0; i < HISTORY_SLOTS; i++)
        m_mathIn1Combo->addItem(QString(QChar('a' + i)));
    m_mathIn1Combo->setFixedSize(100, 56);
    mathComboStyle(m_mathIn1Combo);
    m_mathIn1Combo->hide();

    m_mathOpCombo = new QComboBox(this);
    m_mathOpCombo->addItem("+");
    m_mathOpCombo->addItem("\u2212");   // minus sign
    m_mathOpCombo->addItem("\u00D7");   // multiplication sign
    m_mathOpCombo->addItem("\u00F7");   // division sign
    m_mathOpCombo->addItem("convolute");
    m_mathOpCombo->addItem("correlate");
    m_mathOpCombo->setFixedSize(260, 56);
    mathComboStyle(m_mathOpCombo);
    m_mathOpCombo->hide();

    m_mathIn2Combo = new QComboBox(this);
    for (int i = 0; i < HISTORY_SLOTS; i++)
        m_mathIn2Combo->addItem(QString(QChar('a' + i)));
    m_mathIn2Combo->setFixedSize(100, 56);
    mathComboStyle(m_mathIn2Combo);
    m_mathIn2Combo->hide();

    m_mathCancelBtn = new QPushButton("Cancel", this);
    m_mathCancelBtn->setFixedSize(80, 28);
    m_mathCancelBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_mathCancelBtn, &QPushButton::clicked, this, &FtWindow::onMathCancel);
    m_mathCancelBtn->hide();

    m_mathComputeBtn = new QPushButton("Compute", this);
    m_mathComputeBtn->setFixedSize(80, 28);
    m_mathComputeBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_mathComputeBtn, &QPushButton::clicked, this, &FtWindow::onMathCompute);
    m_mathComputeBtn->hide();

    // Binning widgets (hidden until bin button is active)
    m_binCombo = new QComboBox(this);
    for (int i = 2; i <= 8; i++)
        m_binCombo->addItem(QString::number(i), i);
    m_binCombo->setFixedSize(70, 28);
    m_binCombo->setStyleSheet(
        "QComboBox { background:#222; color:white; border:1px solid #888;"
        "  padding: 2px 8px; }"
        "QComboBox::drop-down { width: 20px; }"
        "QComboBox QAbstractItemView { background:#222; color:white;"
        "  selection-background-color:#555; min-width: 60px; padding: 4px; }"
    );
    m_binCombo->hide();

    m_binKeepSizeBtn = new QCheckBox("Keep original image size", this);
    m_binKeepSizeBtn->setStyleSheet("color: white;");
    m_binKeepSizeBtn->setChecked(true);
    m_binKeepSizeBtn->hide();

    m_applyBinBtn = new QPushButton("Apply binning", this);
    m_applyBinBtn->setFixedSize(110, 26);
    connect(m_applyBinBtn, &QPushButton::clicked, this, &FtWindow::onApplyBinning);
    m_applyBinBtn->hide();

    // Restore history and active slot
#ifndef __EMSCRIPTEN__
    restoreHistory();

    QSettings settings("ft", "ft");
    m_maskCenter = settings.value("maskCenter", false).toBool();
    m_maskBtn->setChecked(m_maskCenter);
    m_displayMode = settings.value("displayMode", 3).toInt();
    m_modeBtn->setText(modeLabel());
    m_activeSlot = settings.value("activeSlot", -1).toInt();
    if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS
        && m_history[m_activeSlot].occupied) {
        m_image          = m_history[m_activeSlot].image;
        m_imagePath      = m_history[m_activeSlot].path;
        m_imageRawPixels = m_history[m_activeSlot].rawPixels;
        m_imageMinVal    = m_history[m_activeSlot].minVal;
        m_imageMaxVal    = m_history[m_activeSlot].maxVal;
        m_imageDispMin   = m_history[m_activeSlot].minVal;
        m_imageDispMax   = m_history[m_activeSlot].maxVal;
        m_pixelSize      = m_history[m_activeSlot].pixelSize;
        if (!m_image.isNull()) {
            m_zoom[0].reset(m_image.width(), m_image.height());
            computeFFT();
        }
    } else {
        m_activeSlot = -1;
    }
#else
    m_displayMode = 3;
    m_modeBtn->setText(modeLabel());
    m_activeSlot = -1;
#endif
}

// ---------------------------------------------------------------------------
//  Layout
// ---------------------------------------------------------------------------
void FtWindow::resizeEvent(QResizeEvent *)
{
    m_loadBtn->move(8, 8);
    m_saveBtn->move(8 + m_loadBtn->width() + 4, 8);
    m_createBtn->move(8 + m_loadBtn->width() + 4, 8 + m_saveBtn->height() + 4);
    int hy0 = height() - height() / 5;
    m_reloadBtn->move(8, 8 + m_loadBtn->height() + 4);
    m_undoBtn->move((width() - m_undoBtn->width()) / 2, 70);
    m_modeBtn->move(width() - m_modeBtn->width() - 8, 8);
    m_maskBtn->move(width() - m_maskBtn->width() - 8, 8 + m_modeBtn->height() + 4);

    // Scale factor for tool dialogue widgets based on panel height
    int hy = height() - height() / 5;
    double sc = std::clamp(hy / 800.0, 0.5, 1.0);
    int fontSize = std::max(9, static_cast<int>(11 * sc));
    int editH    = std::max(16, static_cast<int>(22 * sc));
    int btnH     = std::max(18, static_cast<int>(26 * sc));

    // Styles for widgets on white rectangle background
    QString editSS  = QString("background:white; color:black; border:1px solid #888; font-size: %1px;").arg(fontSize);
    QString cbSS    = QString("color: #333; font-size: %1px;").arg(fontSize);
    QString btnSS   = QString("QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; font-size: %1px; }").arg(fontSize);

    // Bandpass widgets (sizes only; positions set in paintEvent)
    m_smoothEdit->setFixedSize(static_cast<int>(40 * sc), editH);
    m_smoothEdit->setStyleSheet(editSS);
    m_bandEraseOutside->setStyleSheet(cbSS);
    m_applyBandBtn->setFixedSize(static_cast<int>(100 * sc), btnH);
    m_applyBandBtn->setStyleSheet(btnSS);

    m_lineWidthEdit->setFixedSize(static_cast<int>(40 * sc), editH);
    m_lineWidthEdit->setStyleSheet(editSS);
    m_lineDirectionEdit->setFixedSize(static_cast<int>(50 * sc), editH);
    m_lineDirectionEdit->setStyleSheet(editSS);
    m_lineEraseOutsideBtn->setStyleSheet(cbSS);
    m_applyLineBtn->setFixedSize(static_cast<int>(100 * sc), btnH);
    m_applyLineBtn->setStyleSheet(btnSS);

    // Brush/eraser widgets (sizes only)
    m_brushValueEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_brushValueEdit->setStyleSheet(editSS);
    m_brushDiameterEdit->setFixedSize(static_cast<int>(40 * sc), editH);
    m_brushDiameterEdit->setStyleSheet(editSS);
    m_eraserDiameterEdit->setFixedSize(static_cast<int>(40 * sc), editH);
    m_eraserDiameterEdit->setStyleSheet(editSS);

    // Lattice filter widgets (sizes only)
    m_latticeSmoothEdit->setFixedSize(static_cast<int>(40 * sc), editH);
    m_latticeSmoothEdit->setStyleSheet(editSS);
    m_latticeDotDiamEdit->setFixedSize(static_cast<int>(40 * sc), editH);
    m_latticeDotDiamEdit->setStyleSheet(editSS);
    m_latticeEraseOutside->setStyleSheet(cbSS);
    m_latticeApplyBtn->setFixedSize(static_cast<int>(100 * sc), btnH);
    m_latticeApplyBtn->setStyleSheet(btnSS);

    // Fourier crop widgets (sizes only)
    m_ftCropCombo->setFixedSize(static_cast<int>(70 * sc), btnH);
    m_ftCropCombo->setStyleSheet(QString(
        "QComboBox { background:white; color:black; border:1px solid #888;"
        "  padding: 2px 4px; font-size: %1px; }"
        "QComboBox::drop-down { width: %2px; }"
        "QComboBox QAbstractItemView { background:white; color:black;"
        "  selection-background-color:#ccc; min-width: 60px; padding: 4px;"
        "  font-size: %1px; }").arg(fontSize).arg(static_cast<int>(20 * sc)));
    m_ftCropKeepSizeBtn->setStyleSheet(cbSS);
    m_applyFtCropBtn->setFixedSize(static_cast<int>(140 * sc), btnH);
    m_applyFtCropBtn->setStyleSheet(btnSS);

    // Panel 1 eraser/brush widgets (sizes only)
    m_p1EraserDiameterEdit->setFixedSize(static_cast<int>(40 * sc), editH);
    m_p1EraserDiameterEdit->setStyleSheet(editSS);
    m_p1BrushValueEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_p1BrushValueEdit->setStyleSheet(editSS);
    m_p1BrushDiameterEdit->setFixedSize(static_cast<int>(40 * sc), editH);
    m_p1BrushDiameterEdit->setStyleSheet(editSS);
    m_p1TaperWidthEdit->setFixedSize(static_cast<int>(50 * sc), editH);
    m_p1TaperWidthEdit->setStyleSheet(editSS);
    m_applyP1TaperBtn->setFixedSize(static_cast<int>(130 * sc), btnH);
    m_applyP1TaperBtn->setStyleSheet(btnSS);

    // Binning widgets (sizes only)
    m_binCombo->setFixedSize(static_cast<int>(70 * sc), btnH);
    m_binCombo->setStyleSheet(QString(
        "QComboBox { background:white; color:black; border:1px solid #888;"
        "  padding: 2px 4px; font-size: %1px; }"
        "QComboBox::drop-down { width: %2px; }"
        "QComboBox QAbstractItemView { background:white; color:black;"
        "  selection-background-color:#ccc; min-width: 60px; padding: 4px;"
        "  font-size: %1px; }").arg(fontSize).arg(static_cast<int>(20 * sc)));
    m_binKeepSizeBtn->setStyleSheet(cbSS);
    m_applyBinBtn->setFixedSize(static_cast<int>(110 * sc), btnH);
    m_applyBinBtn->setStyleSheet(btnSS);
}
