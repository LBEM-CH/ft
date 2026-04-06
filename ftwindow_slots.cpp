#include "ftwindow_common.h"

// ---------------------------------------------------------------------------
//  Slots
// ---------------------------------------------------------------------------
void FtWindow::onLoadImage()
{
    QString startDir = QCoreApplication::applicationDirPath() + "/../EXAMPLE_IMAGES";
    if (!QDir(startDir).exists())
        startDir = QCoreApplication::applicationDirPath();

    QString path = QFileDialog::getOpenFileName(
        this, "Load image", startDir,
        "Images (*.tif *.tiff *.jpg *.jpeg *.png *.mrc *.MRC)");

    if (path.isEmpty()) return;
    loadImageFile(path);
}

void FtWindow::onReloadImage()
{
    if (m_imagePath.isEmpty() || !QFile::exists(m_imagePath)) return;

    qDebug() << "Reloading image:" << m_imagePath;

    if (m_imagePath.endsWith(".mrc", Qt::CaseInsensitive)) {
        MrcResult r = loadMrc(m_imagePath);
        m_image = r.image;
        m_imageRawPixels = std::move(r.rawPixels);
        m_imageMinVal = r.minVal;
        m_imageMaxVal = r.maxVal;
        m_pixelSize = r.pixelSize;
        if (!m_image.isNull())
            padImageToSquare();
    } else {
        m_image = QImage(m_imagePath);
        m_pixelSize = 1.0;
        if (!m_image.isNull()) {
            padImageToSquare();
            extractImageData();
        }
    }

    m_ftComputed = false;
    m_displayMode = 3;
    m_modeBtn->setText(modeLabel());
    m_modeBtn->hide();
    m_maskBtn->hide();
    m_maskBtn->setChecked(false);
    m_maskCenter = false;

    if (!m_image.isNull()) {
        m_zoom[0].reset(m_image.width(), m_image.height());
        computeFFT();
    }
    update();
}

void FtWindow::onCycleMode()
{
    m_displayMode = (m_displayMode + 1) % 4;
    m_modeBtn->setText(modeLabel());
    update();
}

void FtWindow::onToggleMask(bool checked)
{
    m_maskCenter = checked;
    recomputeDisplayImages();
    update();
}

// ---------------------------------------------------------------------------
//  Loading
// ---------------------------------------------------------------------------
void FtWindow::loadImageFile(const QString &path)
{
    qDebug() << "Loading image:" << path;

    // If no slot is active, pick the first empty one (or last slot as fallback)
    if (m_activeSlot < 0) {
        m_activeSlot = HISTORY_SLOTS - 1;
        for (int i = 0; i < HISTORY_SLOTS; i++) {
            if (!m_history[i].occupied) { m_activeSlot = i; break; }
        }
    }

    if (path.endsWith(".mrc", Qt::CaseInsensitive)) {
        MrcResult r = loadMrc(path);
        m_image = r.image;
        m_imageRawPixels = std::move(r.rawPixels);
        m_imageMinVal = r.minVal;
        m_imageMaxVal = r.maxVal;
        m_pixelSize = r.pixelSize;

        if (m_image.isNull())
            qDebug() << "MRC load FAILED – image is null";
        else {
            qDebug() << "MRC load OK –" << m_image.width() << "x" << m_image.height();
            padImageToSquare();
        }
    } else {
        m_image = QImage(path);
        m_pixelSize = 1.0;
        if (m_image.isNull()) {
            qDebug() << "Image load FAILED for:" << path;
        } else {
            qDebug() << "Image loaded:" << m_image.width() << "x" << m_image.height()
                     << "format:" << m_image.format();
            padImageToSquare();
            extractImageData();
        }
    }

    m_imagePath = path;

    // Store in the active slot
    if (!m_image.isNull()) {
        m_history[m_activeSlot].image        = m_image;
        m_history[m_activeSlot].path         = path;
        m_history[m_activeSlot].rawPixels    = m_imageRawPixels;
        m_history[m_activeSlot].minVal       = m_imageMinVal;
        m_history[m_activeSlot].maxVal       = m_imageMaxVal;
        m_history[m_activeSlot].pixelSize    = m_pixelSize;
        m_history[m_activeSlot].occupied     = true;
    }

    QSettings settings("ft", "ft");
    settings.setValue("lastFile", path);
    settings.setValue("activeSlot", m_activeSlot);

    m_ftComputed = false;
    m_displayMode = 3;
    m_modeBtn->setText(modeLabel());
    m_modeBtn->hide();
    m_maskBtn->hide();
    m_maskBtn->setChecked(false);
    m_maskCenter = false;

    if (!m_image.isNull()) {
        m_zoom[0].reset(m_image.width(), m_image.height());
        computeFFT();
        // Store power spec thumbnail now that FFT is done
        m_history[m_activeSlot].powerSpecImg = computePowerSpecMasked(m_image);
    }

    saveHistory();
    update();
}

// Find smallest n >= val whose only prime factors are 2, 3, or 5
static int nextSmooth235(int val)
{
    if (val <= 1) return 1;
    for (int n = val; ; n++) {
        int t = n;
        while (t % 2 == 0) t /= 2;
        while (t % 3 == 0) t /= 3;
        while (t % 5 == 0) t /= 5;
        if (t == 1) return n;
    }
}

void FtWindow::padImageToSquare()
{
    if (m_image.isNull()) return;
    int w = m_image.width(), h = m_image.height();
    int side = nextSmooth235(std::max(w, h));
    if (w == side && h == side) return;

    int ox = (side - w) / 2;
    int oy = (side - h) / 2;

    // Pad raw pixel data if present (preserves MRC float precision)
    if ((int)m_imageRawPixels.size() == w * h) {
        double sum = 0;
        for (double v : m_imageRawPixels) sum += v;
        double avg = sum / m_imageRawPixels.size();

        std::vector<double> padded(side * side, avg);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                padded[(y + oy) * side + (x + ox)] = m_imageRawPixels[y * w + x];
        m_imageRawPixels = std::move(padded);
        m_imageMinVal = *std::min_element(m_imageRawPixels.begin(), m_imageRawPixels.end());
        m_imageMaxVal = *std::max_element(m_imageRawPixels.begin(), m_imageRawPixels.end());

        // Rebuild the display image from padded raw pixels
        double range = m_imageMaxVal - m_imageMinVal;
        double scale = (range > 0) ? 255.0 / range : 1.0;
        m_image = QImage(side, side, QImage::Format_Grayscale8);
        for (int y = 0; y < side; y++) {
            uchar *row = m_image.scanLine(y);
            for (int x = 0; x < side; x++)
                row[x] = static_cast<uchar>(std::clamp(
                    (m_imageRawPixels[y * side + x] - m_imageMinVal) * scale, 0.0, 255.0));
        }
    } else {
        // No raw pixels — pad the QImage with average grey
        QImage gray = m_image.convertToFormat(QImage::Format_Grayscale8);
        double sum = 0;
        for (int y = 0; y < h; y++) {
            const uchar *row = gray.constScanLine(y);
            for (int x = 0; x < w; x++) sum += row[x];
        }
        int avg = (int)(sum / ((double)w * h));

        QImage paddedImg(side, side, QImage::Format_Grayscale8);
        paddedImg.fill(QColor(avg, avg, avg));
        QPainter pp(&paddedImg);
        pp.drawImage(ox, oy, gray);
        pp.end();
        m_image = paddedImg;
    }
}

void FtWindow::extractImageData()
{
    QImage gray = m_image.convertToFormat(QImage::Format_Grayscale8);
    int w = gray.width(), h = gray.height();
    m_imageRawPixels.resize((size_t)w * h);
    for (int y = 0; y < h; y++) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < w; x++)
            m_imageRawPixels[y * w + x] = row[x];
    }
    m_imageMinVal = *std::min_element(m_imageRawPixels.begin(), m_imageRawPixels.end());
    m_imageMaxVal = *std::max_element(m_imageRawPixels.begin(), m_imageRawPixels.end());
}

// ---------------------------------------------------------------------------
//  FFT
// ---------------------------------------------------------------------------
void FtWindow::computeFFT()
{
    if (m_image.isNull()) return;

    QImage gray = m_image.convertToFormat(QImage::Format_Grayscale8);
    int w = gray.width();
    int h = gray.height();
    int N = nextPow2(std::max(w, h));
    m_fftN = N;
    m_origW = w;
    m_origH = h;

    // Compute average grey for padding beyond image bounds
    double sum = 0;
    for (int y = 0; y < h; y++) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < w; x++) sum += row[x];
    }
    double avg = sum / ((double)w * h);

    std::vector<Complex> data(N * N, Complex(avg, 0.0));
    for (int y = 0; y < h; y++) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < w; x++)
            data[y * N + x] = Complex(row[x], 0.0);
    }

    m_fftProgress = 0.0;
    update();
    QApplication::processEvents();

    {
        int nThreads = (int)std::thread::hardware_concurrency();
        if (nThreads < 1) nThreads = 1;
        int batchSize = nThreads * 16;

        for (int b = 0; b < N; b += batchSize) {
            int bEnd = std::min(b + batchSize, N);
            std::vector<std::thread> threads;
            int perThread = ((bEnd - b) + nThreads - 1) / nThreads;
            for (int t = 0; t < nThreads; t++) {
                int y0 = b + t * perThread;
                int y1 = std::min(y0 + perThread, bEnd);
                if (y0 < y1)
                    threads.emplace_back([&data, N, y0, y1]() {
                        std::vector<Complex> row(N);
                        for (int y = y0; y < y1; y++) {
                            for (int x = 0; x < N; x++) row[x] = data[y * N + x];
                            fft1d(row, false);
                            for (int x = 0; x < N; x++) data[y * N + x] = row[x];
                        }
                    });
            }
            for (auto &t : threads) t.join();
            m_fftProgress = 0.5 * bEnd / N;
            update();
            QApplication::processEvents();
        }

        for (int b = 0; b < N; b += batchSize) {
            int bEnd = std::min(b + batchSize, N);
            std::vector<std::thread> threads;
            int perThread = ((bEnd - b) + nThreads - 1) / nThreads;
            for (int t = 0; t < nThreads; t++) {
                int x0 = b + t * perThread;
                int x1 = std::min(x0 + perThread, bEnd);
                if (x0 < x1)
                    threads.emplace_back([&data, N, x0, x1]() {
                        std::vector<Complex> col(N);
                        for (int x = x0; x < x1; x++) {
                            for (int y = 0; y < N; y++) col[y] = data[y * N + x];
                            fft1d(col, false);
                            for (int y = 0; y < N; y++) data[y * N + x] = col[y];
                        }
                    });
            }
            for (auto &t : threads) t.join();
            m_fftProgress = 0.5 + 0.5 * bEnd / N;
            update();
            QApplication::processEvents();
        }
    }

    fftShift(data, N);
    m_fftData = data;
    recomputeDisplayImages();

    m_fftProgress = -1;
    m_ftComputed = true;
    m_modeBtn->show();
    m_maskBtn->show();

    m_zoom[1].reset(N, N);
    m_zoom[2].reset(N, N);
}

void FtWindow::computeInverseFFT()
{
    if (!m_ftComputed || m_fftN == 0) return;

    int N = m_fftN;
    std::vector<Complex> data = m_fftData;
    fftShift(data, N);

    m_iftProgress = 0.0;
    update();
    QApplication::processEvents();

    {
        int nThreads = (int)std::thread::hardware_concurrency();
        if (nThreads < 1) nThreads = 1;
        int batchSize = nThreads * 16;

        for (int b = 0; b < N; b += batchSize) {
            int bEnd = std::min(b + batchSize, N);
            std::vector<std::thread> threads;
            int perThread = ((bEnd - b) + nThreads - 1) / nThreads;
            for (int t = 0; t < nThreads; t++) {
                int y0 = b + t * perThread;
                int y1 = std::min(y0 + perThread, bEnd);
                if (y0 < y1)
                    threads.emplace_back([&data, N, y0, y1]() {
                        std::vector<Complex> row(N);
                        for (int y = y0; y < y1; y++) {
                            for (int x = 0; x < N; x++) row[x] = data[y * N + x];
                            fft1d(row, true);
                            for (int x = 0; x < N; x++) data[y * N + x] = row[x];
                        }
                    });
            }
            for (auto &t : threads) t.join();
            m_iftProgress = 0.5 * bEnd / N;
            update();
            QApplication::processEvents();
        }

        for (int b = 0; b < N; b += batchSize) {
            int bEnd = std::min(b + batchSize, N);
            std::vector<std::thread> threads;
            int perThread = ((bEnd - b) + nThreads - 1) / nThreads;
            for (int t = 0; t < nThreads; t++) {
                int x0 = b + t * perThread;
                int x1 = std::min(x0 + perThread, bEnd);
                if (x0 < x1)
                    threads.emplace_back([&data, N, x0, x1]() {
                        std::vector<Complex> col(N);
                        for (int x = x0; x < x1; x++) {
                            for (int y = 0; y < N; y++) col[y] = data[y * N + x];
                            fft1d(col, true);
                            for (int y = 0; y < N; y++) data[y * N + x] = col[y];
                        }
                    });
            }
            for (auto &t : threads) t.join();
            m_iftProgress = 0.5 + 0.5 * bEnd / N;
            update();
            QApplication::processEvents();
        }
    }

    m_iftProgress = -1;

    int outW = (m_origW > 0) ? std::min(m_origW, N) : N;
    int outH = (m_origH > 0) ? std::min(m_origH, N) : N;

    m_imageRawPixels.resize(outW * outH);
    for (int y = 0; y < outH; y++)
        for (int x = 0; x < outW; x++)
            m_imageRawPixels[y * outW + x] = data[y * N + x].real();

    m_imageMinVal = *std::min_element(m_imageRawPixels.begin(), m_imageRawPixels.end());
    m_imageMaxVal = *std::max_element(m_imageRawPixels.begin(), m_imageRawPixels.end());
    double range = m_imageMaxVal - m_imageMinVal;
    double scale = (range > 0) ? 255.0 / range : 1.0;

    m_image = QImage(outW, outH, QImage::Format_Grayscale8);
    for (int y = 0; y < outH; y++) {
        uchar *row = m_image.scanLine(y);
        for (int x = 0; x < outW; x++)
            row[x] = static_cast<uchar>(std::clamp(
                (m_imageRawPixels[y * outW + x] - m_imageMinVal) * scale, 0.0, 255.0));
    }

    m_zoom[0].reset(outW, outH);
    update();
}

void FtWindow::recomputeDisplayImages()
{
    int N = m_fftN;
    int total = N * N;
    if (total == 0) return;

    std::vector<Complex> data = m_fftData;

    if (m_maskCenter) {
        int half = N / 2;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int x = half + dx, y = half + dy;
                if (x >= 0 && x < N && y >= 0 && y < N)
                    data[y * N + x] = Complex(0, 0);
            }
    }

    m_cosVals.resize(total);
    m_sinVals.resize(total);
    m_ampVals.resize(total);
    m_phaseVals.resize(total);
    m_powerVals.resize(total);

    for (int i = 0; i < total; i++) {
        m_cosVals[i]   = data[i].real();
        m_sinVals[i]   = data[i].imag();
        double amp     = std::abs(data[i]);
        m_ampVals[i]   = std::log(1.0 + amp);
        m_phaseVals[i] = std::arg(data[i]) * 180.0 / M_PI;
        m_powerVals[i] = std::log(1.0 + amp * amp);
    }

    m_cosImg   = floatToImage(m_cosVals,   N);
    m_sinImg   = floatToImage(m_sinVals,   N);
    m_ampImg   = floatToImage(m_ampVals,   N);
    m_phaseImg = floatToImage(m_phaseVals, N);
    m_powerImg = floatToImage(m_powerVals, N);

    // Complex FT image: brightness = power, hue = phase
    {
        double pMin = *std::min_element(m_powerVals.begin(), m_powerVals.end());
        double pMax = *std::max_element(m_powerVals.begin(), m_powerVals.end());
        double pScale = (pMax > pMin) ? 1.0 / (pMax - pMin) : 1.0;

        m_complexImg = QImage(N, N, QImage::Format_RGB32);
        for (int y = 0; y < N; y++) {
            QRgb *row = reinterpret_cast<QRgb *>(m_complexImg.scanLine(y));
            for (int x = 0; x < N; x++) {
                int idx = y * N + x;
                double val = std::clamp((m_powerVals[idx] - pMin) * pScale, 0.0, 1.0);
                double hue = m_phaseVals[idx] + 180.0;
                QColor c = QColor::fromHsvF(hue / 360.0, 1.0, val);
                row[x] = c.rgb();
            }
        }
    }

    auto mm = [](const std::vector<double> &v) {
        return std::make_pair(*std::min_element(v.begin(), v.end()),
                              *std::max_element(v.begin(), v.end()));
    };
    std::tie(m_cosMin,   m_cosMax)   = mm(m_cosVals);
    std::tie(m_sinMin,   m_sinMax)   = mm(m_sinVals);
    std::tie(m_ampMin,   m_ampMax)   = mm(m_ampVals);
    std::tie(m_phaseMin, m_phaseMax) = mm(m_phaseVals);
    std::tie(m_powerMin, m_powerMax) = mm(m_powerVals);
}

// ---------------------------------------------------------------------------
//  Utility
// ---------------------------------------------------------------------------
QRect FtWindow::upperArrowBounds() const
{
    int cx = width() / 2;
    int hy = height() - height() / 5;
    int arrowW = std::min({width() / 4, hy / 4, 260});
    int arrowH = std::max(arrowW / 5, 20);
    int gap    = arrowH * 2;
    int totalH = arrowH * 2 + gap;
    int topY   = (hy - totalH) / 2;
    int ax = cx - arrowW / 2;
    return QRect(ax, topY - arrowH * 0.15, arrowW, arrowH * 1.3);
}

QRect FtWindow::lowerArrowBounds() const
{
    int cx = width() / 2;
    int hy = height() - height() / 5;
    int arrowW = std::min({width() / 4, hy / 4, 260});
    int arrowH = std::max(arrowW / 5, 20);
    int gap    = arrowH * 2;
    int totalH = arrowH * 2 + gap;
    int topY   = (hy - totalH) / 2;
    int ax = cx - arrowW / 2;
    int ay = topY + arrowH + gap;
    return QRect(ax, ay - arrowH * 0.15, arrowW, arrowH * 1.3);
}

QString FtWindow::modeLabel() const
{
    switch (m_displayMode) {
    case 0: return "cosinus and sinus";
    case 1: return "amplitude and phase";
    case 2: return "complex Fourier transform";
    case 3: return "powerspectrum";
    }
    return "";
}

QImage FtWindow::computePowerSpecMasked(const QImage &img)
{
    if (img.isNull()) return {};

    QImage gray = img.convertToFormat(QImage::Format_Grayscale8);
    int w = gray.width(), h = gray.height();
    int N = nextPow2(std::max(w, h));

    std::vector<Complex> data(N * N, Complex(0, 0));
    for (int y = 0; y < h; y++) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < w; x++)
            data[y * N + x] = Complex(row[x], 0.0);
    }

    fft2d(data, N, false);
    fftShift(data, N);

    int half = N / 2;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int px = half + dx, py = half + dy;
            if (px >= 0 && px < N && py >= 0 && py < N)
                data[py * N + px] = Complex(0, 0);
        }

    int total = N * N;
    std::vector<double> power(total);
    for (int i = 0; i < total; i++) {
        double a = std::abs(data[i]);
        power[i] = std::log(1.0 + a * a);
    }

    return floatToImage(power, N);
}

void FtWindow::saveHistory()
{
    QSettings settings("ft", "ft");
    for (int i = 0; i < HISTORY_SLOTS; i++) {
        QString key = QString("history/%1").arg(i);
        if (m_history[i].occupied)
            settings.setValue(key, m_history[i].path);
        else
            settings.remove(key);
    }
    settings.setValue("activeSlot", m_activeSlot);
}

void FtWindow::restoreHistory()
{
    QSettings settings("ft", "ft");
    for (int i = 0; i < HISTORY_SLOTS; i++) {
        QString key = QString("history/%1").arg(i);
        QString path = settings.value(key).toString();
        if (path.isEmpty() || !QFile::exists(path)) {
            m_history[i].occupied = false;
            continue;
        }

        qDebug() << "Restoring history slot" << i << ":" << path;

        QImage img;
        std::vector<double> rawPixels;
        double minVal = 0, maxVal = 0, pixelSize = 1.0;

        if (path.endsWith(".mrc", Qt::CaseInsensitive)) {
            MrcResult r = loadMrc(path);
            img       = r.image;
            rawPixels = std::move(r.rawPixels);
            minVal    = r.minVal;
            maxVal    = r.maxVal;
            pixelSize = r.pixelSize;
        } else {
            img = QImage(path);
            if (!img.isNull()) {
                QImage gray = img.convertToFormat(QImage::Format_Grayscale8);
                int w = gray.width(), h = gray.height();
                rawPixels.resize((size_t)w * h);
                for (int y = 0; y < h; y++) {
                    const uchar *row = gray.constScanLine(y);
                    for (int x = 0; x < w; x++)
                        rawPixels[y * w + x] = row[x];
                }
                minVal = *std::min_element(rawPixels.begin(), rawPixels.end());
                maxVal = *std::max_element(rawPixels.begin(), rawPixels.end());
            }
        }

        if (img.isNull()) {
            m_history[i].occupied = false;
            continue;
        }

        // Pad to square using the existing padImageToSquare() helper
        m_image          = img;
        m_imageRawPixels = std::move(rawPixels);
        m_imageMinVal    = minVal;
        m_imageMaxVal    = maxVal;
        padImageToSquare();
        img       = m_image;
        rawPixels = std::move(m_imageRawPixels);
        minVal    = m_imageMinVal;
        maxVal    = m_imageMaxVal;

        m_history[i].image        = img;
        m_history[i].path         = path;
        m_history[i].rawPixels    = std::move(rawPixels);
        m_history[i].minVal       = minVal;
        m_history[i].maxVal       = maxVal;
        m_history[i].pixelSize    = pixelSize;
        m_history[i].powerSpecImg = computePowerSpecMasked(img);
        m_history[i].occupied     = true;
    }
}

void FtWindow::eraserApply(QPoint pos)
{
    if (!m_ftComputed) return;

    for (int i = 0; i < m_numDispItems; i++) {
        const DisplayItem &di = m_dispItems[i];
        if (!di.valid || di.zoomIdx < 1) continue;
        if (!di.screenRect.contains(pos)) continue;

        ZoomState &z = m_zoom[di.zoomIdx];
        QRectF src = z.visibleRect(di.imgW, di.imgH);
        double relX = (pos.x() - di.screenRect.x()) / (double)di.screenRect.width();
        double relY = (pos.y() - di.screenRect.y()) / (double)di.screenRect.height();
        int ix = (int)(src.x() + relX * src.width());
        int iy = (int)(src.y() + relY * src.height());

        if (ix >= 0 && ix < m_fftN && iy >= 0 && iy < m_fftN) {
            double diam = m_eraserDiameterEdit->text().toDouble();
            double sigma = diam / 2.0;
            int rad = (sigma > 0.5) ? (int)std::ceil(sigma * 3) : 0;

            for (int dy = -rad; dy <= rad; dy++) {
                for (int dx = -rad; dx <= rad; dx++) {
                    int px = ix + dx, py = iy + dy;
                    if (px < 0 || px >= m_fftN || py < 0 || py >= m_fftN) continue;

                    double weight = 1.0;
                    if (sigma > 0.5)
                        weight = std::exp(-(dx*dx + dy*dy) / (2.0 * sigma * sigma));

                    m_fftData[py * m_fftN + px] *= (1.0 - weight);
                    // Friedel mate
                    int fpx = (m_fftN - px) % m_fftN;
                    int fpy = (m_fftN - py) % m_fftN;
                    m_fftData[fpy * m_fftN + fpx] *= (1.0 - weight);
                }
            }
            recomputeDisplayImages();
            update();
        }
        return;
    }
}

double FtWindow::brushValue() const
{
    if (!m_ftComputed || m_fftN == 0) return 1.0;
    int half = m_fftN / 2;
    double maxAmp = 0;
    for (int y = 0; y < m_fftN; y++) {
        for (int x = 0; x < m_fftN; x++) {
            if (std::abs(x - half) <= 1 && std::abs(y - half) <= 1)
                continue;
            double a = std::abs(m_fftData[y * m_fftN + x]);
            if (a > maxAmp) maxAmp = a;
        }
    }
    return maxAmp;
}

void FtWindow::brushApply(QPoint pos)
{
    if (!m_ftComputed) return;

    for (int i = 0; i < m_numDispItems; i++) {
        const DisplayItem &di = m_dispItems[i];
        if (!di.valid || di.zoomIdx < 1) continue;
        if (!di.screenRect.contains(pos)) continue;

        ZoomState &z = m_zoom[di.zoomIdx];
        QRectF src = z.visibleRect(di.imgW, di.imgH);
        double relX = (pos.x() - di.screenRect.x()) / (double)di.screenRect.width();
        double relY = (pos.y() - di.screenRect.y()) / (double)di.screenRect.height();
        int ix = (int)(src.x() + relX * src.width());
        int iy = (int)(src.y() + relY * src.height());

        if (ix >= 0 && ix < m_fftN && iy >= 0 && iy < m_fftN) {
            double val = m_brushValueEdit->text().toDouble();
            double diam = m_brushDiameterEdit->text().toDouble();
            double sigma = diam / 2.0;
            int rad = (sigma > 0.5) ? (int)std::ceil(sigma * 3) : 0;

            for (int dy = -rad; dy <= rad; dy++) {
                for (int dx = -rad; dx <= rad; dx++) {
                    int px = ix + dx, py = iy + dy;
                    if (px < 0 || px >= m_fftN || py < 0 || py >= m_fftN) continue;

                    double weight = 1.0;
                    if (sigma > 0.5)
                        weight = std::exp(-(dx*dx + dy*dy) / (2.0 * sigma * sigma));

                    double paintVal = val * weight;
                    m_fftData[py * m_fftN + px] += Complex(paintVal, 0);
                    // Friedel mate
                    int fpx = (m_fftN - px) % m_fftN;
                    int fpy = (m_fftN - py) % m_fftN;
                    m_fftData[fpy * m_fftN + fpx] += Complex(paintVal, 0);
                }
            }
            recomputeDisplayImages();
            update();
        }
        return;
    }
}

void FtWindow::rebuildImageFromRaw()
{
    int w = m_image.width(), h = m_image.height();
    if (m_imageRawPixels.empty() || (int)m_imageRawPixels.size() != w * h) return;

    m_imageMinVal = *std::min_element(m_imageRawPixels.begin(), m_imageRawPixels.end());
    m_imageMaxVal = *std::max_element(m_imageRawPixels.begin(), m_imageRawPixels.end());
    double range = m_imageMaxVal - m_imageMinVal;
    double scale = (range > 0) ? 255.0 / range : 1.0;

    m_image = QImage(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; y++) {
        uchar *row = m_image.scanLine(y);
        for (int x = 0; x < w; x++)
            row[x] = static_cast<uchar>(std::clamp(
                (m_imageRawPixels[y * w + x] - m_imageMinVal) * scale, 0.0, 255.0));
    }
}

void FtWindow::p1EraserApply(QPoint pos)
{
    if (m_image.isNull()) return;

    for (int i = 0; i < m_numDispItems; i++) {
        const DisplayItem &di = m_dispItems[i];
        if (!di.valid || di.zoomIdx != 0) continue;
        if (!di.screenRect.contains(pos)) continue;

        ZoomState &z = m_zoom[0];
        QRectF src = z.visibleRect(di.imgW, di.imgH);
        double relX = (pos.x() - di.screenRect.x()) / (double)di.screenRect.width();
        double relY = (pos.y() - di.screenRect.y()) / (double)di.screenRect.height();
        int ix = (int)(src.x() + relX * src.width());
        int iy = (int)(src.y() + relY * src.height());

        int w = m_image.width(), h = m_image.height();
        if (ix < 0 || ix >= w || iy < 0 || iy >= h) return;

        double diam = m_p1EraserDiameterEdit->text().toDouble();
        double sigma = diam / 2.0;
        int rad = (sigma > 0.5) ? (int)std::ceil(sigma * 3) : 0;

        for (int dy = -rad; dy <= rad; dy++) {
            for (int dx = -rad; dx <= rad; dx++) {
                int px = ix + dx, py = iy + dy;
                if (px < 0 || px >= w || py < 0 || py >= h) continue;

                double weight = 1.0;
                if (sigma > 0.5)
                    weight = std::exp(-(dx*dx + dy*dy) / (2.0 * sigma * sigma));

                m_imageRawPixels[py * w + px] *= (1.0 - weight);
            }
        }
        rebuildImageFromRaw();
        update();
        return;
    }
}

void FtWindow::p1BrushApply(QPoint pos)
{
    if (m_image.isNull()) return;

    for (int i = 0; i < m_numDispItems; i++) {
        const DisplayItem &di = m_dispItems[i];
        if (!di.valid || di.zoomIdx != 0) continue;
        if (!di.screenRect.contains(pos)) continue;

        ZoomState &z = m_zoom[0];
        QRectF src = z.visibleRect(di.imgW, di.imgH);
        double relX = (pos.x() - di.screenRect.x()) / (double)di.screenRect.width();
        double relY = (pos.y() - di.screenRect.y()) / (double)di.screenRect.height();
        int ix = (int)(src.x() + relX * src.width());
        int iy = (int)(src.y() + relY * src.height());

        int w = m_image.width(), h = m_image.height();
        if (ix < 0 || ix >= w || iy < 0 || iy >= h) return;

        double val = m_p1BrushValueEdit->text().toDouble();
        double diam = m_p1BrushDiameterEdit->text().toDouble();
        double sigma = diam / 2.0;
        int rad = (sigma > 0.5) ? (int)std::ceil(sigma * 3) : 0;

        for (int dy = -rad; dy <= rad; dy++) {
            for (int dx = -rad; dx <= rad; dx++) {
                int px = ix + dx, py = iy + dy;
                if (px < 0 || px >= w || py < 0 || py >= h) continue;

                double weight = 1.0;
                if (sigma > 0.5)
                    weight = std::exp(-(dx*dx + dy*dy) / (2.0 * sigma * sigma));

                m_imageRawPixels[py * w + px] = m_imageRawPixels[py * w + px] * (1.0 - weight) + val * weight;
            }
        }
        rebuildImageFromRaw();
        update();
        return;
    }
}

void FtWindow::onApplyBandpass()
{
    if (!m_ftComputed || m_fftN == 0) return;

    int N = m_fftN;
    int half = N / 2;
    double innerR = m_bandInnerR * half;
    double outerR = m_bandOuterR * half;
    int smooth = m_smoothEdit->text().toInt();
    if (smooth < 0) smooth = 0;
    bool eraseOutside = m_bandEraseOutside->isChecked();

    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            double dx = x - half;
            double dy = y - half;
            double dist = std::sqrt(dx * dx + dy * dy);

            double factor = 1.0;
            if (eraseOutside) {
                if (dist < innerR) {
                    double d = innerR - dist;
                    factor = (smooth > 0 && d < smooth) ? (1.0 - d / smooth) : 0.0;
                } else if (dist > outerR) {
                    double d = dist - outerR;
                    factor = (smooth > 0 && d < smooth) ? (1.0 - d / smooth) : 0.0;
                } else {
                    factor = 1.0;
                }
            } else {
                if (dist >= innerR && dist <= outerR) {
                    factor = 0.0;
                } else if (smooth > 0 && dist < innerR && dist > innerR - smooth) {
                    factor = (innerR - dist) / smooth;
                } else if (smooth > 0 && dist > outerR && dist < outerR + smooth) {
                    factor = (dist - outerR) / smooth;
                }
            }

            if (factor < 1.0)
                m_fftData[y * N + x] *= factor;
        }
    }

    recomputeDisplayImages();
    computeInverseFFT();
    update();
}

void FtWindow::onApplyLattice()
{
    if (!m_ftComputed || m_fftN == 0) return;

    int N = m_fftN;
    int half = N / 2;
    double dotDiam = m_latticeDotDiamEdit->text().toDouble();
    double dotR = dotDiam / 2.0;
    int smooth = m_latticeSmoothEdit->text().toInt();
    if (smooth < 0) smooth = 0;
    bool eraseOutside = m_latticeEraseOutside->isChecked();

    double ux = m_latticeUx, uy = m_latticeUy;
    double vx = m_latticeVx, vy = m_latticeVy;
    double det = ux * vy - uy * vx;
    if (std::abs(det) < 0.5) return;  // degenerate lattice

    // Inverse lattice matrix for projecting pixel coords onto lattice basis
    double invUx =  vy / det, invUy = -vx / det;
    double invVx = -uy / det, invVy =  ux / det;

    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            double dx = x - half;
            double dy = y - half;

            // Project onto lattice basis and find nearest lattice point
            double fi = dx * invUx + dy * invUy;
            double fj = dx * invVx + dy * invVy;
            int i0 = (int)std::floor(fi);
            int j0 = (int)std::floor(fj);

            // Check the 4 nearest lattice points
            double minDist = 1e18;
            for (int di = 0; di <= 1; di++) {
                for (int dj = 0; dj <= 1; dj++) {
                    double lx = (i0 + di) * ux + (j0 + dj) * vx;
                    double ly = (i0 + di) * uy + (j0 + dj) * vy;
                    double ddx = dx - lx, ddy = dy - ly;
                    double d = std::sqrt(ddx * ddx + ddy * ddy);
                    if (d < minDist) minDist = d;
                }
            }

            double factor = 1.0;
            if (eraseOutside) {
                // Keep pixels near lattice dots, erase everything else
                if (minDist > dotR) {
                    double d = minDist - dotR;
                    factor = (smooth > 0 && d < smooth) ? (1.0 - d / smooth) : 0.0;
                }
            } else {
                // Erase pixels near lattice dots, keep everything else
                if (minDist <= dotR) {
                    factor = 0.0;
                } else if (smooth > 0 && minDist < dotR + smooth) {
                    factor = (minDist - dotR) / smooth;
                }
            }

            if (factor < 1.0)
                m_fftData[y * N + x] *= factor;
        }
    }

    recomputeDisplayImages();
    computeInverseFFT();
    update();
}

void FtWindow::onApplyBinning()
{
    if (m_image.isNull()) return;

    int binFactor = m_binCombo->currentData().toInt();
    if (binFactor <= 1) return;

    int w = m_image.width();
    int h = m_image.height();

    // Work on a copy of raw pixel values
    std::vector<double> &pix = m_imageRawPixels;
    if ((int)pix.size() != w * h) return;

    bool keepSize = m_binKeepSizeBtn->isChecked();

    if (keepSize) {
        // Average each NxN block and fill all pixels in that block with the average
        for (int by = 0; by + binFactor <= h; by += binFactor) {
            for (int bx = 0; bx + binFactor <= w; bx += binFactor) {
                double sum = 0;
                for (int dy = 0; dy < binFactor; dy++)
                    for (int dx = 0; dx < binFactor; dx++)
                        sum += pix[(by + dy) * w + (bx + dx)];
                double avg = sum / (binFactor * binFactor);
                for (int dy = 0; dy < binFactor; dy++)
                    for (int dx = 0; dx < binFactor; dx++)
                        pix[(by + dy) * w + (bx + dx)] = avg;
            }
        }
        // Handle leftover columns (right edge)
        int remX = w % binFactor;
        if (remX > 0) {
            int startX = w - remX;
            for (int by = 0; by + binFactor <= h; by += binFactor) {
                double sum = 0;
                for (int dy = 0; dy < binFactor; dy++)
                    for (int x = startX; x < w; x++)
                        sum += pix[(by + dy) * w + x];
                double avg = sum / (remX * binFactor);
                for (int dy = 0; dy < binFactor; dy++)
                    for (int x = startX; x < w; x++)
                        pix[(by + dy) * w + x] = avg;
            }
        }
        // Handle leftover rows (bottom edge)
        int remY = h % binFactor;
        if (remY > 0) {
            int startY = h - remY;
            for (int bx = 0; bx + binFactor <= w; bx += binFactor) {
                double sum = 0;
                for (int y = startY; y < h; y++)
                    for (int dx = 0; dx < binFactor; dx++)
                        sum += pix[y * w + (bx + dx)];
                double avg = sum / (remY * binFactor);
                for (int y = startY; y < h; y++)
                    for (int dx = 0; dx < binFactor; dx++)
                        pix[y * w + (bx + dx)] = avg;
            }
        }
        // Handle bottom-right corner
        int remX2 = w % binFactor, remY2 = h % binFactor;
        if (remX2 > 0 && remY2 > 0) {
            int startX = w - remX2, startY = h - remY2;
            double sum = 0;
            for (int y = startY; y < h; y++)
                for (int x = startX; x < w; x++)
                    sum += pix[y * w + x];
            double avg = sum / (remX2 * remY2);
            for (int y = startY; y < h; y++)
                for (int x = startX; x < w; x++)
                    pix[y * w + x] = avg;
        }

        m_imageMinVal = *std::min_element(pix.begin(), pix.end());
        m_imageMaxVal = *std::max_element(pix.begin(), pix.end());
        double range = m_imageMaxVal - m_imageMinVal;
        double scale = (range > 0) ? 255.0 / range : 1.0;
        m_image = QImage(w, h, QImage::Format_Grayscale8);
        for (int y = 0; y < h; y++) {
            uchar *row = m_image.scanLine(y);
            for (int x = 0; x < w; x++)
                row[x] = static_cast<uchar>(std::clamp(
                    (pix[y * w + x] - m_imageMinVal) * scale, 0.0, 255.0));
        }
    } else {
        // Shrink: each NxN block becomes one pixel
        int newW = w / binFactor;
        int newH = h / binFactor;
        if (newW < 1 || newH < 1) return;

        std::vector<double> newPix(newW * newH);
        for (int by = 0; by < newH; by++) {
            for (int bx = 0; bx < newW; bx++) {
                double sum = 0;
                for (int dy = 0; dy < binFactor; dy++)
                    for (int dx = 0; dx < binFactor; dx++)
                        sum += pix[(by * binFactor + dy) * w + (bx * binFactor + dx)];
                newPix[by * newW + bx] = sum / (binFactor * binFactor);
            }
        }

        m_imageRawPixels = std::move(newPix);
        m_imageMinVal = *std::min_element(m_imageRawPixels.begin(), m_imageRawPixels.end());
        m_imageMaxVal = *std::max_element(m_imageRawPixels.begin(), m_imageRawPixels.end());
        double range = m_imageMaxVal - m_imageMinVal;
        double scale = (range > 0) ? 255.0 / range : 1.0;
        m_image = QImage(newW, newH, QImage::Format_Grayscale8);
        for (int y = 0; y < newH; y++) {
            uchar *row = m_image.scanLine(y);
            for (int x = 0; x < newW; x++)
                row[x] = static_cast<uchar>(std::clamp(
                    (m_imageRawPixels[y * newW + x] - m_imageMinVal) * scale, 0.0, 255.0));
        }
        m_zoom[0].reset(newW, newH);
        m_pixelSize *= binFactor;
    }

    if (m_ftComputed)
        computeFFT();

    update();
}

void FtWindow::onApplyDirectional()
{
    if (!m_ftComputed || m_fftN == 0) return;

    int N = m_fftN;
    int half = N / 2;
    int smooth = m_smoothEdit->text().toInt();
    if (smooth < 0) smooth = 0;
    bool eraseOutside = m_bandEraseOutside->isChecked();

    double a1 = m_dirAngle1, a2 = m_dirAngle2;
    while (a2 < a1) a2 += 360;
    if (a2 - a1 > 180) { std::swap(a1, a2); while (a2 < a1) a2 += 360; }

    auto inRange = [](double a, double lo, double hi) {
        while (a < lo) a += 360;
        while (a > lo + 360) a -= 360;
        return a <= hi;
    };

    auto isInWedge = [&](double angle) -> bool {
        return inRange(angle, a1, a2) || inRange(angle, a1 + 180, a2 + 180);
    };

    auto wedgeDistance = [&](double angle) -> double {
        auto distTo = [](double a, double lo, double hi) -> double {
            while (a < lo - 180) a += 360;
            while (a > lo + 180) a -= 360;
            if (a >= lo && a <= hi) return 0;
            return std::min(std::abs(a - lo), std::abs(a - hi));
        };
        return std::min(distTo(angle, a1, a2), distTo(angle, a1 + 180, a2 + 180));
    };

    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            double dx = x - half;
            double dy = y - half;
            if (dx == 0 && dy == 0) continue;

            double angle = std::atan2(dy, dx) * 180.0 / M_PI;
            bool inW = isInWedge(angle);
            double edgeDist = wedgeDistance(angle);

            double factor = 1.0;
            if (eraseOutside) {
                if (!inW) {
                    factor = (smooth > 0 && edgeDist < smooth)
                             ? (1.0 - edgeDist / smooth) : 0.0;
                }
            } else {
                if (inW) {
                    factor = 0.0;
                } else if (smooth > 0 && edgeDist < smooth) {
                    factor = edgeDist / smooth;
                }
            }

            if (factor < 1.0)
                m_fftData[y * N + x] *= factor;
        }
    }

    recomputeDisplayImages();
    computeInverseFFT();
    update();
}

// ---------------------------------------------------------------------------
//  Math calculations
// ---------------------------------------------------------------------------
void FtWindow::onMathCancel()
{
    m_mathActive = false;
    m_mathOutCombo->hide();
    m_mathEqualsLabel->hide();
    m_mathIn1Combo->hide();
    m_mathOpCombo->hide();
    m_mathIn2Combo->hide();
    m_mathCancelBtn->hide();
    m_mathComputeBtn->hide();
    update();
}

void FtWindow::onMathCompute()
{
    int outIdx  = m_mathOutCombo->currentIndex();
    int in1Idx  = m_mathIn1Combo->currentIndex();
    int in2Idx  = m_mathIn2Combo->currentIndex();
    int opIdx   = m_mathOpCombo->currentIndex();  // 0=+, 1=-, 2=*, 3=/, 4=conv, 5=corr

    // Retrieve input images from history (or current if active)
    auto getSlotPixels = [&](int idx, int &w, int &h) -> std::vector<double> {
        if (idx == m_activeSlot && !m_image.isNull()) {
            w = m_image.width();
            h = m_image.height();
            return m_imageRawPixels;
        }
        if (idx >= 0 && idx < HISTORY_SLOTS && m_history[idx].occupied) {
            w = m_history[idx].image.width();
            h = m_history[idx].image.height();
            return m_history[idx].rawPixels;
        }
        w = 0; h = 0;
        return {};
    };

    int w1 = 0, h1 = 0, w2 = 0, h2 = 0;
    std::vector<double> pix1 = getSlotPixels(in1Idx, w1, h1);
    std::vector<double> pix2 = getSlotPixels(in2Idx, w2, h2);

    if (pix1.empty() || pix2.empty()) return;

    // Safety: if rawPixels size doesn't match w*h, rebuild from the QImage
    auto fixRawPixels = [&](std::vector<double> &pix, int idx, int w, int h) {
        if ((int)pix.size() == w * h) return;
        QImage img;
        if (idx == m_activeSlot && !m_image.isNull())
            img = m_image;
        else if (idx >= 0 && idx < HISTORY_SLOTS && m_history[idx].occupied)
            img = m_history[idx].image;
        if (img.isNull()) return;
        QImage gray = img.convertToFormat(QImage::Format_Grayscale8);
        pix.resize((size_t)w * h);
        for (int y = 0; y < h; y++) {
            const uchar *row = gray.constScanLine(y);
            for (int x = 0; x < w; x++)
                pix[y * w + x] = row[x];
        }
    };
    fixRawPixels(pix1, in1Idx, w1, h1);
    fixRawPixels(pix2, in2Idx, w2, h2);

    // Pad a non-square image to square with average grey, centered
    auto padToSquare = [](std::vector<double> &src, int &sw, int &sh) {
        if (sw == sh) return;
        int S = std::max(sw, sh);
        double sum = 0;
        for (double v : src) sum += v;
        double avg = sum / src.size();

        std::vector<double> dst(S * S, avg);
        int offX = (S - sw) / 2;
        int offY = (S - sh) / 2;
        for (int y = 0; y < sh; y++)
            for (int x = 0; x < sw; x++)
                dst[(y + offY) * S + (x + offX)] = src[y * sw + x];
        src = std::move(dst);
        sw = S;
        sh = S;
    };

    // For convolution/correlation, use zero-mean + zero-pad + rescale approach
    if (opIdx >= 4) {
        // Step 1: Subtract mean from each image (float to zero average)
        double mean1 = 0, mean2 = 0;
        for (double v : pix1) mean1 += v;
        mean1 /= pix1.size();
        for (double v : pix2) mean2 += v;
        mean2 /= pix2.size();
        for (double &v : pix1) v -= mean1;
        for (double &v : pix2) v -= mean2;

        // Step 2: Zero-pad each image to square (centered, zeros on outside)
        auto zeroPadToSquare = [](std::vector<double> &src, int &sw, int &sh) {
            if (sw == sh) return;
            int sq = std::max(sw, sh);
            std::vector<double> dst(sq * sq, 0.0);
            int offX = (sq - sw) / 2;
            int offY = (sq - sh) / 2;
            for (int y = 0; y < sh; y++)
                for (int x = 0; x < sw; x++)
                    dst[(y + offY) * sq + (x + offX)] = src[y * sw + x];
            src = std::move(dst);
            sw = sq;
            sh = sq;
        };
        zeroPadToSquare(pix1, w1, h1);
        zeroPadToSquare(pix2, w2, h2);

        // Step 3: Rescale both to S x S where S = larger square dimension
        int S = std::max(w1, w2);
        auto scaleToSize = [](const std::vector<double> &src, int sw, int S) {
            if (sw == S) return src;
            std::vector<double> dst(S * S);
            for (int y = 0; y < S; y++) {
                double srcY = y * (sw - 1.0) / (S - 1.0);
                int y0 = (int)srcY;
                int y1 = std::min(y0 + 1, sw - 1);
                double fy = srcY - y0;
                for (int x = 0; x < S; x++) {
                    double srcX = x * (sw - 1.0) / (S - 1.0);
                    int x0 = (int)srcX;
                    int x1 = std::min(x0 + 1, sw - 1);
                    double fx = srcX - x0;
                    double v00 = src[y0 * sw + x0];
                    double v10 = src[y0 * sw + x1];
                    double v01 = src[y1 * sw + x0];
                    double v11 = src[y1 * sw + x1];
                    dst[y * S + x] = v00 * (1 - fx) * (1 - fy)
                                   + v10 * fx       * (1 - fy)
                                   + v01 * (1 - fx) * fy
                                   + v11 * fx       * fy;
                }
            }
            return dst;
        };
        std::vector<double> a = scaleToSize(pix1, w1, S);
        std::vector<double> b = scaleToSize(pix2, w2, S);

        // Step 4: FFT convolution/correlation
        int N = nextPow2(S);
        int halfS = S / 2;

        std::vector<Complex> fa(N * N, Complex(0, 0));
        std::vector<Complex> fb(N * N, Complex(0, 0));
        for (int y = 0; y < S; y++)
            for (int x = 0; x < S; x++) {
                fa[y * N + x] = Complex(a[y * S + x], 0);
                fb[y * N + x] = Complex(b[y * S + x], 0);
            }

        fft2d(fa, N, false);
        fft2d(fb, N, false);

        std::vector<Complex> fc(N * N);
        for (int i = 0; i < N * N; i++) {
            if (opIdx == 4)
                fc[i] = fa[i] * fb[i];             // convolution
            else
                fc[i] = fa[i] * std::conj(fb[i]);  // cross-correlation
        }

        fft2d(fc, N, true);  // inverse FFT

        std::vector<double> result(S * S);
        // Center the result: cyclic shift so zero-lag is at (S/2, S/2)
        // Convolution adds positions (+halfS), correlation subtracts (-halfS)
        for (int y = 0; y < S; y++)
            for (int x = 0; x < S; x++) {
                int fy = (opIdx == 4)
                    ? (y + halfS) % N          // convolution
                    : (y - halfS + N) % N;     // correlation
                int fx = (opIdx == 4)
                    ? (x + halfS) % N
                    : (x - halfS + N) % N;
                result[y * S + x] = fc[fy * N + fx].real();
            }

        // Build the output QImage from the result
        double minVal = *std::min_element(result.begin(), result.end());
        double maxVal = *std::max_element(result.begin(), result.end());
        double range  = maxVal - minVal;
        double scale  = (range > 0) ? 255.0 / range : 1.0;

        QImage outImg(S, S, QImage::Format_Grayscale8);
        for (int y = 0; y < S; y++) {
            uchar *row = outImg.scanLine(y);
            for (int x = 0; x < S; x++)
                row[x] = static_cast<uchar>(std::clamp(
                    (result[y * S + x] - minVal) * scale, 0.0, 255.0));
        }

        // Save current active image back to its slot before switching
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

        // Store result in output slot
        m_history[outIdx].image     = outImg;
        m_history[outIdx].path      = QString("math: %1 %2 %3")
                                          .arg(QChar('a' + in1Idx))
                                          .arg(m_mathOpCombo->currentText())
                                          .arg(QChar('a' + in2Idx));
        m_history[outIdx].rawPixels = std::move(result);
        m_history[outIdx].minVal    = minVal;
        m_history[outIdx].maxVal    = maxVal;
        m_history[outIdx].pixelSize = 1.0;
        m_history[outIdx].powerSpecImg = computePowerSpecMasked(outImg);
        m_history[outIdx].occupied  = true;

        m_activeSlot     = outIdx;
        m_image          = m_history[outIdx].image;
        m_imagePath      = m_history[outIdx].path;
        m_imageRawPixels = m_history[outIdx].rawPixels;
        m_imageMinVal    = m_history[outIdx].minVal;
        m_imageMaxVal    = m_history[outIdx].maxVal;
        m_pixelSize      = m_history[outIdx].pixelSize;
        m_zoom[0].reset(m_image.width(), m_image.height());

        m_ftComputed  = false;
        m_displayMode = 3;
        m_modeBtn->setText(modeLabel());
        m_modeBtn->hide();
        m_maskBtn->hide();
        m_maskBtn->setChecked(false);
        m_maskCenter = false;

        computeFFT();
        m_history[outIdx].powerSpecImg = computePowerSpecMasked(m_image);
        saveHistory();
        onMathCancel();
        return;
    }

    // For arithmetic operations (+, -, *, /), pad to square and scale
    padToSquare(pix1, w1, h1);
    padToSquare(pix2, w2, h2);

    int S = std::max(w1, w2);

    auto scaleToSize = [](const std::vector<double> &src, int sw, int S) {
        if (sw == S) return src;
        std::vector<double> dst(S * S);
        for (int y = 0; y < S; y++) {
            double srcY = y * (sw - 1.0) / (S - 1.0);
            int y0 = (int)srcY;
            int y1 = std::min(y0 + 1, sw - 1);
            double fy = srcY - y0;
            for (int x = 0; x < S; x++) {
                double srcX = x * (sw - 1.0) / (S - 1.0);
                int x0 = (int)srcX;
                int x1 = std::min(x0 + 1, sw - 1);
                double fx = srcX - x0;
                double v00 = src[y0 * sw + x0];
                double v10 = src[y0 * sw + x1];
                double v01 = src[y1 * sw + x0];
                double v11 = src[y1 * sw + x1];
                dst[y * S + x] = v00 * (1 - fx) * (1 - fy)
                               + v10 * fx       * (1 - fy)
                               + v01 * (1 - fx) * fy
                               + v11 * fx       * fy;
            }
        }
        return dst;
    };

    std::vector<double> a = scaleToSize(pix1, w1, S);
    std::vector<double> b = scaleToSize(pix2, w2, S);

    std::vector<double> result(S * S);

    if (opIdx <= 3) {
        // Wien filter noise estimate from range of b
        double bMin = *std::min_element(b.begin(), b.end());
        double bMax = *std::max_element(b.begin(), b.end());
        double noise = std::max(std::abs((bMax - bMin) / 100.0), 1.0);

        // Pixel-wise operations: +, -, *, / (Wien)
        for (int i = 0; i < S * S; i++) {
            switch (opIdx) {
            case 0: result[i] = a[i] + b[i]; break;
            case 1: result[i] = a[i] - b[i]; break;
            case 2: result[i] = a[i] * b[i]; break;
            case 3: result[i] = a[i] * b[i] / (b[i] * b[i] + noise); break;
            }
        }
    }

    // Build the output QImage from the result
    double minVal = *std::min_element(result.begin(), result.end());
    double maxVal = *std::max_element(result.begin(), result.end());
    double range  = maxVal - minVal;
    double scale  = (range > 0) ? 255.0 / range : 1.0;

    QImage outImg(S, S, QImage::Format_Grayscale8);
    for (int y = 0; y < S; y++) {
        uchar *row = outImg.scanLine(y);
        for (int x = 0; x < S; x++)
            row[x] = static_cast<uchar>(std::clamp(
                (result[y * S + x] - minVal) * scale, 0.0, 255.0));
    }

    // Save current active image back to its slot before switching
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

    // Store result in output slot
    m_history[outIdx].image     = outImg;
    m_history[outIdx].path      = QString("math: %1 %2 %3")
                                      .arg(QChar('a' + in1Idx))
                                      .arg(m_mathOpCombo->currentText())
                                      .arg(QChar('a' + in2Idx));
    m_history[outIdx].rawPixels = std::move(result);
    m_history[outIdx].minVal    = minVal;
    m_history[outIdx].maxVal    = maxVal;
    m_history[outIdx].pixelSize = 1.0;
    m_history[outIdx].powerSpecImg = computePowerSpecMasked(outImg);
    m_history[outIdx].occupied  = true;

    // Activate the output slot as the current display
    m_activeSlot     = outIdx;
    m_image          = m_history[outIdx].image;
    m_imagePath      = m_history[outIdx].path;
    m_imageRawPixels = m_history[outIdx].rawPixels;
    m_imageMinVal    = m_history[outIdx].minVal;
    m_imageMaxVal    = m_history[outIdx].maxVal;
    m_pixelSize      = m_history[outIdx].pixelSize;
    m_zoom[0].reset(m_image.width(), m_image.height());

    m_ftComputed  = false;
    m_displayMode = 3;
    m_modeBtn->setText(modeLabel());
    m_modeBtn->hide();
    m_maskBtn->hide();
    m_maskBtn->setChecked(false);
    m_maskCenter = false;

    computeFFT();
    m_history[outIdx].powerSpecImg = computePowerSpecMasked(m_image);
    saveHistory();

    // Close the math overlay
    onMathCancel();
}
