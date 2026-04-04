#ifndef FTWINDOW_H
#define FTWINDOW_H

#include <QWidget>
#include <QImage>
#include <QPushButton>
#include <vector>
#include "fft.h"          // Complex, nextPow2, fft2d, fftShift, floatToImage

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
    bool   valid = false;
};

// ---- main application window ----
class FtWindow : public QWidget {
    Q_OBJECT
public:
    explicit FtWindow(QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *)            override;
    void mousePressEvent(QMouseEvent *event)    override;
    void wheelEvent(QWheelEvent *event)         override;
    void paintEvent(QPaintEvent *)              override;

private slots:
    void onLoadImage();
    void onCycleMode();
    void onToggleMask(bool checked);

private:
    // loading / computation
    void loadImageFile(const QString &path);
    void extractImageData();
    void computeFFT();
    void recomputeDisplayImages();

    // painting helpers
    QRect  upperArrowBounds() const;
    QString modeLabel() const;

    void drawImageWithFrame(QPainter &p, const QRect &frame, const QImage &img,
                            const ZoomState &zoom, int imgW, int imgH);
    void drawAxes(QPainter &p, const QRect &frame, const ZoomState &zoom,
                  int imgW, int imgH, bool reciprocal, bool yAxisRight = false);
    void drawMinMax(QPainter &p, const QRect &frame, double minVal, double maxVal);
    void drawHistogram(QPainter &p, const QRect &frame,
                       const std::vector<double> &vals,
                       double minVal, double maxVal);

    // ---- widgets ----
    QPushButton *m_loadBtn  = nullptr;
    QPushButton *m_modeBtn  = nullptr;
    QPushButton *m_maskBtn  = nullptr;

    // ---- loaded image ----
    QImage              m_image;
    std::vector<double> m_imageRawPixels;
    double              m_imageMinVal = 0, m_imageMaxVal = 0;

    // ---- FFT state ----
    bool  m_ftComputed  = false;
    int   m_displayMode = 0;          // 0=cos/sin, 1=amp/phase, 2=power
    int   m_fftN        = 0;
    std::vector<Complex> m_fftData;   // shifted FFT (kept for mask toggle)
    bool  m_maskCenter  = false;
    double m_fftProgress = -1;    // -1 = not computing, 0..1 = progress

    QImage m_cosImg, m_sinImg, m_ampImg, m_phaseImg, m_powerImg, m_complexImg;
    std::vector<double> m_cosVals, m_sinVals, m_ampVals, m_phaseVals, m_powerVals;
    double m_cosMin = 0, m_cosMax = 0;
    double m_sinMin = 0, m_sinMax = 0;
    double m_ampMin = 0, m_ampMax = 0;
    double m_phaseMin = 0, m_phaseMax = 0;
    double m_powerMin = 0, m_powerMax = 0;

    // ---- zoom (0 = image, 1 = FT left/top, 2 = FT right/bottom) ----
    static constexpr int NUM_ZOOM = 3;
    ZoomState   m_zoom[NUM_ZOOM];

    // rebuilt every paintEvent so wheelEvent knows what is where
    static constexpr int MAX_DISP = 4;
    DisplayItem m_dispItems[MAX_DISP];
    int         m_numDispItems = 0;
};

#endif // FTWINDOW_H
