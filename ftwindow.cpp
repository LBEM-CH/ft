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
}
