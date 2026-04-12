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
    m_loadBtn->setFixedSize(130, 30);
    connect(m_loadBtn, &QPushButton::clicked, this, &FtWindow::onLoadImage);
    new QShortcut(QKeySequence::Open, this, SLOT(onLoadImage()));

    // Save button
    m_saveBtn = new QPushButton("Save image", this);
    m_saveBtn->setFixedSize(130, 30);
    connect(m_saveBtn, &QPushButton::clicked, this, &FtWindow::onSaveImage);

    // Create image button
    m_createBtn = new QPushButton("Create image", this);
    m_createBtn->setFixedSize(130, 30);
    connect(m_createBtn, &QPushButton::clicked, this, &FtWindow::onCreateImage);

    // Reload button
    m_reloadBtn = new QPushButton("Reload image", this);
    m_reloadBtn->setFixedSize(130, 30);
    connect(m_reloadBtn, &QPushButton::clicked, this, &FtWindow::onReloadImage);

    // Undo / Redo buttons
    m_undoBtn = new QPushButton("Undo", this);
    m_undoBtn->setFixedSize(91, 30);
    connect(m_undoBtn, &QPushButton::clicked, this, &FtWindow::onUndo);
    m_redoBtn = new QPushButton("Redo", this);
    m_redoBtn->setFixedSize(91, 30);
    connect(m_redoBtn, &QPushButton::clicked, this, &FtWindow::onRedo);
    updateUndoRedoButtons();

    // Fullscreen toggle button
    m_fullscreenBtn = new QPushButton("Go fullscreen", this);
    m_fullscreenBtn->setFixedSize(180, 30);
    connect(m_fullscreenBtn, &QPushButton::clicked, this, &FtWindow::onToggleFullscreen);

    // Mode cycle button
    m_modeBtn = new QPushButton(modeLabel(), this);
    m_modeBtn->setFixedSize(180, 30);
    connect(m_modeBtn, &QPushButton::clicked, this, &FtWindow::onCycleMode);
    m_modeBtn->hide();

    // Mask-center toggle checkbox
    m_maskBtn = new QCheckBox("mask center for display", this);
    m_maskBtn->setStyleSheet("color: white;");
    m_maskBtn->adjustSize();
    connect(m_maskBtn, &QCheckBox::toggled, this, &FtWindow::onToggleMask);
    m_maskBtn->hide();

    // Bandpass filter widgets (hidden until bandpass mode active)
    m_smoothEdit = new QLineEdit("0", this);
    m_smoothEdit->setFixedSize(40, 22);
    m_smoothEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_smoothEdit->setToolTip(
        "Soft-edge width of the bandpass ring, in Fourier pixels.\n"
        "0 = hard edge (sharp cutoff). Larger values give a smoother\n"
        "Hanning-style transition between the kept and erased regions,\n"
        "which reduces ringing artefacts in the back-transformed image.");
    m_smoothEdit->hide();

    m_bandEraseOutside = new QCheckBox("Erase pixels outside of band", this);
    m_bandEraseOutside->setStyleSheet("color: white;");
    m_bandEraseOutside->setChecked(true);
    m_bandEraseOutside->setToolTip(
        "Checked: keep only Fourier pixels inside the ring (band-pass);\n"
        "         everything outside is set to zero.\n"
        "Unchecked: erase Fourier pixels inside the ring (band-stop);\n"
        "         everything outside is left untouched.");
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
    m_lineWidthEdit->setToolTip(
        "Half-width of the Fourier-space line, in pixels.\n"
        "Sets how thick the kept (or erased) stripe is, measured\n"
        "perpendicular to the chosen direction. A value of 10 means\n"
        "the stripe spans 10 pixels on each side of its centre line.");
    m_lineWidthEdit->hide();

    m_lineDirectionEdit = new QLineEdit("0", this);
    m_lineDirectionEdit->setFixedSize(50, 22);
    m_lineDirectionEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_lineDirectionEdit->setToolTip(
        "Orientation of the Fourier-space line, in degrees.\n"
        "0\u00B0 = horizontal stripe through the Fourier centre,\n"
        "90\u00B0 = vertical. Positive angles rotate counter-clockwise.\n"
        "You can also drag the line in panel 2 to change this.");
    m_lineDirectionEdit->hide();

    m_lineEraseOutsideBtn = new QCheckBox("Erase pixels outside of line", this);
    m_lineEraseOutsideBtn->setStyleSheet("color: white;");
    m_lineEraseOutsideBtn->setChecked(true);
    m_lineEraseOutsideBtn->setToolTip(
        "Checked: keep only the line/stripe and zero everything else\n"
        "         (useful for extracting a single Fourier direction).\n"
        "Unchecked: erase the line/stripe and keep everything else\n"
        "         (useful for removing directional noise such as\n"
        "         scan lines or grid artefacts).");
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
    m_brushValueEdit->setToolTip(
        "Real part of the Fourier-space amplitude to paint.\n"
        "The brush writes this value into the FFT (with zero imaginary\n"
        "part) under the Gaussian footprint, weighted by the brush\n"
        "profile. Use 0 to gently push amplitudes toward zero.");
    m_brushValueEdit->hide();
    m_brushDiamLabel = new QLabel("Paint brush Gaussian diameter:", this);
    m_brushDiamLabel->setStyleSheet("color: white;");
    m_brushDiamLabel->hide();
    m_brushDiameterEdit = new QLineEdit("0", this);
    m_brushDiameterEdit->setFixedSize(40, 22);
    m_brushDiameterEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_brushDiameterEdit->setToolTip(
        "Diameter of the Gaussian paint footprint, in Fourier pixels.\n"
        "Sets the full-width of the smooth bell-shaped brush. Larger\n"
        "values affect more Fourier pixels at once and produce a\n"
        "softer fall-off at the edges of the painted region.");
    m_brushDiameterEdit->hide();

    // Eraser parameter widgets
    m_eraserDiamLabel = new QLabel("Eraser Gaussian diameter:", this);
    m_eraserDiamLabel->setStyleSheet("color: white;");
    m_eraserDiamLabel->hide();
    m_eraserDiameterEdit = new QLineEdit("0", this);
    m_eraserDiameterEdit->setFixedSize(40, 22);
    m_eraserDiameterEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_eraserDiameterEdit->setToolTip(
        "Diameter of the Gaussian eraser footprint, in Fourier pixels.\n"
        "Click in panel 2 to multiply the FFT by (1 \u2212 Gaussian) under\n"
        "this footprint, smoothly attenuating Fourier components.\n"
        "Larger diameters erase a wider region with a softer edge.");
    m_eraserDiameterEdit->hide();

    // Lattice filter widgets (hidden until lattice mode active)
    m_latticeSmoothLabel = new QLabel("Smooth edge by pixels:", this);
    m_latticeSmoothLabel->setStyleSheet("color: white;");
    m_latticeSmoothLabel->hide();
    m_latticeSmoothEdit = new QLineEdit("0", this);
    m_latticeSmoothEdit->setFixedSize(40, 22);
    m_latticeSmoothEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_latticeSmoothEdit->setToolTip(
        "Soft-edge width of each lattice dot, in Fourier pixels.\n"
        "0 = hard circular edge. Larger values give a smooth Hanning-\n"
        "style fall-off around each spot, which suppresses ringing\n"
        "in the back-transformed image.");
    m_latticeSmoothEdit->hide();

    m_latticeDotDiamLabel = new QLabel("Diameter of dots:", this);
    m_latticeDotDiamLabel->setStyleSheet("color: white;");
    m_latticeDotDiamLabel->hide();
    m_latticeDotDiamEdit = new QLineEdit("3", this);
    m_latticeDotDiamEdit->setFixedSize(40, 22);
    m_latticeDotDiamEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_latticeDotDiamEdit->setToolTip(
        "Diameter of each lattice spot, in Fourier pixels.\n"
        "All reciprocal-lattice positions generated by the (u,v) basis\n"
        "vectors will be selected as circles of this size. Use a\n"
        "value just large enough to enclose the diffraction peaks.");
    m_latticeDotDiamEdit->hide();

    m_latticeEraseOutside = new QCheckBox("Erase pixels outside of lattice", this);
    m_latticeEraseOutside->setStyleSheet("color: white;");
    m_latticeEraseOutside->setChecked(true);
    m_latticeEraseOutside->setToolTip(
        "Checked: keep only the lattice spots and zero everything else\n"
        "         (lattice band-pass; useful for crystal averaging).\n"
        "Unchecked: erase the lattice spots and keep everything else\n"
        "         (removes the periodic component).");
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
        m_ftMathOutCombo->setToolTip(
            "Output buffer (A\u2026P) that will receive the result of the\n"
            "Fourier-space operation. Capital letters refer to the FFT\n"
            "of the corresponding history slot. Any existing content of\n"
            "the chosen buffer is overwritten.");
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
        m_ftMathIn1Combo->setToolTip(
            "First input buffer (A\u2026P) for the Fourier-space operation.\n"
            "Refers to the FFT of the corresponding history slot.");
        m_ftMathIn1Combo->hide();

        m_ftMathOpCombo = new QComboBox(this);
        m_ftMathOpCombo->addItem("+");
        m_ftMathOpCombo->addItem("\u2212");   // minus sign
        m_ftMathOpCombo->addItem("\u00D7");   // multiplication sign
        m_ftMathOpCombo->addItem("\u00F7");   // division sign
        m_ftMathOpCombo->setFixedSize(100, 56);
        ftMathComboStyle(m_ftMathOpCombo);
        m_ftMathOpCombo->setToolTip(
            "Operation applied between the two Fourier-space inputs:\n"
            "  +   complex addition\n"
            "  \u2212   complex subtraction\n"
            "  \u00D7   complex multiplication (= real-space convolution)\n"
            "  \u00F7   complex division        (= real-space deconvolution)");
        m_ftMathOpCombo->hide();

        m_ftMathIn2Combo = new QComboBox(this);
        for (int i = 0; i < HISTORY_SLOTS; i++)
            m_ftMathIn2Combo->addItem(QString(QChar('A' + i)));
        m_ftMathIn2Combo->setFixedSize(100, 56);
        ftMathComboStyle(m_ftMathIn2Combo);
        m_ftMathIn2Combo->setToolTip(
            "Second input buffer (A\u2026P) for the Fourier-space operation.\n"
            "Optionally complex-conjugated (see the \"*\" selector).");
        m_ftMathIn2Combo->hide();

        m_ftMathConjCombo = new QComboBox(this);
        m_ftMathConjCombo->addItem(" ");
        m_ftMathConjCombo->addItem("* (complex conjugate)");
        m_ftMathConjCombo->setFixedSize(260, 56);
        ftMathComboStyle(m_ftMathConjCombo);
        m_ftMathConjCombo->setToolTip(
            "If \"*\" is selected the second input is complex-conjugated\n"
            "before the operation. Combined with \u00D7 this gives a\n"
            "Fourier-space cross-correlation (A \u00D7 B*) instead of a\n"
            "convolution.");
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
    m_ftCropCombo->setToolTip(
        "Fourier crop factor N. Only the central 1/N \u00D7 1/N of the\n"
        "Fourier transform is kept; everything outside is discarded.\n"
        "This is equivalent to low-pass filtering followed by Fourier\n"
        "downsampling, and is the cleanest way to reduce resolution.");
    m_ftCropCombo->hide();

    m_ftCropKeepSizeBtn = new QCheckBox("Keep original Fourier transform size", this);
    m_ftCropKeepSizeBtn->setStyleSheet("color: white;");
    m_ftCropKeepSizeBtn->setChecked(true);
    m_ftCropKeepSizeBtn->setToolTip(
        "Checked: the FFT array stays at its original dimensions and\n"
        "         pixels outside the central crop window are zeroed.\n"
        "         The real-space image keeps its sampling.\n"
        "Unchecked: the FFT array is physically shrunk by N. The real-\n"
        "         space image becomes N\u00D7 smaller, with N\u00D7 larger pixels.");
    m_ftCropKeepSizeBtn->hide();

    m_applyFtCropBtn = new QPushButton("Apply Fourier crop", this);
    m_applyFtCropBtn->setFixedSize(140, 26);
    connect(m_applyFtCropBtn, &QPushButton::clicked, this, &FtWindow::onApplyFtCrop);
    m_applyFtCropBtn->hide();

    // Cross-section profile width widget
    m_crossSectionWidthEdit = new QLineEdit("1.0", this);
    m_crossSectionWidthEdit->setFixedSize(40, 22);
    m_crossSectionWidthEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_crossSectionWidthEdit->setToolTip(
        "Width of the integration band used for the 1D cross-section\n"
        "profile, in Fourier pixels. Pixels within this perpendicular\n"
        "distance from the line are averaged into the profile.\n"
        "Increase to reduce noise, decrease for higher angular\n"
        "resolution. The profile updates live as you type.");
    connect(m_crossSectionWidthEdit, &QLineEdit::textChanged, this, [this]() {
        if (m_crossSectionActive && m_ftComputed) {
            computeCrossSectionProfile();
            update();
        }
    });
    m_crossSectionWidthEdit->hide();

    // Panel 1 eraser parameter widgets
    m_p1EraserDiamLabel = new QLabel("Eraser Gaussian diameter:", this);
    m_p1EraserDiamLabel->setStyleSheet("color: white;");
    m_p1EraserDiamLabel->hide();
    m_p1EraserDiameterEdit = new QLineEdit("5", this);
    m_p1EraserDiameterEdit->setFixedSize(40, 22);
    m_p1EraserDiameterEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_p1EraserDiameterEdit->setToolTip(
        "Diameter of the real-space eraser footprint, in image pixels.\n"
        "Click in panel 1 to multiply pixel values by (1 \u2212 Gaussian)\n"
        "under this footprint, smoothly fading them toward zero.\n"
        "Larger diameters affect a wider area with a softer edge.");
    m_p1EraserDiameterEdit->hide();

    // Panel 1 brush parameter widgets
    m_p1BrushValueLabel = new QLabel("Pixel value to enter:", this);
    m_p1BrushValueLabel->setStyleSheet("color: white;");
    m_p1BrushValueLabel->hide();
    m_p1BrushValueEdit = new QLineEdit("1", this);
    m_p1BrushValueEdit->setFixedSize(60, 22);
    m_p1BrushValueEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_p1BrushValueEdit->setToolTip(
        "Real-space pixel value the brush paints into the image.\n"
        "The Gaussian footprint blends this value with the existing\n"
        "pixels, weighted by the brush profile. Use the image min/max\n"
        "scale shown below the panel as a reference.");
    m_p1BrushValueEdit->hide();
    m_p1BrushDiamLabel = new QLabel("Paint brush Gaussian diameter:", this);
    m_p1BrushDiamLabel->setStyleSheet("color: white;");
    m_p1BrushDiamLabel->hide();
    m_p1BrushDiameterEdit = new QLineEdit("5", this);
    m_p1BrushDiameterEdit->setFixedSize(40, 22);
    m_p1BrushDiameterEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_p1BrushDiameterEdit->setToolTip(
        "Diameter of the Gaussian paint footprint, in image pixels.\n"
        "Sets the full-width of the smooth bell-shaped brush. Larger\n"
        "values affect more pixels at once and produce a softer\n"
        "fall-off at the edges of the painted region.");
    m_p1BrushDiameterEdit->hide();

    // Panel 1 taper widgets
    m_p1TaperWidthLabel = new QLabel("Hanning width:", this);
    m_p1TaperWidthLabel->setStyleSheet("color: white;");
    m_p1TaperWidthLabel->hide();
    m_p1TaperWidthEdit = new QLineEdit("32", this);
    m_p1TaperWidthEdit->setFixedSize(50, 22);
    m_p1TaperWidthEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_p1TaperWidthEdit->setToolTip(
        "Width, in image pixels, of the Hanning edge taper applied\n"
        "to all four image borders. Pixels closer than this distance\n"
        "to an edge are smoothly faded toward the image mean. This\n"
        "removes the FFT cross artefact caused by sharp image edges\n"
        "and is recommended before computing a Fourier transform.");
    m_p1TaperWidthEdit->hide();
    m_applyP1TaperBtn = new QPushButton("Apply edge taper", this);
    m_applyP1TaperBtn->setFixedSize(130, 26);
    connect(m_applyP1TaperBtn, &QPushButton::clicked, this, &FtWindow::onApplyEdgeTaper);
    m_applyP1TaperBtn->hide();

    // Gabor filter widgets
    auto makeGaborEdit = [this](const QString &def) {
        auto *e = new QLineEdit(def, this);
        e->setFixedSize(60, 22);
        e->setStyleSheet("background:#222; color:white; border:1px solid #888;");
        e->hide();
        return e;
    };
    m_gaborSigmaEdit  = makeGaborEdit("4");
    m_gaborSigmaEdit->setToolTip(
        "Sigma \u2014 standard deviation of the Gaussian envelope, in pixels.\n"
        "Controls how spatially localised the Gabor filter is. Larger\n"
        "sigma covers more pixels and produces a smoother, more global\n"
        "response; smaller sigma is sharply localised but noisier.\n"
        "Typical values: 2\u20138 pixels.");
    m_gaborLambdaEdit = makeGaborEdit("8");
    m_gaborLambdaEdit->setToolTip(
        "Lambda \u2014 wavelength of the cosine carrier, in pixels.\n"
        "Sets the spatial period the filter is tuned to: a stripe\n"
        "pattern with this period and the chosen orientation gives\n"
        "the strongest response. Should typically be \u2265 2\u00B7sigma so\n"
        "that the carrier completes at least one cycle inside the\n"
        "envelope.");
    m_gaborThetaEdit  = makeGaborEdit("0");
    m_gaborThetaEdit->setToolTip(
        "Theta \u2014 orientation of the Gabor stripes, in degrees.\n"
        "0\u00B0 means the stripes are vertical and the filter responds\n"
        "to horizontal intensity variations. Positive angles rotate\n"
        "the stripes counter-clockwise. Use 0\u00B0, 45\u00B0, 90\u00B0, 135\u00B0 to\n"
        "scan all major orientations.");
    m_gaborGammaEdit  = makeGaborEdit("0.5");
    m_gaborGammaEdit->setToolTip(
        "Gamma \u2014 spatial aspect ratio of the Gaussian envelope.\n"
        "1.0 = circular envelope. Values <1 elongate the envelope\n"
        "along the stripe direction, giving the filter a longer,\n"
        "more directional support. Typical edge/line filters use\n"
        "gamma \u2248 0.3\u20130.7.");
    m_gaborCancelBtn = new QPushButton("Cancel", this);
    m_gaborCancelBtn->setFixedSize(80, 26);
    m_gaborCancelBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_gaborCancelBtn, &QPushButton::clicked, this, &FtWindow::onGaborCancel);
    m_gaborCancelBtn->hide();
    m_gaborComputeBtn = new QPushButton("Compute", this);
    m_gaborComputeBtn->setFixedSize(80, 26);
    m_gaborComputeBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_gaborComputeBtn, &QPushButton::clicked, this, &FtWindow::onApplyGaborFilter);
    m_gaborComputeBtn->hide();

    // Hessian filter widgets
    m_hessianSigmaEdit = new QLineEdit("2", this);
    m_hessianSigmaEdit->setFixedSize(60, 22);
    m_hessianSigmaEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_hessianSigmaEdit->setToolTip(
        "Sigma \u2014 Gaussian smoothing scale (in pixels) used before\n"
        "the Hessian is computed. This sets the size of the structures\n"
        "the filter responds to: small sigma highlights thin features,\n"
        "large sigma highlights broad ridges. Try sigma close to half\n"
        "the expected feature width. Typical values: 1\u20136 pixels.");
    m_hessianSigmaEdit->hide();
    m_hessianPolarityEdit = new QLineEdit("1", this);
    m_hessianPolarityEdit->setFixedSize(60, 22);
    m_hessianPolarityEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_hessianPolarityEdit->setToolTip(
        "Polarity \u2014 sign of the ridges to enhance.\n"
        "  +1 : highlight bright ridges on a dark background\n"
        "       (e.g. filaments brighter than the surroundings)\n"
        "  \u22121 : highlight dark ridges on a bright background\n"
        "       (e.g. carbon edges, dark fibres)\n"
        "Negative responses are clipped to zero, so the output is\n"
        "always non-negative.");
    m_hessianPolarityEdit->hide();
    m_hessianCancelBtn = new QPushButton("Cancel", this);
    m_hessianCancelBtn->setFixedSize(80, 26);
    m_hessianCancelBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_hessianCancelBtn, &QPushButton::clicked, this, &FtWindow::onHessianCancel);
    m_hessianCancelBtn->hide();
    m_hessianComputeBtn = new QPushButton("Compute", this);
    m_hessianComputeBtn->setFixedSize(80, 26);
    m_hessianComputeBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_hessianComputeBtn, &QPushButton::clicked, this, &FtWindow::onApplyHessianFilter);
    m_hessianComputeBtn->hide();

    // Amyloid filament widgets
    m_amyloidRiseEdit = new QLineEdit("4.75", this);
    m_amyloidRiseEdit->setFixedSize(60, 22);
    m_amyloidRiseEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_amyloidRiseEdit->setToolTip(
        "Helical rise in \u00C5ngstr\u00F6m \u2014 the axial translation between\n"
        "successive subunits along the helix. For typical amyloid\n"
        "filaments (cross-\u03B2 structure) the rise is \u2248 4.75 \u00C5,\n"
        "corresponding to the inter-strand spacing.");
    m_amyloidRiseEdit->hide();
    m_amyloidTwistEdit = new QLineEdit("-1", this);
    m_amyloidTwistEdit->setFixedSize(60, 22);
    m_amyloidTwistEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_amyloidTwistEdit->setToolTip(
        "Helical twist in degrees \u2014 the azimuthal rotation between\n"
        "successive subunits. Negative = left-handed twist.\n"
        "For amyloid filaments typical values are \u22121\u00B0 to \u22122\u00B0.\n"
        "The twist determines the crossover distance visible\n"
        "in projection (crossover = 360 / |twist| \u00D7 rise).");
    m_amyloidTwistEdit->hide();
    m_amyloidLongAxisEdit = new QLineEdit("150", this);
    m_amyloidLongAxisEdit->setFixedSize(60, 22);
    m_amyloidLongAxisEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_amyloidLongAxisEdit->setToolTip(
        "Long axis of the elliptical pancake layer in \u00C5ngstr\u00F6m.\n"
        "This is the larger dimension of the oval \u03B2-sheet cross-section.\n"
        "At 1 \u00C5/pixel this equals the long axis in pixels.\n"
        "Typical amyloid filaments: 100\u2013150 \u00C5.");
    m_amyloidLongAxisEdit->hide();
    m_amyloidShortAxisEdit = new QLineEdit("80", this);
    m_amyloidShortAxisEdit->setFixedSize(60, 22);
    m_amyloidShortAxisEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_amyloidShortAxisEdit->setToolTip(
        "Short axis of the elliptical pancake layer in \u00C5ngstr\u00F6m.\n"
        "This is the smaller dimension of the oval \u03B2-sheet cross-section.\n"
        "Setting this equal to the long axis gives a circular pancake.\n"
        "Typical values: 50\u201380% of the long axis.");
    m_amyloidShortAxisEdit->hide();
    m_amyloidSmoothEdit = new QLineEdit("1.5", this);
    m_amyloidSmoothEdit->setFixedSize(60, 22);
    m_amyloidSmoothEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_amyloidSmoothEdit->setToolTip(
        "Axial smoothing sigma in \u00C5ngstr\u00F6m. Controls how much\n"
        "neighbouring pancake layers blend into each other.\n"
        "Smaller values give sharply separated layers (like a\n"
        "diffraction pattern with strong layer lines). Larger\n"
        "values merge the layers into a smoother tube.\n"
        "Typical: 1\u20133 \u00C5. Set to 0 for perfectly sharp layers.");
    m_amyloidSmoothEdit->hide();
    m_amyloidNoiseBtn = new QCheckBox("Add gray noise", this);
    m_amyloidNoiseBtn->setStyleSheet("color: #333;");
    m_amyloidNoiseBtn->setChecked(true);
    m_amyloidNoiseBtn->setToolTip(
        "Add Gaussian noise to the entire image after rendering the\n"
        "filament. This makes the result look like a real cryo-EM\n"
        "micrograph where the signal sits in a noisy background.\n"
        "The noise sigma is set by the field to the right.");
    m_amyloidNoiseBtn->hide();
    m_amyloidNoiseEdit = new QLineEdit("0.3", this);
    m_amyloidNoiseEdit->setFixedSize(40, 22);
    m_amyloidNoiseEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_amyloidNoiseEdit->setToolTip(
        "Noise level as a fraction of the peak filament signal.\n"
        "0.3 = moderate noise (SNR \u2248 3), 1.0 = very noisy (SNR \u2248 1),\n"
        "0.1 = low noise. The noise is additive Gaussian with zero\n"
        "mean and this sigma, applied to the whole image.");
    m_amyloidNoiseEdit->hide();
    m_amyloidSignalBtn = new QPushButton("White signal", this);
    m_amyloidSignalBtn->setFixedSize(110, 26);
    m_amyloidSignalBtn->setStyleSheet(
        "QPushButton { background-color: #eee; color: #111; border: 2px outset #aaa; padding: 2px; font-weight: bold; }");
    m_amyloidSignalBtn->setToolTip(
        "Toggle between white signal (filament bright on dark\n"
        "background) and black signal (filament dark on bright\n"
        "background). Black signal matches the conventional\n"
        "cryo-EM display where protein appears dark.");
    connect(m_amyloidSignalBtn, &QPushButton::clicked, this, [this]() {
        m_amyloidBlackSignal = !m_amyloidBlackSignal;
        if (m_amyloidBlackSignal) {
            m_amyloidSignalBtn->setText("Black signal");
            m_amyloidSignalBtn->setStyleSheet(
                "QPushButton { background-color: #222; color: #eee; border: 2px outset #555; padding: 2px; font-weight: bold; }");
        } else {
            m_amyloidSignalBtn->setText("White signal");
            m_amyloidSignalBtn->setStyleSheet(
                "QPushButton { background-color: #eee; color: #111; border: 2px outset #aaa; padding: 2px; font-weight: bold; }");
        }
    });
    m_amyloidSignalBtn->hide();
    m_amyloidCancelBtn = new QPushButton("Cancel", this);
    m_amyloidCancelBtn->setFixedSize(80, 26);
    m_amyloidCancelBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_amyloidCancelBtn, &QPushButton::clicked, this, &FtWindow::onAmyloidCancel);
    m_amyloidCancelBtn->hide();
    m_amyloidComputeBtn = new QPushButton("Compute", this);
    m_amyloidComputeBtn->setFixedSize(80, 26);
    m_amyloidComputeBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_amyloidComputeBtn, &QPushButton::clicked, this, &FtWindow::onAmyloidCompute);
    m_amyloidComputeBtn->hide();

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
    m_mathOutCombo->setToolTip(
        "Output buffer (a\u2026p) that will receive the result of the\n"
        "real-space operation. Lower-case letters refer to the real-\n"
        "space images stored in the corresponding history slots. Any\n"
        "existing content of the chosen buffer is overwritten.");
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
    m_mathIn1Combo->setToolTip(
        "First input buffer (a\u2026p) for the real-space operation.\n"
        "Refers to the image stored in the corresponding history slot.");
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
    m_mathOpCombo->setToolTip(
        "Operation applied between the two real-space inputs:\n"
        "  +          pixel-wise addition\n"
        "  \u2212          pixel-wise subtraction\n"
        "  \u00D7          pixel-wise multiplication\n"
        "  \u00F7          pixel-wise division\n"
        "  convolute  full 2D convolution (via FFT)\n"
        "  correlate  full 2D cross-correlation (via FFT)");
    m_mathOpCombo->hide();

    m_mathIn2Combo = new QComboBox(this);
    for (int i = 0; i < HISTORY_SLOTS; i++)
        m_mathIn2Combo->addItem(QString(QChar('a' + i)));
    m_mathIn2Combo->setFixedSize(100, 56);
    mathComboStyle(m_mathIn2Combo);
    m_mathIn2Combo->setToolTip(
        "Second input buffer (a\u2026p) for the real-space operation.\n"
        "For convolute / correlate, this is the kernel / template.");
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
    m_binCombo->setToolTip(
        "Binning factor N. The image is averaged over non-overlapping\n"
        "N\u00D7N blocks, reducing each linear dimension by N. Useful for\n"
        "boosting signal-to-noise and shrinking the image before\n"
        "expensive operations.");
    m_binCombo->hide();

    m_binKeepSizeBtn = new QCheckBox("Keep original image size", this);
    m_binKeepSizeBtn->setStyleSheet("color: white;");
    m_binKeepSizeBtn->setChecked(true);
    m_binKeepSizeBtn->setToolTip(
        "Checked: after binning, the image is resampled back to its\n"
        "         original dimensions (each binned pixel becomes an\n"
        "         N\u00D7N block). Useful for visual comparison with the\n"
        "         original; pixel size is unchanged.\n"
        "Unchecked: the image is physically shrunk by N. Width, height\n"
        "         and pixel size all change.");
    m_binKeepSizeBtn->hide();

    m_applyBinBtn = new QPushButton("Apply binning", this);
    m_applyBinBtn->setFixedSize(110, 26);
    connect(m_applyBinBtn, &QPushButton::clicked, this, &FtWindow::onApplyBinning);
    m_applyBinBtn->hide();

    // Particle picking widgets
    m_peakSourceCombo = new QComboBox(this);
    for (int i = 0; i < HISTORY_SLOTS; i++)
        m_peakSourceCombo->addItem(QString(QChar('a' + i)));
    m_peakSourceCombo->setFixedSize(70, 28);
    m_peakSourceCombo->setStyleSheet(
        "QComboBox { background:#222; color:white; border:1px solid #888;"
        "  padding: 2px 8px; }"
        "QComboBox::drop-down { width: 20px; }"
        "QComboBox QAbstractItemView { background:#222; color:white;"
        "  selection-background-color:#555; min-width: 60px; padding: 4px; }");
    m_peakSourceCombo->setToolTip(
        "Source buffer (a\u2026p) in which to search for peaks.\n"
        "Typically the output of a matched filter or cross-correlation\n"
        "between the image and a template. Bright local maxima in this\n"
        "buffer are interpreted as candidate particle positions.");
    m_peakSourceCombo->hide();
    connect(m_peakSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (!m_peakPickActive) return;
        // Reset slider to 75% for the newly selected source buffer
        m_peakThresholdSlider->setValue(750);
        m_peaks.clear();
        update();
    });

    m_peakThresholdLabel = new QLabel("Threshold:", this);
    m_peakThresholdLabel->setStyleSheet("color: #333;");
    m_peakThresholdLabel->hide();
    m_peakThresholdSlider = new QSlider(Qt::Horizontal, this);
    m_peakThresholdSlider->setRange(0, 1000);
    m_peakThresholdSlider->setValue(750);  // 75% default
    m_peakThresholdSlider->setFixedSize(200, 22);
    m_peakThresholdSlider->setStyleSheet(
        "QSlider::groove:horizontal { background:#888; height:6px; border-radius:3px; }"
        "QSlider::handle:horizontal { background:white; border:1px solid #555; width:14px; margin:-5px 0; border-radius:7px; }");
    m_peakThresholdSlider->setToolTip(
        "Peak detection threshold, expressed as a fraction (0\u20131000)\n"
        "of the source buffer's intensity range. Local maxima below\n"
        "this level are ignored. Lower the slider to accept more,\n"
        "weaker peaks; raise it to keep only the strongest ones.");
    m_peakThresholdSlider->hide();
    connect(m_peakThresholdSlider, &QSlider::valueChanged, this, [this]() {
        if (m_peakPickActive) update();  // just update threshold display
    });

    m_peakExclLabel = new QLabel("Exclusion radius:", this);
    m_peakExclLabel->setStyleSheet("color: #333;");
    m_peakExclLabel->hide();
    m_peakExclRadiusSlider = new QSlider(Qt::Horizontal, this);
    m_peakExclRadiusSlider->setRange(32, 200);
    m_peakExclRadiusSlider->setValue(32);
    m_peakExclRadiusSlider->setFixedSize(200, 22);
    m_peakExclRadiusSlider->setStyleSheet(
        "QSlider::groove:horizontal { background:#888; height:6px; border-radius:3px; }"
        "QSlider::handle:horizontal { background:white; border:1px solid #555; width:14px; margin:-5px 0; border-radius:7px; }");
    m_peakExclRadiusSlider->setToolTip(
        "Minimum centre-to-centre distance between two accepted peaks,\n"
        "in image pixels. When two candidate peaks are closer than\n"
        "this radius, only the stronger one is kept. Set this to a\n"
        "bit more than the particle radius to avoid double-picking.");
    m_peakExclRadiusSlider->hide();
    connect(m_peakExclRadiusSlider, &QSlider::valueChanged, this, [this]() {
        if (m_peakPickActive) update();
    });

    m_peakCancelBtn = new QPushButton("Cancel", this);
    m_peakCancelBtn->setFixedSize(80, 26);
    m_peakCancelBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_peakCancelBtn, &QPushButton::clicked, this, &FtWindow::onPeakCancel);
    m_peakCancelBtn->hide();

    m_peakComputeBtn = new QPushButton("Compute", this);
    m_peakComputeBtn->setFixedSize(80, 26);
    m_peakComputeBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_peakComputeBtn, &QPushButton::clicked, this, &FtWindow::onPeakCompute);
    m_peakComputeBtn->hide();

    m_peakShowPosBtn = new QPushButton("Hide positions", this);
    m_peakShowPosBtn->setFixedSize(110, 26);
    m_peakShowPosBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_peakShowPosBtn, &QPushButton::clicked, this, [this]() {
        m_peakShowPositions = !m_peakShowPositions;
        m_peakShowPosBtn->setText(m_peakShowPositions ? "Hide positions" : "Show positions");
        update();
    });
    m_peakShowPosBtn->hide();

    // Extract particles widgets
    m_extractSourceCombo = new QComboBox(this);
    for (int i = 0; i < HISTORY_SLOTS; i++)
        m_extractSourceCombo->addItem(QString(QChar('a' + i)));
    m_extractSourceCombo->setFixedSize(70, 28);
    m_extractSourceCombo->setStyleSheet(
        "QComboBox { background:#222; color:white; border:1px solid #888;"
        "  padding: 2px 8px; }"
        "QComboBox::drop-down { width: 20px; }"
        "QComboBox QAbstractItemView { background:#222; color:white;"
        "  selection-background-color:#555; min-width: 60px; padding: 4px; }");
    m_extractSourceCombo->setToolTip(
        "Source image buffer (a\u2026p) the particles are cut out from.\n"
        "Should be the original (or filtered) micrograph that the\n"
        "picked positions refer to, not the cross-correlation map.");
    m_extractSourceCombo->hide();

    m_extractTargetCombo = new QComboBox(this);
    for (int i = 0; i < HISTORY_SLOTS; i++)
        m_extractTargetCombo->addItem(QString(QChar('a' + i)));
    m_extractTargetCombo->setFixedSize(70, 28);
    m_extractTargetCombo->setStyleSheet(
        "QComboBox { background:#222; color:white; border:1px solid #888;"
        "  padding: 2px 8px; }"
        "QComboBox::drop-down { width: 20px; }"
        "QComboBox QAbstractItemView { background:#222; color:white;"
        "  selection-background-color:#555; min-width: 60px; padding: 4px; }");
    m_extractTargetCombo->setToolTip(
        "Target buffer (a\u2026p) where the assembled stack of extracted\n"
        "particle boxes will be stored. Any existing content of the\n"
        "target buffer is overwritten.");
    m_extractTargetCombo->hide();

    m_extractSizeCombo = new QComboBox(this);
    m_extractSizeCombo->addItem("64", 64);
    m_extractSizeCombo->addItem("128", 128);
    m_extractSizeCombo->setFixedSize(70, 28);
    m_extractSizeCombo->setStyleSheet(
        "QComboBox { background:#222; color:white; border:1px solid #888;"
        "  padding: 2px 8px; }"
        "QComboBox::drop-down { width: 20px; }"
        "QComboBox QAbstractItemView { background:#222; color:white;"
        "  selection-background-color:#555; min-width: 60px; padding: 4px; }");
    m_extractSizeCombo->setToolTip(
        "Side length, in pixels, of each square particle box. The\n"
        "box is centred on the picked position. Choose a size large\n"
        "enough to fully contain the particle plus some background\n"
        "(typically \u2248 1.5\u00D7 the particle diameter).");
    m_extractSizeCombo->hide();

    m_extractCancelBtn = new QPushButton("Cancel", this);
    m_extractCancelBtn->setFixedSize(80, 26);
    m_extractCancelBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_extractCancelBtn, &QPushButton::clicked, this, &FtWindow::onExtractCancel);
    m_extractCancelBtn->hide();

    m_extractComputeBtn = new QPushButton("Compute", this);
    m_extractComputeBtn->setFixedSize(80, 26);
    m_extractComputeBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_extractComputeBtn, &QPushButton::clicked, this, &FtWindow::onExtractCompute);
    m_extractComputeBtn->hide();

    // Restore history and active slot
#ifndef __EMSCRIPTEN__
    restoreHistory();

    QSettings settings("ft", "ft");
    m_maskCenter = settings.value("maskCenter", false).toBool();
    m_maskBtn->setChecked(m_maskCenter);
    m_displayMode = settings.value("displayMode", 3).toInt();
    m_modeBtn->setText(modeLabel());

    // Restore math combo selections
    m_mathOutCombo->setCurrentIndex(settings.value("mathOutIdx", 0).toInt());
    m_mathIn1Combo->setCurrentIndex(settings.value("mathIn1Idx", 0).toInt());
    m_mathOpCombo->setCurrentIndex(settings.value("mathOpIdx", 0).toInt());
    m_mathIn2Combo->setCurrentIndex(settings.value("mathIn2Idx", 0).toInt());
    m_ftMathOutCombo->setCurrentIndex(settings.value("ftMathOutIdx", 0).toInt());
    m_ftMathIn1Combo->setCurrentIndex(settings.value("ftMathIn1Idx", 0).toInt());
    m_ftMathOpCombo->setCurrentIndex(settings.value("ftMathOpIdx", 0).toInt());
    m_ftMathIn2Combo->setCurrentIndex(settings.value("ftMathIn2Idx", 0).toInt());
    m_ftMathConjCombo->setCurrentIndex(settings.value("ftMathConjIdx", 0).toInt());

    // Restore particle picking settings
    m_peakSourceCombo->setCurrentIndex(settings.value("peakSourceIdx", 0).toInt());
    m_peakThresholdSlider->setValue(settings.value("peakThreshold", 750).toInt());
    m_peakExclRadiusSlider->setValue(settings.value("peakExclRadius", 32).toInt());

    // Restore extract particles settings
    m_extractSourceCombo->setCurrentIndex(settings.value("extractSourceIdx", 0).toInt());
    m_extractTargetCombo->setCurrentIndex(settings.value("extractTargetIdx", 1).toInt());
    m_extractSizeCombo->setCurrentIndex(settings.value("extractSizeIdx", 0).toInt());

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
    m_undoBtn->move((width() - 186) / 2, 70);
    m_redoBtn->move((width() - 186) / 2 + 95, 70);
    m_fullscreenBtn->move(width() - m_fullscreenBtn->width() - 8, 8);
    m_modeBtn->move(width() - m_modeBtn->width() - 8, 8 + m_fullscreenBtn->height() + 4);
    m_maskBtn->move(width() - m_maskBtn->width() - 8, 8 + m_fullscreenBtn->height() + 4 + m_modeBtn->height() + 4);

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

    // Cross-section profile width widget (sizes only)
    m_crossSectionWidthEdit->setFixedSize(static_cast<int>(40 * sc), editH);
    m_crossSectionWidthEdit->setStyleSheet(editSS);

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

    // Gabor filter widgets (sizes only)
    m_gaborSigmaEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_gaborSigmaEdit->setStyleSheet(editSS);
    m_gaborLambdaEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_gaborLambdaEdit->setStyleSheet(editSS);
    m_gaborThetaEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_gaborThetaEdit->setStyleSheet(editSS);
    m_gaborGammaEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_gaborGammaEdit->setStyleSheet(editSS);
    m_gaborCancelBtn->setFixedSize(static_cast<int>(80 * sc), btnH);
    m_gaborCancelBtn->setStyleSheet(btnSS);
    m_gaborComputeBtn->setFixedSize(static_cast<int>(80 * sc), btnH);
    m_gaborComputeBtn->setStyleSheet(btnSS);

    // Hessian filter widgets (sizes only)
    m_hessianSigmaEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_hessianSigmaEdit->setStyleSheet(editSS);
    m_hessianPolarityEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_hessianPolarityEdit->setStyleSheet(editSS);
    m_hessianCancelBtn->setFixedSize(static_cast<int>(80 * sc), btnH);
    m_hessianCancelBtn->setStyleSheet(btnSS);
    m_hessianComputeBtn->setFixedSize(static_cast<int>(80 * sc), btnH);
    m_hessianComputeBtn->setStyleSheet(btnSS);

    // Particle picking widgets (sizes only)
    m_peakSourceCombo->setFixedSize(static_cast<int>(70 * sc), btnH);
    m_peakSourceCombo->setStyleSheet(QString(
        "QComboBox { background:white; color:black; border:1px solid #888;"
        "  padding: 2px 4px; font-size: %1px; }"
        "QComboBox::drop-down { width: %2px; }"
        "QComboBox QAbstractItemView { background:white; color:black;"
        "  selection-background-color:#ccc; min-width: 60px; padding: 4px;"
        "  font-size: %1px; }").arg(fontSize).arg(static_cast<int>(20 * sc)));
    m_peakThresholdSlider->setFixedSize(static_cast<int>(200 * sc), editH);
    m_peakExclRadiusSlider->setFixedSize(static_cast<int>(200 * sc), editH);
    m_peakCancelBtn->setFixedSize(static_cast<int>(80 * sc), btnH);
    m_peakCancelBtn->setStyleSheet(btnSS);
    m_peakComputeBtn->setFixedSize(static_cast<int>(80 * sc), btnH);
    m_peakComputeBtn->setStyleSheet(btnSS);
    m_peakShowPosBtn->setFixedSize(static_cast<int>(110 * sc), btnH);
    m_peakShowPosBtn->setStyleSheet(btnSS);

    // Extract particles widgets (sizes only)
    auto extractComboSS = QString(
        "QComboBox { background:white; color:black; border:1px solid #888;"
        "  padding: 2px 4px; font-size: %1px; }"
        "QComboBox::drop-down { width: %2px; }"
        "QComboBox QAbstractItemView { background:white; color:black;"
        "  selection-background-color:#ccc; min-width: 60px; padding: 4px;"
        "  font-size: %1px; }").arg(fontSize).arg(static_cast<int>(20 * sc));
    m_extractSourceCombo->setFixedSize(static_cast<int>(70 * sc), btnH);
    m_extractSourceCombo->setStyleSheet(extractComboSS);
    m_extractTargetCombo->setFixedSize(static_cast<int>(70 * sc), btnH);
    m_extractTargetCombo->setStyleSheet(extractComboSS);
    m_extractSizeCombo->setFixedSize(static_cast<int>(70 * sc), btnH);
    m_extractSizeCombo->setStyleSheet(extractComboSS);
    m_extractCancelBtn->setFixedSize(static_cast<int>(80 * sc), btnH);
    m_extractCancelBtn->setStyleSheet(btnSS);
    m_extractComputeBtn->setFixedSize(static_cast<int>(80 * sc), btnH);
    m_extractComputeBtn->setStyleSheet(btnSS);

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
