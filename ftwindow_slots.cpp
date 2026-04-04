#include "ftwindow_common.h"

// ---------------------------------------------------------------------------
//  Slots
// ---------------------------------------------------------------------------
void FtWindow::onLoadImage()
{
    QSettings settings("ft", "ft");
    QString lastDir = QFileInfo(settings.value("lastFile").toString()).absolutePath();

    QString path = QFileDialog::getOpenFileName(
        this, "Load image", lastDir,
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
    } else {
        m_image = QImage(m_imagePath);
        m_pixelSize = 1.0;
        if (!m_image.isNull())
            extractImageData();
    }

    m_ftComputed = false;
    m_displayMode = 2;
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

    if (!m_image.isNull()) {
        for (int i = HISTORY_SLOTS - 1; i > 0; i--)
            m_history[i] = std::move(m_history[i - 1]);
        m_history[0].image        = m_image;
        m_history[0].path         = m_imagePath;
        m_history[0].rawPixels    = m_imageRawPixels;
        m_history[0].minVal       = m_imageMinVal;
        m_history[0].maxVal       = m_imageMaxVal;
        m_history[0].pixelSize    = m_pixelSize;
        m_history[0].powerSpecImg = computePowerSpecMasked(m_image);
        m_history[0].occupied     = true;
        saveHistory();
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
        else
            qDebug() << "MRC load OK –" << m_image.width() << "x" << m_image.height();
    } else {
        m_image = QImage(path);
        m_pixelSize = 1.0;
        if (m_image.isNull()) {
            qDebug() << "Image load FAILED for:" << path;
        } else {
            qDebug() << "Image loaded:" << m_image.width() << "x" << m_image.height()
                     << "format:" << m_image.format();
            extractImageData();
        }
    }

    m_imagePath = path;

    QSettings settings("ft", "ft");
    settings.setValue("lastFile", path);

    m_ftComputed = false;
    m_displayMode = 2;
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

    std::vector<Complex> data(N * N, Complex(0.0, 0.0));
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

    int origW = m_image.width();
    int origH = m_image.height();
    int outW = std::min(origW, N);
    int outH = std::min(origH, N);

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
    case 0: return "sinus and cosinus";
    case 1: return "amplitude and phase";
    case 2: return "powerspectrum";
    case 3: return "complex Fourier transform";
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
    update();
}
