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

    // Reload button
    m_reloadBtn = new QPushButton("Reload image", this);
    m_reloadBtn->setFixedSize(100, 30);
    connect(m_reloadBtn, &QPushButton::clicked, this, &FtWindow::onReloadImage);

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
    restoreHistory();

    QSettings settings("ft", "ft");
    m_activeSlot = settings.value("activeSlot", -1).toInt();
    if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS
        && m_history[m_activeSlot].occupied) {
        m_image          = m_history[m_activeSlot].image;
        m_imagePath      = m_history[m_activeSlot].path;
        m_imageRawPixels = m_history[m_activeSlot].rawPixels;
        m_imageMinVal    = m_history[m_activeSlot].minVal;
        m_imageMaxVal    = m_history[m_activeSlot].maxVal;
        m_pixelSize      = m_history[m_activeSlot].pixelSize;
        if (!m_image.isNull()) {
            m_zoom[0].reset(m_image.width(), m_image.height());
            computeFFT();
        }
    } else {
        m_activeSlot = -1;
    }
}

// ---------------------------------------------------------------------------
//  Layout
// ---------------------------------------------------------------------------
void FtWindow::resizeEvent(QResizeEvent *)
{
    m_loadBtn->move(8, 8);
    int hy0 = height() - height() / 5;
    m_reloadBtn->move(8, 8 + m_loadBtn->height() + 4);
    m_modeBtn->move(width() - m_modeBtn->width() - 8, 8);
    m_maskBtn->move(width() - m_maskBtn->width() - 8, 8 + m_modeBtn->height() + 4);

    // Bandpass widgets: bottom-right of panel 2
    int hy = height() - height() / 5;
    int bpX = width() - 250;
    int bpY = hy - 90;
    m_smoothEdit->move(bpX + 160, bpY);
    m_bandEraseOutside->move(bpX, bpY + 28);
    m_applyBandBtn->move(bpX, bpY + 54);

    // Brush/eraser widgets: bottom-right of panel 2
    m_brushValueLabel->move(bpX, bpY);
    m_brushValueEdit->move(bpX + 140, bpY - 2);
    m_brushDiamLabel->move(bpX, bpY + 26);
    m_brushDiameterEdit->move(bpX + 210, bpY + 24);
    m_eraserDiamLabel->move(bpX, bpY);
    m_eraserDiameterEdit->move(bpX + 180, bpY - 2);

    // Lattice filter widgets: bottom-right of panel 2
    m_latticeSmoothLabel->move(bpX, bpY);
    m_latticeSmoothEdit->move(bpX + 160, bpY - 2);
    m_latticeDotDiamLabel->move(bpX, bpY + 26);
    m_latticeDotDiamEdit->move(bpX + 120, bpY + 24);
    m_latticeEraseOutside->move(bpX, bpY + 52);
    m_latticeApplyBtn->move(bpX, bpY + 78);

    // Panel 1 eraser/brush widgets: bottom-left of panel 1
    int p1ToolX = 10;
    int p1ToolY = hy - 90;
    m_p1EraserDiamLabel->move(p1ToolX, p1ToolY);
    m_p1EraserDiameterEdit->move(p1ToolX + 180, p1ToolY - 2);
    m_p1BrushValueLabel->move(p1ToolX, p1ToolY);
    m_p1BrushValueEdit->move(p1ToolX + 140, p1ToolY - 2);
    m_p1BrushDiamLabel->move(p1ToolX, p1ToolY + 26);
    m_p1BrushDiameterEdit->move(p1ToolX + 210, p1ToolY + 24);

    // Binning widgets: bottom-left of panel 1
    int binX = 10;
    int binY = hy - 90;
    m_binCombo->move(binX, binY);
    m_binKeepSizeBtn->move(binX, binY + 30);
    m_applyBinBtn->move(binX, binY + 60);
}
