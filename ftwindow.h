#ifndef FTWINDOW_H
#define FTWINDOW_H

#include <QWidget>
#include <QImage>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <vector>
#include <deque>
#include <functional>
#include "fft.h"          // Complex, nextPow2, fft2d, fftShift, floatToImage

class QTimer;

// ---- zoom state for one display panel ----
struct ZoomState {
    double factor  = 1.0;   // 1.0 = full image visible
    double centerX = 0;     // image-pixel coordinate at viewport centre
    double centerY = 0;

    void reset(int w, int h) {
        factor  = 1.0;
        centerX = w / 2.0;
        centerY = h / 2.0;
    }

    QRectF visibleRect(int w, int h) const {
        double vw = w / factor;
        double vh = h / factor;
        double x0 = centerX - vw / 2.0;
        double y0 = centerY - vh / 2.0;
        // clamp so visible rect stays inside image
        if (vw >= w) { x0 = 0; }
        else {
            if (x0 < 0) x0 = 0;
            if (x0 + vw > w) x0 = w - vw;
        }
        if (vh >= h) { y0 = 0; }
        else {
            if (y0 < 0) y0 = 0;
            if (y0 + vh > h) y0 = h - vh;
        }
        return QRectF(x0, y0, vw, vh);
    }
};

// ---- bookkeeping for one displayed image rectangle ----
struct DisplayItem {
    QRect  screenRect;      // where on screen
    int    imgW  = 0;       // source image width in pixels
    int    imgH  = 0;       // source image height in pixels
    int    zoomIdx = -1;    // index into m_zoom[]
    const std::vector<double> *rawVals = nullptr;  // for value lookup
    bool   valid = false;
};

// ---- main application window ----
class FtWindow : public QWidget {
    Q_OBJECT
public:
    explicit FtWindow(QWidget *parent = nullptr);

    // Override the directory used as the initial location in the
    // "Load image" file dialog. Intended for embedding applications
    // (e.g. the 4d STEM app) that ship EXAMPLE_IMAGES at a different
    // install path than the standalone ft build. When the override is
    // empty (default), the historical <applicationDirPath>/../EXAMPLE_IMAGES
    // fallback is used.
    static void setExampleImagesDir(const QString &dir);
    static QString exampleImagesDir();

    // Public entry point for embedding applications (e.g. the 4d STEM app) to
    // push an image into the analyzer. The image is loaded into buffer "a"
    // (slot 0) when that slot is free; otherwise the first free slot is used,
    // falling back to "a" when every slot is occupied. The currently-active
    // buffer is persisted to its slot before switching so live edits survive.
    void loadImageIntoBuffer(const QString &path);

signals:
    // Emitted by onToggleFullscreen when FtWindow is embedded (i.e. not a
    // top-level window). Standalone builds handle fullscreen themselves via
    // showFullScreen()/showNormal(); embedding apps should connect to this
    // signal and toggle the fullscreen state of their own top-level window.
    // After toggling, the host should call updateFullscreenButton(bool) so
    // the button text stays in sync.
    void fullscreenToggleRequested();

protected:
    void resizeEvent(QResizeEvent *)            override;
    void mousePressEvent(QMouseEvent *event)      override;
    void mouseReleaseEvent(QMouseEvent *event)    override;
    void mouseMoveEvent(QMouseEvent *event)       override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event)         override;
    bool event(QEvent *event)                  override;
    bool applyPinchZoom(const QPoint &pos, double step);
    void paintEvent(QPaintEvent *)              override;

private slots:
    void onLoadImage();
    void onSaveImage();
    void onCreateImage();
    void onCreateImageSized(int sz);
    void onReloadImage();
    void onCycleMode();
    void onToggleFullscreen();
public:
    void updateFullscreenButton(bool isFullscreen);
private slots:
    void onToggleMask(bool checked);
    void onApplyBandpass();
    void onApplyEdgeTaper();
    void onApplySymmetry();
    void onApplyFtSymmetry();
    void onInvertContrast();
    void onApplyLineFilter();
    void onApplyGaborFilter();
    void onGaborCancel();
    void onApplyHessianFilter();
    void onHessianCancel();
    void onUndo();
    void onRedo();

private:
    // loading / computation
    void loadImageFile(const QString &path);
    void loadImageData(const QString &fileName, const QByteArray &fileData);
    // Shrink an over-large source image (and its raw pixels) so a single image
    // cannot consume an outsized share of the WASM heap. No-op on desktop and
    // for images already within the size cap.
    void downsampleForMemoryLimit();
    // Show the out-of-memory dialog. `context` is a verb phrase such as
    // "apply the bandpass filter".
    void reportOutOfMemory(const QString &context);
    // Approximate heap footprint of the current live state (history buffers +
    // active image + raw pixels + FFT), used to size preflight probes.
    qint64 currentStateBytes() const;
    // Rough peak working-set a single calculation needs on the current image.
    qint64 estimatedWorkingBytes() const;
    // Preflight before a calculation: ensure there is room for both an undo
    // snapshot and the operation's working buffers. Trims, then if needed
    // sacrifices, the undo history to make room; returns false (after showing
    // the dialog) only when even the bare operation will not fit. Always true
    // on desktop.
    bool ensureCalcHeadroom(const QString &context);
    // Drop the partially-loaded current image and clear the active slot after
    // a load is refused for lack of memory.
    void discardCurrentImageState();

    // Implementations of the calculation handlers. The public slots above are
    // thin wrappers that run a memory preflight (ensureCalcHeadroom) and only
    // then call these, so an out-of-memory situation refuses the operation
    // cleanly instead of aborting the WASM module.
    void onNewImageCreateImpl();
    void onApplyBandpassImpl();
    void onApplyLatticeImpl();
    void onApplyBinningImpl();
    void onInvertContrastImpl();
    void onApplyEdgeTaperImpl();
    void onApplySymmetryImpl();
    void onApplyFtSymmetryImpl();
    void onApplyGaborFilterImpl();
    void onApplyHessianFilterImpl();
    void onApplyFtCropImpl();
    void onApplyFtPadImpl();
    void onApplyDirectionalImpl();
    void onApplyLineFilterImpl();
    void onFtMathComputeImpl();
    void onMathComputeImpl();
    void onExtractComputeImpl();
    void onCtfComputeImpl();
    void onPhaseRampComputeImpl();
    void onAmyloidComputeImpl();
#ifdef __EMSCRIPTEN__
    void fetchAndLoadImage(const QString &relativePath);
#endif
    void padImageToSquare();
    void extractImageData();
    void computeFFT(bool keepZoom = false);
    void computeInverseFFT();
    // Interactive (FT / FT⁻¹ arrow) variants. On desktop these just call the
    // synchronous versions above; in the WASM build they run the transform in
    // event-loop-yielding chunks so the blue progress fill actually animates
    // across the arrow (a blocking loop never repaints the browser canvas).
    void computeFFTAnimated(bool keepZoom = false);
    void computeInverseFFTAnimated();
    void recomputeDisplayImages();
    void chainSteps(std::vector<std::function<void()>> steps);
    void rebuildImageWithLUT();      // rebuild m_image using display min/max
    void rebuildFTImageWithLUT(int which); // rebuild FT display image for given mode
    void buildComplexImage();        // (re)build m_complexImg from m_complexDispMin/Max
    void resetComplexDisplayRange(); // reset m_complexDispMin/Max to the auto range

    // painting helpers
    QRect  upperArrowBounds() const;
    QRect  lowerArrowBounds() const;
    QString modeLabel() const;

    void drawImageWithFrame(QPainter &p, const QRect &frame, const QImage &img,
                            const ZoomState &zoom, int imgW, int imgH);
    void drawAxes(QPainter &p, const QRect &frame, const ZoomState &zoom,
                  int imgW, int imgH, bool reciprocal, double pixelSize,
                  bool yAxisRight = false);
    void drawShadowRect(QPainter &p, const QRect &rect);
    void drawMinMax(QPainter &p, const QRect &frame, double minVal, double maxVal,
                    double curVal, bool hasCur,
                    const QString &mouseText = QString());
    void drawHistogram(QPainter &p, const QRect &frame,
                       const std::vector<double> &vals,
                       double minVal, double maxVal,
                       int availableBelow = 200,
                       int histIndex = -1,
                       double dispMin = 0, double dispMax = 0);

    // Draw a parameter-label text and register its bounding rect so that the
    // tooltip shows up when the mouse hovers over the label (not just the
    // entry widget). `tip` is the tooltip string (typically fetched from the
    // corresponding entry widget's toolTip()).
    void drawParamLabel(QPainter &p, const QFontMetrics &fm,
                        int x, int y, const QString &text, const QString &tip,
                        int fieldH = 22);

    // Painted parameter-label hover rectangles (populated in paintEvent,
    // consumed in mouseMoveEvent). Cleared at the start of each paintEvent.
    std::vector<std::pair<QRect, QString>> m_paramLabelTips;

    // ---- widgets ----
    QPushButton *m_loadBtn   = nullptr;
    QPushButton *m_saveBtn   = nullptr;
    QPushButton *m_createBtn = nullptr;
    QPushButton *m_reloadBtn = nullptr;
    QPushButton *m_undoBtn   = nullptr;
    QPushButton *m_redoBtn   = nullptr;
    QPushButton *m_fullscreenBtn = nullptr;
    QPushButton *m_modeBtn   = nullptr;
    // m_maskBtn (panel-2 "mask center for display") is custom-painted in
    // paintEvent so that the panel-2 parameter window can cover it.
    QRect       m_maskBtnRect;
    bool        m_maskBtnVisible = false;

    // ---- loaded image ----
    bool                m_loadingImage = false;
    QImage              m_image;
    QString             m_imagePath;
    std::vector<double> m_imageRawPixels;
    double              m_imageMinVal = 0, m_imageMaxVal = 0;
    double              m_pixelSize = 1.0;  // in Angstrom

    // ---- FFT state ----
    bool  m_ftComputed  = false;
    int   m_displayMode = 0;          // 0=cos/sin, 1=amp/phase, 2=complex, 3=power
    int   m_fftN        = 0;
    int   m_origW       = 0;          // image width at FFT time (for rotation center)
    int   m_origH       = 0;          // image height at FFT time
    std::vector<Complex> m_fftData;   // shifted FFT (kept for mask toggle)
    bool  m_maskCenter  = false;
    double m_fftProgress = -1;    // -1 = not computing, 0..1 = progress
    double m_iftProgress = -1;   // inverse FFT progress

    QImage m_cosImg, m_sinImg, m_ampImg, m_phaseImg, m_powerImg, m_complexImg;
    std::vector<double> m_cosVals, m_sinVals, m_ampVals, m_phaseVals, m_powerVals;
    double m_cosMin = 0, m_cosMax = 0;
    double m_sinMin = 0, m_sinMax = 0;
    double m_ampMin = 0, m_ampMax = 0;
    double m_phaseMin = 0, m_phaseMax = 0;
    double m_powerMin = 0, m_powerMax = 0;

    // ---- display LUT parameters (can differ from global min/max) ----
    double m_imageDispMin = 0, m_imageDispMax = 0;
    double m_cosDispMin = 0, m_cosDispMax = 0;
    double m_sinDispMin = 0, m_sinDispMax = 0;
    double m_ampDispMin = 0, m_ampDispMax = 0;
    double m_phaseDispMin = 0, m_phaseDispMax = 0;
    double m_powerDispMin = 0, m_powerDispMax = 0;
    // Brightness range (in power-value units) for the coloured complex-FT
    // display. Adjusted via the histogram below the FT in complex mode.
    double m_complexDispMin = 0, m_complexDispMax = 0;
    // True once the user has dragged a sub-range in complex mode. The default
    // range excludes the DC peak, so it differs from the global power range;
    // this flag lets the histogram skip the grey overlay until a real
    // selection is made.
    bool   m_complexRangeCustom = false;

    // ---- histogram interaction ----
    static constexpr int HIST_P1 = 0;
    static constexpr int HIST_POWER = 1;
    static constexpr int HIST_FT_LEFT = 2;
    static constexpr int HIST_FT_RIGHT = 3;
    static constexpr int NUM_HISTS = 4;
    QRect m_histRects[NUM_HISTS];        // screen rects of histograms
    bool  m_histDragging = false;
    int   m_histDragTarget = -1;         // which histogram
    int   m_histDragStartX = 0;         // screen X at mouse press
    bool  m_imageContrastLocked = false; // lock real-space contrast range
    bool  m_ftContrastLocked = false;    // lock Fourier-space contrast range
    // Panel-1 / panel-2 toggle buttons next to the histograms are all
    // custom-painted so the panel-1/panel-2 tool dialogs can sit on top of
    // them. Click hit-testing uses these rects, gated by the tool-dialog rect.
    QRect       m_imageHistLockRect;
    QRect       m_ftHistLockRect;
    QRect       m_p1ToolRect;       // current panel-1 tool-dialog rect
    QRect       m_p2ToolRect;       // current panel-2 tool-dialog rect

    // ---- mark image center toggle (custom-painted) ----
    QRect       m_markImageCenterRect;
    bool         m_imageCenterMarked = false;

    // ---- image history (panel 3) ----
    static constexpr int HISTORY_SLOTS = 16;
    int m_activeSlot = -1;     // which slot (0..9) is shown in panel 1, -1 = none
    struct HistoryEntry {
        QImage  image;
        QImage  powerSpecImg;       // power spectrum with masked center
        QString path;
        std::vector<double> rawPixels;
        double  minVal = 0, maxVal = 0;
        double  pixelSize = 1.0;
        bool    occupied = false;
        // Cached forward FFT of this slot's image, so re-activating a slot
        // restores it instead of recomputing (the expensive part of a buffer
        // switch). Saved on leaving a slot and restored on entering one; empty
        // fftData / ftComputed == false means "not cached, must recompute".
        std::vector<Complex> fftData;
        int  fftN = 0;
        int  fftOrigW = 0, fftOrigH = 0;
        bool ftComputed = false;
    };
    HistoryEntry m_history[HISTORY_SLOTS];
    QRect        m_historyRects[HISTORY_SLOTS];    // panel 3 screen rects
    QRect        m_powerSpecRects[HISTORY_SLOTS];  // panel 4 screen rects

    struct BufferSnapshot {
        bool valid = false;
        int activeSlot = -1;
        HistoryEntry history[HISTORY_SLOTS];
        QImage image;
        QString imagePath;
        std::vector<double> imageRawPixels;
        double imageMinVal = 0, imageMaxVal = 0;
        double imageDispMin = 0, imageDispMax = 0;
        double pixelSize = 1.0;
        bool ftComputed = false;
        int fftN = 0;
        int origW = 0;
        int origH = 0;
        std::vector<Complex> fftData;
    };

    static constexpr int MAX_UNDO = 10;
    std::deque<BufferSnapshot> m_undoStack;
    std::deque<BufferSnapshot> m_redoStack;

    static QImage computePowerSpecMasked(const QImage &img);
    // Build the masked power-spectrum thumbnail directly from the current
    // m_fftData (already centred), avoiding a fresh forward FFT. Equivalent to
    // computePowerSpecMasked(m_image) whenever m_ftComputed is true.
    QImage powerSpecFromCurrentFFT() const;
    void saveHistory();
    void restoreHistory();
    BufferSnapshot captureCurrentState() const;
    void applySnapshot(const BufferSnapshot &snapshot, bool keepZoom = false);
    void storeUndoSnapshot();
    void clearRedoStack();
    void updateUndoRedoButtons();
    // Approximate heap footprint of one snapshot (raw pixels + images + FFT).
    static qint64 snapshotBytes(const BufferSnapshot &s);
    // Evict redo, then oldest undo, snapshots until the combined undo+redo
    // history fits the memory budget. Always keeps the newest undo step.
    void trimUndoMemory();

    // ---- zoom (0 = image, 1 = FT left/top, 2 = FT right/bottom) ----
    static constexpr int NUM_ZOOM = 3;
    ZoomState   m_zoom[NUM_ZOOM];

    // rebuilt every paintEvent so wheelEvent knows what is where
    static constexpr int MAX_DISP = 4;
    DisplayItem m_dispItems[MAX_DISP];
    int         m_numDispItems = 0;

    QPoint      m_mousePos;         // current mouse position
    QRect       m_titleRect;        // title bar click region ("Fourier Analyzer")
    QRect       m_manualRect;       // "Manual" click region below title

    // ---- tool buttons ----
    static constexpr int P1_TOOL_BUTTONS = 18;
    static constexpr int P2_TOOL_BUTTONS = 13;
    QRect       m_p1BtnRects[P1_TOOL_BUTTONS];       // panel 1 left edge
    QRect       m_toolBtnRects[P2_TOOL_BUTTONS];     // panel 2 right edge

    // Panel 1 tools
    bool        m_shiftActive = false;
    bool        m_rotateActive = false;
    bool        m_p1Dragging = false;
    QPoint      m_p1DragStart;         // screen pos at drag start
    bool        m_p1PanDragging = false;
    QPoint      m_p1PanStart;          // screen pos at pan drag start
    bool        m_p1EraserActive = false;
    bool        m_p1BrushActive = false;
    bool        m_p1TaperActive = false;
    bool        m_p1SymmetrizeActive = false;
    bool        m_p1ToolDragging = false;  // mouse button held while painting/erasing in panel 1

    // Panel 1 eraser/brush parameter widgets
    QLabel     *m_p1EraserDiamLabel = nullptr;
    QLineEdit  *m_p1EraserDiameterEdit = nullptr;
    QLabel     *m_p1BrushValueLabel = nullptr;
    QLineEdit  *m_p1BrushValueEdit = nullptr;
    QLabel     *m_p1BrushSolidLabel = nullptr;
    QLineEdit  *m_p1BrushSolidDiameterEdit = nullptr;
    QLabel     *m_p1BrushDiamLabel = nullptr;
    QLineEdit  *m_p1BrushDiameterEdit = nullptr;
    QLabel     *m_p1TaperWidthLabel = nullptr;
    QLineEdit  *m_p1TaperWidthEdit = nullptr;
    QPushButton *m_applyP1TaperBtn = nullptr;
    QLabel     *m_p1SymmetryLabel = nullptr;
    QLineEdit  *m_p1SymmetryEdit = nullptr;
    QPushButton *m_applyP1SymmetryBtn = nullptr;

    void p1EraserApply(QPoint pos);
    void p1BrushApply(QPoint pos);
    void rebuildImageFromRaw();

    // Particle picking
    bool        m_peakPickActive = false;
    QComboBox  *m_peakSourceCombo     = nullptr;
    QSlider    *m_peakThresholdSlider = nullptr;
    QLabel     *m_peakThresholdLabel  = nullptr;
    QLabel     *m_peakExclLabel       = nullptr;
    QSlider    *m_peakExclRadiusSlider = nullptr;
    QPushButton *m_peakCancelBtn      = nullptr;
    QPushButton *m_peakComputeBtn     = nullptr;
    QPushButton *m_peakShowPosBtn     = nullptr;
    bool        m_peakShowPositions   = true;
    struct PeakCoord { int x; int y; };
    std::vector<PeakCoord> m_peaks;
    void runPeakSearch();
    void onPeakCancel();
    void onPeakCompute();

    // Gabor filter
    bool        m_gaborActive = false;
    QLineEdit  *m_gaborSigmaEdit   = nullptr;
    QLineEdit  *m_gaborLambdaEdit  = nullptr;
    QLineEdit  *m_gaborThetaEdit   = nullptr;
    QLineEdit  *m_gaborGammaEdit   = nullptr;
    QPushButton *m_gaborCancelBtn  = nullptr;
    QPushButton *m_gaborComputeBtn = nullptr;

    // Hessian filter
    bool        m_hessianActive = false;
    QLineEdit  *m_hessianSigmaEdit    = nullptr;
    QLineEdit  *m_hessianPolarityEdit = nullptr;
    QPushButton *m_hessianCancelBtn   = nullptr;
    QPushButton *m_hessianComputeBtn  = nullptr;

    // Measure tool (panel 1)
    bool        m_measureActive = false;
    int         m_measurePlacing = 0;    // 0=idle, 1=first point placed
    QPointF     m_measureP0;             // image coords
    QPointF     m_measureP1;             // image coords
    bool        m_measureHasLine = false;
    QPushButton *m_measureCancelBtn = nullptr;
    void onMeasureCancel();

    // Amyloid filament drawing
    bool        m_amyloidActive = false;
    int         m_amyloidPlacing = 0;  // 0=idle, 1=placed start waiting for end
    QPointF     m_amyloidStartPt;      // image coords
    struct AmyloidFilament {
        std::vector<QPointF> pts;      // control points in image coords
    };
    std::vector<AmyloidFilament> m_amyloidFilaments;
    int         m_amyloidDragFil = -1; // filament index being dragged
    int         m_amyloidDragPt  = -1; // control point index being dragged
    bool        m_amyloidRendered = false; // true after Compute, suppresses overlay lines
    QLineEdit  *m_amyloidRiseEdit     = nullptr;
    QLineEdit  *m_amyloidTwistEdit    = nullptr;
    QComboBox  *m_amyloidMapCombo      = nullptr;
    QComboBox  *m_amyloidSizeCombo     = nullptr;
    QCheckBox  *m_amyloidNoiseBtn     = nullptr;
    QLineEdit  *m_amyloidNoiseEdit    = nullptr;
    QLineEdit  *m_amyloidPersistEdit  = nullptr;  // persistence length (μm)
    QLineEdit  *m_amyloidWaveEdit     = nullptr;  // waviness wavelength (px)
    QLineEdit  *m_amyloidAmplEdit     = nullptr;  // waviness amplitude (px)
    QPushButton *m_amyloidSignalBtn   = nullptr;
    bool        m_amyloidBlackSignal = false;  // false=white signal, true=black signal
    QPushButton *m_amyloidCancelBtn   = nullptr;
    QPushButton *m_amyloidComputeBtn  = nullptr;
    void onAmyloidCompute();
    void onAmyloidCancel();

    // Extract particles
    bool        m_extractActive = false;
    QComboBox  *m_extractSourceCombo = nullptr;
    QComboBox  *m_extractTargetCombo = nullptr;
    QComboBox  *m_extractSizeCombo   = nullptr;
    QPushButton *m_extractCancelBtn  = nullptr;
    QPushButton *m_extractComputeBtn = nullptr;
    void onExtractCancel();
    void onExtractCompute();

    // Panel 2 tools
    bool        m_eraserActive = false;
    bool        m_brushActive = false;
    bool        m_toolDragging = false;    // mouse button held while painting/erasing

    void eraserApply(QPoint pos);
    void brushApply(QPoint pos);
    double brushValue() const;             // max amplitude outside center 3x3
    void onApplyBinning();

    // "New image" overlay (Create-or-Copy popup, launched from top-left button)
    bool        m_newImageActive = false;
    QComboBox  *m_newImgSrcCombo    = nullptr;   // "New 512..4096" or slots a..p
    QComboBox  *m_newImgTgtCombo    = nullptr;   // target slot a..p
    QPushButton *m_newImgCreateBtn  = nullptr;   // "Execute"
    QPushButton *m_newImgCancelBtn  = nullptr;   // "Cancel"
    void onNewImageOpen();
    void onNewImageCreate();   // "Execute": create new or copy, per source selection
    void onNewImageCancel();

    // Math calculations overlay
    bool        m_mathActive = false;
    QComboBox  *m_mathOutCombo  = nullptr;   // output buffer a..p
    QComboBox  *m_mathIn1Combo  = nullptr;   // first input buffer a..p
    QComboBox  *m_mathOpCombo   = nullptr;   // operation +,-,*,/,conv,corr
    QComboBox  *m_mathIn2Combo  = nullptr;   // second input buffer a..p
    QPushButton *m_mathCancelBtn = nullptr;
    QPushButton *m_mathComputeBtn = nullptr;
    QLabel     *m_mathEqualsLabel = nullptr;
    void onMathCompute();
    void onMathCancel();
    double m_mathProgress = -1;    // -1 = not computing, 0..1 = progress
    double m_invertProgress = -1;   // -1 = not computing, 0..1 = progress

    // Binning UI
    bool        m_binActive = false;
    QComboBox  *m_binCombo  = nullptr;
    QPushButton *m_applyBinBtn = nullptr;
    QCheckBox  *m_binKeepSizeBtn = nullptr;

    // Crop UI
    bool        m_cropActive = false;
    QLineEdit  *m_cropTLxEdit = nullptr;   // top-left X (image pixels)
    QLineEdit  *m_cropTLyEdit = nullptr;   // top-left Y
    QLineEdit  *m_cropBRxEdit = nullptr;   // bottom-right X (exclusive)
    QLineEdit  *m_cropBRyEdit = nullptr;   // bottom-right Y (exclusive)
    QPushButton *m_cropCancelBtn = nullptr;
    QPushButton *m_applyCropBtn  = nullptr;
    bool        m_cropHasSelection = false;
    bool        m_cropDragging = false;
    bool        m_cropMoving = false;      // dragging an existing square to a new position
    QPointF     m_cropAnchor;              // image coords of the drag-start corner
    QPointF     m_cropGrabOffset;          // image-coord offset from square top-left to grab point
    QRect       m_cropRect;                // selection in image coords (always square)
    void onApplyCrop();
    void onCropCancel();
    void syncCropEdits();                  // refresh the 4 edits from m_cropRect

    // Brush/eraser parameter widgets
    QLineEdit  *m_brushValueEdit    = nullptr;
    QLineEdit  *m_brushDiameterEdit = nullptr;
    QLineEdit  *m_eraserDiameterEdit = nullptr;
    QLabel     *m_brushValueLabel   = nullptr;
    QLabel     *m_brushDiamLabel    = nullptr;
    QLabel     *m_eraserDiamLabel   = nullptr;

    // ---- bandpass filter ----
    bool        m_bandpassActive = false;
    double      m_bandInnerR = 0.1;        // fraction of N/2 (0..1)
    double      m_bandOuterR = 0.6;        // fraction of N/2 (0..1)
    int         m_bandDragging = 0;        // 0=none, 1=inner, 2=outer

    QLineEdit  *m_smoothEdit   = nullptr;
    QCheckBox  *m_bandEraseOutside = nullptr;
    QPushButton *m_applyBandBtn = nullptr;
    QPushButton *m_resetBandBtn = nullptr;

    void drawBandpassRing(QPainter &p, const QRect &screenRect,
                          const ZoomState &zoom, int imgW, int imgH);

    // ---- line filter ----
    bool        m_lineFilterActive = false;
    int         m_lineDragging = 0;    // 0=none, 1=offset (right half), 2=rotate (left half)
    double      m_lineOffset = 0.0;    // signed offset from Fourier center in pixels
    QLineEdit  *m_lineWidthEdit = nullptr;
    QLineEdit  *m_lineDirectionEdit = nullptr;
    QLineEdit  *m_lineOffsetEdit = nullptr;
    QCheckBox  *m_lineEraseOutsideBtn = nullptr;
    QPushButton *m_applyLineBtn = nullptr;

    void drawLineFilter(QPainter &p, const QRect &screenRect,
                        const ZoomState &zoom, int imgW, int imgH);

    // ---- Fourier-space rotate ----
    bool        m_ftRotateActive = false;
    bool        m_p2Dragging = false;
    QPoint      m_p2DragStart;
    bool        m_p2PanDragging = false;
    QPoint      m_p2PanStart;

    // ---- cross-section profile ----
    bool        m_crossSectionActive = false;
    bool        m_crossSectionDragging = false;
    double      m_crossSectionAngle = 0.0;    // internal angle (deg, screen convention: CW, y-down)
    QLineEdit  *m_crossSectionWidthEdit = nullptr;  // integration width in reciprocal pixels
    QLineEdit  *m_crossSectionDirEdit = nullptr;    // evaluation-line direction in degrees (math convention)
    std::vector<double> m_crossSectionProfile;       // 1D integrated amplitude profile
    std::vector<double> m_crossSectionPhaseProfile;  // 1D phase profile (rad), same sampling
    std::vector<bool>   m_crossSectionValid;    // true where data exists
    int         m_crossSectionCenter = 0;       // center index in profile
    double      m_crossSectionProjMin = 0;      // min projection distance (pixels from center)
    double      m_crossSectionProjMax = 0;      // max projection distance (pixels from center)

    void drawCrossSectionLines(QPainter &p, const QRect &screenRect,
                               const ZoomState &zoom, int imgW, int imgH);
    void computeCrossSectionProfile();
    void syncCrossSectionDirEdit();   // refresh the direction edit from m_crossSectionAngle

    // ---- Fourier-space symmetrize ----
    bool        m_p2SymmetrizeActive = false;
    QLineEdit  *m_p2SymmetryEdit = nullptr;
    QPushButton *m_applyP2SymmetryBtn = nullptr;

    // ---- lattice filter ----
    bool        m_latticeActive = false;
    double      m_latticeUx = 16, m_latticeUy = 0;
    double      m_latticeVx = 0,  m_latticeVy = -16;
    int         m_latticeDragging = 0;   // 0=none, 1=u, 2=v

    QLabel     *m_latticeSmoothLabel  = nullptr;
    QLineEdit  *m_latticeSmoothEdit   = nullptr;
    QLabel     *m_latticeDotDiamLabel = nullptr;
    QLineEdit  *m_latticeDotDiamEdit  = nullptr;
    QLineEdit  *m_latticeUxEdit       = nullptr;
    QLineEdit  *m_latticeUyEdit       = nullptr;
    QLineEdit  *m_latticeVxEdit       = nullptr;
    QLineEdit  *m_latticeVyEdit       = nullptr;
    QCheckBox  *m_latticeEraseOutside = nullptr;
    QPushButton *m_latticeApplyBtn    = nullptr;
    void syncLatticeVectorEdits();   // write m_lattice{U,V}{x,y} into edits

    void drawLattice(QPainter &p, const QRect &screenRect,
                     const ZoomState &zoom, int imgW, int imgH);
    void onApplyLattice();

    // ---- Fourier math ----
    bool        m_ftMathActive = false;
    QComboBox  *m_ftMathOutCombo  = nullptr;
    QComboBox  *m_ftMathIn1Combo  = nullptr;
    QComboBox  *m_ftMathOpCombo   = nullptr;
    QComboBox  *m_ftMathIn2Combo  = nullptr;
    QComboBox  *m_ftMathConjCombo = nullptr;
    QLabel     *m_ftMathEqualsLabel = nullptr;
    QPushButton *m_ftMathCancelBtn  = nullptr;
    QPushButton *m_ftMathComputeBtn = nullptr;
    void onFtMathCompute();
    void onFtMathCancel();
    double m_ftMathProgress = -1;  // -1 = not computing, 0..1 = progress
    double m_toolProgress = -1;    // progress for tool-option rectangles

    QTimer *m_reloadAnimTimer = nullptr;
    double  m_reloadProgress = -1;  // reload button animation (-1 = idle)

    // ---- Fourier crop ----
    bool        m_ftCropActive = false;
    QComboBox  *m_ftCropCombo     = nullptr;
    QCheckBox  *m_ftCropKeepSizeBtn = nullptr;
    QPushButton *m_applyFtCropBtn = nullptr;
    QPushButton *m_applyFtPadBtn  = nullptr;
    void onApplyFtCrop();
    void onApplyFtPad();

    // ---- CTF ----
    bool        m_ctfActive = false;
    QLineEdit  *m_ctfVoltageEdit      = nullptr;
    QLineEdit  *m_ctfEnergySpreadEdit = nullptr;
    QLineEdit  *m_ctfDefocusSpreadEdit = nullptr;
    QLineEdit  *m_ctfOpenAngleEdit    = nullptr;
    QLineEdit  *m_ctfCsEdit           = nullptr;
    QLineEdit  *m_ctfDefocusEdit      = nullptr;
    QLineEdit  *m_ctfAstigEdit        = nullptr;
    QLineEdit  *m_ctfAstigAngleEdit   = nullptr;
    QLineEdit  *m_ctfAmpContrastEdit  = nullptr;
    QLineEdit  *m_ctfBeamtiltEdit     = nullptr;
    QLineEdit  *m_ctfBeamtiltDirEdit  = nullptr;
    QPushButton *m_ctfCancelBtn       = nullptr;
    QPushButton *m_ctfComputeBtn      = nullptr;
    std::vector<double> m_ctfProfile;      // 1D CTF amplitude profile (|C|, center->corner)
    std::vector<double> m_ctfPhaseProfile; // 1D CTF phase profile (arg C, rad), same sampling
    double      m_ctfAngleDeg = 0.0;       // profile direction (deg, CCW from +x)
    bool        m_ctfDragging = false;
    void onCtfCompute();
    void onCtfCancel();
    void computeCtfProfile1D();
    void drawCtfDirectionLine(QPainter &p, const QRect &screenRect,
                              const ZoomState &zoom, int imgW, int imgH);

    // ---- directional filter ----
    bool        m_directionalActive = false;
    double      m_dirAngle1 = -15.0;      // degrees from horizontal
    double      m_dirAngle2 =  15.0;
    int         m_dirDragging = 0;        // 0=none, 1=edge1, 2=edge2

    void drawDirectionalWedge(QPainter &p, const QRect &screenRect,
                              const ZoomState &zoom, int imgW, int imgH);
    void onApplyDirectional();

    // ---- phase ramp ----
    bool        m_phaseRampActive = false;
    QComboBox  *m_phaseRampSizeCombo = nullptr;
    QLineEdit  *m_phaseRampDirEdit   = nullptr;
    QLineEdit  *m_phaseRampStepEdit  = nullptr;
    QPushButton *m_phaseRampCancelBtn = nullptr;
    QPushButton *m_phaseRampComputeBtn = nullptr;
    void onPhaseRampCompute();
    void onPhaseRampCancel();
};

#endif // FTWINDOW_H
