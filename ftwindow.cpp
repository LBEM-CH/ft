#include "ftwindow_common.h"

// ---------------------------------------------------------------------------
//  Static configuration (used by embedding applications)
// ---------------------------------------------------------------------------
static QString g_exampleImagesDir;

// ---------------------------------------------------------------------------
//  Checkbox styling
//
//  A bare "color: white" stylesheet leaves the checkbox *indicator* to the
//  native platform style. On Windows (including the WASM build) that indicator
//  renders as a black box with no visible checkmark against our dark panels,
//  so the user cannot tell whether the box is checked. Styling the indicator
//  explicitly makes it render identically on every platform, including WASM:
//  the box stays white in both states, and when checked a black checkmark is
//  overlaid on it.
//
//  The checkmark is supplied via "image: url(<path>)". Qt's stylesheet url()
//  loads a file that QPixmap can open — it does NOT accept "data:" URIs (an
//  inline data URI silently renders nothing on every platform). So we decode
//  an embedded PNG to a temp file once and point the stylesheet at that path;
//  this needs no .qrc/resource and works on native and WASM (MEMFS) alike.
// ---------------------------------------------------------------------------
static QString checkMarkPngPath()
{
    // 16x16 black checkmark on a transparent background.
    static const char kCheckPngB64[] =
        "iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAA4ElEQVR4nL3S"
        "O04DMRAG4C/ZSDTQIE5ADwVSKLlETpBzpMoJaGhRCmrECWgpkXKGtEAD7ZIU"
        "mUHGbFaAECNZtmc8/8M2/xwDDP8CqPkN8wCHOC9y345RzAu0mEfui51kKiPl"
        "TrGOsYhzn6x0SUqGU7xG80MXWS72oylvu4nxGM3POFa9RiIdYYmbyO/FfBnN"
        "LSaVrY9Ng/vC43XUJkXuKnIjVaTkCzzhPRpusYr1EgdB1Pl86WeMlwJkjTec"
        "VOc6I6WNbS8rAaa7pPeBnOEOs9j/6OvWMnu/7K7isKi1fQAbxB0n6vwBzzMA"
        "AAAASUVORK5CYII=";
    static QString path;
    if (!path.isEmpty())
        return path;
    const QByteArray png = QByteArray::fromBase64(QByteArray(kCheckPngB64));
    const QString p = QDir::tempPath() + "/ft_checkmark.png";
    QFile f(p);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(png);
        f.close();
        path = p;
    }
    return path;
}

//  Pass fontPx > 0 to also pin the label font size (used from resizeEvent,
//  which re-applies checkbox styles on every resize — WASM fires one at
//  startup — and must keep the indicator rules, or the box falls back to the
//  invisible native rendering).
static QString checkBoxStyle(const QString &textColor, int fontPx = -1)
{
    const QString check = checkMarkPngPath();
    const QString font = fontPx > 0
        ? QStringLiteral("font-size: %1px;").arg(fontPx)
        : QString();
    return QStringLiteral(
               "QCheckBox { color: %1; %3 }"
               "QCheckBox::indicator {"
               "  width: 16px; height: 16px;"
               "  border: 1px solid #888; border-radius: 3px;"
               "  background: white;"
               "}"
               "QCheckBox::indicator:checked {"
               "  background: white; border: 1px solid #888;"
               "  image: url(\"%2\");"
               "}")
        .arg(textColor, check, font);
}

void FtWindow::setExampleImagesDir(const QString &dir)
{
    g_exampleImagesDir = dir;
}

QString FtWindow::exampleImagesDir()
{
    return g_exampleImagesDir;
}

// ---------------------------------------------------------------------------
//  Constructor
// ---------------------------------------------------------------------------
FtWindow::FtWindow(QWidget *parent) : QWidget(parent)
{
    setWindowTitle("ft");

    setMouseTracking(true);
    grabGesture(Qt::PinchGesture);

    buildToolGroups();

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

    // New image button (opens Create-or-Copy popup in panel 1)
    m_createBtn = new QPushButton("New image", this);
    m_createBtn->setFixedSize(130, 30);
    connect(m_createBtn, &QPushButton::clicked, this, &FtWindow::onCreateImage);

    // Reload / Save / Delete live in the gutter between the two history panels
    // (3 and 4), since they all act on the buffer whose thumbnails sit there.
    m_reloadBtn = new QPushButton("Reload image", this);
    m_reloadBtn->setFixedSize(130, 30);
    connect(m_reloadBtn, &QPushButton::clicked, this, &FtWindow::onReloadImage);

    // Delete button (clears the active buffer after a confirmation dialog)
    m_deleteBtn = new QPushButton("Delete image", this);
    m_deleteBtn->setFixedSize(130, 30);
    connect(m_deleteBtn, &QPushButton::clicked, this, &FtWindow::onDeleteImage);

    // Undo / Redo buttons
    m_undoBtn = new QPushButton("Undo", this);
    m_undoBtn->setFixedSize(130, 30);
    connect(m_undoBtn, &QPushButton::clicked, this, &FtWindow::onUndo);
    m_redoBtn = new QPushButton("Redo", this);
    m_redoBtn->setFixedSize(130, 30);
    connect(m_redoBtn, &QPushButton::clicked, this, &FtWindow::onRedo);
    updateUndoRedoButtons();

    // Fullscreen toggle button
    m_fullscreenBtn = new QPushButton("Go fullscreen", this);
    m_fullscreenBtn->setFixedSize(180, 30);
    connect(m_fullscreenBtn, &QPushButton::clicked, this, &FtWindow::onToggleFullscreen);

    // Mode cycle button
    m_modeBtn = new QPushButton(modeLabel(), this);
    m_modeBtn->setFixedSize(180, 30);
    m_modeBtn->setToolTip("Switch between display modes for the Fourier space");
    connect(m_modeBtn, &QPushButton::clicked, this, &FtWindow::onCycleMode);
    m_modeBtn->hide();

    // m_maskBtn ("mask center for display") is custom-painted in paintEvent
    // so the panel-2 tool dialog can cover it. Click handling lives in
    // mousePressEvent via m_maskBtnRect.

    // Any top-level button click should dismiss the "New image" popup
    // (these buttons intercept mouse events, so mousePressEvent does not run).
    auto dismissNewImg = [this]() { if (m_newImageActive) onNewImageCancel(); };
    for (QPushButton *b : {m_loadBtn, m_saveBtn, m_reloadBtn, m_deleteBtn,
                           m_undoBtn, m_redoBtn, m_fullscreenBtn, m_modeBtn}) {
        connect(b, &QPushButton::pressed, this, dismissNewImg);
    }

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
    m_bandEraseOutside->setStyleSheet(checkBoxStyle("white"));
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

    m_resetBandBtn = new QPushButton("Reset", this);
    m_resetBandBtn->setFixedSize(80, 26);
    connect(m_resetBandBtn, &QPushButton::clicked, this, [this]() {
        m_bandInnerR = 0.1;
        m_bandOuterR = 0.9;
        update();
    });
    m_resetBandBtn->hide();

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

    m_lineOffsetEdit = new QLineEdit("0", this);
    m_lineOffsetEdit->setFixedSize(50, 22);
    m_lineOffsetEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_lineOffsetEdit->setToolTip(
        "Signed offset of the line from the Fourier centre, in pixels,\n"
        "measured perpendicular to the line direction. Drag the line in\n"
        "the right half of panel 2 to change this interactively.");
    m_lineOffsetEdit->hide();
    connect(m_lineOffsetEdit, &QLineEdit::editingFinished, this, [this]() {
        bool ok = false;
        double v = m_lineOffsetEdit->text().toDouble(&ok);
        if (ok) { m_lineOffset = v; update(); }
    });

    m_lineEraseOutsideBtn = new QCheckBox("Erase pixels outside of line", this);
    m_lineEraseOutsideBtn->setStyleSheet(checkBoxStyle("white"));
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
    m_latticeEraseOutside->setStyleSheet(checkBoxStyle("white"));
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

    // Lattice basis vector entry (u = (Ux, Uy), v = (Vx, Vy))
    auto makeLatticeVecEdit = [this](const QString &tip) {
        auto *e = new QLineEdit(this);
        e->setFixedSize(52, 22);
        e->setStyleSheet("background:#222; color:white; border:1px solid #888;");
        e->setToolTip(tip);
        e->hide();
        return e;
    };
    QString uTip = "x/y component of the reciprocal-lattice basis vector u,\n"
                   "in Fourier pixels relative to the FFT centre. All lattice\n"
                   "spots generated by integer combinations of u and v are\n"
                   "selected. Edit to set u numerically, or drag the u handle\n"
                   "in panel 2.";
    QString vTip = "x/y component of the reciprocal-lattice basis vector v,\n"
                   "in Fourier pixels relative to the FFT centre. All lattice\n"
                   "spots generated by integer combinations of u and v are\n"
                   "selected. Edit to set v numerically, or drag the v handle\n"
                   "in panel 2.";
    m_latticeUxEdit = makeLatticeVecEdit(uTip);
    m_latticeUyEdit = makeLatticeVecEdit(uTip);
    m_latticeVxEdit = makeLatticeVecEdit(vTip);
    m_latticeVyEdit = makeLatticeVecEdit(vTip);
    auto applyLatticeVecEdits = [this]() {
        bool ok = false;
        double v;
        v = m_latticeUxEdit->text().toDouble(&ok); if (ok) m_latticeUx = v;
        v = m_latticeUyEdit->text().toDouble(&ok); if (ok) m_latticeUy = v;
        v = m_latticeVxEdit->text().toDouble(&ok); if (ok) m_latticeVx = v;
        v = m_latticeVyEdit->text().toDouble(&ok); if (ok) m_latticeVy = v;
        syncLatticeVectorEdits();
        update();
    };
    connect(m_latticeUxEdit, &QLineEdit::editingFinished, this, applyLatticeVecEdits);
    connect(m_latticeUyEdit, &QLineEdit::editingFinished, this, applyLatticeVecEdits);
    connect(m_latticeVxEdit, &QLineEdit::editingFinished, this, applyLatticeVecEdits);
    connect(m_latticeVyEdit, &QLineEdit::editingFinished, this, applyLatticeVecEdits);
    syncLatticeVectorEdits();

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

    m_ftCropKeepSizeBtn = new QCheckBox("Keep original size", this);
    m_ftCropKeepSizeBtn->setStyleSheet(checkBoxStyle("white"));
    m_ftCropKeepSizeBtn->setChecked(true);
    m_ftCropKeepSizeBtn->setToolTip(
        "Checked: the FFT array stays at its original dimensions and\n"
        "         pixels outside the central crop window are zeroed.\n"
        "         The real-space image keeps its sampling.\n"
        "Unchecked: the FFT array is physically shrunk by N. The real-\n"
        "         space image becomes N\u00D7 smaller, with N\u00D7 larger pixels.");
    m_ftCropKeepSizeBtn->hide();

    m_applyFtCropBtn = new QPushButton("Fourier crop", this);
    m_applyFtCropBtn->setFixedSize(120, 26);
    connect(m_applyFtCropBtn, &QPushButton::clicked, this, &FtWindow::onApplyFtCrop);
    m_applyFtCropBtn->hide();

    m_applyFtPadBtn = new QPushButton("Fourier pad", this);
    m_applyFtPadBtn->setFixedSize(120, 26);
    m_applyFtPadBtn->setToolTip(
        "Zero-pad the current Fourier transform to N\u00D7 its current\n"
        "linear dimensions, where N is the selected factor. This\n"
        "oversamples the real-space image (finer pixel sampling\n"
        "without adding information). The FFT size is capped at\n"
        "4096\u00D74096.");
    connect(m_applyFtPadBtn, &QPushButton::clicked, this, &FtWindow::onApplyFtPad);
    m_applyFtPadBtn->hide();

    // Cross-section evaluation-line direction widget (degrees)
    m_crossSectionDirEdit = new QLineEdit("0", this);
    m_crossSectionDirEdit->setFixedSize(50, 22);
    m_crossSectionDirEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_crossSectionDirEdit->setToolTip(
        "Direction of the evaluation line through the Fourier-transform\n"
        "center, in degrees (counter-clockwise from the horizontal axis).\n"
        "Type a value to rotate the red lines, or drag them in panel 2 to\n"
        "set the direction; the field and profiles update live.");
    connect(m_crossSectionDirEdit, &QLineEdit::textChanged, this, [this]() {
        if (m_crossSectionActive && m_ftComputed) {
            bool ok = false;
            double deg = m_crossSectionDirEdit->text().toDouble(&ok);
            if (ok) {
                m_crossSectionAngle = -deg;   // math (y-up) -> screen (y-down)
                computeCrossSectionProfile();
                update();
            }
        }
    });
    m_crossSectionDirEdit->hide();

    // Cross-section integration-width widget (reciprocal pixels, minimum 1)
    m_crossSectionWidthEdit = new QLineEdit("5", this);
    m_crossSectionWidthEdit->setFixedSize(50, 22);
    m_crossSectionWidthEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_crossSectionWidthEdit->setValidator(new QDoubleValidator(1.0, 1.0e6, 2, m_crossSectionWidthEdit));
    m_crossSectionWidthEdit->setToolTip(
        "Width of the integration band used for the 1D profiles, in\n"
        "reciprocal (Fourier) pixels. Pixels within this band around the\n"
        "line are integrated into the amplitude and phase profiles.\n"
        "Minimum 1. Increase to reduce noise, decrease for higher angular\n"
        "resolution. The profiles update live as you type.");
    connect(m_crossSectionWidthEdit, &QLineEdit::textChanged, this, [this]() {
        if (m_crossSectionActive && m_ftComputed) {
            computeCrossSectionProfile();
            update();
        }
    });
    m_crossSectionWidthEdit->hide();

    // Panel 2 Fourier-space symmetrize widgets
    m_p2SymmetryEdit = new QLineEdit("4", this);
    m_p2SymmetryEdit->setFixedSize(50, 22);
    m_p2SymmetryEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_p2SymmetryEdit->setToolTip(
        "Rotational symmetry order N to enforce on the Fourier\n"
        "transform around its center (DC term). The current FT is\n"
        "averaged with its rotated copies at angles k·360°/N\n"
        "(k = 0…N−1). The real-space image is updated via the\n"
        "inverse FFT. Use N = 2 for two-fold, 3 for three-fold, etc.");
    m_p2SymmetryEdit->hide();
    m_applyP2SymmetryBtn = new QPushButton("Apply symmetry", this);
    m_applyP2SymmetryBtn->setFixedSize(130, 26);
    connect(m_applyP2SymmetryBtn, &QPushButton::clicked, this, &FtWindow::onApplyFtSymmetry);
    m_applyP2SymmetryBtn->hide();

    // CTF parameter widgets
    auto makeCtfEdit = [this](const QString &def) {
        auto *e = new QLineEdit(def, this);
        e->setFixedSize(60, 22);
        e->setStyleSheet("background:#222; color:white; border:1px solid #888;");
        e->hide();
        return e;
    };
    m_ctfVoltageEdit = makeCtfEdit("300");
    m_ctfVoltageEdit->setToolTip(
        "Acceleration voltage of the microscope in kV. Determines the\n"
        "relativistic electron wavelength used in the Scherzer contrast\n"
        "transfer function.");
    m_ctfEnergySpreadEdit = makeCtfEdit("0.7");
    m_ctfEnergySpreadEdit->setToolTip(
        "Energy spread of the electron beam in eV. Used to model the\n"
        "temporal coherence envelope of the CTF.");
    m_ctfOpenAngleEdit = makeCtfEdit("0.1");
    m_ctfOpenAngleEdit->setToolTip(
        "Beam half-convergence angle (gun opening semi-angle) in mrad.\n"
        "Controls the spatial-coherence envelope\n"
        "   E_s(q) = exp(-\u03C0\u00B2\u00B7\u03B1\u00B2\u00B7q\u00B2\u00B7(\u0394f + Cs\u00B7\u03BB\u00B2\u00B7q\u00B2)\u00B2)\n"
        "that damps the CTF at high spatial frequencies due to a\n"
        "non-zero illumination aperture.");
    m_ctfDefocusSpreadEdit = makeCtfEdit("5");
    m_ctfDefocusSpreadEdit->setToolTip(
        "Defocus spread (\u0394z) in nm. Models the defocus-dependent\n"
        "temporal-coherence envelope\n"
        "   E_t(q) = exp(-\u00BD(\u03C0\u00B7\u03BB\u00B7\u0394z\u00B7q\u00B2)\u00B2).\n"
        "Combined in quadrature with the chromatic contribution from\n"
        "the energy spread.");
    m_ctfCsEdit = makeCtfEdit("2.7");
    m_ctfCsEdit->setToolTip(
        "Spherical aberration constant Cs of the objective lens in mm.");
    m_ctfDefocusEdit = makeCtfEdit("1000");
    m_ctfDefocusEdit->setToolTip(
        "Defocus value in nm (positive = underfocus).");
    m_ctfAstigEdit = makeCtfEdit("0");
    m_ctfAstigEdit->setToolTip(
        "Astigmatism amplitude in nm. This is the defocus deviation\n"
        "along the astigmatism axis relative to the average defocus.\n"
        "The defocus varies azimuthally as\n"
        "   \u0394f(\u03B8) = \u0394f_avg + \u0394f_A \u00B7 cos(2(\u03B8 - \u03B1))\n"
        "so the average defocus is found 45\u00B0 away from the astigmatism\n"
        "direction.");
    m_ctfAstigAngleEdit = makeCtfEdit("0");
    m_ctfAstigAngleEdit->setToolTip(
        "Astigmatism direction in degrees, measured counter-clockwise\n"
        "from the horizontal axis, following the usual EM convention.");
    m_ctfAmpContrastEdit = makeCtfEdit("7");
    m_ctfAmpContrastEdit->setToolTip(
        "Amplitude contrast in percent. Used as the amplitude-contrast\n"
        "term B in the CTF; the phase-contrast term is then\n"
        "   A = √(1 − B²),\n"
        "and the CTF is A·sin(−χ) + B·cos(−χ).");
    m_ctfBeamtiltEdit = makeCtfEdit("0");
    m_ctfBeamtiltEdit->setToolTip(
        "Beam tilt magnitude in mrad. A tilted illumination adds a\n"
        "coma-like phase shift to the wave aberration\n"
        "   Δχ = 2π·Cs·λ²·q³·τ·cos(θ − τ_dir)\n"
        "where τ is the tilt angle (rad) and τ_dir its direction.");
    m_ctfBeamtiltDirEdit = makeCtfEdit("0");
    m_ctfBeamtiltDirEdit->setToolTip(
        "Beam tilt direction in degrees, measured counter-clockwise\n"
        "from the horizontal axis, following the usual EM convention.");
    m_ctfCancelBtn = new QPushButton("Cancel", this);
    m_ctfCancelBtn->setFixedSize(80, 26);
    m_ctfCancelBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_ctfCancelBtn, &QPushButton::clicked, this, &FtWindow::onCtfCancel);
    m_ctfCancelBtn->hide();
    m_ctfComputeBtn = new QPushButton("Compute", this);
    m_ctfComputeBtn->setFixedSize(80, 26);
    m_ctfComputeBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_ctfComputeBtn, &QPushButton::clicked, this, &FtWindow::onCtfCompute);
    m_ctfComputeBtn->hide();
    for (QLineEdit *e : { m_ctfVoltageEdit, m_ctfEnergySpreadEdit,
                          m_ctfDefocusSpreadEdit, m_ctfOpenAngleEdit,
                          m_ctfCsEdit, m_ctfDefocusEdit,
                          m_ctfAstigEdit, m_ctfAstigAngleEdit,
                          m_ctfAmpContrastEdit, m_ctfBeamtiltEdit,
                          m_ctfBeamtiltDirEdit })
        connect(e, &QLineEdit::returnPressed, this, &FtWindow::onCtfCompute);

    // CTF FIT parameter widgets (only kV, Cs and a target buffer; the defocus,
    // astigmatism and astigmatism angle are recovered by the fit itself).
    m_ctfFitVoltageEdit = makeCtfEdit("300");
    m_ctfFitVoltageEdit->setToolTip(
        "Acceleration voltage of the microscope in kV. Determines the\n"
        "relativistic electron wavelength used in the CTF fit.");
    m_ctfFitCsEdit = makeCtfEdit("2.7");
    m_ctfFitCsEdit->setToolTip(
        "Spherical aberration constant Cs of the objective lens in mm.");
    m_ctfFitInputCombo = new QComboBox(this);
    for (int i = 0; i < HISTORY_SLOTS; i++)
        m_ctfFitInputCombo->addItem(QString(QChar('A' + i)));
    m_ctfFitInputCombo->setFixedSize(60, 22);
    m_ctfFitInputCombo->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_ctfFitInputCombo->setToolTip(
        "Input buffer (A…P) whose Fourier transform the CTF is fitted to.\n"
        "The fitted CTF composite is written into the currently selected\n"
        "buffer.");
    m_ctfFitInputCombo->hide();
    m_ctfFitResHiEdit = makeCtfEdit("3");
    m_ctfFitResHiEdit->setToolTip(
        "Upper resolution limit in Ångström (the finest, i.e. smallest\n"
        "d-spacing) that is included in the CTF fit. Frequencies beyond\n"
        "this (finer than the Nyquist limit) are ignored.");
    m_ctfFitResLoEdit = makeCtfEdit("30");
    m_ctfFitResLoEdit->setToolTip(
        "Lower resolution limit in Ångström (the coarsest, i.e. largest\n"
        "d-spacing) that is included in the CTF fit. Very low frequencies\n"
        "below this are ignored.");
    m_ctfFitCancelBtn = new QPushButton("Cancel", this);
    m_ctfFitCancelBtn->setFixedSize(80, 26);
    m_ctfFitCancelBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_ctfFitCancelBtn, &QPushButton::clicked, this, &FtWindow::onCtfFitCancel);
    m_ctfFitCancelBtn->hide();
    m_ctfFitExecuteBtn = new QPushButton("Execute", this);
    m_ctfFitExecuteBtn->setFixedSize(80, 26);
    m_ctfFitExecuteBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_ctfFitExecuteBtn, &QPushButton::clicked, this, &FtWindow::onCtfFitExecute);
    m_ctfFitExecuteBtn->hide();
    for (QLineEdit *e : { m_ctfFitVoltageEdit, m_ctfFitCsEdit,
                          m_ctfFitResHiEdit, m_ctfFitResLoEdit })
        connect(e, &QLineEdit::returnPressed, this, &FtWindow::onCtfFitExecute);

    // Phase ramp parameter widgets
    m_phaseRampSizeCombo = new QComboBox(this);
    for (int sz : {512, 1024, 2048, 4096})
        m_phaseRampSizeCombo->addItem(QString::number(sz), sz);
    m_phaseRampSizeCombo->setCurrentIndex(1);   // 1024 default
    m_phaseRampSizeCombo->setFixedSize(80, 28);
    m_phaseRampSizeCombo->setStyleSheet(
        "QComboBox { background:#222; color:white; border:1px solid #888;"
        "  padding: 2px 8px; }"
        "QComboBox::drop-down { width: 20px; }"
        "QComboBox QAbstractItemView { background:#222; color:white;"
        "  selection-background-color:#555; min-width: 70px; padding: 4px; }"
    );
    m_phaseRampSizeCombo->setToolTip(
        "Linear size N of the Fourier transform to be created (NxN).");
    m_phaseRampSizeCombo->hide();

    m_phaseRampDirEdit = new QLineEdit("30", this);
    m_phaseRampDirEdit->setFixedSize(60, 22);
    m_phaseRampDirEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_phaseRampDirEdit->setToolTip(
        "Direction of the phase ramp, in degrees, measured counter-\n"
        "clockwise from the +x axis. The phase increases linearly along\n"
        "this direction.");
    m_phaseRampDirEdit->hide();

    m_phaseRampStepEdit = new QLineEdit("10", this);
    m_phaseRampStepEdit->setFixedSize(60, 22);
    m_phaseRampStepEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_phaseRampStepEdit->setToolTip(
        "Phase increment per pixel along the ramp direction, in degrees.\n"
        "The phase at the origin is zero and grows by this amount for\n"
        "each unit step along the chosen direction.");
    m_phaseRampStepEdit->hide();

    m_phaseRampCancelBtn = new QPushButton("Cancel", this);
    m_phaseRampCancelBtn->setFixedSize(80, 26);
    m_phaseRampCancelBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_phaseRampCancelBtn, &QPushButton::clicked, this, &FtWindow::onPhaseRampCancel);
    m_phaseRampCancelBtn->hide();

    m_phaseRampComputeBtn = new QPushButton("Compute", this);
    m_phaseRampComputeBtn->setFixedSize(80, 26);
    m_phaseRampComputeBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_phaseRampComputeBtn, &QPushButton::clicked, this, &FtWindow::onPhaseRampCompute);
    m_phaseRampComputeBtn->hide();

    for (QLineEdit *e : {m_phaseRampDirEdit, m_phaseRampStepEdit})
        connect(e, &QLineEdit::returnPressed, this, &FtWindow::onPhaseRampCompute);

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
    m_p1BrushSolidLabel = new QLabel("Paint brush solid diameter:", this);
    m_p1BrushSolidLabel->setStyleSheet("color: white;");
    m_p1BrushSolidLabel->hide();
    m_p1BrushSolidDiameterEdit = new QLineEdit("0", this);
    m_p1BrushSolidDiameterEdit->setFixedSize(40, 22);
    m_p1BrushSolidDiameterEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_p1BrushSolidDiameterEdit->setToolTip(
        "Diameter, in image pixels, of a sharp-edged solid disk used\n"
        "as the base paint brush footprint. Pixels inside the disk\n"
        "are painted with the target value. If a Gaussian diameter\n"
        "is also set, this disk is blurred by that Gaussian to soften\n"
        "the edge. Set to 0 to use a pure Gaussian brush.");
    m_p1BrushSolidDiameterEdit->hide();
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

    // Panel 1 symmetrize widgets
    m_p1SymmetryLabel = new QLabel("Symmetry to apply:", this);
    m_p1SymmetryLabel->setStyleSheet("color: white;");
    m_p1SymmetryLabel->hide();
    m_p1SymmetryEdit = new QLineEdit("4", this);
    m_p1SymmetryEdit->setFixedSize(50, 22);
    m_p1SymmetryEdit->setStyleSheet("background:#222; color:white; border:1px solid #888;");
    m_p1SymmetryEdit->setToolTip(
        "Rotational symmetry order N to apply around the image\n"
        "center. The image is averaged with its rotated copies at\n"
        "angles k·360°/N (k = 0…N−1), enforcing N-fold rotational\n"
        "symmetry. Use N = 2 for two-fold, 3 for three-fold, etc.");
    m_p1SymmetryEdit->hide();
    m_applyP1SymmetryBtn = new QPushButton("Apply symmetry", this);
    m_applyP1SymmetryBtn->setFixedSize(130, 26);
    connect(m_applyP1SymmetryBtn, &QPushButton::clicked, this, &FtWindow::onApplySymmetry);
    m_applyP1SymmetryBtn->hide();

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
    m_amyloidRiseEdit->setStyleSheet("background:white; color:black; border:1px solid #888;");
    m_amyloidRiseEdit->setToolTip(
        "Helical rise in \u00C5ngstr\u00F6m \u2014 the axial translation between\n"
        "successive subunits along the helix. For typical amyloid\n"
        "filaments (cross-\u03B2 structure) the rise is \u2248 4.75 \u00C5,\n"
        "corresponding to the inter-strand spacing.");
    m_amyloidRiseEdit->hide();
    m_amyloidTwistEdit = new QLineEdit("-1", this);
    m_amyloidTwistEdit->setFixedSize(60, 22);
    m_amyloidTwistEdit->setStyleSheet("background:white; color:black; border:1px solid #888;");
    m_amyloidTwistEdit->setToolTip(
        "Helical twist in degrees \u2014 the azimuthal rotation between\n"
        "successive subunits. Negative = left-handed twist.\n"
        "For amyloid filaments typical values are \u22121\u00B0 to \u22122\u00B0.\n"
        "The twist determines the crossover distance visible\n"
        "in projection (crossover = 360 / |twist| \u00D7 rise).");
    m_amyloidTwistEdit->hide();
    m_amyloidMapCombo = new QComboBox(this);
    for (int i = 0; i < HISTORY_SLOTS; i++)
        m_amyloidMapCombo->addItem(QString(QChar('a' + i)));
    m_amyloidMapCombo->setFixedSize(50, 22);
    m_amyloidMapCombo->setStyleSheet(
        "QComboBox { background:white; color:black; border:1px solid #888;"
        "  padding: 2px 8px; }"
        "QComboBox::drop-down { width: 20px; }"
        "QComboBox QAbstractItemView { background:white; color:black;"
        "  selection-background-color:#cce; min-width: 60px; padding: 4px; }");
    m_amyloidMapCombo->setToolTip(
        "Select the 2D map (buffer a\u2026p) that provides the\n"
        "cross-section of the fibril. This map is extruded into\n"
        "a thin 3D slab and placed repeatedly along the fibril\n"
        "trajectory with the specified helical rise and twist.");
    m_amyloidMapCombo->hide();
    m_amyloidSizeCombo = new QComboBox(this);
    m_amyloidSizeCombo->addItem("512");
    m_amyloidSizeCombo->addItem("1024");
    m_amyloidSizeCombo->addItem("2048");
    m_amyloidSizeCombo->addItem("4096");
    m_amyloidSizeCombo->setCurrentIndex(1);
    m_amyloidSizeCombo->setFixedSize(70, 22);
    m_amyloidSizeCombo->setStyleSheet(
        "QComboBox { background:white; color:black; border:1px solid #888;"
        "  padding: 2px 8px; }"
        "QComboBox::drop-down { width: 20px; }"
        "QComboBox QAbstractItemView { background:white; color:black;"
        "  selection-background-color:#cce; min-width: 70px; padding: 4px; }");
    m_amyloidSizeCombo->setToolTip(
        "Side length (in pixels) of the black image created for\n"
        "drawing amyloid fibril trajectories. Used when the current\n"
        "buffer is empty or the Amyloid tool needs a fresh canvas.");
    m_amyloidSizeCombo->hide();
    m_amyloidNoiseBtn = new QCheckBox("Add gray noise", this);
    m_amyloidNoiseBtn->setStyleSheet(checkBoxStyle("#333"));
    m_amyloidNoiseBtn->setChecked(true);
    m_amyloidNoiseBtn->setToolTip(
        "Add Gaussian noise to the entire image after rendering the\n"
        "filament. This makes the result look like a real cryo-EM\n"
        "micrograph where the signal sits in a noisy background.\n"
        "The noise sigma is set by the field to the right.");
    m_amyloidNoiseBtn->hide();
    m_amyloidNoiseEdit = new QLineEdit("0.3", this);
    m_amyloidNoiseEdit->setFixedSize(40, 22);
    m_amyloidNoiseEdit->setStyleSheet("background:white; color:black; border:1px solid #888;");
    m_amyloidNoiseEdit->setToolTip(
        "Noise level as a fraction of the peak filament signal.\n"
        "0.3 = moderate noise (SNR \u2248 3), 1.0 = very noisy (SNR \u2248 1),\n"
        "0.1 = low noise. The noise is additive Gaussian with zero\n"
        "mean and this sigma, applied to the whole image.");
    m_amyloidNoiseEdit->hide();
    m_amyloidPersistEdit = new QLineEdit("14", this);
    m_amyloidPersistEdit->setFixedSize(60, 22);
    m_amyloidPersistEdit->setStyleSheet("background:white; color:black; border:1px solid #888;");
    m_amyloidPersistEdit->setToolTip(
        "Persistence length in \u00B5m \u2014 the characteristic length scale\n"
        "over which a filament maintains its directional orientation.\n"
        "For amyloid fibrils, typical values are 1\u201320 \u00B5m.\n"
        "A larger value makes the filament stiffer (less bending\n"
        "allowed). Set to 0 to disable the curvature constraint.");
    m_amyloidPersistEdit->hide();
    m_amyloidWaveEdit = new QLineEdit("50", this);
    m_amyloidWaveEdit->setFixedSize(60, 22);
    m_amyloidWaveEdit->setStyleSheet("background:white; color:black; border:1px solid #888;");
    m_amyloidWaveEdit->setToolTip(
        "Waviness wavelength in pixels \u2014 period of the sinusoidal\n"
        "lateral bending that displaces the cross-section along the\n"
        "trajectory axis. Typical values 30\u2013100 px.");
    m_amyloidWaveEdit->hide();
    m_amyloidAmplEdit = new QLineEdit("1", this);
    m_amyloidAmplEdit->setFixedSize(60, 22);
    m_amyloidAmplEdit->setStyleSheet("background:white; color:black; border:1px solid #888;");
    m_amyloidAmplEdit->setToolTip(
        "Waviness amplitude in pixels \u2014 peak lateral displacement\n"
        "of the sinusoidal bending along the trajectory axis.\n"
        "Set to 0 to disable waviness.");
    m_amyloidAmplEdit->hide();
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
        m_amyloidSignalBtn->setText(m_amyloidBlackSignal ? "Black signal" : "White signal");
        QResizeEvent ev(size(), size());
        resizeEvent(&ev);
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
    connect(m_amyloidRiseEdit,  &QLineEdit::returnPressed, this, &FtWindow::onAmyloidCompute);
    connect(m_amyloidTwistEdit, &QLineEdit::returnPressed, this, &FtWindow::onAmyloidCompute);
    connect(m_amyloidNoiseEdit,   &QLineEdit::returnPressed, this, &FtWindow::onAmyloidCompute);
    connect(m_amyloidPersistEdit, &QLineEdit::returnPressed, this, &FtWindow::onAmyloidCompute);
    connect(m_amyloidWaveEdit,    &QLineEdit::returnPressed, this, &FtWindow::onAmyloidCompute);
    connect(m_amyloidAmplEdit,    &QLineEdit::returnPressed, this, &FtWindow::onAmyloidCompute);

    // Measure tool Cancel button
    m_measureCancelBtn = new QPushButton("Cancel", this);
    m_measureCancelBtn->setFixedSize(80, 26);
    m_measureCancelBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_measureCancelBtn, &QPushButton::clicked, this, &FtWindow::onMeasureCancel);
    m_measureCancelBtn->hide();

    // Shift / rotate tool Cancel buttons
    const QString cancelBtnStyle =
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }";
    m_shiftCancelBtn = new QPushButton("Cancel", this);
    m_shiftCancelBtn->setFixedSize(80, 26);
    m_shiftCancelBtn->setStyleSheet(cancelBtnStyle);
    connect(m_shiftCancelBtn, &QPushButton::clicked, this, &FtWindow::onShiftCancel);
    m_shiftCancelBtn->hide();

    m_rotateCancelBtn = new QPushButton("Cancel", this);
    m_rotateCancelBtn->setFixedSize(80, 26);
    m_rotateCancelBtn->setStyleSheet(cancelBtnStyle);
    connect(m_rotateCancelBtn, &QPushButton::clicked, this, &FtWindow::onRotateCancel);
    m_rotateCancelBtn->hide();

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

    // "New image" popup widgets (Create-or-Copy overlay in panel 1)
    auto newImgComboStyle = [](QComboBox *cb) {
        cb->setStyleSheet(
            "QComboBox { background:white; color:black; border:1px solid #888;"
            "  padding: 2px 6px; font-size: 18px; font-weight: bold; }"
            "QComboBox::drop-down { width: 22px; }"
            "QComboBox QAbstractItemView { background:white; color:black;"
            "  selection-background-color:#ccc; padding: 4px;"
            "  font-size: 18px; }");
    };
    m_newImgSrcCombo = new QComboBox(this);
    m_newImgSrcCombo->addItem("New 512");
    m_newImgSrcCombo->addItem("New 1024");
    m_newImgSrcCombo->addItem("New 2048");
    m_newImgSrcCombo->addItem("New 4096");
    for (int i = 0; i < HISTORY_SLOTS; i++)
        m_newImgSrcCombo->addItem(QString(QChar('a' + i)));
    m_newImgSrcCombo->setFixedSize(140, 34);
    newImgComboStyle(m_newImgSrcCombo);
    m_newImgSrcCombo->setToolTip(
        "Source: either a blank new image of the given size,\n"
        "or an existing history slot (a..p) to copy from.");
    m_newImgSrcCombo->hide();

    m_newImgTgtCombo = new QComboBox(this);
    for (int i = 0; i < HISTORY_SLOTS; i++)
        m_newImgTgtCombo->addItem(QString(QChar('a' + i)));
    m_newImgTgtCombo->setFixedSize(80, 34);
    newImgComboStyle(m_newImgTgtCombo);
    m_newImgTgtCombo->setToolTip(
        "Target history slot (a..p) to receive the new or copied image.");
    m_newImgTgtCombo->hide();

    m_newImgCancelBtn = new QPushButton("Cancel", this);
    m_newImgCancelBtn->setFixedSize(100, 30);
    m_newImgCancelBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee;"
        "  padding: 2px; font-weight: bold; }");
    connect(m_newImgCancelBtn, &QPushButton::clicked, this, &FtWindow::onNewImageCancel);
    m_newImgCancelBtn->hide();

    m_newImgCreateBtn = new QPushButton("Execute", this);
    m_newImgCreateBtn->setFixedSize(100, 30);
    m_newImgCreateBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee;"
        "  padding: 2px; font-weight: bold; }");
    connect(m_newImgCreateBtn, &QPushButton::clicked, this, &FtWindow::onNewImageCreate);
    m_newImgCreateBtn->hide();

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
    m_binKeepSizeBtn->setStyleSheet(checkBoxStyle("white"));
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

    // Crop widgets (hidden until crop mode active)
    auto makeCropEdit = [this](const QString &tip) {
        auto *e = new QLineEdit("0", this);
        e->setFixedSize(60, 22);
        e->setStyleSheet("background:#222; color:white; border:1px solid #888;");
        e->setValidator(new QIntValidator(0, 1000000, e));
        e->setToolTip(tip);
        e->hide();
        return e;
    };
    QString cropTip =
        "Selection corner in image pixels (origin = top-left). Drag a\n"
        "square on the image to set the region, or type exact values.\n"
        "The selection is always kept square (1:1); if you type a\n"
        "non-square box it is reduced to the largest enclosed square.";
    m_cropTLxEdit = makeCropEdit(cropTip);
    m_cropTLyEdit = makeCropEdit(cropTip);
    m_cropBRxEdit = makeCropEdit(cropTip);
    m_cropBRyEdit = makeCropEdit(cropTip);
    // Re-evaluate the selection when a coordinate field is committed. The axis
    // of the edited field (driver: 0=TLx, 1=TLy, 2=BRx, 3=BRy) sets the square's
    // side length and the perpendicular corner follows, so the user can both
    // grow and shrink the box while it stays square.
    auto applyCropEdits = [this](int driver) {
        if (m_image.isNull()) return;
        int W = m_image.width(), H = m_image.height();
        bool ok = false;
        int tlx = m_cropTLxEdit->text().toInt(&ok); if (!ok) tlx = m_cropRect.left();
        int tly = m_cropTLyEdit->text().toInt(&ok); if (!ok) tly = m_cropRect.top();
        int brx = m_cropBRxEdit->text().toInt(&ok); if (!ok) brx = m_cropRect.left() + m_cropRect.width();
        int bry = m_cropBRyEdit->text().toInt(&ok); if (!ok) bry = m_cropRect.top()  + m_cropRect.height();
        tlx = std::clamp(tlx, 0, W); tly = std::clamp(tly, 0, H);
        brx = std::clamp(brx, 0, W); bry = std::clamp(bry, 0, H);
        if (brx < tlx) std::swap(tlx, brx);
        if (bry < tly) std::swap(tly, bry);
        int side = (driver == 0 || driver == 2) ? (brx - tlx) : (bry - tly);
        side = std::max(0, side);
        side = std::min({side, W - tlx, H - tly});
        m_cropRect = QRect(tlx, tly, side, side);
        m_cropHasSelection = (side >= 2);
        syncCropEdits();
        update();
    };
    connect(m_cropTLxEdit, &QLineEdit::editingFinished, this, [applyCropEdits]() { applyCropEdits(0); });
    connect(m_cropTLyEdit, &QLineEdit::editingFinished, this, [applyCropEdits]() { applyCropEdits(1); });
    connect(m_cropBRxEdit, &QLineEdit::editingFinished, this, [applyCropEdits]() { applyCropEdits(2); });
    connect(m_cropBRyEdit, &QLineEdit::editingFinished, this, [applyCropEdits]() { applyCropEdits(3); });

    m_cropCancelBtn = new QPushButton("Cancel", this);
    m_cropCancelBtn->setFixedSize(80, 26);
    m_cropCancelBtn->setStyleSheet(
        "QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; }");
    connect(m_cropCancelBtn, &QPushButton::clicked, this, &FtWindow::onCropCancel);
    m_cropCancelBtn->hide();

    m_applyCropBtn = new QPushButton("Crop image", this);
    m_applyCropBtn->setFixedSize(110, 26);
    connect(m_applyCropBtn, &QPushButton::clicked, this, &FtWindow::onApplyCrop);
    m_applyCropBtn->hide();

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

    // All four toggle buttons next to the histograms ("freeze display
    // contrast" x2, "mark image center", "mask center for display") are
    // custom-painted in paintEvent so that panel-1 / panel-2 tool dialogs
    // can sit on top of them. State lives in m_imageContrastLocked,
    // m_ftContrastLocked, m_imageCenterMarked, m_maskCenter; click handling
    // is in mousePressEvent via the corresponding rects.

    // Restore history and active slot
#ifndef __EMSCRIPTEN__
    restoreHistory();

    QSettings settings("ft", "ft");
    m_maskCenter = settings.value("maskCenter", false).toBool();
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

    // Restore amyloid filament settings
    m_p1BrushValueEdit->setText(settings.value("p1BrushValue", "1").toString());
    m_p1BrushSolidDiameterEdit->setText(settings.value("p1BrushSolidDiameter", "0").toString());
    m_p1BrushDiameterEdit->setText(settings.value("p1BrushGaussianDiameter", "5").toString());
    auto saveP1Brush = [this]() {
        QSettings s("ft", "ft");
        s.setValue("p1BrushValue",           m_p1BrushValueEdit->text());
        s.setValue("p1BrushSolidDiameter",   m_p1BrushSolidDiameterEdit->text());
        s.setValue("p1BrushGaussianDiameter",m_p1BrushDiameterEdit->text());
    };
    connect(m_p1BrushValueEdit,          &QLineEdit::editingFinished, this, saveP1Brush);
    connect(m_p1BrushSolidDiameterEdit,  &QLineEdit::editingFinished, this, saveP1Brush);
    connect(m_p1BrushDiameterEdit,       &QLineEdit::editingFinished, this, saveP1Brush);

    m_amyloidRiseEdit->setText(settings.value("amyloidRise", "4.75").toString());
    m_amyloidTwistEdit->setText(settings.value("amyloidTwist", "-1").toString());
    m_amyloidMapCombo->setCurrentIndex(settings.value("amyloidMapIdx", 0).toInt());
    m_amyloidSizeCombo->setCurrentIndex(settings.value("amyloidSizeIdx", 1).toInt());
    m_amyloidNoiseBtn->setChecked(settings.value("amyloidNoise", true).toBool());
    m_amyloidNoiseEdit->setText(settings.value("amyloidNoiseSigma", "0.3").toString());
    m_amyloidWaveEdit->setText(settings.value("amyloidWave", "50").toString());
    m_amyloidAmplEdit->setText(settings.value("amyloidAmpl", "1").toString());
    m_amyloidBlackSignal = settings.value("amyloidBlackSignal", false).toBool();
    if (m_amyloidBlackSignal)
        m_amyloidSignalBtn->setText("Black signal");

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
    } else if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS
               && m_history[m_activeSlot].deferred) {
        // The slot that was active last time was skipped by restoreHistory
        // (large image, or file on a network volume). Keep it selected but
        // leave the panels empty — clicking it loads it.
    } else {
        m_activeSlot = -1;
    }
#else
    m_displayMode = 3;
    m_modeBtn->setText(modeLabel());
    m_activeSlot = -1;
#endif

    // If no active buffer was restored, select the first occupied slot (if any)
    if (m_activeSlot < 0) {
        for (int i = 0; i < HISTORY_SLOTS; i++) {
            if (m_history[i].occupied) {
                m_activeSlot    = i;
                m_image         = m_history[i].image;
                m_imagePath     = m_history[i].path;
                m_imageRawPixels = m_history[i].rawPixels;
                m_imageMinVal   = m_history[i].minVal;
                m_imageMaxVal   = m_history[i].maxVal;
                m_imageDispMin  = m_history[i].minVal;
                m_imageDispMax  = m_history[i].maxVal;
                m_pixelSize     = m_history[i].pixelSize;
                if (!m_image.isNull()) {
                    m_zoom[0].reset(m_image.width(), m_image.height());
                    computeFFT();
                }
                break;
            }
        }
    }

    // First-time launch (nothing restored, no occupied slot): default to
    // buffer a so a slot is always selected. The empty-slot case is handled
    // exactly like clicking an empty history slot in mousePressEvent.
    if (m_activeSlot < 0)
        m_activeSlot = 0;

    // If no slot is occupied (fresh launch or empty session restore),
    // auto-load a default example into buffer a so the user has something
    // to work with immediately.
    bool anySlotOccupied = false;
    for (int i = 0; i < HISTORY_SLOTS; i++)
        if (m_history[i].occupied) { anySlotOccupied = true; break; }
    if (!anySlotOccupied) {
        m_activeSlot = 0;   // buffer a
        fetchAndLoadImage(QStringLiteral("Exercise_01-Photos/lorenz_1999.png"));
    }
}

FtWindow::~FtWindow()
{
    // Tell any in-flight slot-loading worker that this window is going away, so
    // it does not post its result back to a dead object (see startSlotLoad()).
    std::lock_guard<std::mutex> lock(m_life->mutex);
    m_life->alive = false;
}

// ---------------------------------------------------------------------------
//  Layout
// ---------------------------------------------------------------------------
void FtWindow::resizeEvent(QResizeEvent *)
{
    m_loadBtn->move(8, 8);
    m_createBtn->move(8 + m_loadBtn->width() + 4, 8);
    int hy0 = height() - height() / 5;

    // Reload / Save / Delete sit stacked in the gutter between the two history
    // panels (3 and 4). paintEvent keeps the thumbnail grids clear of the same
    // gutter — both sides derive its width from historyButtonGutter().
    {
        int bw   = m_reloadBtn->width();
        int bh   = m_reloadBtn->height();
        int gap  = 6;
        int bx   = width() / 2 - bw / 2;
        int top  = hy0 + 2;
        int by   = top + ((height() - top) - (3 * bh + 2 * gap)) / 2;
        if (by < top + 4) by = top + 4;
        m_reloadBtn->move(bx, by);
        m_saveBtn  ->move(bx, by + (bh + gap));
        m_deleteBtn->move(bx, by + 2 * (bh + gap));
    }
    // When running standalone, the "Fourier Analyzer" title and the "Manual"
    // button below it occupy the top-center area, so push undo/redo below
    // both of them. When embedded, the title is hidden but the Manual
    // button is still drawn at the top, so reserve space for one box.
    int undoY = isWindow() ? (8 + 42 * 2) : (8 + 42);
    m_undoBtn->move((width() - m_undoBtn->width()) / 2, undoY);
    m_redoBtn->move((width() - m_redoBtn->width()) / 2, undoY + m_undoBtn->height() + 4);
    m_fullscreenBtn->move(width() - m_fullscreenBtn->width() - 8, 8);
    m_modeBtn->move(width() - m_modeBtn->width() - 8, 8 + m_fullscreenBtn->height() + 4);

    // Scale factor for tool dialogue widgets based on panel height
    int hy = height() - height() / 5;
    double sc = std::clamp(hy / 800.0, 0.5, 1.0);
    int fontSize = std::max(9, static_cast<int>(11 * sc));
    int editH    = std::max(16, static_cast<int>(22 * sc));
    int btnH     = std::max(18, static_cast<int>(26 * sc));

    // Styles for widgets on white rectangle background
    QString editSS  = QString("background:white; color:black; border:1px solid #888; font-size: %1px;").arg(fontSize);
    // Keep the full indicator styling (white box + black checkmark) here, not
    // just text color — otherwise the resize clobbers checkBoxStyle() from the
    // constructor and the box reverts to the invisible native indicator.
    QString cbSS    = checkBoxStyle("#333", fontSize);
    QString btnSS   = QString("QPushButton { background-color: #888; border: 2px outset #aaa; color: #eee; padding: 2px; font-size: %1px; }").arg(fontSize);

    // Bandpass widgets (sizes only; positions set in paintEvent)
    m_smoothEdit->setFixedSize(static_cast<int>(40 * sc), editH);
    m_smoothEdit->setStyleSheet(editSS);
    m_bandEraseOutside->setStyleSheet(cbSS);
    m_applyBandBtn->setFixedSize(static_cast<int>(100 * sc), btnH);
    m_applyBandBtn->setStyleSheet(btnSS);
    m_resetBandBtn->setFixedSize(static_cast<int>(80 * sc), btnH);
    m_resetBandBtn->setStyleSheet(btnSS);

    m_lineWidthEdit->setFixedSize(static_cast<int>(40 * sc), editH);
    m_lineWidthEdit->setStyleSheet(editSS);
    m_lineDirectionEdit->setFixedSize(static_cast<int>(50 * sc), editH);
    m_lineDirectionEdit->setStyleSheet(editSS);
    m_lineOffsetEdit->setFixedSize(static_cast<int>(50 * sc), editH);
    m_lineOffsetEdit->setStyleSheet(editSS);
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
    for (QLineEdit *e : {m_latticeUxEdit, m_latticeUyEdit,
                         m_latticeVxEdit, m_latticeVyEdit}) {
        e->setFixedSize(static_cast<int>(55 * sc), editH);
        e->setStyleSheet(editSS);
    }
    m_latticeEraseOutside->setStyleSheet(cbSS);
    m_latticeApplyBtn->setFixedSize(static_cast<int>(100 * sc), btnH);
    m_latticeApplyBtn->setStyleSheet(btnSS);

    // Cross-section direction + integration-width widgets (sizes only)
    m_crossSectionDirEdit->setFixedSize(static_cast<int>(50 * sc), editH);
    m_crossSectionDirEdit->setStyleSheet(editSS);
    m_crossSectionWidthEdit->setFixedSize(static_cast<int>(50 * sc), editH);
    m_crossSectionWidthEdit->setStyleSheet(editSS);
    m_p2SymmetryEdit->setFixedSize(static_cast<int>(50 * sc), editH);
    m_p2SymmetryEdit->setStyleSheet(editSS);
    m_applyP2SymmetryBtn->setFixedSize(static_cast<int>(130 * sc), btnH);
    m_applyP2SymmetryBtn->setStyleSheet(btnSS);

    // CTF widgets (sizes only)
    m_ctfVoltageEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_ctfVoltageEdit->setStyleSheet(editSS);
    m_ctfEnergySpreadEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_ctfEnergySpreadEdit->setStyleSheet(editSS);
    m_ctfDefocusSpreadEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_ctfDefocusSpreadEdit->setStyleSheet(editSS);
    m_ctfOpenAngleEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_ctfOpenAngleEdit->setStyleSheet(editSS);
    m_ctfCsEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_ctfCsEdit->setStyleSheet(editSS);
    m_ctfDefocusEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_ctfDefocusEdit->setStyleSheet(editSS);
    m_ctfAstigEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_ctfAstigEdit->setStyleSheet(editSS);
    m_ctfAstigAngleEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_ctfAstigAngleEdit->setStyleSheet(editSS);
    m_ctfAmpContrastEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_ctfAmpContrastEdit->setStyleSheet(editSS);
    m_ctfBeamtiltEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_ctfBeamtiltEdit->setStyleSheet(editSS);
    m_ctfBeamtiltDirEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_ctfBeamtiltDirEdit->setStyleSheet(editSS);
    m_ctfCancelBtn->setFixedSize(static_cast<int>(80 * sc), btnH);
    m_ctfCancelBtn->setStyleSheet(btnSS);
    m_ctfComputeBtn->setFixedSize(static_cast<int>(80 * sc), btnH);
    m_ctfComputeBtn->setStyleSheet(btnSS);

    // Phase ramp widgets (sizes only)
    m_phaseRampSizeCombo->setFixedSize(static_cast<int>(80 * sc), btnH);
    m_phaseRampSizeCombo->setStyleSheet(QString(
        "QComboBox { background:white; color:black; border:1px solid #888;"
        "  padding: 2px 4px; font-size: %1px; }"
        "QComboBox::drop-down { width: %2px; }"
        "QComboBox QAbstractItemView { background:white; color:black;"
        "  selection-background-color:#ccc; min-width: 70px; padding: 4px;"
        "  font-size: %1px; }").arg(fontSize).arg(static_cast<int>(20 * sc)));
    m_phaseRampDirEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_phaseRampDirEdit->setStyleSheet(editSS);
    m_phaseRampStepEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_phaseRampStepEdit->setStyleSheet(editSS);
    m_phaseRampCancelBtn->setFixedSize(static_cast<int>(80 * sc), btnH);
    m_phaseRampCancelBtn->setStyleSheet(btnSS);
    m_phaseRampComputeBtn->setFixedSize(static_cast<int>(80 * sc), btnH);
    m_phaseRampComputeBtn->setStyleSheet(btnSS);

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
    m_applyFtCropBtn->setFixedSize(static_cast<int>(120 * sc), btnH);
    m_applyFtCropBtn->setStyleSheet(btnSS);
    m_applyFtPadBtn->setFixedSize(static_cast<int>(120 * sc), btnH);
    m_applyFtPadBtn->setStyleSheet(btnSS);

    // Panel 1 eraser/brush widgets (sizes only)
    m_p1EraserDiameterEdit->setFixedSize(static_cast<int>(40 * sc), editH);
    m_p1EraserDiameterEdit->setStyleSheet(editSS);
    m_p1BrushValueEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_p1BrushValueEdit->setStyleSheet(editSS);
    m_p1BrushSolidDiameterEdit->setFixedSize(static_cast<int>(40 * sc), editH);
    m_p1BrushSolidDiameterEdit->setStyleSheet(editSS);
    m_p1BrushDiameterEdit->setFixedSize(static_cast<int>(40 * sc), editH);
    m_p1BrushDiameterEdit->setStyleSheet(editSS);
    m_p1TaperWidthEdit->setFixedSize(static_cast<int>(50 * sc), editH);
    m_p1TaperWidthEdit->setStyleSheet(editSS);
    m_applyP1TaperBtn->setFixedSize(static_cast<int>(130 * sc), btnH);
    m_applyP1TaperBtn->setStyleSheet(btnSS);
    m_p1SymmetryEdit->setFixedSize(static_cast<int>(50 * sc), editH);
    m_p1SymmetryEdit->setStyleSheet(editSS);
    m_applyP1SymmetryBtn->setFixedSize(static_cast<int>(130 * sc), btnH);
    m_applyP1SymmetryBtn->setStyleSheet(btnSS);

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

    // Crop widgets (sizes only)
    for (QLineEdit *e : {m_cropTLxEdit, m_cropTLyEdit, m_cropBRxEdit, m_cropBRyEdit}) {
        e->setFixedSize(static_cast<int>(60 * sc), editH);
        e->setStyleSheet(editSS);
    }
    m_cropCancelBtn->setFixedSize(static_cast<int>(80 * sc), btnH);
    m_cropCancelBtn->setStyleSheet(btnSS);
    m_applyCropBtn->setFixedSize(static_cast<int>(110 * sc), btnH);
    m_applyCropBtn->setStyleSheet(btnSS);

    // Amyloid filament widgets (sizes only)
    QString amyloidComboSS = QString(
        "QComboBox { background:white; color:black; border:1px solid #888;"
        "  padding: 2px 4px; font-size: %1px; }"
        "QComboBox::drop-down { width: %2px; }"
        "QComboBox QAbstractItemView { background:white; color:black;"
        "  selection-background-color:#cce; min-width: 60px; padding: 4px;"
        "  font-size: %1px; }").arg(fontSize).arg(static_cast<int>(20 * sc));
    m_amyloidRiseEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_amyloidRiseEdit->setStyleSheet(editSS);
    m_amyloidTwistEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_amyloidTwistEdit->setStyleSheet(editSS);
    m_amyloidMapCombo->setFixedSize(static_cast<int>(50 * sc), btnH);
    m_amyloidMapCombo->setStyleSheet(amyloidComboSS);
    m_amyloidSizeCombo->setFixedSize(static_cast<int>(70 * sc), btnH);
    m_amyloidSizeCombo->setStyleSheet(amyloidComboSS);
    m_amyloidNoiseBtn->setStyleSheet(cbSS);
    m_amyloidNoiseEdit->setFixedSize(static_cast<int>(40 * sc), editH);
    m_amyloidNoiseEdit->setStyleSheet(editSS);
    m_amyloidPersistEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_amyloidPersistEdit->setStyleSheet(editSS);
    m_amyloidWaveEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_amyloidWaveEdit->setStyleSheet(editSS);
    m_amyloidAmplEdit->setFixedSize(static_cast<int>(60 * sc), editH);
    m_amyloidAmplEdit->setStyleSheet(editSS);
    m_amyloidSignalBtn->setFixedSize(static_cast<int>(110 * sc), btnH);
    if (m_amyloidBlackSignal)
        m_amyloidSignalBtn->setStyleSheet(QString(
            "QPushButton { background-color: #222; color: #eee; border: 2px outset #555;"
            "  padding: 2px; font-weight: bold; font-size: %1px; }").arg(fontSize));
    else
        m_amyloidSignalBtn->setStyleSheet(QString(
            "QPushButton { background-color: #eee; color: #111; border: 2px outset #aaa;"
            "  padding: 2px; font-weight: bold; font-size: %1px; }").arg(fontSize));
    m_amyloidCancelBtn->setFixedSize(static_cast<int>(80 * sc), btnH);
    m_amyloidCancelBtn->setStyleSheet(btnSS);
    m_amyloidComputeBtn->setFixedSize(static_cast<int>(80 * sc), btnH);
    m_amyloidComputeBtn->setStyleSheet(btnSS);
}
