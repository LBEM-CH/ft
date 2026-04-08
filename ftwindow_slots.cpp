#include "ftwindow_common.h"

// ---------------------------------------------------------------------------
//  Slots
// ---------------------------------------------------------------------------
void FtWindow::onLoadImage()
{
#ifdef __EMSCRIPTEN__
    // Build a list of example images served alongside the WASM app.
    QStringList items;
    items << "Exercise1-Photos/band_1995.png"
          << "Exercise1-Photos/brain.png"
          << "Exercise1-Photos/church.png"
          << "Exercise1-Photos/jacques_andreas_2017.png"
          << "Exercise1-Photos/jacques_titan.png"
          << "Exercise1-Photos/lorenz_1999.png"
          << "Exercise1-Photos/party_1995.png"
          << "Exercise1-Photos/plants.png"
          << "Exercise1-Photos/simon_1999.png"
          << "Exercise1-Photos/thomas_1999.png"
          << "Exercise2-Restauration/restauration.png"
          << "Exercise3-2D-Crystal/2d_protein_crystal_1024.png"
          << "Exercise3-2D-Crystal/Polyhead_virus_512.png"
          << "Exercise4-Eyes/eyes.png"
          << "Exercise5-Text/Dot_black_center_1024.png"
          << "Exercise5-Text/Fourier_text_white_1024.png"
          << "Exercise5-Text/Fourier_text_black_1024.png"
          << "Exercise5-Text/Fourier_word_black_1024.png"
          << "Exercise5-Text/Letter_f_black_1024.png"
          << "Exercise5-Text/Letter_o_white_center_1024.png"
          << "Exercise5-Text/Letter_o_white_corner_1024.png"
          << "Exercise5-Text/Ring_black_1024.png"
          << "Exercise5-Text/Smiley_black_1024.png"
          << "Exercise5-Text/Smiley_small_black_1024.png"
          << "Exercise6-Photo-Multiplication/andreas.png"
          << "Exercise7-Single_particle_EM/apoferritin_1024.png"
          << "Exercise7-Single_particle_EM/apoferritin_1024_anisotropic.png"
          << "Exercise8-Apple/apple_clean.png"
          << "Exercise8-Apple/apple_corners_labeled.png"
          << "Exercise8-Apple/apple_noisy.png"
          << "Exercise8-Apple/apple_very_noisy.png";

    // Use non-blocking open() instead of exec() for WASM compatibility
    auto *dlg = new QInputDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle("Load Example Image");
    dlg->setLabelText("Select an image:");
    dlg->setComboBoxItems(items);
    dlg->setComboBoxEditable(false);
    connect(dlg, &QDialog::accepted, this, [this, dlg]() {
        QString chosen = dlg->textValue();
        if (!chosen.isEmpty())
            fetchAndLoadImage(chosen);
    });
    dlg->open();
#else
    QString startDir = QCoreApplication::applicationDirPath() + "/../EXAMPLE_IMAGES";
    if (!QDir(startDir).exists())
        startDir = QCoreApplication::applicationDirPath();

    QString path = QFileDialog::getOpenFileName(
        this, "Load image", startDir,
        "Images (*.tif *.tiff *.jpg *.jpeg *.png *.mrc *.MRC)");

    if (path.isEmpty()) return;
    loadImageFile(path);
#endif
}

void FtWindow::onSaveImage()
{
    if (m_image.isNull()) return;

#ifdef __EMSCRIPTEN__
    // In WASM, save image via browser download
    QByteArray pngData;
    QBuffer buf(&pngData);
    buf.open(QIODevice::WriteOnly);
    m_image.save(&buf, "PNG");
    buf.close();
    QFileDialog::saveFileContent(pngData, "image.png");
#else
    QString path = QFileDialog::getSaveFileName(
        this, "Save image as PNG", QString(),
        "PNG Image (*.png)");
    if (path.isEmpty()) return;

    if (!path.endsWith(".png", Qt::CaseInsensitive))
        path += ".png";

    m_image.save(path, "PNG");
#endif
}

void FtWindow::onCreateImage()
{
    // If no slot is active, pick the first empty one (or last slot as fallback)
    if (m_activeSlot < 0) {
        m_activeSlot = HISTORY_SLOTS - 1;
        for (int i = 0; i < HISTORY_SLOTS; i++) {
            if (!m_history[i].occupied) { m_activeSlot = i; break; }
        }
    }

    int sz = 1024;
    m_image = QImage(sz, sz, QImage::Format_Grayscale8);
    m_image.fill(0);
    m_imageRawPixels.assign((size_t)sz * sz, 0.0);
    m_imageMinVal = 0;
    m_imageMaxVal = 0;
    m_imageDispMin = 0;
    m_imageDispMax = 0;
    m_pixelSize = 1.0;
    m_imagePath.clear();

    m_history[m_activeSlot].image     = m_image;
    m_history[m_activeSlot].path.clear();
    m_history[m_activeSlot].rawPixels = m_imageRawPixels;
    m_history[m_activeSlot].minVal    = 0;
    m_history[m_activeSlot].maxVal    = 0;
    m_history[m_activeSlot].pixelSize = 1.0;
    m_history[m_activeSlot].occupied  = true;

    m_ftComputed = false;
    m_modeBtn->setText(modeLabel());
    m_modeBtn->hide();
    m_maskBtn->hide();

    m_zoom[0].reset(sz, sz);
    computeFFT();
    m_history[m_activeSlot].powerSpecImg = computePowerSpecMasked(m_image);

#ifndef __EMSCRIPTEN__
    QSettings settings("ft", "ft");
    settings.setValue("activeSlot", m_activeSlot);
#endif
    saveHistory();
    update();
}

void FtWindow::onReloadImage()
{
#ifdef __EMSCRIPTEN__
    if (m_imagePath.isEmpty()) return;

    // Start reload-button progress animation
    m_reloadProgress = 0.0;
    if (!m_reloadAnimTimer) {
        m_reloadAnimTimer = new QTimer(this);
        m_reloadAnimTimer->setInterval(80);
        connect(m_reloadAnimTimer, &QTimer::timeout, this, [this]() {
            m_reloadProgress = std::min(m_reloadProgress + 0.03, 0.95);
            int pct = static_cast<int>(m_reloadProgress * 100);
            m_reloadBtn->setStyleSheet(
                QString("QPushButton { background: qlineargradient("
                        "x1:0,y1:0,x2:1,y2:0,"
                        "stop:0 rgb(180,210,255), stop:%1 rgb(180,210,255),"
                        "stop:%2 white, stop:1 white); }")
                .arg(pct / 100.0, 0, 'f', 3)
                .arg((pct + 1) / 100.0, 0, 'f', 3));
        });
    }
    m_reloadAnimTimer->start();

    fetchAndLoadImage(m_imagePath);
#else
    if (m_imagePath.isEmpty() || !QFile::exists(m_imagePath)) return;

    qDebug() << "Reloading image:" << m_imagePath;

    if (m_imagePath.endsWith(".mrc", Qt::CaseInsensitive)) {
        MrcResult r = loadMrc(m_imagePath);
        m_image = r.image;
        m_imageRawPixels = std::move(r.rawPixels);
        m_imageMinVal = r.minVal;
        m_imageMaxVal = r.maxVal;
        m_imageDispMin = r.minVal;
        m_imageDispMax = r.maxVal;
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
    m_modeBtn->setText(modeLabel());
    m_modeBtn->hide();
    m_maskBtn->hide();

    if (!m_image.isNull()) {
        m_zoom[0].reset(m_image.width(), m_image.height());
        computeFFT();
    }
    update();
#endif // !__EMSCRIPTEN__
}

void FtWindow::onCycleMode()
{
    m_displayMode = (m_displayMode + 1) % 4;
    m_modeBtn->setText(modeLabel());
#ifndef __EMSCRIPTEN__
    QSettings settings("ft", "ft");
    settings.setValue("displayMode", m_displayMode);
#endif
    update();
}

void FtWindow::onToggleFullscreen()
{
#ifdef __EMSCRIPTEN__
    // Use the browser Fullscreen API via JavaScript
    bool isFS = EM_ASM_INT({
        return document.fullscreenElement ? 1 : 0;
    });
    if (isFS) {
        EM_ASM({ document.exitFullscreen(); });
        m_fullscreenBtn->setText("Go fullscreen");
    } else {
        EM_ASM({ document.documentElement.requestFullscreen(); });
        m_fullscreenBtn->setText("Leave fullscreen");
    }
#else
    if (isFullScreen()) {
        showNormal();
        m_fullscreenBtn->setText("Go fullscreen");
    } else {
        showFullScreen();
        m_fullscreenBtn->setText("Leave fullscreen");
    }
#endif
}

void FtWindow::onToggleMask(bool checked)
{
    m_maskCenter = checked;
#ifndef __EMSCRIPTEN__
    QSettings settings("ft", "ft");
    settings.setValue("maskCenter", checked);
#endif
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
        m_imageDispMin = r.minVal;
        m_imageDispMax = r.maxVal;
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

#ifndef __EMSCRIPTEN__
    QSettings settings("ft", "ft");
    settings.setValue("lastFile", path);
    settings.setValue("activeSlot", m_activeSlot);
#endif

    m_ftComputed = false;
    m_modeBtn->setText(modeLabel());
    m_modeBtn->hide();
    m_maskBtn->hide();

    if (!m_image.isNull()) {
        m_zoom[0].reset(m_image.width(), m_image.height());
        computeFFT();
        // Store power spec thumbnail now that FFT is done
        m_history[m_activeSlot].powerSpecImg = computePowerSpecMasked(m_image);
    }

    saveHistory();
    update();
}

void FtWindow::loadImageData(const QString &fileName, const QByteArray &fileData)
{
    qDebug() << "Loading image from data:" << fileName << "(" << fileData.size() << "bytes)";

    // If no slot is active, pick the first empty one (or last slot as fallback)
    if (m_activeSlot < 0) {
        m_activeSlot = HISTORY_SLOTS - 1;
        for (int i = 0; i < HISTORY_SLOTS; i++) {
            if (!m_history[i].occupied) { m_activeSlot = i; break; }
        }
    }

    if (fileName.endsWith(".mrc", Qt::CaseInsensitive)) {
        MrcResult r = loadMrcFromData(fileData);
        m_image = r.image;
        m_imageRawPixels = std::move(r.rawPixels);
        m_imageMinVal = r.minVal;
        m_imageMaxVal = r.maxVal;
        m_imageDispMin = r.minVal;
        m_imageDispMax = r.maxVal;
        m_pixelSize = r.pixelSize;

        if (m_image.isNull())
            qDebug() << "MRC load FAILED – image is null";
        else {
            qDebug() << "MRC load OK –" << m_image.width() << "x" << m_image.height();
            padImageToSquare();
        }
    } else {
        m_image.loadFromData(fileData);
        m_pixelSize = 1.0;
        if (m_image.isNull()) {
            qDebug() << "Image load FAILED for:" << fileName;
        } else {
            qDebug() << "Image loaded:" << m_image.width() << "x" << m_image.height()
                     << "format:" << m_image.format();
            padImageToSquare();
            extractImageData();
        }
    }

    m_imagePath = fileName;

    // Store in the active slot
    if (!m_image.isNull()) {
        m_history[m_activeSlot].image        = m_image;
        m_history[m_activeSlot].path         = fileName;
        m_history[m_activeSlot].rawPixels    = m_imageRawPixels;
        m_history[m_activeSlot].minVal       = m_imageMinVal;
        m_history[m_activeSlot].maxVal       = m_imageMaxVal;
        m_history[m_activeSlot].pixelSize    = m_pixelSize;
        m_history[m_activeSlot].occupied     = true;
    }

    m_ftComputed = false;
    m_modeBtn->setText(modeLabel());
    m_modeBtn->hide();
    m_maskBtn->hide();

    if (!m_image.isNull()) {
        m_zoom[0].reset(m_image.width(), m_image.height());
        computeFFT();
        m_history[m_activeSlot].powerSpecImg = computePowerSpecMasked(m_image);
    }

    saveHistory();
    update();
}

#ifdef __EMSCRIPTEN__
void FtWindow::fetchAndLoadImage(const QString &relativePath)
{
    struct FetchCtx {
        FtWindow *self;
        QString path;
    };
    FetchCtx *ctx = new FetchCtx{this, relativePath};

    m_loadingImage = true;
    update();

    QString urlStr = QStringLiteral("images/") + relativePath;
    qDebug() << "Fetching:" << urlStr;

    emscripten_async_wget_data(
        urlStr.toUtf8().constData(),
        ctx,
        // onload
        [](void *arg, void *buf, int sz) {
            FetchCtx *c = static_cast<FetchCtx *>(arg);
            qDebug() << "Fetched" << sz << "bytes for" << c->path;
            QByteArray data(static_cast<const char *>(buf), sz);
            c->self->m_loadingImage = false;
            if (c->self->m_reloadAnimTimer) c->self->m_reloadAnimTimer->stop();
            c->self->m_reloadBtn->setStyleSheet("");
            c->self->m_reloadProgress = -1;
            c->self->loadImageData(c->path, data);
            delete c;
        },
        // onerror
        [](void *arg) {
            FetchCtx *c = static_cast<FetchCtx *>(arg);
            qWarning() << "Failed to fetch image:" << c->path;
            c->self->m_loadingImage = false;
            if (c->self->m_reloadAnimTimer) c->self->m_reloadAnimTimer->stop();
            c->self->m_reloadBtn->setStyleSheet("");
            c->self->m_reloadProgress = -1;
            c->self->update();
            delete c;
        }
    );
}
#endif

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
    m_imageDispMin = m_imageMinVal;
    m_imageDispMax = m_imageMaxVal;
}

// ---------------------------------------------------------------------------
//  FFT
// ---------------------------------------------------------------------------
void FtWindow::computeFFT()
{
    if (m_image.isNull()) return;

    m_fftProgress = 0.0;
    repaint();

    QImage gray = m_image.convertToFormat(QImage::Format_Grayscale8);
    int w = gray.width();
    int h = gray.height();
    int N = nextGoodFFTSize(std::max(w, h));
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

    // Data prepared – show a small slice of progress
    m_fftProgress = 0.02;
    repaint();

    {
#ifdef __EMSCRIPTEN__
        // Single-threaded FFT for WASM (no pthreads)
        std::vector<Complex> tmp(N);
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) tmp[x] = data[y * N + x];
            fft1d(tmp, false);
            for (int x = 0; x < N; x++) data[y * N + x] = tmp[x];
            if ((y & 31) == 0) {
                m_fftProgress = 0.02 + 0.48 * y / N;
                repaint();
                QApplication::processEvents();
            }
        }
        m_fftProgress = 0.5;
        repaint();
        QApplication::processEvents();

        for (int x = 0; x < N; x++) {
            for (int y = 0; y < N; y++) tmp[y] = data[y * N + x];
            fft1d(tmp, false);
            for (int y = 0; y < N; y++) data[y * N + x] = tmp[y];
            if ((x & 31) == 0) {
                m_fftProgress = 0.5 + 0.48 * x / N;
                repaint();
                QApplication::processEvents();
            }
        }
        m_fftProgress = 1.0;
        repaint();
        QApplication::processEvents();
#else
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
#endif
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
    repaint();

    {
#ifdef __EMSCRIPTEN__
        // Single-threaded inverse FFT for WASM
        std::vector<Complex> tmp(N);
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) tmp[x] = data[y * N + x];
            fft1d(tmp, true);
            for (int x = 0; x < N; x++) data[y * N + x] = tmp[x];
            if ((y & 31) == 0) {
                m_iftProgress = 0.5 * y / N;
                repaint();
                QApplication::processEvents();
            }
        }
        m_iftProgress = 0.5;
        repaint();
        QApplication::processEvents();

        for (int x = 0; x < N; x++) {
            for (int y = 0; y < N; y++) tmp[y] = data[y * N + x];
            fft1d(tmp, true);
            for (int y = 0; y < N; y++) data[y * N + x] = tmp[y];
            if ((x & 31) == 0) {
                m_iftProgress = 0.5 + 0.5 * x / N;
                repaint();
                QApplication::processEvents();
            }
        }
        m_iftProgress = 1.0;
        repaint();
        QApplication::processEvents();
#else
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
#endif
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

    m_imageDispMin = m_imageMinVal;
    m_imageDispMax = m_imageMaxVal;

    // Sync the active history slot so tools (e.g. particle picking) see the updated map
    if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS) {
        m_history[m_activeSlot].image     = m_image;
        m_history[m_activeSlot].rawPixels = m_imageRawPixels;
        m_history[m_activeSlot].minVal    = m_imageMinVal;
        m_history[m_activeSlot].maxVal    = m_imageMaxVal;
        m_history[m_activeSlot].occupied  = true;
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

    // Initialize display ranges to global ranges
    m_cosDispMin = m_cosMin;     m_cosDispMax = m_cosMax;
    m_sinDispMin = m_sinMin;     m_sinDispMax = m_sinMax;
    m_ampDispMin = m_ampMin;     m_ampDispMax = m_ampMax;
    m_phaseDispMin = m_phaseMin; m_phaseDispMax = m_phaseMax;
    m_powerDispMin = m_powerMin; m_powerDispMax = m_powerMax;
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
    int N = nextGoodFFTSize(std::max(w, h));

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
#ifndef __EMSCRIPTEN__
    QSettings settings("ft", "ft");
    for (int i = 0; i < HISTORY_SLOTS; i++) {
        QString key = QString("history/%1").arg(i);
        if (m_history[i].occupied)
            settings.setValue(key, m_history[i].path);
        else
            settings.remove(key);
    }
    settings.setValue("activeSlot", m_activeSlot);
#endif
}

void FtWindow::restoreHistory()
{
#ifdef __EMSCRIPTEN__
    // In WASM there is no filesystem to restore history from
    return;
#else
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
#endif // !__EMSCRIPTEN__
}

FtWindow::BufferSnapshot FtWindow::captureCurrentState() const
{
    BufferSnapshot snapshot;
    snapshot.valid = true;
    snapshot.activeSlot = m_activeSlot;
    for (int i = 0; i < HISTORY_SLOTS; i++)
        snapshot.history[i] = m_history[i];
    snapshot.image = m_image;
    snapshot.imagePath = m_imagePath;
    snapshot.imageRawPixels = m_imageRawPixels;
    snapshot.imageMinVal = m_imageMinVal;
    snapshot.imageMaxVal = m_imageMaxVal;
    snapshot.imageDispMin = m_imageDispMin;
    snapshot.imageDispMax = m_imageDispMax;
    snapshot.pixelSize = m_pixelSize;
    snapshot.ftComputed = m_ftComputed;
    snapshot.fftN = m_fftN;
    snapshot.origW = m_origW;
    snapshot.origH = m_origH;
    snapshot.fftData = m_fftData;
    return snapshot;
}

void FtWindow::applySnapshot(const BufferSnapshot &snapshot)
{
    if (!snapshot.valid) return;

    m_activeSlot = snapshot.activeSlot;
    for (int i = 0; i < HISTORY_SLOTS; i++)
        m_history[i] = snapshot.history[i];
    m_image = snapshot.image;
    m_imagePath = snapshot.imagePath;
    m_imageRawPixels = snapshot.imageRawPixels;
    m_imageMinVal = snapshot.imageMinVal;
    m_imageMaxVal = snapshot.imageMaxVal;
    m_imageDispMin = snapshot.imageDispMin;
    m_imageDispMax = snapshot.imageDispMax;
    m_pixelSize = snapshot.pixelSize;
    m_ftComputed = snapshot.ftComputed;
    m_fftN = snapshot.fftN;
    m_origW = snapshot.origW;
    m_origH = snapshot.origH;
    m_fftData = snapshot.fftData;

    if (!m_image.isNull())
        m_zoom[0].reset(m_image.width(), m_image.height());
    if (m_ftComputed && m_fftN > 0) {
        m_zoom[1].reset(m_fftN, m_fftN);
        m_zoom[2].reset(m_fftN, m_fftN);
        recomputeDisplayImages();
    }

    saveHistory();
    update();
}

void FtWindow::clearRedoSnapshot()
{
    m_redoSnapshot = BufferSnapshot();
    m_showRedo = false;
    updateUndoButton();
}

void FtWindow::storeUndoSnapshot()
{
    m_undoSnapshot = captureCurrentState();
    clearRedoSnapshot();
    updateUndoButton();
}

void FtWindow::updateUndoButton()
{
    if (!m_undoBtn) return;
    m_undoBtn->setText(m_showRedo ? "Redo last action" : "Undo last action");
    bool enabled = m_showRedo ? m_redoSnapshot.valid : m_undoSnapshot.valid;
    m_undoBtn->setEnabled(enabled);
}

void FtWindow::onUndoRedo()
{
    if (!m_showRedo) {
        if (!m_undoSnapshot.valid) return;
        m_redoSnapshot = captureCurrentState();
        applySnapshot(m_undoSnapshot);
        m_showRedo = true;
    } else {
        if (!m_redoSnapshot.valid) return;
        m_undoSnapshot = captureCurrentState();
        applySnapshot(m_redoSnapshot);
        m_showRedo = false;
    }
    updateUndoButton();
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
    m_imageDispMin = m_imageMinVal;
    m_imageDispMax = m_imageMaxVal;
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

void FtWindow::rebuildImageWithLUT()
{
    int w = m_image.width(), h = m_image.height();
    if (m_imageRawPixels.empty() || (int)m_imageRawPixels.size() != w * h) return;

    double dmin = m_imageDispMin, dmax = m_imageDispMax;
    double range = dmax - dmin;
    double scale = (range > 0) ? 255.0 / range : 1.0;

    m_image = QImage(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; y++) {
        uchar *row = m_image.scanLine(y);
        for (int x = 0; x < w; x++)
            row[x] = static_cast<uchar>(std::clamp(
                (m_imageRawPixels[y * w + x] - dmin) * scale, 0.0, 255.0));
    }

    // Also update the history slot
    if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS)
        m_history[m_activeSlot].image = m_image;
}

void FtWindow::rebuildFTImageWithLUT(int which)
{
    int N = m_fftN;
    if (N == 0) return;

    auto rebuild = [&](const std::vector<double> &vals, QImage &img, double dmin, double dmax) {
        double range = dmax - dmin;
        double scale = (range > 0) ? 255.0 / range : 1.0;
        img = QImage(N, N, QImage::Format_Grayscale8);
        for (int y = 0; y < N; y++) {
            uchar *row = img.scanLine(y);
            for (int x = 0; x < N; x++)
                row[x] = static_cast<uchar>(std::clamp(
                    (vals[y * N + x] - dmin) * scale, 0.0, 255.0));
        }
    };

    switch (which) {
    case HIST_POWER:
        rebuild(m_powerVals, m_powerImg, m_powerDispMin, m_powerDispMax);
        break;
    case HIST_FT_LEFT:
        if (m_displayMode == 0)
            rebuild(m_cosVals, m_cosImg, m_cosDispMin, m_cosDispMax);
        else if (m_displayMode == 1)
            rebuild(m_ampVals, m_ampImg, m_ampDispMin, m_ampDispMax);
        break;
    case HIST_FT_RIGHT:
        if (m_displayMode == 0)
            rebuild(m_sinVals, m_sinImg, m_sinDispMin, m_sinDispMax);
        else if (m_displayMode == 1)
            rebuild(m_phaseVals, m_phaseImg, m_phaseDispMin, m_phaseDispMax);
        break;
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
    storeUndoSnapshot();

    int N = m_fftN;
    int half = N / 2;
    double innerR = m_bandInnerR * half;
    double outerR = m_bandOuterR * half;
    int smooth = m_smoothEdit->text().toInt();
    if (smooth < 0) smooth = 0;
    bool eraseOutside = m_bandEraseOutside->isChecked();

    m_toolProgress = 0.1;
    update();

    chainSteps({
        [this, N, half, innerR, outerR, smooth, eraseOutside]() {
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
            m_toolProgress = 0.5;
        },
        [this]() {
            recomputeDisplayImages();
            computeInverseFFT();
            m_toolProgress = -1;
            update();
        }
    });
}

void FtWindow::onApplyLattice()
{
    if (!m_ftComputed || m_fftN == 0) return;
    storeUndoSnapshot();

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

    double invUx =  vy / det, invUy = -vx / det;
    double invVx = -uy / det, invVy =  ux / det;

    m_toolProgress = 0.1;
    update();

    chainSteps({
        [this, N, half, dotR, smooth, eraseOutside, ux, uy, vx, vy, invUx, invUy, invVx, invVy]() {
            for (int y = 0; y < N; y++) {
                for (int x = 0; x < N; x++) {
                    double dx = x - half;
                    double dy = y - half;

                    double fi = dx * invUx + dy * invUy;
                    double fj = dx * invVx + dy * invVy;
                    int i0 = (int)std::floor(fi);
                    int j0 = (int)std::floor(fj);

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
                        if (minDist > dotR) {
                            double d = minDist - dotR;
                            factor = (smooth > 0 && d < smooth) ? (1.0 - d / smooth) : 0.0;
                        }
                    } else {
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
            m_toolProgress = 0.5;
        },
        [this]() {
            recomputeDisplayImages();
            computeInverseFFT();
            m_toolProgress = -1;
            update();
        }
    });
}

void FtWindow::onApplyBinning()
{
    if (m_image.isNull()) return;
    storeUndoSnapshot();

    int binFactor = m_binCombo->currentData().toInt();
    if (binFactor <= 1) return;

    int w = m_image.width();
    int h = m_image.height();

    std::vector<double> &pix = m_imageRawPixels;
    if ((int)pix.size() != w * h) return;

    bool keepSize = m_binKeepSizeBtn->isChecked();

    m_toolProgress = 0.1;
    update();

    chainSteps({
        [this, binFactor, w, h, keepSize]() {
            std::vector<double> &pix = m_imageRawPixels;
            if (keepSize) {
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
            m_toolProgress = 0.5;
        },
        [this]() {
            if (m_ftComputed)
                computeFFT();
            m_toolProgress = -1;
            update();
        }
    });
}

void FtWindow::chainSteps(std::vector<std::function<void()>> steps)
{
#ifdef __EMSCRIPTEN__
    if (steps.empty()) return;
    auto q = std::make_shared<std::vector<std::function<void()>>>(std::move(steps));
    auto run = std::make_shared<std::function<void()>>();
    *run = [this, q, run]() {
        if (q->empty()) return;
        auto fn = std::move(q->front());
        q->erase(q->begin());
        fn();
        update();
        if (!q->empty())
            QTimer::singleShot(20, this, [run]() { (*run)(); });
    };
    QTimer::singleShot(50, this, [run]() { (*run)(); });
#else
    for (auto &fn : steps) {
        fn();
        repaint();
    }
#endif
}

void FtWindow::onInvertContrast()
{
    if (m_image.isNull()) return;
    storeUndoSnapshot();

    int w = m_image.width();
    int h = m_image.height();
    if (m_imageRawPixels.empty() || (int)m_imageRawPixels.size() != w * h) return;

    m_invertProgress = 0.1;
    update();

    chainSteps({
        [this]() {
            double sum = m_imageMinVal + m_imageMaxVal;
            for (double &v : m_imageRawPixels)
                v = sum - v;
            m_invertProgress = 0.3;
        },
        [this]() {
            rebuildImageFromRaw();
            m_invertProgress = 0.5;
        },
        [this]() {
            if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS) {
                m_history[m_activeSlot].image     = m_image;
                m_history[m_activeSlot].rawPixels = m_imageRawPixels;
                m_history[m_activeSlot].minVal    = m_imageMinVal;
                m_history[m_activeSlot].maxVal    = m_imageMaxVal;
                m_history[m_activeSlot].occupied  = true;
            }
            if (m_ftComputed) {
                // Optimisation: inverting pixel values (v → 255−v in 8-bit)
                // negates every Fourier coefficient except DC.
                // DC_new = 255·N² − DC_old.  O(N²) instead of O(N² log N).
                int N = m_fftN;
                int half = N / 2;
                Complex oldDC = m_fftData[half * N + half];
                for (auto &c : m_fftData) c = -c;
                m_fftData[half * N + half] =
                    Complex(255.0 * (double)N * N, 0.0) - oldDC;
                recomputeDisplayImages();
            }
            saveHistory();
            m_invertProgress = -1;
        }
    });
}

void FtWindow::onApplyEdgeTaper()
{
    if (m_image.isNull()) return;
    storeUndoSnapshot();

    int w = m_image.width();
    int h = m_image.height();
    if ((int)m_imageRawPixels.size() != w * h) return;

    bool ok = false;
    double taperWidth = m_p1TaperWidthEdit->text().toDouble(&ok);
    if (!ok || taperWidth <= 0.0) return;

    double maxWidth = std::max(1.0, std::min(w, h) / 2.0 - 1.0);
    taperWidth = std::clamp(taperWidth, 1.0, maxWidth);

    double edgeSum = 0.0;
    int edgeCount = 0;
    for (int x = 0; x < w; x++) {
        edgeSum += m_imageRawPixels[x];
        edgeSum += m_imageRawPixels[(h - 1) * w + x];
        edgeCount += 2;
    }
    for (int y = 1; y < h - 1; y++) {
        edgeSum += m_imageRawPixels[y * w];
        edgeSum += m_imageRawPixels[y * w + (w - 1)];
        edgeCount += 2;
    }
    if (edgeCount == 0) return;

    double edgeAvg = edgeSum / edgeCount;

    m_toolProgress = 0.1;
    update();

    chainSteps({
        [this, w, h, taperWidth, edgeAvg]() {
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    double dist = std::min({
                        static_cast<double>(x),
                        static_cast<double>(w - 1 - x),
                        static_cast<double>(y),
                        static_cast<double>(h - 1 - y)
                    });
                    if (dist >= taperWidth) continue;

                    double phase = (taperWidth - dist) / taperWidth;
                    double keep = 0.5 * (1.0 + std::cos(M_PI * phase));
                    double &pix = m_imageRawPixels[y * w + x];
                    pix = pix * keep + edgeAvg * (1.0 - keep);
                }
            }
            rebuildImageFromRaw();
            m_toolProgress = 0.5;
        },
        [this]() {
            if (m_ftComputed)
                computeFFT();

            if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS) {
                m_history[m_activeSlot].image = m_image;
                m_history[m_activeSlot].rawPixels = m_imageRawPixels;
                m_history[m_activeSlot].minVal = m_imageMinVal;
                m_history[m_activeSlot].maxVal = m_imageMaxVal;
                m_history[m_activeSlot].pixelSize = m_pixelSize;
                m_history[m_activeSlot].occupied = true;
                if (m_ftComputed)
                    m_history[m_activeSlot].powerSpecImg = computePowerSpecMasked(m_image);
            }

            saveHistory();
            m_toolProgress = -1;
            update();
        }
    });
}

void FtWindow::onApplyFtCrop()
{
    if (!m_ftComputed || m_fftN == 0) return;
    storeUndoSnapshot();

    int factor = m_ftCropCombo->currentData().toInt();
    if (factor <= 1) return;

    int N = m_fftN;
    int half = N / 2;
    int cropHalf = half / factor;
    bool keepSize = m_ftCropKeepSizeBtn->isChecked();

    m_toolProgress = 0.1;
    update();

    chainSteps({
        [this, N, half, cropHalf, keepSize]() mutable {
            if (keepSize) {
                for (int y = 0; y < N; y++) {
                    for (int x = 0; x < N; x++) {
                        int dx = std::abs(x - half);
                        int dy = std::abs(y - half);
                        if (dx > cropHalf || dy > cropHalf)
                            m_fftData[y * N + x] = Complex(0.0, 0.0);
                    }
                }
            } else {
                int newN = cropHalf * 2;
                if (newN < 2) newN = 2;
                newN = nextGoodFFTSize(newN);
                cropHalf = newN / 2;

                std::vector<Complex> newData(newN * newN, Complex(0.0, 0.0));
                for (int y = 0; y < newN; y++) {
                    for (int x = 0; x < newN; x++) {
                        int srcX = half - cropHalf + x;
                        int srcY = half - cropHalf + y;
                        if (srcX >= 0 && srcX < N && srcY >= 0 && srcY < N)
                            newData[y * newN + x] = m_fftData[srcY * N + srcX];
                    }
                }

                m_fftData = newData;
                m_fftN = newN;
                m_origW = newN;
                m_origH = newN;
                m_zoom[1].reset(newN, newN);
                m_zoom[2].reset(newN, newN);
            }
            m_toolProgress = 0.5;
        },
        [this]() {
            recomputeDisplayImages();
            computeInverseFFT();
            m_toolProgress = -1;
            update();
        }
    });
}

void FtWindow::onApplyDirectional()
{
    if (!m_ftComputed || m_fftN == 0) return;
    storeUndoSnapshot();

    int N = m_fftN;
    int half = N / 2;
    int smooth = m_smoothEdit->text().toInt();
    if (smooth < 0) smooth = 0;
    bool eraseOutside = m_bandEraseOutside->isChecked();

    double a1 = m_dirAngle1, a2 = m_dirAngle2;
    while (a2 < a1) a2 += 360;
    if (a2 - a1 > 180) { std::swap(a1, a2); while (a2 < a1) a2 += 360; }

    m_toolProgress = 0.1;
    update();

    chainSteps({
        [this, N, half, smooth, eraseOutside, a1, a2]() {
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
            m_toolProgress = 0.5;
        },
        [this]() {
            recomputeDisplayImages();
            computeInverseFFT();
            m_toolProgress = -1;
            update();
        }
    });
}

void FtWindow::onApplyLineFilter()
{
    if (!m_ftComputed || m_fftN == 0) return;
    storeUndoSnapshot();

    bool okWidth = false;
    bool okAngle = false;
    double lineWidth = m_lineWidthEdit->text().toDouble(&okWidth);
    double angleDeg = m_lineDirectionEdit->text().toDouble(&okAngle);
    if (!okWidth || lineWidth <= 0.0) return;
    if (!okAngle) angleDeg = 0.0;

    double halfWidth = lineWidth / 2.0;
    double angle = angleDeg * M_PI / 180.0;
    double normX = -std::sin(angle);
    double normY =  std::cos(angle);
    double imgCenter = m_fftN / 2.0 + 0.5;
    bool eraseOutside = m_lineEraseOutsideBtn->isChecked();
    int N = m_fftN;
    double lineOff = m_lineOffset;

    m_toolProgress = 0.1;
    update();

    chainSteps({
        [this, N, halfWidth, normX, normY, imgCenter, eraseOutside, lineOff]() {
            for (int y = 0; y < N; y++) {
                for (int x = 0; x < N; x++) {
                    double relX = x - imgCenter;
                    double relY = y - imgCenter;
                    double dist = std::abs(relX * normX + relY * normY - lineOff);
                    bool inside = dist <= halfWidth;
                    if ((eraseOutside && !inside) || (!eraseOutside && inside))
                        m_fftData[y * N + x] = Complex(0.0, 0.0);
                }
            }
            m_toolProgress = 0.5;
        },
        [this]() {
            recomputeDisplayImages();
            computeInverseFFT();
            m_toolProgress = -1;
            update();
        }
    });
}

// ---------------------------------------------------------------------------
//  Math calculations
// ---------------------------------------------------------------------------
void FtWindow::onFtMathCancel()
{
    m_ftMathActive = false;
    m_ftMathOutCombo->hide();
    m_ftMathEqualsLabel->hide();
    m_ftMathIn1Combo->hide();
    m_ftMathOpCombo->hide();
    m_ftMathIn2Combo->hide();
    m_ftMathConjCombo->hide();
    m_ftMathCancelBtn->hide();
    m_ftMathComputeBtn->hide();
    update();
}

void FtWindow::onFtMathCompute()
{
    storeUndoSnapshot();

    int outIdx = m_ftMathOutCombo->currentIndex();
    int in1Idx = m_ftMathIn1Combo->currentIndex();
    int in2Idx = m_ftMathIn2Combo->currentIndex();
    int opIdx  = m_ftMathOpCombo->currentIndex();   // 0=+, 1=-, 2=*, 3=/
    bool conjugate = (m_ftMathConjCombo->currentIndex() == 1);

#ifndef __EMSCRIPTEN__
    {
        QSettings settings("ft", "ft");
        settings.setValue("ftMathOutIdx",  outIdx);
        settings.setValue("ftMathIn1Idx",  in1Idx);
        settings.setValue("ftMathOpIdx",   opIdx);
        settings.setValue("ftMathIn2Idx",  in2Idx);
        settings.setValue("ftMathConjIdx", conjugate ? 1 : 0);
    }
#endif

    // Helper: get raw pixels from a slot
    auto getSlotPixels = [&](int idx, int &w, int &h) -> std::vector<double> {
        if (idx == m_activeSlot && !m_image.isNull()) {
            w = m_image.width(); h = m_image.height();
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

    // Safety: rebuild rawPixels from QImage if sizes don't match
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

    // Compute FFT for a slot's raw pixels, returning shifted FFT and its N
    auto computeSlotFFT = [](const std::vector<double> &pixels, int w, int h) {
        int S = std::max(w, h);
        int N = nextGoodFFTSize(S);
        double sum = 0;
        for (auto v : pixels) sum += v;
        double avg = pixels.empty() ? 0.0 : sum / pixels.size();
        std::vector<Complex> data(N * N, Complex(avg, 0.0));
        // Center the image in the NxN grid
        int offX = (N - w) / 2, offY = (N - h) / 2;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                data[(y + offY) * N + (x + offX)] = Complex(pixels[y * w + x], 0.0);
        // Row FFTs
        std::vector<Complex> row(N);
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) row[x] = data[y * N + x];
            fft1d(row, false);
            for (int x = 0; x < N; x++) data[y * N + x] = row[x];
        }
        // Column FFTs
        std::vector<Complex> col(N);
        for (int x = 0; x < N; x++) {
            for (int y = 0; y < N; y++) col[y] = data[y * N + x];
            fft1d(col, false);
            for (int y = 0; y < N; y++) data[y * N + x] = col[y];
        }
        fftShift(data, N);
        return std::make_pair(std::move(data), N);
    };

    m_ftMathProgress = 0.0;
    update();

    struct FtMathWork {
        std::vector<double> pix1, pix2;
        int w1, h1, w2, h2;
        int outIdx, in1Idx, in2Idx, opIdx;
        bool conjugate;
        std::vector<Complex> fft1data, fft2data, result;
        int N1 = 0, N2 = 0, N = 0;
    };
    auto st = std::make_shared<FtMathWork>();
    st->pix1 = std::move(pix1); st->pix2 = std::move(pix2);
    st->w1 = w1; st->h1 = h1; st->w2 = w2; st->h2 = h2;
    st->outIdx = outIdx; st->in1Idx = in1Idx; st->in2Idx = in2Idx;
    st->opIdx = opIdx; st->conjugate = conjugate;

    chainSteps({
        // Stage 1: FFT of input 1
        [this, st, computeSlotFFT]() {
            auto [d, n] = computeSlotFFT(st->pix1, st->w1, st->h1);
            st->fft1data = std::move(d); st->N1 = n;
            m_ftMathProgress = 0.3;
        },
        // Stage 2: FFT of input 2
        [this, st, computeSlotFFT]() {
            auto [d, n] = computeSlotFFT(st->pix2, st->w2, st->h2);
            st->fft2data = std::move(d); st->N2 = n;
            m_ftMathProgress = 0.6;
        },
        // Stage 3: zero-pad, conjugate, operation
        [this, st]() {
            st->N = std::max(st->N1, st->N2);
            int N = st->N;
            // Zero-pad smaller FFT
            auto zeroPadFFT = [](const std::vector<Complex> &src, int srcN, int dstN) {
                if (srcN == dstN) return src;
                std::vector<Complex> dst(dstN * dstN, Complex(0.0, 0.0));
                int off = (dstN - srcN) / 2;
                for (int y = 0; y < srcN; y++)
                    for (int x = 0; x < srcN; x++)
                        dst[(y + off) * dstN + (x + off)] = src[y * srcN + x];
                double scale = (double)(dstN * dstN) / (double)(srcN * srcN);
                for (auto &c : dst) c *= scale;
                return dst;
            };
            if (st->N1 < N) st->fft1data = zeroPadFFT(st->fft1data, st->N1, N);
            if (st->N2 < N) st->fft2data = zeroPadFFT(st->fft2data, st->N2, N);
            if (st->conjugate)
                for (auto &c : st->fft2data) c = std::conj(c);
            // Perform operation
            st->result.resize(N * N);
            if (st->opIdx == 3) {
                double maxAmp = 0;
                for (auto &c : st->fft2data)
                    maxAmp = std::max(maxAmp, std::abs(c));
                double noise = std::max(maxAmp * maxAmp / 10000.0, 1e-10);
                for (int i = 0; i < N * N; i++) {
                    double denom = std::norm(st->fft2data[i]) + noise;
                    st->result[i] = st->fft1data[i] * std::conj(st->fft2data[i]) / denom;
                }
            } else {
                for (int i = 0; i < N * N; i++) {
                    switch (st->opIdx) {
                    case 0: st->result[i] = st->fft1data[i] + st->fft2data[i]; break;
                    case 1: st->result[i] = st->fft1data[i] - st->fft2data[i]; break;
                    case 2: st->result[i] = st->fft1data[i] * st->fft2data[i]; break;
                    }
                }
            }
            st->fft1data.clear(); st->fft2data.clear();
            m_ftMathProgress = 0.7;
        },
        // Stage 4: inverse FFT rows
        [this, st]() {
            int N = st->N;
            fftShift(st->result, N);
            std::vector<Complex> row(N);
            for (int y = 0; y < N; y++) {
                for (int x = 0; x < N; x++) row[x] = st->result[y * N + x];
                fft1d(row, true);
                for (int x = 0; x < N; x++) st->result[y * N + x] = row[x];
            }
            m_ftMathProgress = 0.85;
        },
        // Stage 5: inverse FFT cols
        [this, st]() {
            int N = st->N;
            std::vector<Complex> col(N);
            for (int x = 0; x < N; x++) {
                for (int y = 0; y < N; y++) col[y] = st->result[y * N + x];
                fft1d(col, true);
                for (int y = 0; y < N; y++) st->result[y * N + x] = col[y];
            }
            m_ftMathProgress = 0.95;
        },
        // Stage 6: build output, save to slot
        [this, st]() {
            int N = st->N;
            int halfN = N / 2;
            int outS = N;
            std::vector<double> realResult(outS * outS);
            if (st->opIdx == 2 || st->opIdx == 3) {
                for (int y = 0; y < outS; y++)
                    for (int x = 0; x < outS; x++) {
                        int sy = (y + halfN) % N;
                        int sx = (x + halfN) % N;
                        realResult[y * outS + x] = st->result[sy * N + sx].real();
                    }
            } else {
                for (int y = 0; y < outS; y++)
                    for (int x = 0; x < outS; x++)
                        realResult[y * outS + x] = st->result[y * N + x].real();
            }
            st->result.clear();

            double minVal = *std::min_element(realResult.begin(), realResult.end());
            double maxVal = *std::max_element(realResult.begin(), realResult.end());
            double range  = maxVal - minVal;
            double scale  = (range > 0) ? 255.0 / range : 1.0;

            QImage outImg(outS, outS, QImage::Format_Grayscale8);
            for (int y = 0; y < outS; y++) {
                uchar *rw = outImg.scanLine(y);
                for (int x = 0; x < outS; x++)
                    rw[x] = static_cast<uchar>(std::clamp(
                        (realResult[y * outS + x] - minVal) * scale, 0.0, 255.0));
            }

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

            int outIdx = st->outIdx;
            m_history[outIdx].image     = outImg;
            m_history[outIdx].path      = QString("ftmath: %1 %2 %3")
                                              .arg(QChar('A' + st->in1Idx))
                                              .arg(m_ftMathOpCombo->currentText())
                                              .arg(QChar('A' + st->in2Idx));
            m_history[outIdx].rawPixels = std::move(realResult);
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
            m_modeBtn->setText(modeLabel());
            m_modeBtn->hide();
            m_maskBtn->hide();

            computeFFT();
            m_history[outIdx].powerSpecImg = computePowerSpecMasked(m_image);
            saveHistory();

            m_ftMathProgress = -1;
            onFtMathCancel();
        }
    });
}

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
    storeUndoSnapshot();

    int outIdx  = m_mathOutCombo->currentIndex();
    int in1Idx  = m_mathIn1Combo->currentIndex();
    int in2Idx  = m_mathIn2Combo->currentIndex();
    int opIdx   = m_mathOpCombo->currentIndex();  // 0=+, 1=-, 2=*, 3=/, 4=conv, 5=corr

#ifndef __EMSCRIPTEN__
    {
        QSettings settings("ft", "ft");
        settings.setValue("mathOutIdx", outIdx);
        settings.setValue("mathIn1Idx", in1Idx);
        settings.setValue("mathOpIdx",  opIdx);
        settings.setValue("mathIn2Idx", in2Idx);
    }
#endif

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

    m_mathProgress = 0.1;
    update();

    struct MathWork {
        std::vector<double> pix1, pix2;
        int w1, h1, w2, h2;
        int outIdx, in1Idx, in2Idx, opIdx;
    };
    auto st = std::make_shared<MathWork>();
    st->pix1 = std::move(pix1); st->pix2 = std::move(pix2);
    st->w1 = w1; st->h1 = h1; st->w2 = w2; st->h2 = h2;
    st->outIdx = outIdx; st->in1Idx = in1Idx; st->in2Idx = in2Idx; st->opIdx = opIdx;

    // Helper lambdas (stateless, captured by value)
    auto zeroPadToSquareFn = [](std::vector<double> &src, int &sw, int &sh) {
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
    auto scaleToSizeFn = [](const std::vector<double> &src, int sw, int S) {
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

    // Lambda to save result to slot and finish
    auto finishMath = [this](std::shared_ptr<MathWork> st,
                             std::vector<double> result, int S) {
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
        int outIdx = st->outIdx;
        m_history[outIdx].image     = outImg;
        m_history[outIdx].path      = QString("math: %1 %2 %3")
                                          .arg(QChar('a' + st->in1Idx))
                                          .arg(m_mathOpCombo->currentText())
                                          .arg(QChar('a' + st->in2Idx));
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
        m_modeBtn->setText(modeLabel());
        m_modeBtn->hide();
        m_maskBtn->hide();

        computeFFT();
        m_history[outIdx].powerSpecImg = computePowerSpecMasked(m_image);
        saveHistory();
        m_mathProgress = -1;
        onMathCancel();
    };

    if (opIdx >= 4) {
        // Convolution/correlation path
        struct ConvWork {
            std::vector<Complex> fa, fb, fc;
            std::vector<double> a, b;
            int S = 0, N = 0;
        };
        auto cw = std::make_shared<ConvWork>();

        chainSteps({
            // Stage 1: prep data
            [this, st, cw, zeroPadToSquareFn, scaleToSizeFn]() mutable {
                double mean1 = 0, mean2 = 0;
                for (double v : st->pix1) mean1 += v;
                mean1 /= st->pix1.size();
                for (double v : st->pix2) mean2 += v;
                mean2 /= st->pix2.size();
                for (double &v : st->pix1) v -= mean1;
                for (double &v : st->pix2) v -= mean2;

                zeroPadToSquareFn(st->pix1, st->w1, st->h1);
                zeroPadToSquareFn(st->pix2, st->w2, st->h2);

                cw->S = std::max(st->w1, st->w2);
                cw->a = scaleToSizeFn(st->pix1, st->w1, cw->S);
                cw->b = scaleToSizeFn(st->pix2, st->w2, cw->S);
                st->pix1.clear(); st->pix2.clear();

                cw->N = nextGoodFFTSize(cw->S);
                int N = cw->N, S = cw->S;
                cw->fa.assign(N * N, Complex(0, 0));
                cw->fb.assign(N * N, Complex(0, 0));
                for (int y = 0; y < S; y++)
                    for (int x = 0; x < S; x++) {
                        cw->fa[y * N + x] = Complex(cw->a[y * S + x], 0);
                        cw->fb[y * N + x] = Complex(cw->b[y * S + x], 0);
                    }
                cw->a.clear(); cw->b.clear();
                m_mathProgress = 0.15;
            },
            // Stage 2: FFT of a
            [this, cw]() {
                fft2d(cw->fa, cw->N, false);
                m_mathProgress = 0.35;
            },
            // Stage 3: FFT of b
            [this, cw]() {
                fft2d(cw->fb, cw->N, false);
                m_mathProgress = 0.6;
            },
            // Stage 4: multiply + inverse FFT
            [this, st, cw]() {
                int N = cw->N;
                cw->fc.resize(N * N);
                for (int i = 0; i < N * N; i++) {
                    if (st->opIdx == 4)
                        cw->fc[i] = cw->fa[i] * cw->fb[i];
                    else
                        cw->fc[i] = cw->fa[i] * std::conj(cw->fb[i]);
                }
                cw->fa.clear(); cw->fb.clear();
                fft2d(cw->fc, N, true);
                m_mathProgress = 0.9;
            },
            // Stage 5: build result and finish
            [this, st, cw, finishMath]() {
                int S = cw->S, N = cw->N;
                int halfS = S / 2;
                std::vector<double> result(S * S);
                for (int y = 0; y < S; y++)
                    for (int x = 0; x < S; x++) {
                        int fy = (st->opIdx == 4)
                            ? (y + halfS) % N
                            : (y - halfS + N) % N;
                        int fx = (st->opIdx == 4)
                            ? (x + halfS) % N
                            : (x - halfS + N) % N;
                        result[y * S + x] = cw->fc[fy * N + fx].real();
                    }
                cw->fc.clear();
                finishMath(st, std::move(result), S);
            }
        });
    } else {
        // Arithmetic operations path (+, -, *, /)
        chainSteps({
            // Stage 1: pad, scale, compute
            [this, st, padToSquare, scaleToSizeFn]() mutable {
                padToSquare(st->pix1, st->w1, st->h1);
                padToSquare(st->pix2, st->w2, st->h2);
                int S = std::max(st->w1, st->w2);
                std::vector<double> a = scaleToSizeFn(st->pix1, st->w1, S);
                std::vector<double> b = scaleToSizeFn(st->pix2, st->w2, S);
                st->pix1.clear(); st->pix2.clear();

                // Store S in w1 for later use
                st->w1 = S;
                st->pix1.resize(S * S);

                double bMin = *std::min_element(b.begin(), b.end());
                double bMax = *std::max_element(b.begin(), b.end());
                double noise = std::max(std::abs((bMax - bMin) / 100.0), 1.0);

                for (int i = 0; i < S * S; i++) {
                    switch (st->opIdx) {
                    case 0: st->pix1[i] = a[i] + b[i]; break;
                    case 1: st->pix1[i] = a[i] - b[i]; break;
                    case 2: st->pix1[i] = a[i] * b[i]; break;
                    case 3: st->pix1[i] = a[i] * b[i] / (b[i] * b[i] + noise); break;
                    }
                }
                m_mathProgress = 0.7;
            },
            // Stage 2: finish
            [this, st, finishMath]() {
                int S = st->w1;
                finishMath(st, std::move(st->pix1), S);
            }
        });
    }
}

// ---------------------------------------------------------------------------
//  Particle picking – peak search with exclusion radius
// ---------------------------------------------------------------------------
void FtWindow::runPeakSearch()
{
    m_peaks.clear();

    // Use the selected source buffer for peak search
    int srcIdx = m_peakSourceCombo->currentIndex();
    if (srcIdx < 0 || srcIdx >= HISTORY_SLOTS || !m_history[srcIdx].occupied) return;

    const auto &src = m_history[srcIdx];
    int w = src.image.width();
    int h = src.image.height();
    int n = static_cast<int>(src.rawPixels.size());
    if (n != w * h || n == 0) return;

    double srcMin = src.minVal;
    double srcMax = src.maxVal;
    double threshold = srcMin + (srcMax - srcMin)
                       * m_peakThresholdSlider->value() / 1000.0;
    double exclR = m_peakExclRadiusSlider->value();
    double exclR2 = exclR * exclR;

    std::vector<bool> excluded(n, false);

    while (true) {
        // Find the highest non-excluded pixel above threshold
        double bestVal = threshold;
        int bestIdx = -1;
        for (int i = 0; i < n; i++) {
            if (!excluded[i] && src.rawPixels[i] > bestVal) {
                bestVal = src.rawPixels[i];
                bestIdx = i;
            }
        }
        if (bestIdx < 0) break;

        int px = bestIdx % w;
        int py = bestIdx / w;
        m_peaks.push_back({px, py});

        // Mark exclusion zone
        int r = static_cast<int>(std::ceil(exclR));
        int y0 = std::max(0, py - r);
        int y1 = std::min(h - 1, py + r);
        int x0 = std::max(0, px - r);
        int x1 = std::min(w - 1, px + r);
        for (int yy = y0; yy <= y1; yy++) {
            for (int xx = x0; xx <= x1; xx++) {
                double dx = xx - px;
                double dy = yy - py;
                if (dx * dx + dy * dy <= exclR2) {
                    excluded[yy * w + xx] = true;
                }
            }
        }
    }
}

void FtWindow::onPeakCancel()
{
    m_peakPickActive = false;
    m_peaks.clear();
    m_peakSourceCombo->hide();
    m_peakThresholdSlider->hide();
    m_peakThresholdLabel->hide();
    m_peakExclLabel->hide();
    m_peakExclRadiusSlider->hide();
    m_peakCancelBtn->hide();
    m_peakComputeBtn->hide();
    m_peakShowPosBtn->hide();
    update();
}

void FtWindow::onPeakCompute()
{
    // Refresh min/max of the selected source buffer before computing
    int srcIdx = m_peakSourceCombo->currentIndex();
    if (srcIdx >= 0 && srcIdx < HISTORY_SLOTS && m_history[srcIdx].occupied) {
        const auto &pix = m_history[srcIdx].rawPixels;
        if (!pix.empty()) {
            double mn = pix[0], mx = pix[0];
            for (size_t i = 1; i < pix.size(); i++) {
                if (pix[i] < mn) mn = pix[i];
                if (pix[i] > mx) mx = pix[i];
            }
            m_history[srcIdx].minVal = mn;
            m_history[srcIdx].maxVal = mx;
        }
    }
    runPeakSearch();
#ifndef __EMSCRIPTEN__
    {
        QSettings settings("ft", "ft");
        settings.setValue("peakSourceIdx", m_peakSourceCombo->currentIndex());
        settings.setValue("peakThreshold", m_peakThresholdSlider->value());
        settings.setValue("peakExclRadius", m_peakExclRadiusSlider->value());
    }
#endif
    update();
}

// ---------------------------------------------------------------------------
//  Extract particles – tile picked particles into a target buffer
// ---------------------------------------------------------------------------
void FtWindow::onExtractCancel()
{
    m_extractActive = false;
    m_extractSourceCombo->hide();
    m_extractTargetCombo->hide();
    m_extractSizeCombo->hide();
    m_extractCancelBtn->hide();
    m_extractComputeBtn->hide();
    update();
}

void FtWindow::onExtractCompute()
{
    if (m_peaks.empty()) return;

    int srcIdx = m_extractSourceCombo->currentIndex();
    int tgtIdx = m_extractTargetCombo->currentIndex();
    if (srcIdx < 0 || srcIdx >= HISTORY_SLOTS || !m_history[srcIdx].occupied) return;
    if (tgtIdx < 0 || tgtIdx >= HISTORY_SLOTS) return;

    int boxSize = m_extractSizeCombo->currentData().toInt();
    int tilesPerRow = 1024 / boxSize;   // 16 for 64, 8 for 128
    int maxParticles = tilesPerRow * tilesPerRow;  // 256 for 64, 64 for 128

    const auto &srcEntry = m_history[srcIdx];
    int srcW = srcEntry.image.width();
    int srcH = srcEntry.image.height();
    const auto &srcPix = srcEntry.rawPixels;

    // Create 1024x1024 black target image
    int outSize = 1024;
    std::vector<double> outPix(outSize * outSize, 0.0);

    int half = boxSize / 2;
    int nExtracted = std::min(static_cast<int>(m_peaks.size()), maxParticles);

    for (int p = 0; p < nExtracted; p++) {
        int col = p % tilesPerRow;
        int row = p / tilesPerRow;
        int tileX0 = col * boxSize;
        int tileY0 = row * boxSize;

        int cx = m_peaks[p].x;
        int cy = m_peaks[p].y;

        // First pass: compute average of valid (in-bounds) pixels
        double sum = 0.0;
        int count = 0;
        for (int dy = 0; dy < boxSize; dy++) {
            for (int dx = 0; dx < boxSize; dx++) {
                int sx = cx - half + dx;
                int sy = cy - half + dy;
                if (sx >= 0 && sx < srcW && sy >= 0 && sy < srcH) {
                    sum += srcPix[sy * srcW + sx];
                    count++;
                }
            }
        }
        double avg = (count > 0) ? sum / count : 0.0;

        // Second pass: extract pixels, fill out-of-bounds with average
        for (int dy = 0; dy < boxSize; dy++) {
            for (int dx = 0; dx < boxSize; dx++) {
                int sx = cx - half + dx;
                int sy = cy - half + dy;
                double val = avg;
                if (sx >= 0 && sx < srcW && sy >= 0 && sy < srcH)
                    val = srcPix[sy * srcW + sx];
                outPix[(tileY0 + dy) * outSize + (tileX0 + dx)] = val;
            }
        }
    }

    // Compute min/max
    double mn = outPix[0], mx = outPix[0];
    for (size_t i = 1; i < outPix.size(); i++) {
        if (outPix[i] < mn) mn = outPix[i];
        if (outPix[i] > mx) mx = outPix[i];
    }
    double range = mx - mn;
    double scale = (range > 0) ? 255.0 / range : 1.0;

    // Build QImage
    QImage outImg(outSize, outSize, QImage::Format_Grayscale8);
    for (int y = 0; y < outSize; y++) {
        uchar *row = outImg.scanLine(y);
        for (int x = 0; x < outSize; x++)
            row[x] = static_cast<uchar>(std::clamp(
                (outPix[y * outSize + x] - mn) * scale, 0.0, 255.0));
    }

    // Store into target history slot
    storeUndoSnapshot();
    m_history[tgtIdx].image     = outImg;
    m_history[tgtIdx].rawPixels = std::move(outPix);
    m_history[tgtIdx].minVal    = mn;
    m_history[tgtIdx].maxVal    = mx;
    m_history[tgtIdx].pixelSize = srcEntry.pixelSize;
    m_history[tgtIdx].path.clear();
    m_history[tgtIdx].occupied  = true;
    m_history[tgtIdx].powerSpecImg = computePowerSpecMasked(outImg);

    // Switch display to the target slot
    m_activeSlot     = tgtIdx;
    m_image          = m_history[tgtIdx].image;
    m_imagePath.clear();
    m_imageRawPixels = m_history[tgtIdx].rawPixels;
    m_imageMinVal    = mn;
    m_imageMaxVal    = mx;
    m_imageDispMin   = mn;
    m_imageDispMax   = mx;
    m_pixelSize      = m_history[tgtIdx].pixelSize;
    m_zoom[0].reset(outSize, outSize);
    m_ftComputed = false;

    saveHistory();
    update();
}
