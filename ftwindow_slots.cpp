#include "ftwindow_common.h"
#include <cstdlib>   // std::malloc / std::free (allocation preflight probe)
#include <QFileInfo>
#include <QImageReader>
#ifndef __EMSCRIPTEN__
#include <QStorageInfo>   // network-volume check for the startup history restore
#endif

// Probe whether `bytes` can currently be allocated. The WASM build links with
// ABORTING_MALLOC=0, so a failed malloc returns null instead of aborting the
// app, making this a safe test. A single contiguous probe is deliberately
// conservative: it is stricter than the many smaller allocations the real work
// performs, so passing it is a strong signal the operation will fit. On desktop
// memory is effectively unlimited, so the probe is skipped.
static bool probeAlloc(qint64 bytes)
{
#ifdef __EMSCRIPTEN__
    if (bytes <= 0) return true;
    void *p = std::malloc(static_cast<size_t>(bytes));
    if (!p) return false;
    std::free(p);
    return true;
#else
    Q_UNUSED(bytes);
    return true;
#endif
}

// Run body(i) for i in [begin, end) across the available worker threads (the
// same pool used by the FFT). Falls back to a serial loop when threads are
// unavailable or the range is small. body must be safe to call concurrently —
// callers here only ever write to disjoint indices.
template <typename F>
static void parallelFor(int begin, int end, F &&body)
{
    int n = end - begin;
    if (n <= 0) return;
#if FT_HAVE_THREADS
    int nThreads = (int)std::thread::hardware_concurrency();
    if (nThreads < 1) nThreads = 1;
    if (nThreads > 1 && n >= 4096) {
        std::vector<std::thread> threads;
        int per = (n + nThreads - 1) / nThreads;
        for (int t = 0; t < nThreads; t++) {
            int a = begin + t * per;
            int b = std::min(a + per, end);
            if (a < b)
                threads.emplace_back([a, b, &body]() {
                    for (int i = a; i < b; i++) body(i);
                });
        }
        for (auto &th : threads) th.join();
        return;
    }
#endif
    for (int i = begin; i < end; i++) body(i);
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
static FtWindow *g_fsWindow = nullptr;
extern "C" EMSCRIPTEN_KEEPALIVE void ft_on_fullscreen_change(int isFs)
{
    if (g_fsWindow) g_fsWindow->updateFullscreenButton(isFs != 0);
}

// Largest source-image dimension permitted in the 32-bit WASM build. Bigger
// inputs are downsampled on load so one image cannot, on its own, consume an
// outsized fraction of the capped linear heap (see CMakeLists.txt). A 4096
// image already costs ~128 MB of raw pixels plus ~256 MB of FFT working set.
static constexpr int kMaxLoadDim = 4096;
// Cap on the total memory held by the undo + redo history. Each undo snapshot
// deep-copies every buffer, so the history can dwarf the live data; this bound
// keeps headroom free for the calculation itself in the capped WASM heap.
static constexpr qint64 kUndoBudgetBytes = 768LL * 1024 * 1024;
#else
// Effectively unlimited on desktop: behaviour is unchanged there.
static constexpr int kMaxLoadDim = 1 << 20;
static constexpr qint64 kUndoBudgetBytes = 8LL * 1024 * 1024 * 1024;
#endif

// ---------------------------------------------------------------------------
//  Slots
// ---------------------------------------------------------------------------
void FtWindow::onLoadImage()
{
#ifdef __EMSCRIPTEN__
    // Build a list of example images served alongside the WASM app.
    QStringList items;
    items << "Exercise_01-Photos/band_1995.png"
          << "Exercise_01-Photos/brain.png"
          << "Exercise_01-Photos/church.png"
          << "Exercise_01-Photos/jacques_andreas_2017.png"
          << "Exercise_01-Photos/jacques_titan.png"
          << "Exercise_01-Photos/lorenz_1999.png"
          << "Exercise_01-Photos/party_1995.png"
          << "Exercise_01-Photos/plants.png"
          << "Exercise_02-Restauration/restauration_512.png"
          << "Exercise_03-Single_particle_EM/apoferritin_1024.png"
          << "Exercise_03-Single_particle_EM/apoferritin_1024_anisotropic.png"
          << "Exercise_03-Single_particle_EM/apoferritin_2048.png"
          << "Exercise_03-Single_particle_EM/apoferritin_2048_anisotropic.png"
          << "Exercise_04-2D-Crystal/2d_protein_crystal_1024.png"
          << "Exercise_04-2D-Crystal/Biozentrum-Basel-Eyes_1024.png"
          << "Exercise_04-2D-Crystal/Polyhead_virus_512.png"
          << "Exercise_05-Apple-Reconstruction/APPL0000000100.mrc"
          << "Exercise_05-Apple-Reconstruction/APPL0012345604.mrc"
          << "Exercise_05-Apple-Reconstruction/apple_clean.png"
          << "Exercise_05-Apple-Reconstruction/apple_corners_labeled.png"
          << "Exercise_05-Apple-Reconstruction/apple_noisy.png"
          << "Exercise_05-Apple-Reconstruction/apple_very_noisy.png"
          << "Exercise_06-Photo-Multiplication/andreas_1024.png"
          << "Exercise_07-PhaseRamp/jacques_titan.png"
          << "Exercise_08-TextRecognition-by-Correlation/Dot_black_center_1024.png"
          << "Exercise_08-TextRecognition-by-Correlation/Fourier_text_black_1024.png"
          << "Exercise_08-TextRecognition-by-Correlation/Fourier_text_white_1024.png"
          << "Exercise_08-TextRecognition-by-Correlation/Fourier_word_black_1024.png"
          << "Exercise_08-TextRecognition-by-Correlation/Letter_f_black_1024.png"
          << "Exercise_08-TextRecognition-by-Correlation/Letter_o_white_center_1024.png"
          << "Exercise_08-TextRecognition-by-Correlation/Letter_o_white_corner_1024.png"
          << "Exercise_08-TextRecognition-by-Correlation/Ring_black_1024.png"
          << "Exercise_08-TextRecognition-by-Correlation/Smiley_black_1024.png"
          << "Exercise_08-TextRecognition-by-Correlation/Smiley_small_black_1024.png"
          << "Exercise_09-Artemin-Protein/size_1024_rescaled_cropped/apoartcns_006.png"
          << "Exercise_09-Artemin-Protein/size_1024_rescaled_cropped/apoartcns_010.png"
          << "Exercise_09-Artemin-Protein/size_1024_rescaled_cropped/apoartcns_011.png"
          << "Exercise_09-Artemin-Protein/size_1024_rescaled_cropped/apoartcns_012.png"
          << "Exercise_09-Artemin-Protein/size_1024_rescaled_cropped/apoartcns_013.png"
          << "Exercise_09-Artemin-Protein/size_1024_rescaled_cropped/apoartcns_014.png"
          << "Exercise_09-Artemin-Protein/size_1024_rescaled_cropped/apoartcns_015.png"
          << "Exercise_09-Artemin-Protein/size_1024_rescaled_cropped/apoartcns_016.png"
          << "Exercise_09-Artemin-Protein/size_1024_rescaled_cropped/reference1_1024_rescaled_cropped.png"
          << "Exercise_09-Artemin-Protein/size_1024_rescaled_cropped/reference2_1024_rescaled_cropped.png"
          << "Exercise_09-Artemin-Protein/size_2048_cropped/apoartcns_006.png"
          << "Exercise_09-Artemin-Protein/size_2048_cropped/apoartcns_010.png"
          << "Exercise_09-Artemin-Protein/size_2048_cropped/apoartcns_011.png"
          << "Exercise_09-Artemin-Protein/size_2048_cropped/apoartcns_012.png"
          << "Exercise_09-Artemin-Protein/size_2048_cropped/apoartcns_013.png"
          << "Exercise_09-Artemin-Protein/size_2048_cropped/apoartcns_014.png"
          << "Exercise_09-Artemin-Protein/size_2048_cropped/apoartcns_015.png"
          << "Exercise_09-Artemin-Protein/size_2048_cropped/apoartcns_016.png"
          << "Exercise_09-Artemin-Protein/size_2048_cropped/reference1_2048_cropped.png"
          << "Exercise_09-Artemin-Protein/size_2048_cropped/reference2_2048_cropped.png"
          << "Exercise_09-Artemin-Protein/size_2048_rescaled/apoartcns_006.png"
          << "Exercise_09-Artemin-Protein/size_2048_rescaled/apoartcns_010.png"
          << "Exercise_09-Artemin-Protein/size_2048_rescaled/apoartcns_011.png"
          << "Exercise_09-Artemin-Protein/size_2048_rescaled/apoartcns_012.png"
          << "Exercise_09-Artemin-Protein/size_2048_rescaled/apoartcns_013.png"
          << "Exercise_09-Artemin-Protein/size_2048_rescaled/apoartcns_014.png"
          << "Exercise_09-Artemin-Protein/size_2048_rescaled/apoartcns_015.png"
          << "Exercise_09-Artemin-Protein/size_2048_rescaled/apoartcns_016.png"
          << "Exercise_09-Artemin-Protein/size_2048_rescaled/reference1_2048_rescaled.png"
          << "Exercise_09-Artemin-Protein/size_2048_rescaled/reference2_2048_rescaled.png"
          << "Exercise_10-Fibrils/aSyn_PFF1_c2_1024.mrc"
          << "Exercise_10-Fibrils/aSyn_PFF1_c2_BGzero_1024.mrc"
          << "Exercise_10-Fibrils/aSyn_PFF2_c2_1024.mrc"
          << "Exercise_10-Fibrils/aSyn_PFF2_c2_BGzero_1024.mrc"
          << "Exercise_10-Fibrils/aSyn_cryoEM_image_1024.mrc"
          << "Exercise_10-Fibrils/aSyn_cryoEM_image_1024mask.png"
          << "Exercise_10-Fibrils/aSyn_cryoEM_image_2048.mrc"
          << "Exercise_10-Fibrils/aSyn_dragon_c1_BGzero_1024.mrc"
          << "Exercise_10-Fibrils/aSyn_dragon_c2_BGzero_1024.mrc"
          << "Exercise_11-CTF/Example_200nm_0nm.mrc"
          << "Exercise_11-CTF/Example_325nm_50nm.mrc"
          << "Exercise_11-CTF/Example_400nm_0nm.mrc"
          << "Exercise_11-CTF/Example_300nm_200nm.mrc"
          << "Exercise_11-CTF/Example_975nm_50nm.mrc"
          << "Exercise_11-CTF/Example_1500nm_200nm.mrc"
          << "Exercise_11-CTF/Example_5000nm_0nm.mrc"
          << "Exercise_11-CTF/Example_4150nm_1700nm.mrc"
          << "Exercise_11-CTF/Example_0nm_1000nm.mrc"
          << "Exercise_11-CTF/Example_50nm_500nm.mrc"
          << "Exercise_11-CTF/Example_550nm_500nm.mrc"
          << "Exercise_11-CTF/Example_8500nm_3000nm.mrc";

    // Use a QListWidget inside a QDialog so the list scrolls within the
    // dialog instead of spilling off the page like a combo-box popup.
    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle("Load Example Image");
    dlg->setModal(true);

    auto *layout = new QVBoxLayout(dlg);
    auto *list = new QListWidget(dlg);
    list->addItems(items);
    if (!items.isEmpty())
        list->setCurrentRow(0);
    layout->addWidget(list);

    // Let the user upload an image straight from their own computer via the
    // browser's native file picker, as an alternative to the example list.
    auto *uploadBtn = new QPushButton("Upload from my computer…", dlg);
    layout->addWidget(uploadBtn);
    connect(uploadBtn, &QPushButton::clicked, this, [this, dlg]() {
        dlg->reject();   // close the example chooser without loading a list item
        QFileDialog::getOpenFileContent(
            "Images (*.tif *.tiff *.jpg *.jpeg *.png *.mrc *.MRC)",
            [this](const QString &fileName, const QByteArray &fileContent) {
                if (fileName.isEmpty())   // user cancelled the picker
                    return;
                loadImageData(fileName, fileContent);
            });
    });

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, dlg, &QDialog::accept);

    // Constrain dialog so it fits inside the browser viewport.
    if (auto *scr = QGuiApplication::primaryScreen()) {
        QSize avail = scr->availableSize();
        dlg->resize(qMin(500, avail.width() - 40),
                    qMin(500, avail.height() - 80));
    } else {
        dlg->resize(500, 500);
    }

    connect(dlg, &QDialog::accepted, this, [this, list]() {
        if (auto *it = list->currentItem()) {
            QString chosen = it->text();
            if (!chosen.isEmpty())
                fetchAndLoadImage(chosen);
        }
    });
    dlg->open();
#else
    // Prefer an embedder-supplied override (see FtWindow::setExampleImagesDir),
    // fall back to the standalone layout, then to the app directory.
    QString startDir = FtWindow::exampleImagesDir();
    if (startDir.isEmpty() || !QDir(startDir).exists())
        startDir = QCoreApplication::applicationDirPath() + "/../EXAMPLE_IMAGES";
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
    onNewImageOpen();
}

void FtWindow::onNewImageOpen()
{
    // Dismiss any currently-open math calculation overlays so they do not
    // linger behind the Create-or-Copy popup.
    if (m_mathActive)   onMathCancel();
    if (m_ftMathActive) onFtMathCancel();

    // Source default: always "New 1024".
    m_newImgSrcCombo->setCurrentIndex(1);   // "New 1024"

    // Target default: active slot if any, else first empty slot, else 0.
    int defaultTgt = -1;
    if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS) {
        defaultTgt = m_activeSlot;
    } else {
        for (int i = 0; i < HISTORY_SLOTS; i++) {
            if (!m_history[i].occupied) { defaultTgt = i; break; }
        }
        if (defaultTgt < 0) defaultTgt = 0;
    }
    m_newImgTgtCombo->setCurrentIndex(defaultTgt);

    m_newImageActive = true;
    m_newImgSrcCombo->show();
    m_newImgTgtCombo->show();
    m_newImgCreateBtn->show();
    m_newImgCancelBtn->show();
    update();
}

void FtWindow::onNewImageCancel()
{
    m_newImageActive = false;
    m_newImgSrcCombo->hide();
    m_newImgTgtCombo->hide();
    m_newImgCreateBtn->hide();
    m_newImgCancelBtn->hide();
    update();
}

void FtWindow::onNewImageCreate()
{
    if (!ensureCalcHeadroom(tr("create the image"))) return;
    onNewImageCreateImpl();
}

void FtWindow::onNewImageCreateImpl()
{
    int srcIdx = m_newImgSrcCombo->currentIndex();
    int tgtIdx = m_newImgTgtCombo->currentIndex();
    if (tgtIdx < 0 || tgtIdx >= HISTORY_SLOTS) { onNewImageCancel(); return; }

    if (srcIdx < 4) {
        // Create a new blank image of the selected size in the target slot.
        static const int sizes[4] = { 512, 1024, 2048, 4096 };
        int sz = sizes[srcIdx];

        // Save any currently-active buffer back to its slot before switching.
        if (m_activeSlot >= 0 && m_activeSlot != tgtIdx && !m_image.isNull()) {
            m_history[m_activeSlot].image        = m_image;
            m_history[m_activeSlot].path         = m_imagePath;
            m_history[m_activeSlot].rawPixels    = m_imageRawPixels;
            m_history[m_activeSlot].minVal       = m_imageMinVal;
            m_history[m_activeSlot].maxVal       = m_imageMaxVal;
            m_history[m_activeSlot].pixelSize    = m_pixelSize;
            m_history[m_activeSlot].powerSpecImg = computePowerSpecMasked(m_image);
            m_history[m_activeSlot].occupied     = true;
        }
        m_activeSlot = tgtIdx;
        onNewImageCancel();
        onCreateImageSized(sz);
        return;
    }

    // Source is a history slot a..p
    int slotIdx = srcIdx - 4;
    if (slotIdx < 0 || slotIdx >= HISTORY_SLOTS) { onNewImageCancel(); return; }
    if (slotIdx == tgtIdx) { onNewImageCancel(); return; }

    // Resolve source snapshot (use live buffer if it is the active slot).
    HistoryEntry src;
    if (slotIdx == m_activeSlot && !m_image.isNull()) {
        src.image        = m_image;
        src.path         = m_imagePath;
        src.rawPixels    = m_imageRawPixels;
        src.minVal       = m_imageMinVal;
        src.maxVal       = m_imageMaxVal;
        src.pixelSize    = m_pixelSize;
        src.powerSpecImg = computePowerSpecMasked(m_image);
        src.occupied     = true;
    } else if (m_history[slotIdx].occupied) {
        src = m_history[slotIdx];
    } else {
        onNewImageCancel();
        return;  // nothing to copy
    }

    storeUndoSnapshot();

    m_history[tgtIdx] = src;
    m_history[tgtIdx].occupied = true;

    m_activeSlot     = tgtIdx;
    m_image          = m_history[tgtIdx].image;
    m_imagePath      = m_history[tgtIdx].path;
    m_imageRawPixels = m_history[tgtIdx].rawPixels;
    m_imageMinVal    = m_history[tgtIdx].minVal;
    m_imageMaxVal    = m_history[tgtIdx].maxVal;
    m_imageDispMin   = m_imageMinVal;
    m_imageDispMax   = m_imageMaxVal;
    m_pixelSize      = m_history[tgtIdx].pixelSize;
    m_zoom[0].reset(m_image.width(), m_image.height());

    m_ftComputed = false;
    m_modeBtn->setText(modeLabel());
    m_modeBtn->hide();
    m_maskBtnVisible = false;

    computeFFT();
    m_history[tgtIdx].powerSpecImg = computePowerSpecMasked(m_image);

#ifndef __EMSCRIPTEN__
    QSettings settings("ft", "ft");
    settings.setValue("activeSlot", m_activeSlot);
#endif
    saveHistory();
    onNewImageCancel();
}

void FtWindow::onCreateImageSized(int sz)
{
    // If no slot is active, pick the first empty one (or last slot as fallback)
    if (m_activeSlot < 0) {
        m_activeSlot = HISTORY_SLOTS - 1;
        for (int i = 0; i < HISTORY_SLOTS; i++) {
            if (!m_history[i].occupied) { m_activeSlot = i; break; }
        }
    }

    if (sz <= 0) sz = 1024;
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
    m_maskBtnVisible = false;

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
    m_maskBtnVisible = false;

    if (!m_image.isNull()) {
        m_zoom[0].reset(m_image.width(), m_image.height());
        computeFFT();
    }
    update();
#endif // !__EMSCRIPTEN__
}

// Clear the active buffer (image, raw pixels, FFT, power spectrum) after the
// user confirms. The file on disk is never touched — only the slot is emptied,
// and an undo snapshot is taken first so the buffer can be brought back.
void FtWindow::onDeleteImage()
{
    if (m_activeSlot < 0 || m_activeSlot >= HISTORY_SLOTS) return;
    const HistoryEntry &entry = m_history[m_activeSlot];
    // A buffer that is still being read counts as deletable: the whole point of
    // loading it in the background is that the user can throw it away without
    // waiting for it.
    if (!entry.occupied && !entry.deferred && !entry.loading && m_image.isNull())
        return;   // nothing in this buffer to delete

    const int slot = m_activeSlot;
    const QString label = QString(QChar('a' + slot));

    auto *box = new QMessageBox(this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setIcon(QMessageBox::Warning);
    box->setWindowTitle(QStringLiteral("Empty buffer"));
    box->setText(QStringLiteral("Empty the buffer %1?").arg(label));
    box->setInformativeText(
        QStringLiteral("The file on disk is not deleted, "
                       "and Undo brings the buffer back."));
    QPushButton *cancelBtn = box->addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    QPushButton *deleteBtn = box->addButton(QStringLiteral("Empty buffer"), QMessageBox::DestructiveRole);
    box->setDefaultButton(cancelBtn);

    connect(box, &QMessageBox::finished, this, [this, box, deleteBtn, slot]() {
        if (box->clickedButton() != deleteBtn) return;
        if (slot != m_activeSlot) return;   // buffer switched while the dialog was open

        storeUndoSnapshot();

        // Throw away the result of a read that is still running for this slot,
        // so it cannot repopulate the buffer we are about to empty.
        cancelSlotLoad(slot);

        m_history[slot] = HistoryEntry();

        m_image = QImage();
        m_imagePath.clear();
        m_imageRawPixels.clear();
        m_imageRawPixels.shrink_to_fit();
        m_imageMinVal = m_imageMaxVal = 0;
        m_imageDispMin = m_imageDispMax = 0;
        m_pixelSize = 1.0;
        m_fftData.clear();
        m_fftData.shrink_to_fit();
        m_ftComputed = false;

        m_modeBtn->hide();
        m_maskBtnVisible = false;

        saveHistory();
        updateUndoRedoButtons();
        update();
    });

    box->open();
}

void FtWindow::onCycleMode()
{
    m_displayMode = (m_displayMode + 1) % 4;
    m_modeBtn->setText(modeLabel());
#ifndef __EMSCRIPTEN__
    QSettings settings("ft", "ft");
    settings.setValue("displayMode", m_displayMode);
#endif
    // Sync zoom/pan between both FT panels
    m_zoom[2] = m_zoom[1];
    if (m_brushActive && m_ftComputed) {
        double bv = brushValue();
        m_brushValueEdit->setText(bv > 0 ? QString::number(bv, 'g', 5) : "1");
    }
    update();
}

void FtWindow::onToggleFullscreen()
{
#ifdef __EMSCRIPTEN__
    // iPad/iPhone Safari accepts requestFullscreen on non-video elements
    // but cancels it within a frame, with no fullscreenerror to detect.
    // Detect iOS and show "Add to Home Screen" instructions instead.
    // (Standalone/home-screen launches already run without Safari chrome.)
    int iosState = EM_ASM_INT({
        var ua = navigator.userAgent || "";
        var isIPad = /iPad|iPhone|iPod/.test(ua) ||
                     (navigator.platform === "MacIntel" && navigator.maxTouchPoints > 1);
        if (!isIPad) return 0;
        var standalone = window.navigator.standalone === true ||
                         (window.matchMedia && window.matchMedia("(display-mode: standalone)").matches);
        return standalone ? 2 : 1;
    });
    if (iosState == 1) {
        auto *msg = new QMessageBox(this);
        msg->setAttribute(Qt::WA_DeleteOnClose);
        msg->setWindowTitle("Fullscreen on iPad / iPhone");
        msg->setTextFormat(Qt::RichText);
        msg->setText(
            "<p>Safari on iPad and iPhone does not support a stable in-browser "
            "fullscreen mode for web apps.</p>"
            "<p><b>To use Fourier Analyzer fullscreen:</b></p>"
            "<ol>"
            "<li>Tap the <b>Share</b> button in Safari (the square with an upward arrow).</li>"
            "<li>Scroll down and tap <b>Add to Home Screen</b>.</li>"
            "<li>Tap <b>Add</b>.</li>"
            "<li>Launch Fourier Analyzer from its home-screen icon &mdash; it opens "
            "fullscreen automatically.</li>"
            "</ol>");
        msg->setStandardButtons(QMessageBox::Ok);
        msg->open();
        return;
    }
    if (iosState == 2) {
        // Already running standalone — fullscreen toggle is a no-op.
        return;
    }
    // Drive the browser Fullscreen API from JS. Target Qt's #screen
    // container (falls back to its canvas) instead of documentElement,
    // which iPad Safari exits immediately. The actual button label is
    // synced from JS via fullscreenchange listeners installed once.
    installFullscreenSync();
    EM_ASM({
        // Target the #screen container, NOT its inner <canvas>. Qt resizes
        // the canvas (DPR + pixel dims) the moment fullscreen begins, and
        // iPad Safari exits fullscreen when the fullscreen element itself
        // changes size. The container stays stable while the canvas inside
        // resizes freely.
        var target = document.getElementById('screen') || document.documentElement;
        var isFS = document.fullscreenElement ||
                   document.webkitFullscreenElement ||
                   document.webkitCurrentFullScreenElement;
        if (isFS) {
            var exit = document.exitFullscreen ||
                       document.webkitExitFullscreen ||
                       document.webkitCancelFullScreen;
            if (exit) exit.call(document);
        } else {
            var req = target.requestFullscreen ||
                      target.webkitRequestFullscreen ||
                      target.webkitRequestFullScreen;
            if (req) {
                try {
                    var p = req.call(target);
                    if (p && p.then) p.then(null, function(e) {
                        console.warn('fullscreen rejected:', e);
                    });
                } catch (e) {
                    console.warn('fullscreen threw:', e);
                }
            } else {
                console.warn('Fullscreen API not available');
            }
        }
    });
#else
    // Embedded in another application: defer to the host. A child widget
    // cannot be made fullscreen directly; the host has to toggle its own
    // top-level window. The button text is then refreshed via
    // updateFullscreenButton() once the host reports the new state.
    if (!isWindow()) {
        emit fullscreenToggleRequested();
        return;
    }
    // changeEvent() refreshes the button label on the resulting state change.
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
#endif
}

void FtWindow::installFullscreenSync()
{
#ifdef __EMSCRIPTEN__
    g_fsWindow = this;
    EM_ASM({
        if (window.__ftFsInit) return;
        window.__ftFsInit = true;
        var sync = function() {
            var fs = document.fullscreenElement ||
                     document.webkitFullscreenElement ||
                     document.webkitCurrentFullScreenElement;
            if (window.Module && window.Module.ccall) {
                try {
                    window.Module.ccall('ft_on_fullscreen_change',
                        null, ['number'], [fs ? 1 : 0]);
                } catch (e) { console.warn('fs sync failed:', e); }
            }
        };
        document.addEventListener('fullscreenchange', sync);
        document.addEventListener('webkitfullscreenchange', sync);
    });
#endif
}

void FtWindow::updateFullscreenButton(bool isFullscreen)
{
    if (m_fullscreenBtn)
        m_fullscreenBtn->setText(isFullscreen ? "Leave fullscreen" : "Go fullscreen");
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
void FtWindow::reportOutOfMemory(const QString &context)
{
    // Use a heap-allocated, non-blocking message box (open() not exec()) and
    // setText() rather than the static QMessageBox::warning() convenience:
    // under Qt for WebAssembly the blocking static variant does not render its
    // body text, showing only the title.
    auto *msg = new QMessageBox(this);
    msg->setAttribute(Qt::WA_DeleteOnClose);
    msg->setIcon(QMessageBox::Warning);
    msg->setWindowTitle(tr("Out of memory"));
    msg->setText(
        tr("Not enough memory to %1.\n\n"
           "This browser tab has a fixed memory budget, and the images "
           "currently loaded have used it up. Overwrite an existing image "
           "buffer (the \"New image\" tool can target any slot a…p), or "
           "reload the page, then open fewer or smaller images.").arg(context));
    msg->setStandardButtons(QMessageBox::Ok);
    msg->open();
}

qint64 FtWindow::currentStateBytes() const
{
    auto entryBytes = [](const HistoryEntry &e) -> qint64 {
        return (qint64)e.rawPixels.size() * (qint64)sizeof(double)
             + (qint64)e.image.sizeInBytes()
             + (qint64)e.powerSpecImg.sizeInBytes();
    };
    qint64 b = 0;
    for (int i = 0; i < HISTORY_SLOTS; i++)
        b += entryBytes(m_history[i]);
    b += (qint64)m_image.sizeInBytes();
    b += (qint64)m_imageRawPixels.size() * (qint64)sizeof(double);
    b += (qint64)m_fftData.size() * (qint64)sizeof(Complex);
    return b;
}

qint64 FtWindow::estimatedWorkingBytes() const
{
    // Use whichever is larger: the current FFT size or the (possibly new)
    // image dimension, so the estimate is valid both during a calculation and
    // when loading a fresh image (where m_fftN still reflects the old image).
    qint64 N = m_fftN;
    if (!m_image.isNull())
        N = std::max<qint64>(N, std::max(m_image.width(), m_image.height()));
    if (N <= 0) return 64LL * 1024 * 1024;   // generic floor when nothing is loaded
    // Headroom for one operation: a complex FFT buffer (16 B/px), a working
    // copy, and a few real-space / display buffers (8 B/px each).
    return N * N * (qint64)(16 + 16 + 8 + 8 + 8 + 8);
}

bool FtWindow::ensureCalcHeadroom(const QString &context)
{
    trimUndoMemory();
    if (probeAlloc(currentStateBytes() + estimatedWorkingBytes()))
        return true;

    // Not enough room while keeping the undo history. Undo is a convenience;
    // sacrifice it (and redo) to free memory for the operation itself.
    if (!m_undoStack.empty() || !m_redoStack.empty()) {
        m_undoStack.clear();
        m_redoStack.clear();
        updateUndoRedoButtons();
        qWarning() << "Undo history cleared to free memory for:" << context;
    }
    if (probeAlloc(estimatedWorkingBytes()))
        return true;

    reportOutOfMemory(context);
    return false;
}

void FtWindow::discardCurrentImageState()
{
    // Drop the half-built current image and any FFT working set so the heap is
    // released, and clear the slot we were loading into.
    m_image = QImage();
    m_imageRawPixels.clear();
    m_imageRawPixels.shrink_to_fit();
    m_fftData.clear();
    m_fftData.shrink_to_fit();
    m_ftComputed = false;

    if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS)
        m_history[m_activeSlot] = HistoryEntry();
}

void FtWindow::downsampleForMemoryLimit()
{
    if (m_image.isNull()) return;
    const int w = m_image.width();
    const int h = m_image.height();
    const int maxDim = std::max(w, h);
    if (maxDim <= kMaxLoadDim) return;

    const int factor = (maxDim + kMaxLoadDim - 1) / kMaxLoadDim;   // ceil
    const int nw = std::max(1, w / factor);
    const int nh = std::max(1, h / factor);

    if (static_cast<qint64>(m_imageRawPixels.size()) == static_cast<qint64>(w) * h) {
        // Box-average the raw pixels so MRC float precision is preserved.
        std::vector<double> down(static_cast<size_t>(nw) * nh, 0.0);
        for (int y = 0; y < nh; y++) {
            for (int x = 0; x < nw; x++) {
                double sum = 0; int cnt = 0;
                for (int dy = 0; dy < factor; dy++) {
                    const int sy = y * factor + dy;
                    if (sy >= h) break;
                    const double *srow = m_imageRawPixels.data() + static_cast<size_t>(sy) * w;
                    for (int dx = 0; dx < factor; dx++) {
                        const int sx = x * factor + dx;
                        if (sx >= w) break;
                        sum += srow[sx];
                        cnt++;
                    }
                }
                down[static_cast<size_t>(y) * nw + x] = (cnt > 0) ? sum / cnt : 0.0;
            }
        }
        m_imageRawPixels = std::move(down);
        m_imageMinVal = *std::min_element(m_imageRawPixels.begin(), m_imageRawPixels.end());
        m_imageMaxVal = *std::max_element(m_imageRawPixels.begin(), m_imageRawPixels.end());
        m_imageDispMin = m_imageMinVal;
        m_imageDispMax = m_imageMaxVal;
        const double range = m_imageMaxVal - m_imageMinVal;
        const double scale = (range > 0) ? 255.0 / range : 1.0;
        m_image = QImage(nw, nh, QImage::Format_Grayscale8);
        for (int y = 0; y < nh; y++) {
            uchar *row = m_image.scanLine(y);
            for (int x = 0; x < nw; x++)
                row[x] = static_cast<uchar>(std::clamp(
                    (m_imageRawPixels[static_cast<size_t>(y) * nw + x] - m_imageMinVal) * scale,
                    0.0, 255.0));
        }
    } else {
        // No matching raw pixels (e.g. a regular image before extraction):
        // scale the QImage; extractImageData() rebuilds raw pixels afterwards.
        m_image = m_image.scaled(nw, nh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    qDebug() << "Downsampled large input by factor" << factor
             << "from" << w << "x" << h << "to" << nw << "x" << nh
             << "to stay within the memory budget";
}

void FtWindow::loadImageIntoBuffer(const QString &path)
{
    // Persist the currently-active buffer back to its slot before switching,
    // mirroring the Create/Copy flow, so any live edits are not lost.
    if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS && !m_image.isNull()) {
        m_history[m_activeSlot].image        = m_image;
        m_history[m_activeSlot].path         = m_imagePath;
        m_history[m_activeSlot].rawPixels    = m_imageRawPixels;
        m_history[m_activeSlot].minVal       = m_imageMinVal;
        m_history[m_activeSlot].maxVal       = m_imageMaxVal;
        m_history[m_activeSlot].pixelSize    = m_pixelSize;
        m_history[m_activeSlot].powerSpecImg = computePowerSpecMasked(m_image);
        m_history[m_activeSlot].occupied     = true;
    }

    // Prefer buffer "a" (slot 0) when it is free; otherwise the first free
    // slot, falling back to "a" when every slot is occupied.
    int target = -1;
    if (!m_history[0].occupied) {
        target = 0;
    } else {
        for (int i = 0; i < HISTORY_SLOTS; i++) {
            if (!m_history[i].occupied) { target = i; break; }
        }
        if (target < 0) target = 0;
    }

    // Pin the chosen slot so loadImageFile() loads into it rather than
    // auto-selecting one itself.
    m_activeSlot = target;
    loadImageFile(path);
}

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
            downsampleForMemoryLimit();
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
            downsampleForMemoryLimit();
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
    m_maskBtnVisible = false;

    if (!m_image.isNull()) {
        // Refuse before the FFT allocates if the heap cannot take it.
        if (!ensureCalcHeadroom(tr("load this image"))) {
            discardCurrentImageState();
            saveHistory();
            update();
            return;
        }
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
            downsampleForMemoryLimit();
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
            downsampleForMemoryLimit();
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
    m_maskBtnVisible = false;

    if (!m_image.isNull()) {
        // Refuse before the FFT allocates if the heap cannot take it.
        if (!ensureCalcHeadroom(tr("load this image"))) {
            discardCurrentImageState();
            saveHistory();
            update();
            return;
        }
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
#else
void FtWindow::fetchAndLoadImage(const QString &relativePath)
{
    // Native build: load the file from disk relative to the application directory.
    QString filePath = QCoreApplication::applicationDirPath() + QStringLiteral("/images/") + relativePath;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to load default image:" << filePath;
        return;
    }
    QByteArray data = file.readAll();
    file.close();
    loadImageData(relativePath, data);
}
#endif

// The image every empty session starts with, relative to the example-images
// directory. Used by both the standalone app and embedders such as the 4d app.
static const char *const kDefaultExampleImage = "Exercise_01-Photos/lorenz_1999.png";

#ifndef __EMSCRIPTEN__
QString FtWindow::resolveExampleImage(const QString &relativePath)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList dirs;
    // Embedder-supplied location first (the 4d app points this at its own
    // resources directory before constructing the window).
    const QString overrideDir = FtWindow::exampleImagesDir();
    if (!overrideDir.isEmpty())
        dirs << overrideDir;
    dirs << appDir + QStringLiteral("/../EXAMPLE_IMAGES")   // standalone build tree
         << appDir + QStringLiteral("/EXAMPLE_IMAGES")
         << appDir + QStringLiteral("/../Resources/EXAMPLE_IMAGES");  // macOS bundle

    for (const QString &dir : dirs) {
        QFileInfo fi(dir + QLatin1Char('/') + relativePath);
        if (fi.exists() && fi.isFile())
            return fi.absoluteFilePath();
    }
    return QString();
}
#endif

void FtWindow::loadDefaultExampleIfEmpty()
{
    // "Empty" must account for buffers that only hold a path: a deferred or
    // still-loading slot is a remembered image, not a free buffer.
    for (int i = 0; i < HISTORY_SLOTS; i++) {
        const HistoryEntry &e = m_history[i];
        if (e.occupied || e.deferred || e.loading) return;
    }
    if (!m_image.isNull()) return;

    m_activeSlot = 0;   // buffer a

#ifdef __EMSCRIPTEN__
    fetchAndLoadImage(QString::fromLatin1(kDefaultExampleImage));
#else
    const QString path = resolveExampleImage(QString::fromLatin1(kDefaultExampleImage));
    if (path.isEmpty()) {
        qWarning() << "Default example image not found:" << kDefaultExampleImage
                   << "- looked under" << FtWindow::exampleImagesDir()
                   << "and" << QCoreApplication::applicationDirPath();
        return;
    }
    loadImageFile(path);
#endif
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
    if (!m_imageContrastLocked) {
        m_imageDispMin = m_imageMinVal;
        m_imageDispMax = m_imageMaxVal;
    }
}

// ---------------------------------------------------------------------------
//  FFT
// ---------------------------------------------------------------------------
void FtWindow::computeFFT(bool keepZoom)
{
    if (m_image.isNull()) return;

    // This transform comes from a real image, so inverting it must give that
    // image back: reset the mode in case the buffer previously held a pupil.
    m_ftInverseOutput = InverseOutput::RealPart;

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

    // Centered real-space convention: the real-space image's logical origin
    // lives at (N/2, N/2). fftShift swaps quadrants to convert it to the
    // (0,0)-origin form that the raw FFT expects. The matching post-shift
    // happens in computeInverseFFT, so a forward-then-inverse round trip is
    // self-consistent and the FT of a phase ramp no longer carries an
    // unwanted (-1)^(u+v) checkerboard phase factor.
    fftShift(data, N);

    // Data prepared – show a small slice of progress
    m_fftProgress = 0.02;
    repaint();

    {
#if !FT_HAVE_THREADS
        // Single-threaded FFT fallback (WASM built without -pthread)
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
    m_maskBtnVisible = true;

    if (!keepZoom) {
        m_zoom[1].reset(N, N);
        m_zoom[2].reset(N, N);
    }

    // Keep the active line-profile (cross-section) overlays in sync with the
    // newly computed FT, e.g. after a panel-1 edit auto-recomputes the FFT.
    if (m_crossSectionActive)
        computeCrossSectionProfile();
}

void FtWindow::computeInverseFFT(InverseOutput out)
{
    if (!m_ftComputed || m_fftN == 0) return;

    int N = m_fftN;
    std::vector<Complex> data = m_fftData;
    fftShift(data, N);

    m_iftProgress = 0.0;
    repaint();

    {
#if !FT_HAVE_THREADS
        // Single-threaded inverse FFT fallback (WASM built without -pthread)
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

    // Centered real-space convention: convert the (0,0)-origin output of the
    // raw inverse FFT to centered form. Matches the pre-shift in computeFFT.
    fftShift(data, N);

    int outW = (m_origW > 0) ? std::min(m_origW, N) : N;
    int outH = (m_origH > 0) ? std::min(m_origH, N) : N;

    m_imageRawPixels.resize(outW * outH);
    for (int y = 0; y < outH; y++)
        for (int x = 0; x < outW; x++) {
            const Complex &v = data[y * N + x];
            // Intensity keeps |h|², which is what a detector records and the only
            // form in which a complex wave's asymmetry survives; the real part
            // alone would be centrosymmetric for a real-valued transform.
            m_imageRawPixels[y * outW + x] =
                (out == InverseOutput::Intensity) ? std::norm(v) : v.real();
        }

    m_imageMinVal = *std::min_element(m_imageRawPixels.begin(), m_imageRawPixels.end());
    m_imageMaxVal = *std::max_element(m_imageRawPixels.begin(), m_imageRawPixels.end());
    if (!m_imageContrastLocked) {
        m_imageDispMin = m_imageMinVal;
        m_imageDispMax = m_imageMaxVal;
    }
    double dmin = m_imageDispMin, dmax = m_imageDispMax;
    double range = dmax - dmin;
    double scale = (range > 0) ? 255.0 / range : 1.0;

    m_image = QImage(outW, outH, QImage::Format_Grayscale8);
    for (int y = 0; y < outH; y++) {
        uchar *row = m_image.scanLine(y);
        for (int x = 0; x < outW; x++)
            row[x] = static_cast<uchar>(std::clamp(
                (m_imageRawPixels[y * outW + x] - dmin) * scale, 0.0, 255.0));
    }

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

#if defined(__EMSCRIPTEN__) && FT_HAVE_THREADS
// Run one FFT pass batch: transform lines [b, bEnd) of `data` (rows when
// columns==false, columns when columns==true) using the worker-thread pool.
// Used by the WASM-animated transforms (computeFFTAnimated / *Inverse*).
static void runFFTBatch(std::vector<Complex> &data, int N, int b, int bEnd,
                        bool inverse, bool columns)
{
    int nThreads = (int)std::thread::hardware_concurrency();
    if (nThreads < 1) nThreads = 1;
    std::vector<std::thread> threads;
    int perThread = ((bEnd - b) + nThreads - 1) / nThreads;
    for (int t = 0; t < nThreads; t++) {
        int i0 = b + t * perThread;
        int i1 = std::min(i0 + perThread, bEnd);
        if (i0 >= i1) continue;
        threads.emplace_back([&data, N, i0, i1, inverse, columns]() {
            std::vector<Complex> line(N);
            for (int i = i0; i < i1; i++) {
                if (columns) {
                    for (int y = 0; y < N; y++) line[y] = data[y * N + i];
                    fft1d(line, inverse);
                    for (int y = 0; y < N; y++) data[y * N + i] = line[y];
                } else {
                    for (int x = 0; x < N; x++) line[x] = data[i * N + x];
                    fft1d(line, inverse);
                    for (int x = 0; x < N; x++) data[i * N + x] = line[x];
                }
            }
        });
    }
    for (auto &t : threads) t.join();
}
#endif // defined(__EMSCRIPTEN__) && FT_HAVE_THREADS

// ---------------------------------------------------------------------------
//  Interactive FFT (arrow buttons) — animated in WASM
// ---------------------------------------------------------------------------
// In the browser the canvas is only composited when the main thread returns to
// the event loop, so the blocking loops in computeFFT()/computeInverseFFT()
// (which rely on QApplication::processEvents()) never show the traversing blue
// progress fill. These variants run the same transform as a chain of
// event-loop-yielding steps (via chainSteps), repainting between batches. The
// setup/finalize blocks intentionally mirror computeFFT()/computeInverseFFT();
// keep them in sync if those change. On desktop (or a single-threaded WASM
// build) they fall back to the synchronous version, which already animates.
void FtWindow::computeFFTAnimated(bool keepZoom)
{
#if defined(__EMSCRIPTEN__) && FT_HAVE_THREADS
    if (m_image.isNull()) return;

    // --- Setup (mirrors computeFFT) ---
    QImage gray = m_image.convertToFormat(QImage::Format_Grayscale8);
    int w = gray.width();
    int h = gray.height();
    int N = nextGoodFFTSize(std::max(w, h));
    m_fftN = N;
    m_origW = w;
    m_origH = h;

    double sum = 0;
    for (int y = 0; y < h; y++) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < w; x++) sum += row[x];
    }
    double avg = sum / ((double)w * h);

    auto data = std::make_shared<std::vector<Complex>>(N * N, Complex(avg, 0.0));
    for (int y = 0; y < h; y++) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < w; x++)
            (*data)[y * N + x] = Complex(row[x], 0.0);
    }
    fftShift(*data, N);

    m_fftProgress = 0.02;

    int nThreads = (int)std::thread::hardware_concurrency();
    if (nThreads < 1) nThreads = 1;
    int batchSize = nThreads * 16;

    std::vector<std::function<void()>> steps;
    for (int b = 0; b < N; b += batchSize) {        // row pass
        int bEnd = std::min(b + batchSize, N);
        steps.push_back([this, data, N, bEnd, b]() {
            runFFTBatch(*data, N, b, bEnd, false, false);
            m_fftProgress = 0.02 + 0.48 * bEnd / N;
        });
    }
    for (int b = 0; b < N; b += batchSize) {        // column pass
        int bEnd = std::min(b + batchSize, N);
        steps.push_back([this, data, N, bEnd, b]() {
            runFFTBatch(*data, N, b, bEnd, false, true);
            m_fftProgress = 0.5 + 0.48 * bEnd / N;
        });
    }
    steps.push_back([this, data, N, keepZoom]() {   // finalize (mirrors computeFFT)
        fftShift(*data, N);
        m_fftData = *data;
        recomputeDisplayImages();
        m_fftProgress = -1;
        m_ftComputed = true;
        m_modeBtn->show();
        m_maskBtnVisible = true;
        if (!keepZoom) {
            m_zoom[1].reset(N, N);
            m_zoom[2].reset(N, N);
        }
        // Keep the active line-profile (cross-section) overlays in sync with
        // the newly computed FT.
        if (m_crossSectionActive)
            computeCrossSectionProfile();
        update();
    });

    chainSteps(std::move(steps));
#else
    computeFFT(keepZoom);
#endif
}

void FtWindow::computeInverseFFTAnimated()
{
#if defined(__EMSCRIPTEN__) && FT_HAVE_THREADS
    if (!m_ftComputed || m_fftN == 0) return;

    int N = m_fftN;
    auto data = std::make_shared<std::vector<Complex>>(m_fftData);
    fftShift(*data, N);

    m_iftProgress = 0.0;

    int nThreads = (int)std::thread::hardware_concurrency();
    if (nThreads < 1) nThreads = 1;
    int batchSize = nThreads * 16;

    std::vector<std::function<void()>> steps;
    for (int b = 0; b < N; b += batchSize) {        // row pass
        int bEnd = std::min(b + batchSize, N);
        steps.push_back([this, data, N, bEnd, b]() {
            runFFTBatch(*data, N, b, bEnd, true, false);
            m_iftProgress = 0.5 * bEnd / N;
        });
    }
    for (int b = 0; b < N; b += batchSize) {        // column pass
        int bEnd = std::min(b + batchSize, N);
        steps.push_back([this, data, N, bEnd, b]() {
            runFFTBatch(*data, N, b, bEnd, true, true);
            m_iftProgress = 0.5 + 0.5 * bEnd / N;
        });
    }
    steps.push_back([this, data, N]() {             // finalize (mirrors computeInverseFFT)
        m_iftProgress = -1;
        fftShift(*data, N);

        int outW = (m_origW > 0) ? std::min(m_origW, N) : N;
        int outH = (m_origH > 0) ? std::min(m_origH, N) : N;

        m_imageRawPixels.resize(outW * outH);
        for (int y = 0; y < outH; y++)
            for (int x = 0; x < outW; x++) {
                const Complex &v = (*data)[y * N + x];
                m_imageRawPixels[y * outW + x] =
                    (m_ftInverseOutput == InverseOutput::Intensity) ? std::norm(v) : v.real();
            }

        m_imageMinVal = *std::min_element(m_imageRawPixels.begin(), m_imageRawPixels.end());
        m_imageMaxVal = *std::max_element(m_imageRawPixels.begin(), m_imageRawPixels.end());
        if (!m_imageContrastLocked) {
            m_imageDispMin = m_imageMinVal;
            m_imageDispMax = m_imageMaxVal;
        }
        double dmin = m_imageDispMin, dmax = m_imageDispMax;
        double range = dmax - dmin;
        double scale = (range > 0) ? 255.0 / range : 1.0;

        m_image = QImage(outW, outH, QImage::Format_Grayscale8);
        for (int y = 0; y < outH; y++) {
            uchar *row = m_image.scanLine(y);
            for (int x = 0; x < outW; x++)
                row[x] = static_cast<uchar>(std::clamp(
                    (m_imageRawPixels[y * outW + x] - dmin) * scale, 0.0, 255.0));
        }

        if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS) {
            m_history[m_activeSlot].image     = m_image;
            m_history[m_activeSlot].rawPixels = m_imageRawPixels;
            m_history[m_activeSlot].minVal    = m_imageMinVal;
            m_history[m_activeSlot].maxVal    = m_imageMaxVal;
            m_history[m_activeSlot].occupied  = true;
        }

        m_zoom[0].reset(outW, outH);
        update();
    });

    chainSteps(std::move(steps));
#else
    // Honour whatever the current Fourier data means (real image vs. pupil).
    computeInverseFFT(m_ftInverseOutput);
#endif
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

    parallelFor(0, total, [&](int i) {
        m_cosVals[i]   = data[i].real();
        m_sinVals[i]   = data[i].imag();
        double amp     = std::abs(data[i]);
        m_ampVals[i]   = std::log(1.0 + amp);
        m_phaseVals[i] = std::arg(data[i]) * 180.0 / M_PI;
        m_phaseVals[i] = std::round(m_phaseVals[i] * 100.0) / 100.0;
        if (m_phaseVals[i] <= -180.0) m_phaseVals[i] = 180.0;
        m_powerVals[i] = std::log(1.0 + amp * amp);
    });

    m_cosImg   = floatToImage(m_cosVals,   N);
    m_sinImg   = floatToImage(m_sinVals,   N);
    m_ampImg   = floatToImage(m_ampVals,   N);
    m_phaseImg = floatToImage(m_phaseVals, N);
    m_powerImg = floatToImage(m_powerVals, N);

    // Complex FT image: brightness from the power spectrum, hue from phase.
    // Use the auto brightness range as default, but keep any range the user
    // has set via the histogram when the FT contrast is locked.
    if (!m_ftContrastLocked)
        resetComplexDisplayRange();
    buildComplexImage();

    auto mm = [](const std::vector<double> &v) {
        return std::make_pair(*std::min_element(v.begin(), v.end()),
                              *std::max_element(v.begin(), v.end()));
    };
    std::tie(m_cosMin,   m_cosMax)   = mm(m_cosVals);
    std::tie(m_sinMin,   m_sinMax)   = mm(m_sinVals);
    std::tie(m_ampMin,   m_ampMax)   = mm(m_ampVals);
    std::tie(m_phaseMin, m_phaseMax) = mm(m_phaseVals);
    std::tie(m_powerMin, m_powerMax) = mm(m_powerVals);

    // Initialize display ranges to global ranges (unless locked)
    if (!m_ftContrastLocked) {
        m_cosDispMin = m_cosMin;     m_cosDispMax = m_cosMax;
        m_sinDispMin = m_sinMin;     m_sinDispMax = m_sinMax;
        m_ampDispMin = m_ampMin;     m_ampDispMax = m_ampMax;
        m_phaseDispMin = m_phaseMin; m_phaseDispMax = m_phaseMax;
        m_powerDispMin = m_powerMin; m_powerDispMax = m_powerMax;
    } else {
        // Rebuild FT images using the locked display ranges
        rebuildFTImageWithLUT(HIST_POWER);
        rebuildFTImageWithLUT(HIST_FT_LEFT);
        rebuildFTImageWithLUT(HIST_FT_RIGHT);
    }
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
    case 2: return "complex FT";
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

    // Centered real-space convention (mirrors computeFFT).
    fftShift(data, N);
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

QImage FtWindow::powerSpecFromCurrentFFT() const
{
    // m_fftData is already in centred convention (DC at N/2, N/2), exactly
    // what computePowerSpecMasked produces after its fft2d + fftShift — so the
    // power spectrum is |m_fftData|^2 with the central 3x3 (DC) suppressed, no
    // forward transform needed.
    if (!m_ftComputed || m_fftN == 0 || m_fftData.empty())
        return {};
    int N = m_fftN;
    int total = N * N;
    std::vector<double> power(total);
    parallelFor(0, total, [&](int i) {
        double a = std::abs(m_fftData[i]);
        power[i] = std::log(1.0 + a * a);
    });
    int half = N / 2;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int x = half + dx, y = half + dy;
            if (x >= 0 && x < N && y >= 0 && y < N)
                power[y * N + x] = 0.0;
        }
    return floatToImage(power, N);
}

#ifndef __EMSCRIPTEN__
// Startup restore budget: a history slot is only loaded while the app opens if
// its image is smaller than this in both dimensions. Larger images are read,
// converted, padded and forward-transformed in full, which is exactly what made
// opening slow — they become deferred placeholders instead.
static constexpr int kHistoryRestoreMaxDim = 1200;

// True when `path` sits on a volume that is not a network share. Reading a slot
// from a mounted network drive can block for seconds (or forever, if the server
// is gone), so those slots are never touched during startup.
static bool isOnLocalVolume(const QString &path)
{
    const QStorageInfo storage(QFileInfo(path).absoluteFilePath());
    if (!storage.isValid() || !storage.isReady())
        return false;

    // UNC / SMB style device names ("//server/share", "\\server\share").
    const QString device = QString::fromLatin1(storage.device());
    if (device.startsWith(QLatin1String("//")) || device.startsWith(QLatin1String("\\\\")))
        return false;

    // Network filesystem types across macOS / Linux / Windows. Matched as
    // substrings so variants like "fuse.sshfs" or "nfs4" are covered too.
    static const char *const kNetworkFs[] = {
        "nfs", "smb", "cifs", "afp", "webdav", "davfs", "sshfs",
        "ftp", "ncpfs", "9p", "afs", "gvfs", "fuseblk.network"
    };
    const QString fsType = QString::fromLatin1(storage.fileSystemType()).toLower();
    for (const char *fs : kNetworkFs)
        if (fsType.contains(QLatin1String(fs)))
            return false;

    return true;
}

// Read just the image dimensions of `path` — the MRC header, or the image
// header via QImageReader — without decoding any pixel data.
static bool readImageDimensions(const QString &path, int &w, int &h)
{
    if (path.endsWith(QLatin1String(".mrc"), Qt::CaseInsensitive))
        return readMrcDimensions(path, w, h);

    QImageReader reader(path);
    const QSize size = reader.size();
    if (!size.isValid() || size.isEmpty())
        return false;
    w = size.width();
    h = size.height();
    return true;
}

// Decide whether a stored history slot is cheap enough to load while the app is
// opening. On refusal, `reason` explains why (for the log).
static bool isRestoreCheapAtStartup(const QString &path, QString *reason)
{
    if (!isOnLocalVolume(path)) {
        if (reason) *reason = QStringLiteral("not on a local volume");
        return false;
    }

    int w = 0, h = 0;
    if (!readImageDimensions(path, w, h)) {
        if (reason) *reason = QStringLiteral("dimensions unreadable");
        return false;
    }
    if (w >= kHistoryRestoreMaxDim || h >= kHistoryRestoreMaxDim) {
        if (reason)
            *reason = QStringLiteral("%1x%2 is too large").arg(w).arg(h);
        return false;
    }
    return true;
}
#endif // !__EMSCRIPTEN__

void FtWindow::saveHistory()
{
#ifndef __EMSCRIPTEN__
    QSettings settings("ft", "ft");
    for (int i = 0; i < HISTORY_SLOTS; i++) {
        QString key = QString("history/%1").arg(i);
        // Deferred and still-loading slots hold no data but must keep their path
        // in the session, otherwise skipping them at startup — or quitting while
        // one is still being read — would silently delete them.
        if (m_history[i].occupied
            || ((m_history[i].deferred || m_history[i].loading) && !m_history[i].path.isEmpty()))
            settings.setValue(key, m_history[i].path);
        else
            settings.remove(key);
    }
    settings.setValue("activeSlot", m_activeSlot);
#endif
}

// Pad an image (and its raw pixel data) out to the next FFT-friendly square,
// the same geometry padImageToSquare() produces — but without touching any
// member state, so a worker thread may call it.
static void padSlotImageToSquare(QImage &img, std::vector<double> &raw,
                                 double &minVal, double &maxVal)
{
    if (img.isNull()) return;
    int w = img.width(), h = img.height();
    int side = nextSmooth235(std::max(w, h));
    if (w == side && h == side) return;

    int ox = (side - w) / 2;
    int oy = (side - h) / 2;

    if ((int)raw.size() == w * h) {
        double sum = 0;
        for (double v : raw) sum += v;
        double avg = sum / raw.size();

        std::vector<double> padded((size_t)side * side, avg);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                padded[(size_t)(y + oy) * side + (x + ox)] = raw[(size_t)y * w + x];
        raw = std::move(padded);
        minVal = *std::min_element(raw.begin(), raw.end());
        maxVal = *std::max_element(raw.begin(), raw.end());

        double range = maxVal - minVal;
        double scale = (range > 0) ? 255.0 / range : 1.0;
        img = QImage(side, side, QImage::Format_Grayscale8);
        for (int y = 0; y < side; y++) {
            uchar *row = img.scanLine(y);
            for (int x = 0; x < side; x++)
                row[x] = static_cast<uchar>(std::clamp(
                    (raw[(size_t)y * side + x] - minVal) * scale, 0.0, 255.0));
        }
    } else {
        QImage gray = img.convertToFormat(QImage::Format_Grayscale8);
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
        img = paddedImg;
    }
}

FtWindow::SlotImageData FtWindow::readSlotImage(const QString &path)
{
    SlotImageData d;
    if (path.isEmpty() || !QFile::exists(path)) return d;

    if (path.endsWith(".mrc", Qt::CaseInsensitive)) {
        MrcResult r  = loadMrc(path);
        d.image      = r.image;
        d.rawPixels  = std::move(r.rawPixels);
        d.minVal     = r.minVal;
        d.maxVal     = r.maxVal;
        d.pixelSize  = r.pixelSize;
    } else {
        d.image = QImage(path);
        if (!d.image.isNull()) {
            QImage gray = d.image.convertToFormat(QImage::Format_Grayscale8);
            int w = gray.width(), h = gray.height();
            d.rawPixels.resize((size_t)w * h);
            for (int y = 0; y < h; y++) {
                const uchar *row = gray.constScanLine(y);
                for (int x = 0; x < w; x++)
                    d.rawPixels[(size_t)y * w + x] = row[x];
            }
            d.minVal = *std::min_element(d.rawPixels.begin(), d.rawPixels.end());
            d.maxVal = *std::max_element(d.rawPixels.begin(), d.rawPixels.end());
        }
    }

    if (d.image.isNull()) return d;

    padSlotImageToSquare(d.image, d.rawPixels, d.minVal, d.maxVal);
    d.powerSpec = computePowerSpecMasked(d.image);
    d.ok = true;
    return d;
}

bool FtWindow::loadHistorySlotFromDisk(int i)
{
#ifdef __EMSCRIPTEN__
    Q_UNUSED(i);
    return false;
#else
    if (i < 0 || i >= HISTORY_SLOTS) return false;

    SlotImageData d = readSlotImage(m_history[i].path);
    if (!d.ok) {
        const QString path = m_history[i].path;
        m_history[i].occupied = false;
        m_history[i].deferred = false;
        m_history[i].loading  = false;
        if (path.isEmpty() || !QFile::exists(path))
            m_history[i].path.clear();
        return false;
    }

    m_history[i].image        = d.image;
    m_history[i].rawPixels    = std::move(d.rawPixels);
    m_history[i].minVal       = d.minVal;
    m_history[i].maxVal       = d.maxVal;
    m_history[i].pixelSize    = d.pixelSize;
    m_history[i].powerSpecImg = d.powerSpec;
    m_history[i].occupied     = true;
    m_history[i].deferred     = false;
    m_history[i].loading      = false;
    return true;
#endif
}

void FtWindow::startSlotLoad(int i)
{
#ifndef __EMSCRIPTEN__
    if (i < 0 || i >= HISTORY_SLOTS) return;
    if (m_history[i].loading) return;              // already on its way
    const QString path = m_history[i].path;
    if (path.isEmpty()) return;

    m_history[i].deferred = false;
    m_history[i].loading  = true;

#if FT_HAVE_THREADS
    const quint64 token = ++m_slotLoadToken[i];
    auto life = m_life;

    std::thread worker([this, life, i, token, path]() {
        SlotImageData data = readSlotImage(path);

        // Only post back while the window provably still exists. ~QObject
        // discards events posted to it, so a window destroyed after this point
        // never sees the result either.
        std::lock_guard<std::mutex> lock(life->mutex);
        if (!life->alive) return;
        QMetaObject::invokeMethod(
            this,
            [this, i, token, data = std::move(data)]() mutable {
                finishSlotLoad(i, token, std::move(data));
            },
            Qt::QueuedConnection);
    });
    worker.detach();
#else
    // No threads available: read it inline and install the result directly.
    finishSlotLoad(i, m_slotLoadToken[i], readSlotImage(path));
#endif
    update();
#else
    Q_UNUSED(i);
#endif
}

// Discard the result of an in-flight read of slot `i` (the worker thread runs to
// completion, but the token no longer matches, so nothing is installed).
void FtWindow::cancelSlotLoad(int i)
{
    if (i < 0 || i >= HISTORY_SLOTS) return;
    ++m_slotLoadToken[i];
    m_history[i].loading = false;
}

void FtWindow::finishSlotLoad(int i, quint64 token, SlotImageData data)
{
    if (i < 0 || i >= HISTORY_SLOTS) return;
    if (token != m_slotLoadToken[i]) return;   // cancelled or superseded meanwhile

    m_history[i].loading = false;

    if (!data.ok) {
        // File vanished or is unreadable: drop the slot entirely.
        m_history[i] = HistoryEntry();
        if (m_activeSlot == i) {
            m_image = QImage();
            m_imagePath.clear();
            m_imageRawPixels.clear();
            m_ftComputed = false;
        }
        saveHistory();
        update();
        return;
    }

    m_history[i].image        = data.image;
    m_history[i].rawPixels    = data.rawPixels;
    m_history[i].minVal       = data.minVal;
    m_history[i].maxVal       = data.maxVal;
    m_history[i].pixelSize    = data.pixelSize;
    m_history[i].powerSpecImg = data.powerSpec;
    m_history[i].occupied     = true;
    m_history[i].deferred     = false;

    // Only pull it into the panels if it is still the buffer the user is on;
    // they may have clicked elsewhere while it was reading.
    if (m_activeSlot == i) {
        m_image          = m_history[i].image;
        m_imagePath      = m_history[i].path;
        m_imageRawPixels = m_history[i].rawPixels;
        m_imageMinVal    = m_history[i].minVal;
        m_imageMaxVal    = m_history[i].maxVal;
        m_imageDispMin   = m_history[i].minVal;
        m_imageDispMax   = m_history[i].maxVal;
        m_pixelSize      = m_history[i].pixelSize;
        if (!m_image.isNull()) {
            m_zoom[0].reset(m_image.width(), m_image.height());
            computeFFT(true);
        }
    }

    saveHistory();
    update();
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
        m_history[i].occupied = false;
        m_history[i].deferred = false;
        if (path.isEmpty() || !QFile::exists(path)) {
            m_history[i].path.clear();
            continue;
        }

        m_history[i].path = path;

        // Restoring a slot costs a full file read plus a forward FFT, so at
        // startup only cheap slots are loaded: small images that live on a
        // local disk. Everything else is kept as a deferred placeholder and
        // loaded the moment the user clicks it.
        QString reason;
        if (!isRestoreCheapAtStartup(path, &reason)) {
            qDebug() << "History slot" << i << "deferred (" << reason << "):" << path;
            m_history[i].deferred = true;
            continue;
        }

        loadHistorySlotFromDisk(i);
    }
#endif // !__EMSCRIPTEN__
}

FtWindow::BufferSnapshot FtWindow::captureCurrentState() const
{
    BufferSnapshot snapshot;
    snapshot.valid = true;
    snapshot.activeSlot = m_activeSlot;
    for (int i = 0; i < HISTORY_SLOTS; i++) {
        snapshot.history[i] = m_history[i];
        // A read in flight is not part of the state we can restore later: undoing
        // back to it must offer the slot as a deferred placeholder ("click to
        // load"), not as a load that nobody is running any more.
        if (snapshot.history[i].loading) {
            snapshot.history[i].loading  = false;
            snapshot.history[i].deferred = true;
        }
    }
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
    snapshot.ftInverseOutput = m_ftInverseOutput;
    return snapshot;
}

void FtWindow::applySnapshot(const BufferSnapshot &snapshot, bool keepZoom)
{
    if (!snapshot.valid) return;

    m_activeSlot = snapshot.activeSlot;
    for (int i = 0; i < HISTORY_SLOTS; i++) {
        // Any read still in flight would land on top of the state we are about
        // to restore, so drop its result.
        if (m_history[i].loading)
            cancelSlotLoad(i);
        m_history[i] = snapshot.history[i];
    }
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
    m_ftInverseOutput = snapshot.ftInverseOutput;

    if (!keepZoom) {
        if (!m_image.isNull())
            m_zoom[0].reset(m_image.width(), m_image.height());
        if (m_ftComputed && m_fftN > 0) {
            m_zoom[1].reset(m_fftN, m_fftN);
            m_zoom[2].reset(m_fftN, m_fftN);
        }
    }
    if (m_ftComputed && m_fftN > 0)
        recomputeDisplayImages();

    saveHistory();
    update();
}

void FtWindow::clearRedoStack()
{
    m_redoStack.clear();
    updateUndoRedoButtons();
}

qint64 FtWindow::snapshotBytes(const BufferSnapshot &s)
{
    auto entryBytes = [](const HistoryEntry &e) -> qint64 {
        return (qint64)e.rawPixels.size() * (qint64)sizeof(double)
             + (qint64)e.image.sizeInBytes()
             + (qint64)e.powerSpecImg.sizeInBytes();
    };
    qint64 b = 0;
    for (int i = 0; i < HISTORY_SLOTS; i++)
        b += entryBytes(s.history[i]);
    b += (qint64)s.image.sizeInBytes();
    b += (qint64)s.imageRawPixels.size() * (qint64)sizeof(double);
    b += (qint64)s.fftData.size() * (qint64)sizeof(Complex);
    return b;
}

void FtWindow::trimUndoMemory()
{
    auto total = [this]() -> qint64 {
        qint64 b = 0;
        for (const auto &s : m_undoStack) b += snapshotBytes(s);
        for (const auto &s : m_redoStack) b += snapshotBytes(s);
        return b;
    };
    // Sacrifice redo history first, then the oldest undo steps, but always
    // keep the most recent undo step so a single Undo remains possible.
    while (total() > kUndoBudgetBytes && !m_redoStack.empty())
        m_redoStack.pop_front();
    while (total() > kUndoBudgetBytes && m_undoStack.size() > 1)
        m_undoStack.pop_front();
}

void FtWindow::storeUndoSnapshot()
{
    // Probe first: if there isn't room to copy the current state, drop the
    // undo history rather than risk an allocation that would abort the app.
    // The calculation that requested the snapshot still runs, just without an
    // undo step.
    if (!probeAlloc(currentStateBytes())) {
        m_undoStack.clear();
        m_redoStack.clear();
        updateUndoRedoButtons();
        qWarning() << "Undo history dropped – insufficient memory for snapshot";
        return;
    }
    m_undoStack.push_back(captureCurrentState());
    if ((int)m_undoStack.size() > MAX_UNDO)
        m_undoStack.pop_front();
    clearRedoStack();
    trimUndoMemory();
    updateUndoRedoButtons();
}

void FtWindow::updateUndoRedoButtons()
{
    if (m_undoBtn) {
        int n = (int)m_undoStack.size();
        m_undoBtn->setText(QString("Undo (%1)").arg(n));
        m_undoBtn->setEnabled(n > 0);
    }
    if (m_redoBtn) {
        int n = (int)m_redoStack.size();
        m_redoBtn->setText(QString("Redo (%1)").arg(n));
        m_redoBtn->setEnabled(n > 0);
    }
}

void FtWindow::onUndo()
{
    if (m_undoStack.empty()) return;
    // Saving the current state for Redo is best-effort: if memory is too tight
    // to capture it, skip Redo rather than block the Undo the user asked for.
    if (probeAlloc(currentStateBytes())) {
        m_redoStack.push_back(captureCurrentState());
        if ((int)m_redoStack.size() > MAX_UNDO)
            m_redoStack.pop_front();
    } else {
        m_redoStack.clear();
    }
    applySnapshot(m_undoStack.back(), true);
    m_undoStack.pop_back();
    trimUndoMemory();
    updateUndoRedoButtons();
}

void FtWindow::onRedo()
{
    if (m_redoStack.empty()) return;
    if (probeAlloc(currentStateBytes())) {
        m_undoStack.push_back(captureCurrentState());
        if ((int)m_undoStack.size() > MAX_UNDO)
            m_undoStack.pop_front();
    } else {
        m_undoStack.clear();
    }
    applySnapshot(m_redoStack.back(), true);
    m_redoStack.pop_back();
    trimUndoMemory();
    updateUndoRedoButtons();
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

            bool erasePhase = (di.rawVals == &m_phaseVals);

            for (int dy = -rad; dy <= rad; dy++) {
                for (int dx = -rad; dx <= rad; dx++) {
                    int px = ix + dx, py = iy + dy;
                    if (px < 0 || px >= m_fftN || py < 0 || py >= m_fftN) continue;

                    double weight = 1.0;
                    if (sigma > 0.5)
                        weight = std::exp(-(dx*dx + dy*dy) / (2.0 * sigma * sigma));

                    int idx  = py * m_fftN + px;
                    int fpx  = (m_fftN - px) % m_fftN;
                    int fpy  = (m_fftN - py) % m_fftN;
                    int fidx = fpy * m_fftN + fpx;

                    if (erasePhase) {
                        // Blend phase toward zero, keep amplitude
                        Complex cur = m_fftData[idx];
                        double amp = std::abs(cur);
                        double phase = std::arg(cur);
                        double newPhase = phase * (1.0 - weight);
                        Complex newVal = std::polar(amp, newPhase);
                        m_fftData[idx]  = newVal;
                        m_fftData[fidx] = std::conj(newVal);
                    } else {
                        m_fftData[idx]  *= (1.0 - weight);
                        m_fftData[fidx] *= (1.0 - weight);
                    }
                }
            }
            recomputeDisplayImages();
            repaint();
        }
        return;
    }
}

double FtWindow::brushValue() const
{
    if (!m_ftComputed || m_fftN == 0) return 0.0;
    switch (m_displayMode) {
    case 0:  return m_cosMax;
    case 1:  return m_ampMax;
    case 2:  return m_cosMax;
    case 3:  return m_powerMax;
    default: return 0.0;
    }
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

            // Determine paint mode from which display data the panel shows
            bool paintSin   = (di.rawVals == &m_sinVals);
            bool paintPhase = (di.rawVals == &m_phaseVals);
            bool paintPower = (di.rawVals == &m_powerVals);
            bool paintAmp   = (di.rawVals == &m_ampVals);

            for (int dy = -rad; dy <= rad; dy++) {
                for (int dx = -rad; dx <= rad; dx++) {
                    int px = ix + dx, py = iy + dy;
                    if (px < 0 || px >= m_fftN || py < 0 || py >= m_fftN) continue;

                    double weight = 1.0;
                    if (sigma > 0.5)
                        weight = std::exp(-(dx*dx + dy*dy) / (2.0 * sigma * sigma));

                    int idx  = py * m_fftN + px;
                    int fpx  = (m_fftN - px) % m_fftN;
                    int fpy  = (m_fftN - py) % m_fftN;
                    int fidx = fpy * m_fftN + fpx;

                    if (paintPower) {
                        // Powerspectrum mode: val is desired change in
                        // log(1+amp^2) display units; solve for new real part
                        Complex cur = m_fftData[idx];
                        double a = cur.real(), b = cur.imag();
                        double curPow = std::log(1.0 + a * a + b * b);
                        double newPow = curPow + val * weight;
                        double newA2 = std::exp(newPow) - 1.0 - b * b;
                        double newA = (newA2 > 0) ? std::copysign(std::sqrt(newA2), a) : 0.0;
                        double delta = newA - a;
                        m_fftData[idx]  += Complex(delta, 0);
                        m_fftData[fidx] += Complex(delta, 0);  // symmetric
                    } else if (paintAmp) {
                        // Amplitude panel: val is in log(1+amp) display units,
                        // convert to target amplitude and set real part (cosine)
                        double targetAmp = std::max(0.0, std::exp(val) - 1.0);
                        Complex cur = m_fftData[idx];
                        double curAmp = std::abs(cur);
                        double phase  = (curAmp > 0) ? std::arg(cur) : 0.0;
                        double newAmp = curAmp + weight * (targetAmp - curAmp);
                        Complex newVal = std::polar(newAmp, phase);
                        m_fftData[idx]  = newVal;
                        m_fftData[fidx] = std::conj(newVal);   // Friedel mate
                    } else if (paintPhase) {
                        // Phase panel: val is in degrees, set phase keeping amplitude
                        double targetPhase = val * M_PI / 180.0;
                        Complex cur = m_fftData[idx];
                        double curAmp = std::abs(cur);
                        double curPhase = std::arg(cur);
                        double newPhase = curPhase + weight * (targetPhase - curPhase);
                        Complex newVal = std::polar(curAmp, newPhase);
                        m_fftData[idx]  = newVal;
                        m_fftData[fidx] = std::conj(newVal);   // Friedel mate
                    } else if (paintSin) {
                        // paint into imaginary (sine) component
                        double paintVal = val * weight;
                        m_fftData[idx]  += Complex(0, paintVal);
                        m_fftData[fidx] += Complex(0, -paintVal); // antisymmetric
                    } else {
                        // cosine (real) component – original behaviour
                        double paintVal = val * weight;
                        m_fftData[idx]  += Complex(paintVal, 0);
                        m_fftData[fidx] += Complex(paintVal, 0);  // symmetric
                    }
                }
            }
            recomputeDisplayImages();
            repaint();
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
    if (!m_imageContrastLocked) {
        m_imageDispMin = m_imageMinVal;
        m_imageDispMax = m_imageMaxVal;
    }
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

// ---------------------------------------------------------------------------
//  Reset the complex-FT brightness range to its automatic default: from the
//  global minimum power up to the maximum power excluding the central 3x3
//  pixels, so the huge DC peak does not crush the displayed dynamic range.
// ---------------------------------------------------------------------------
void FtWindow::resetComplexDisplayRange()
{
    int N = m_fftN;
    if (N == 0 || (int)m_powerVals.size() != N * N) return;

    int half = N / 2;
    double pMin = std::numeric_limits<double>::infinity();
    double pMax = -std::numeric_limits<double>::infinity();
    double pMaxAll = pMax;
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            double v = m_powerVals[y * N + x];
            if (v < pMin) pMin = v;
            if (v > pMaxAll) pMaxAll = v;
            bool nearCenter = std::abs(x - half) <= 1 && std::abs(y - half) <= 1;
            if (!nearCenter && v > pMax) pMax = v;
        }
    }
    if (pMax <= pMin) pMax = pMaxAll;

    m_complexDispMin = pMin;
    m_complexDispMax = pMax;
    m_complexRangeCustom = false;
}

// ---------------------------------------------------------------------------
//  (Re)build the coloured complex-FT image: brightness from the power
//  spectrum mapped through [m_complexDispMin, m_complexDispMax], hue from the
//  phase. A near-zero range (e.g. a uniformly-bright FT such as a phase ramp,
//  whose power varies only by floating-point round-off) is treated as flat so
//  the pure hue shows instead of amplified noise.
// ---------------------------------------------------------------------------
void FtWindow::buildComplexImage()
{
    int N = m_fftN;
    if (N == 0 || (int)m_powerVals.size() != N * N
        || (int)m_phaseVals.size() != N * N) return;

    double pMin = m_complexDispMin;
    double pMax = m_complexDispMax;
    double range = pMax - pMin;
    double mag = std::max({std::abs(pMax), std::abs(pMin), 1.0});
    bool flatBrightness = range <= 1e-9 * mag;
    double pScale = flatBrightness ? 0.0 : 1.0 / range;

    m_complexImg = QImage(N, N, QImage::Format_RGB32);
    // Detach/allocate once on this thread, then address rows by raw offset
    // so the parallel workers never call scanLine() concurrently.
    uchar *base = m_complexImg.bits();
    qsizetype bpl = m_complexImg.bytesPerLine();
    parallelFor(0, N, [&](int y) {
        QRgb *row = reinterpret_cast<QRgb *>(base + y * bpl);
        for (int x = 0; x < N; x++) {
            int idx = y * N + x;
            double val = flatBrightness
                ? 1.0
                : std::clamp((m_powerVals[idx] - pMin) * pScale, 0.0, 1.0);
            double hue = m_phaseVals[idx] + 180.0;
            QColor c = QColor::fromHsvF(hue / 360.0, 1.0, val);
            row[x] = c.rgb();
        }
    });
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
        double solidDiam = m_p1BrushSolidDiameterEdit->text().toDouble();
        double gaussDiam = m_p1BrushDiameterEdit->text().toDouble();
        double solidR = solidDiam / 2.0;
        double sigma = gaussDiam / 2.0;
        bool hasSolid = solidR > 0.0;
        bool hasGauss = sigma > 0.5;

        int rad;
        if (hasSolid && hasGauss)      rad = (int)std::ceil(solidR + 3.0 * sigma);
        else if (hasSolid)             rad = (int)std::ceil(solidR);
        else if (hasGauss)             rad = (int)std::ceil(3.0 * sigma);
        else                           rad = 0;

        int K = 2 * rad + 1;
        std::vector<double> kernel(K * K, 0.0);

        if (hasSolid) {
            double sr2 = solidR * solidR;
            for (int dy = -rad; dy <= rad; dy++) {
                for (int dx = -rad; dx <= rad; dx++) {
                    if (dx * dx + dy * dy <= sr2)
                        kernel[(dy + rad) * K + (dx + rad)] = 1.0;
                }
            }
            if (hasGauss) {
                int gr = (int)std::ceil(3.0 * sigma);
                std::vector<double> g(2 * gr + 1);
                for (int i = -gr; i <= gr; i++)
                    g[i + gr] = std::exp(-(i * i) / (2.0 * sigma * sigma));
                std::vector<double> tmp(K * K, 0.0);
                for (int y = 0; y < K; y++) {
                    for (int x = 0; x < K; x++) {
                        double s = 0.0, wsum = 0.0;
                        for (int i = -gr; i <= gr; i++) {
                            int xi = x + i;
                            if (xi < 0 || xi >= K) continue;
                            s += kernel[y * K + xi] * g[i + gr];
                            wsum += g[i + gr];
                        }
                        tmp[y * K + x] = (wsum > 0.0) ? s / wsum : 0.0;
                    }
                }
                for (int y = 0; y < K; y++) {
                    for (int x = 0; x < K; x++) {
                        double s = 0.0, wsum = 0.0;
                        for (int i = -gr; i <= gr; i++) {
                            int yi = y + i;
                            if (yi < 0 || yi >= K) continue;
                            s += tmp[yi * K + x] * g[i + gr];
                            wsum += g[i + gr];
                        }
                        kernel[y * K + x] = (wsum > 0.0) ? s / wsum : 0.0;
                    }
                }
            }
        } else if (hasGauss) {
            for (int dy = -rad; dy <= rad; dy++) {
                for (int dx = -rad; dx <= rad; dx++) {
                    kernel[(dy + rad) * K + (dx + rad)] =
                        std::exp(-(dx * dx + dy * dy) / (2.0 * sigma * sigma));
                }
            }
        } else {
            kernel[0] = 1.0;
        }

        double kmax = 0.0;
        for (double v : kernel) if (v > kmax) kmax = v;
        if (kmax > 0.0) for (auto &v : kernel) v /= kmax;

        for (int dy = -rad; dy <= rad; dy++) {
            for (int dx = -rad; dx <= rad; dx++) {
                int px = ix + dx, py = iy + dy;
                if (px < 0 || px >= w || py < 0 || py >= h) continue;
                double weight = kernel[(dy + rad) * K + (dx + rad)];
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
    if (!ensureCalcHeadroom(tr("apply the bandpass filter"))) return;
    onApplyBandpassImpl();
}

void FtWindow::onApplyBandpassImpl()
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

void FtWindow::syncLatticeVectorEdits()
{
    auto fmt = [](double v) { return QString::number(v, 'f', 1); };
    if (m_latticeUxEdit) m_latticeUxEdit->setText(fmt(m_latticeUx));
    if (m_latticeUyEdit) m_latticeUyEdit->setText(fmt(m_latticeUy));
    if (m_latticeVxEdit) m_latticeVxEdit->setText(fmt(m_latticeVx));
    if (m_latticeVyEdit) m_latticeVyEdit->setText(fmt(m_latticeVy));
}

void FtWindow::onApplyLattice()
{
    if (!ensureCalcHeadroom(tr("apply the lattice filter"))) return;
    onApplyLatticeImpl();
}

void FtWindow::onApplyLatticeImpl()
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
    if (!ensureCalcHeadroom(tr("bin the image"))) return;
    onApplyBinningImpl();
}

void FtWindow::onApplyBinningImpl()
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

void FtWindow::syncCropEdits()
{
    if (!m_cropTLxEdit) return;
    QSignalBlocker b1(m_cropTLxEdit), b2(m_cropTLyEdit),
                   b3(m_cropBRxEdit), b4(m_cropBRyEdit);
    m_cropTLxEdit->setText(QString::number(m_cropRect.left()));
    m_cropTLyEdit->setText(QString::number(m_cropRect.top()));
    m_cropBRxEdit->setText(QString::number(m_cropRect.left() + m_cropRect.width()));
    m_cropBRyEdit->setText(QString::number(m_cropRect.top()  + m_cropRect.height()));
}

void FtWindow::onCropCancel()
{
    m_cropActive = false;
    m_cropDragging = false;
    m_cropHasSelection = false;
    m_cropTLxEdit->hide();
    m_cropTLyEdit->hide();
    m_cropBRxEdit->hide();
    m_cropBRyEdit->hide();
    m_cropCancelBtn->hide();
    m_applyCropBtn->hide();
    update();
}

void FtWindow::onApplyCrop()
{
    if (m_image.isNull() || !m_cropHasSelection) return;

    int W = m_image.width(), H = m_image.height();
    int x0 = std::clamp(m_cropRect.left(), 0, W);
    int y0 = std::clamp(m_cropRect.top(),  0, H);
    int side = m_cropRect.width();
    side = std::min({side, W - x0, H - y0});
    if (side < 1) return;

    std::vector<double> &pix = m_imageRawPixels;
    if ((int)pix.size() != W * H) return;

    storeUndoSnapshot();

    int newW = side, newH = side;
    std::vector<double> newPix((size_t)newW * newH);
    for (int y = 0; y < newH; y++)
        for (int x = 0; x < newW; x++)
            newPix[(size_t)y * newW + x] = pix[(size_t)(y0 + y) * W + (x0 + x)];

    m_imageRawPixels = std::move(newPix);
    m_imageMinVal = *std::min_element(m_imageRawPixels.begin(), m_imageRawPixels.end());
    m_imageMaxVal = *std::max_element(m_imageRawPixels.begin(), m_imageRawPixels.end());
    if (!m_imageContrastLocked) {
        m_imageDispMin = m_imageMinVal;
        m_imageDispMax = m_imageMaxVal;
    }
    double range = m_imageMaxVal - m_imageMinVal;
    double scale = (range > 0) ? 255.0 / range : 1.0;
    m_image = QImage(newW, newH, QImage::Format_Grayscale8);
    for (int y = 0; y < newH; y++) {
        uchar *row = m_image.scanLine(y);
        for (int x = 0; x < newW; x++)
            row[x] = static_cast<uchar>(std::clamp(
                (m_imageRawPixels[(size_t)y * newW + x] - m_imageMinVal) * scale, 0.0, 255.0));
    }
    m_zoom[0].reset(newW, newH);
    // Cropping does not change the sampling, so m_pixelSize is unchanged.

    m_cropHasSelection = false;
    if (m_ftComputed) computeFFT();
    update();
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
    if (!ensureCalcHeadroom(tr("invert the contrast"))) return;
    onInvertContrastImpl();
}

void FtWindow::onInvertContrastImpl()
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
    if (!ensureCalcHeadroom(tr("apply the edge taper"))) return;
    onApplyEdgeTaperImpl();
}

void FtWindow::onApplyEdgeTaperImpl()
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

void FtWindow::onApplySymmetry()
{
    if (!ensureCalcHeadroom(tr("apply the symmetry"))) return;
    onApplySymmetryImpl();
}

void FtWindow::onApplySymmetryImpl()
{
    if (m_image.isNull()) return;

    int w = m_image.width();
    int h = m_image.height();
    if ((int)m_imageRawPixels.size() != w * h) return;

    bool ok = false;
    int order = m_p1SymmetryEdit->text().toInt(&ok);
    if (!ok || order < 1) return;
    if (order > 64) order = 64;   // sanity clamp
    if (order == 1) return;       // nothing to do

    storeUndoSnapshot();

    // Mean pixel value used to fill samples that fall outside the image
    double meanVal = 0.0;
    for (double v : m_imageRawPixels) meanVal += v;
    meanVal /= static_cast<double>(w * h);

    auto src = std::make_shared<std::vector<double>>(m_imageRawPixels);
    auto dst = std::make_shared<std::vector<double>>(w * h, 0.0);

    m_toolProgress = 0.05;
    update();

    std::vector<std::function<void()>> steps;
    for (int k = 0; k < order; k++) {
        steps.push_back([this, src, dst, k, order, w, h, meanVal]() {
            const double cx = w / 2;
            const double cy = h / 2;
            const double theta = 2.0 * M_PI * k / static_cast<double>(order);
            const double c = std::cos(theta);
            const double s = std::sin(theta);
            for (int y = 0; y < h; y++) {
                double dy = y - cy;
                for (int x = 0; x < w; x++) {
                    double dx = x - cx;
                    // Source coords obtained by inverse-rotating output coords
                    double sx = cx + dx * c + dy * s;
                    double sy = cy - dx * s + dy * c;
                    double sample;
                    int x0 = static_cast<int>(std::floor(sx));
                    int y0 = static_cast<int>(std::floor(sy));
                    if (x0 < 0 || x0 >= w - 1 || y0 < 0 || y0 >= h - 1) {
                        sample = meanVal;
                    } else {
                        double fx = sx - x0;
                        double fy = sy - y0;
                        const double *p = src->data() + y0 * w + x0;
                        double v00 = p[0];
                        double v10 = p[1];
                        double v01 = p[w];
                        double v11 = p[w + 1];
                        sample = (1.0 - fx) * (1.0 - fy) * v00
                               +        fx  * (1.0 - fy) * v10
                               + (1.0 - fx) *        fy  * v01
                               +        fx  *        fy  * v11;
                    }
                    (*dst)[y * w + x] += sample;
                }
            }
            m_toolProgress = 0.05 + 0.85 * (k + 1) / static_cast<double>(order);
        });
    }

    steps.push_back([this, dst, w, h, order]() {
        double invN = 1.0 / static_cast<double>(order);
        for (int i = 0; i < w * h; i++)
            m_imageRawPixels[i] = (*dst)[i] * invN;
        rebuildImageFromRaw();
        m_toolProgress = 0.95;
    });

    steps.push_back([this]() {
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
    });

    chainSteps(std::move(steps));
}

void FtWindow::onApplyFtSymmetry()
{
    if (!ensureCalcHeadroom(tr("apply the Fourier symmetry"))) return;
    onApplyFtSymmetryImpl();
}

void FtWindow::onApplyFtSymmetryImpl()
{
    if (!m_ftComputed || m_fftN == 0) return;

    bool ok = false;
    int order = m_p2SymmetryEdit->text().toInt(&ok);
    if (!ok || order < 1) return;
    if (order > 64) order = 64;
    if (order == 1) return;

    storeUndoSnapshot();

    const int N = m_fftN;
    const int halfN = N / 2;
    // Rotation centre in real-space pixel coordinates. Matches the panel-1
    // symmetrize convention: integer pixel (w/2, h/2) of the original image.
    const double rcx = (m_origW > 0 ? m_origW : N) / 2;
    const double rcy = (m_origH > 0 ? m_origH : N) / 2;

    // Un-shift current FFT to DC-at-(0,0) layout for rotation arithmetic.
    auto src = std::make_shared<std::vector<Complex>>(m_fftData);
    fftShift(*src, N);
    auto acc = std::make_shared<std::vector<Complex>>(N * N, Complex(0, 0));

    m_toolProgress = 0.05;
    update();

    std::vector<std::function<void()>> steps;
    for (int k = 0; k < order; k++) {
        steps.push_back([this, src, acc, k, order, N, halfN, rcx, rcy]() {
            const double angle = 2.0 * M_PI * k / static_cast<double>(order);
            // Inverse rotation maps target frequency back to source frequency.
            const double cosA = std::cos(-angle);
            const double sinA = std::sin(-angle);
            for (int v = 0; v < N; v++) {
                for (int u = 0; u < N; u++) {
                    double us = (u <= halfN) ? (double)u : (double)(u - N);
                    double vs = (v <= halfN) ? (double)v : (double)(v - N);

                    double uSrcF = us * cosA - vs * sinA;
                    double vSrcF = us * sinA + vs * cosA;

                    int uSrcI = (int)std::round(uSrcF);
                    int vSrcI = (int)std::round(vSrcF);

                    int su = ((uSrcI % N) + N) % N;
                    int sv = ((vSrcI % N) + N) % N;

                    // Phase correction so the rotation centre matches the
                    // original image centre, not the array origin.
                    double du = (double)uSrcI - us;
                    double dv = (double)vSrcI - vs;
                    double phase = -2.0 * M_PI * (du * rcx + dv * rcy) / N;
                    Complex phasor(std::cos(phase), std::sin(phase));

                    (*acc)[v * N + u] += (*src)[sv * N + su] * phasor;
                }
            }
            m_toolProgress = 0.05 + 0.80 * (k + 1) / static_cast<double>(order);
        });
    }

    steps.push_back([this, acc, N, order]() {
        double invN = 1.0 / static_cast<double>(order);
        for (auto &c : *acc) c *= invN;
        // Re-shift back to centred layout for m_fftData storage.
        fftShift(*acc, N);
        m_fftData = std::move(*acc);
        recomputeDisplayImages();
        m_toolProgress = 0.90;
    });

    steps.push_back([this]() {
        computeInverseFFT();
        if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS) {
            m_history[m_activeSlot].powerSpecImg = computePowerSpecMasked(m_image);
        }
        saveHistory();
        m_toolProgress = -1;
        update();
    });

    chainSteps(std::move(steps));
}

void FtWindow::onGaborCancel()
{
    m_gaborActive = false;
    m_gaborSigmaEdit->hide();
    m_gaborLambdaEdit->hide();
    m_gaborThetaEdit->hide();
    m_gaborGammaEdit->hide();
    m_gaborCancelBtn->hide();
    m_gaborComputeBtn->hide();
    update();
}

void FtWindow::onApplyGaborFilter()
{
    if (!ensureCalcHeadroom(tr("apply the Gabor filter"))) return;
    onApplyGaborFilterImpl();
}

void FtWindow::onApplyGaborFilterImpl()
{
    if (m_image.isNull()) return;

    // Read parameters from the parameter window
    bool okS = false, okL = false, okT = false, okG = false;
    double sigma  = m_gaborSigmaEdit->text().toDouble(&okS);
    double lambda = m_gaborLambdaEdit->text().toDouble(&okL);
    double thetaDeg = m_gaborThetaEdit->text().toDouble(&okT);
    double gamma  = m_gaborGammaEdit->text().toDouble(&okG);
    if (!okS || sigma  <= 0.0) sigma  = 4.0;
    if (!okL || lambda <= 0.0) lambda = 8.0;
    if (!okT)                  thetaDeg = 0.0;
    if (!okG || gamma  <= 0.0) gamma  = 0.5;
    const double theta = thetaDeg * M_PI / 180.0;
    const double psi   = 0.0;

    storeUndoSnapshot();

    int w = m_image.width();
    int h = m_image.height();
    if ((int)m_imageRawPixels.size() != w * h) return;

    int khalf = std::max(1, (int)std::ceil(3.0 * sigma));
    int ksize = 2 * khalf + 1;

    std::vector<double> kernel(ksize * ksize);
    double kernelSum = 0.0;
    double cosT = std::cos(theta);
    double sinT = std::sin(theta);
    for (int ky = -khalf; ky <= khalf; ky++) {
        for (int kx = -khalf; kx <= khalf; kx++) {
            double xr =  kx * cosT + ky * sinT;
            double yr = -kx * sinT + ky * cosT;
            double env = std::exp(-(xr * xr + gamma * gamma * yr * yr)
                                   / (2.0 * sigma * sigma));
            double carrier = std::cos(2.0 * M_PI * xr / lambda + psi);
            double v = env * carrier;
            kernel[(ky + khalf) * ksize + (kx + khalf)] = v;
            kernelSum += v;
        }
    }
    // Zero-mean kernel so DC is removed — typical for Gabor edge/texture response.
    double kmean = kernelSum / (ksize * ksize);
    for (double &v : kernel) v -= kmean;

    m_toolProgress = 0.1;
    update();

    chainSteps({
        [this, w, h, ksize, khalf, kernel]() {
            std::vector<double> out(w * h, 0.0);
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    double acc = 0.0;
                    for (int ky = -khalf; ky <= khalf; ky++) {
                        int sy = y + ky;
                        if (sy < 0) sy = -sy;
                        else if (sy >= h) sy = 2 * h - 2 - sy;
                        if (sy < 0 || sy >= h) continue;
                        for (int kx = -khalf; kx <= khalf; kx++) {
                            int sx = x + kx;
                            if (sx < 0) sx = -sx;
                            else if (sx >= w) sx = 2 * w - 2 - sx;
                            if (sx < 0 || sx >= w) continue;
                            acc += m_imageRawPixels[sy * w + sx]
                                 * kernel[(ky + khalf) * ksize + (kx + khalf)];
                        }
                    }
                    out[y * w + x] = acc;
                }
            }
            m_imageRawPixels = std::move(out);

            // Recompute min/max from filtered values
            m_imageMinVal = m_imageRawPixels[0];
            m_imageMaxVal = m_imageRawPixels[0];
            for (double v : m_imageRawPixels) {
                if (v < m_imageMinVal) m_imageMinVal = v;
                if (v > m_imageMaxVal) m_imageMaxVal = v;
            }
            if (!m_imageContrastLocked) {
                m_imageDispMin = m_imageMinVal;
                m_imageDispMax = m_imageMaxVal;
            }

            rebuildImageFromRaw();
            m_toolProgress = 0.5;
        },
        [this]() {
            if (m_ftComputed)
                computeFFT();

            if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS) {
                m_history[m_activeSlot].image     = m_image;
                m_history[m_activeSlot].rawPixels = m_imageRawPixels;
                m_history[m_activeSlot].minVal    = m_imageMinVal;
                m_history[m_activeSlot].maxVal    = m_imageMaxVal;
                m_history[m_activeSlot].pixelSize = m_pixelSize;
                m_history[m_activeSlot].occupied  = true;
                if (m_ftComputed)
                    m_history[m_activeSlot].powerSpecImg = computePowerSpecMasked(m_image);
            }

            saveHistory();
            m_toolProgress = -1;
            update();
        }
    });
}

void FtWindow::onHessianCancel()
{
    m_hessianActive = false;
    m_hessianSigmaEdit->hide();
    m_hessianPolarityEdit->hide();
    m_hessianCancelBtn->hide();
    m_hessianComputeBtn->hide();
    update();
}

void FtWindow::onApplyHessianFilter()
{
    if (!ensureCalcHeadroom(tr("apply the Hessian filter"))) return;
    onApplyHessianFilterImpl();
}

void FtWindow::onApplyHessianFilterImpl()
{
    if (m_image.isNull()) return;

    bool okS = false, okP = false;
    double sigma    = m_hessianSigmaEdit->text().toDouble(&okS);
    double polarity = m_hessianPolarityEdit->text().toDouble(&okP);
    if (!okS || sigma <= 0.0) sigma = 2.0;
    if (!okP || polarity == 0.0) polarity = 1.0;
    polarity = (polarity > 0.0) ? 1.0 : -1.0;

    storeUndoSnapshot();

    int w = m_image.width();
    int h = m_image.height();
    if ((int)m_imageRawPixels.size() != w * h) return;

    // Build 1D Gaussian kernel for separable smoothing
    int khalf = std::max(1, (int)std::ceil(3.0 * sigma));
    int ksize = 2 * khalf + 1;
    std::vector<double> gk(ksize);
    double gsum = 0.0;
    for (int i = -khalf; i <= khalf; i++) {
        double v = std::exp(-(i * i) / (2.0 * sigma * sigma));
        gk[i + khalf] = v;
        gsum += v;
    }
    for (double &v : gk) v /= gsum;

    m_toolProgress = 0.1;
    update();

    chainSteps({
        [this, w, h, ksize, khalf, gk, sigma, polarity]() {
            // Separable Gaussian smoothing with reflect boundaries
            std::vector<double> tmp(w * h, 0.0);
            std::vector<double> sm(w * h, 0.0);

            // Horizontal pass
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    double acc = 0.0;
                    for (int k = -khalf; k <= khalf; k++) {
                        int sx = x + k;
                        if (sx < 0) sx = -sx;
                        else if (sx >= w) sx = 2 * w - 2 - sx;
                        if (sx < 0) sx = 0;
                        if (sx >= w) sx = w - 1;
                        acc += m_imageRawPixels[y * w + sx] * gk[k + khalf];
                    }
                    tmp[y * w + x] = acc;
                }
            }
            // Vertical pass
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    double acc = 0.0;
                    for (int k = -khalf; k <= khalf; k++) {
                        int sy = y + k;
                        if (sy < 0) sy = -sy;
                        else if (sy >= h) sy = 2 * h - 2 - sy;
                        if (sy < 0) sy = 0;
                        if (sy >= h) sy = h - 1;
                        acc += tmp[sy * w + x] * gk[k + khalf];
                    }
                    sm[y * w + x] = acc;
                }
            }

            // Hessian via 3x3 finite differences on the smoothed image,
            // gamma-normalised by sigma^2 to make the response scale-comparable.
            std::vector<double> out(w * h, 0.0);
            auto S = [&](int x, int y) -> double {
                if (x < 0) x = 0; else if (x >= w) x = w - 1;
                if (y < 0) y = 0; else if (y >= h) y = h - 1;
                return sm[y * w + x];
            };
            const double s2 = sigma * sigma;
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    double Ixx = S(x + 1, y) - 2.0 * S(x, y) + S(x - 1, y);
                    double Iyy = S(x, y + 1) - 2.0 * S(x, y) + S(x, y - 1);
                    double Ixy = (S(x + 1, y + 1) - S(x - 1, y + 1)
                                - S(x + 1, y - 1) + S(x - 1, y - 1)) * 0.25;
                    Ixx *= s2; Iyy *= s2; Ixy *= s2;

                    double tr = Ixx + Iyy;
                    double diff = Ixx - Iyy;
                    double disc = std::sqrt(diff * diff + 4.0 * Ixy * Ixy);
                    double l1 = 0.5 * (tr + disc);  // larger eigenvalue
                    double l2 = 0.5 * (tr - disc);  // smaller eigenvalue

                    // Eigenvalue with largest magnitude — strongest curvature
                    double lambda = (std::fabs(l1) >= std::fabs(l2)) ? l1 : l2;

                    // Bright ridges (polarity=+1): bright line on dark background -> lambda < 0
                    // Dark  ridges (polarity=-1): dark  line on bright background -> lambda > 0
                    double resp = -polarity * lambda;
                    if (resp < 0.0) resp = 0.0;
                    out[y * w + x] = resp;
                }
            }

            m_imageRawPixels = std::move(out);

            m_imageMinVal = m_imageRawPixels[0];
            m_imageMaxVal = m_imageRawPixels[0];
            for (double v : m_imageRawPixels) {
                if (v < m_imageMinVal) m_imageMinVal = v;
                if (v > m_imageMaxVal) m_imageMaxVal = v;
            }
            if (!m_imageContrastLocked) {
                m_imageDispMin = m_imageMinVal;
                m_imageDispMax = m_imageMaxVal;
            }

            rebuildImageFromRaw();
            m_toolProgress = 0.5;
        },
        [this]() {
            if (m_ftComputed)
                computeFFT();

            if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS) {
                m_history[m_activeSlot].image     = m_image;
                m_history[m_activeSlot].rawPixels = m_imageRawPixels;
                m_history[m_activeSlot].minVal    = m_imageMinVal;
                m_history[m_activeSlot].maxVal    = m_imageMaxVal;
                m_history[m_activeSlot].pixelSize = m_pixelSize;
                m_history[m_activeSlot].occupied  = true;
                if (m_ftComputed)
                    m_history[m_activeSlot].powerSpecImg = computePowerSpecMasked(m_image);
            }

            saveHistory();
            m_toolProgress = -1;
            update();
        }
    });
}

void FtWindow::onMeasureCancel()
{
    m_measureActive = false;
    m_measurePlacing = 0;
    m_measureHasLine = false;
    m_measureCancelBtn->hide();
    update();
}

void FtWindow::onShiftCancel()
{
    m_shiftActive = false;
    m_p1Dragging = false;
    m_shiftCancelBtn->hide();
    update();
}

void FtWindow::onRotateCancel()
{
    m_rotateActive = false;
    m_p1Dragging = false;
    m_rotateCancelBtn->hide();
    update();
}

void FtWindow::onApplyFtCrop()
{
    if (!ensureCalcHeadroom(tr("crop in Fourier space"))) return;
    onApplyFtCropImpl();
}

void FtWindow::onApplyFtCropImpl()
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

void FtWindow::onApplyFtPad()
{
    if (!ensureCalcHeadroom(tr("pad in Fourier space"))) return;
    onApplyFtPadImpl();
}

void FtWindow::onApplyFtPadImpl()
{
    if (!m_ftComputed || m_fftN == 0) return;

    int factor = m_ftCropCombo->currentData().toInt();
    if (factor <= 1) return;

    constexpr int FT_PAD_MAX = 4096;
    int N = m_fftN;
    if (N >= FT_PAD_MAX) return;

    int newN = N * factor;
    if (newN > FT_PAD_MAX) newN = FT_PAD_MAX;
    // Round to the next FFT-friendly size without exceeding the cap.
    int adjN = nextGoodFFTSize(newN);
    if (adjN <= FT_PAD_MAX) newN = adjN;
    if (newN <= N) return;  // nothing to do

    storeUndoSnapshot();

    m_toolProgress = 0.1;
    update();

    int oldN = N;
    int oldHalf = N / 2;
    int newHalf = newN / 2;
    chainSteps({
        [this, oldN, oldHalf, newN, newHalf]() {
            std::vector<Complex> newData(static_cast<size_t>(newN) * newN, Complex(0.0, 0.0));
            // Center-embed the old FFT inside the zero-padded array so that
            // the DC component stays at (newHalf, newHalf).
            int offX = newHalf - oldHalf;
            int offY = newHalf - oldHalf;
            for (int y = 0; y < oldN; y++) {
                for (int x = 0; x < oldN; x++) {
                    newData[(y + offY) * newN + (x + offX)] =
                        m_fftData[y * oldN + x];
                }
            }
            m_fftData = std::move(newData);
            m_fftN = newN;
            m_origW = newN;
            m_origH = newN;
            m_zoom[1].reset(newN, newN);
            m_zoom[2].reset(newN, newN);
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
    if (!ensureCalcHeadroom(tr("apply the directional filter"))) return;
    onApplyDirectionalImpl();
}

void FtWindow::onApplyDirectionalImpl()
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
    if (!ensureCalcHeadroom(tr("apply the line filter"))) return;
    onApplyLineFilterImpl();
}

void FtWindow::onApplyLineFilterImpl()
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
                    double proj = relX * normX + relY * normY;
                    double dist1 = std::abs(proj - lineOff);
                    double dist2 = std::abs(proj + lineOff);  // Friedel mate
                    bool inside = dist1 <= halfWidth || dist2 <= halfWidth;
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
    if (!ensureCalcHeadroom(tr("compute the Fourier math"))) return;
    onFtMathComputeImpl();
}

void FtWindow::onFtMathComputeImpl()
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

    // Helper: get raw pixels + pixel size from a slot
    auto getSlotPixels = [&](int idx, int &w, int &h, double &px) -> std::vector<double> {
        if (idx == m_activeSlot && !m_image.isNull()) {
            w = m_image.width(); h = m_image.height();
            px = m_pixelSize;
            return m_imageRawPixels;
        }
        if (idx >= 0 && idx < HISTORY_SLOTS && m_history[idx].occupied) {
            w = m_history[idx].image.width();
            h = m_history[idx].image.height();
            px = m_history[idx].pixelSize;
            return m_history[idx].rawPixels;
        }
        w = 0; h = 0; px = 1.0;
        return {};
    };

    int w1 = 0, h1 = 0, w2 = 0, h2 = 0;
    double px1 = 1.0, px2 = 1.0;
    std::vector<double> pix1 = getSlotPixels(in1Idx, w1, h1, px1);
    std::vector<double> pix2 = getSlotPixels(in2Idx, w2, h2, px2);
    if (px1 <= 0) px1 = 1.0;
    if (px2 <= 0) px2 = 1.0;
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

    // Determine a common target (pxT, N) so the two FFTs share both the same
    // reciprocal pixel size (dq = 1/(N·pxT)) and the same Nyquist (1/(2·pxT)).
    // Any image with a larger pixel size than pxT is bilinearly upsampled in
    // real space before being zero-padded and transformed, which produces a
    // smooth FFT without the aliasing artefacts of Fourier-domain interpolation.
    double pxT = std::min(px1, px2);
    auto effSize = [&](int w, int h, double px) {
        double f = px / pxT;
        int ew = std::max(1, (int)std::round(w * f));
        int eh = std::max(1, (int)std::round(h * f));
        return std::pair<int,int>(ew, eh);
    };
    auto [ew1, eh1] = effSize(w1, h1, px1);
    auto [ew2, eh2] = effSize(w2, h2, px2);
    int Ntarget = nextGoodFFTSize(std::max({ew1, eh1, ew2, eh2}));

    // FFT a slot's raw pixels at the common target frame. The image is first
    // bilinearly interpolated in real space from (w_src, h_src, px_src) to
    // (w_eff, h_eff, pxT), then zero-padded (around the mean) to NxN and FFT'd.
    auto computeSlotFFT = [pxT, Ntarget](const std::vector<double> &pixels,
                                         int wSrc, int hSrc, double pxSrc) {
        int N = Ntarget;
        double f = pxSrc / pxT;
        int wEff = std::max(1, (int)std::round(wSrc * f));
        int hEff = std::max(1, (int)std::round(hSrc * f));

        double sum = 0;
        for (auto v : pixels) sum += v;
        double avg = pixels.empty() ? 0.0 : sum / pixels.size();

        std::vector<Complex> data(N * N, Complex(avg, 0.0));
        int offX = (N - wEff) / 2, offY = (N - hEff) / 2;

        // Bilinear real-space resampling: target (wEff, hEff) ← source (wSrc, hSrc).
        for (int y = 0; y < hEff; y++) {
            double ys = (hEff > 1)
                          ? (double)y * (hSrc - 1) / (hEff - 1)
                          : 0.0;
            int y0 = (int)std::floor(ys);
            double fy = ys - y0;
            int y1 = std::min(y0 + 1, hSrc - 1);
            for (int x = 0; x < wEff; x++) {
                double xs = (wEff > 1)
                              ? (double)x * (wSrc - 1) / (wEff - 1)
                              : 0.0;
                int x0 = (int)std::floor(xs);
                double fx = xs - x0;
                int x1 = std::min(x0 + 1, wSrc - 1);
                double v00 = pixels[y0 * wSrc + x0];
                double v10 = pixels[y0 * wSrc + x1];
                double v01 = pixels[y1 * wSrc + x0];
                double v11 = pixels[y1 * wSrc + x1];
                double vx0 = v00 * (1.0 - fx) + v10 * fx;
                double vx1 = v01 * (1.0 - fx) + v11 * fx;
                data[(y + offY) * N + (x + offX)] =
                    Complex(vx0 * (1.0 - fy) + vx1 * fy, 0.0);
            }
        }

        // Centered real-space convention (mirrors computeFFT).
        fftShift(data, N);

        std::vector<Complex> row(N);
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) row[x] = data[y * N + x];
            fft1d(row, false);
            for (int x = 0; x < N; x++) data[y * N + x] = row[x];
        }
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
        double px1 = 1.0, px2 = 1.0;
        int outIdx, in1Idx, in2Idx, opIdx;
        bool conjugate;
        std::vector<Complex> fft1data, fft2data, result;
        int N1 = 0, N2 = 0, N = 0;
        double pxTarget = 1.0;
    };
    auto st = std::make_shared<FtMathWork>();
    st->pix1 = std::move(pix1); st->pix2 = std::move(pix2);
    st->w1 = w1; st->h1 = h1; st->w2 = w2; st->h2 = h2;
    st->px1 = px1; st->px2 = px2;
    st->outIdx = outIdx; st->in1Idx = in1Idx; st->in2Idx = in2Idx;
    st->opIdx = opIdx; st->conjugate = conjugate;

    st->pxTarget = pxT;
    chainSteps({
        // Stage 1: FFT of input 1 (in common target frame)
        [this, st, computeSlotFFT]() {
            auto [d, n] = computeSlotFFT(st->pix1, st->w1, st->h1, st->px1);
            st->fft1data = std::move(d); st->N1 = n;
            m_ftMathProgress = 0.3;
        },
        // Stage 2: FFT of input 2 (in common target frame)
        [this, st, computeSlotFFT]() {
            auto [d, n] = computeSlotFFT(st->pix2, st->w2, st->h2, st->px2);
            st->fft2data = std::move(d); st->N2 = n;
            m_ftMathProgress = 0.6;
        },
        // Stage 3: conjugate, operation (both FFTs already share the frame)
        [this, st]() {
            st->N = st->N1;  // N1 == N2 == Ntarget by construction
            if (st->conjugate)
                for (auto &c : st->fft2data) c = std::conj(c);
            int N = st->N;
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
            // Centered real-space convention: convert the (0,0)-origin output
            // of the raw inverse FFT to centered form. This also moves the
            // convolution/correlation peak from index (0,0) to the centre
            // automatically (no per-op manual fftShift needed in stage 6).
            fftShift(st->result, N);
            m_ftMathProgress = 0.95;
        },
        // Stage 6: build output, save to slot
        [this, st]() {
            int N = st->N;
            int outS = N;
            std::vector<double> realResult(outS * outS);
            for (int y = 0; y < outS; y++)
                for (int x = 0; x < outS; x++)
                    realResult[y * outS + x] = st->result[y * N + x].real();
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
            m_history[outIdx].pixelSize = st->pxTarget;
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
            m_maskBtnVisible = false;

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
    if (!ensureCalcHeadroom(tr("compute the image math"))) return;
    onMathComputeImpl();
}

void FtWindow::onMathComputeImpl()
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
        m_maskBtnVisible = false;

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

    // Build sorted index of all pixels above threshold (descending by value)
    std::vector<int> candidates;
    candidates.reserve(n / 4);
    for (int i = 0; i < n; i++) {
        if (src.rawPixels[i] > threshold)
            candidates.push_back(i);
    }
    std::sort(candidates.begin(), candidates.end(), [&](int a, int b) {
        return src.rawPixels[a] > src.rawPixels[b];
    });

    std::vector<bool> excluded(n, false);
    int exclRi = static_cast<int>(std::ceil(exclR));

    for (int idx : candidates) {
        if (excluded[idx]) continue;

        int px = idx % w;
        int py = idx / w;
        m_peaks.push_back({px, py});

        // Mark exclusion zone
        int y0 = std::max(0, py - exclRi);
        int y1 = std::min(h - 1, py + exclRi);
        int x0 = std::max(0, px - exclRi);
        int x1 = std::min(w - 1, px + exclRi);
        for (int yy = y0; yy <= y1; yy++) {
            double dy = yy - py;
            double dy2 = dy * dy;
            for (int xx = x0; xx <= x1; xx++) {
                double dx = xx - px;
                if (dx * dx + dy2 <= exclR2)
                    excluded[yy * w + xx] = true;
            }
        }
    }
}

void FtWindow::onPeakCancel()
{
    m_peakPickActive = false;
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
    if (!ensureCalcHeadroom(tr("extract the particles"))) return;
    onExtractComputeImpl();
}

void FtWindow::onExtractComputeImpl()
{
    if (m_peaks.empty()) return;

    int srcIdx = m_extractSourceCombo->currentIndex();
    int tgtIdx = m_extractTargetCombo->currentIndex();
    if (srcIdx < 0 || srcIdx >= HISTORY_SLOTS || !m_history[srcIdx].occupied) return;
    if (tgtIdx < 0 || tgtIdx >= HISTORY_SLOTS) return;

    // Left-to-right blue progress bar in the parameter-window background.
    m_toolProgress = 0.1;
    update();

    chainSteps({
        // --- Step 1: extract the boxed particles into the target slot ---
        [this, srcIdx, tgtIdx]() {
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
    if (!m_imageContrastLocked) {
        m_imageDispMin   = mn;
        m_imageDispMax   = mx;
    }
    m_pixelSize      = m_history[tgtIdx].pixelSize;
    m_zoom[0].reset(outSize, outSize);
    m_ftComputed = false;
            m_toolProgress = 0.5;
        },
        // --- Step 2: persist history and remember the chosen options ---
        [this]() {
    saveHistory();
#ifndef __EMSCRIPTEN__
    {
        QSettings settings("ft", "ft");
        settings.setValue("extractSourceIdx", m_extractSourceCombo->currentIndex());
        settings.setValue("extractTargetIdx", m_extractTargetCombo->currentIndex());
        settings.setValue("extractSizeIdx", m_extractSizeCombo->currentIndex());
    }
#endif
            m_toolProgress = -1;
        }
    });
}

void FtWindow::onCtfCancel()
{
    m_ctfActive = false;
    m_ctfProfile.clear();
    m_ctfPhaseProfile.clear();
    m_ctfVoltageEdit->hide();
    m_ctfEnergySpreadEdit->hide();
    m_ctfDefocusSpreadEdit->hide();
    m_ctfOpenAngleEdit->hide();
    m_ctfCsEdit->hide();
    m_ctfDefocusEdit->hide();
    m_ctfAstigEdit->hide();
    m_ctfAstigAngleEdit->hide();
    m_ctfAmpContrastEdit->hide();
    m_ctfBeamtiltEdit->hide();
    m_ctfBeamtiltDirEdit->hide();
    m_ctfCancelBtn->hide();
    m_ctfPupilBtn->hide();
    m_ctfComplexBtn->hide();
    m_ctfRealBtn->hide();
    update();
}

void FtWindow::computeCtfProfile1D()
{
    bool okV = false, okE = false, okC = false, okD = false;
    bool okA = false, okAA = false, okDS = false, okOA = false;
    bool okAC = false, okBT = false, okBTD = false;
    double voltageKV     = m_ctfVoltageEdit->text().toDouble(&okV);
    double energyEV      = m_ctfEnergySpreadEdit->text().toDouble(&okE);
    double defocusSpreadNM = m_ctfDefocusSpreadEdit->text().toDouble(&okDS);
    double openAngleMrad = m_ctfOpenAngleEdit->text().toDouble(&okOA);
    double csMM          = m_ctfCsEdit->text().toDouble(&okC);
    double defocusNM     = m_ctfDefocusEdit->text().toDouble(&okD);
    double astigNM       = m_ctfAstigEdit->text().toDouble(&okA);
    double astigAngleDeg = m_ctfAstigAngleEdit->text().toDouble(&okAA);
    double ampContrastPct = m_ctfAmpContrastEdit->text().toDouble(&okAC);
    double beamtiltMrad   = m_ctfBeamtiltEdit->text().toDouble(&okBT);
    double beamtiltDirDeg = m_ctfBeamtiltDirEdit->text().toDouble(&okBTD);
    if (!okV || voltageKV <= 0) voltageKV = 300.0;
    if (!okE)                   energyEV  = 0.7;
    if (!okDS)                  defocusSpreadNM = 5.0;
    if (!okOA)                  openAngleMrad = 0.1;
    if (!okC)                   csMM      = 2.7;
    if (!okD)                   defocusNM = 1000.0;
    if (!okA)                   astigNM   = 0.0;
    if (!okAA)                  astigAngleDeg = 0.0;
    if (!okAC)                  ampContrastPct = 7.0;
    if (!okBT)                  beamtiltMrad   = 0.0;
    if (!okBTD)                 beamtiltDirDeg = 0.0;

    double V = voltageKV * 1000.0;
    double lambdaA = 12.2639 / std::sqrt(V * (1.0 + V * 0.978466e-6));
    double CsA     = csMM * 1.0e7;
    double dfA     = - defocusNM * 10.0;
    double astigA  = - astigNM * 10.0;
    double astigAngleRad = astigAngleDeg * M_PI / 180.0;
    double dzChromA = CsA * (energyEV / V);
    double dzUserA  = defocusSpreadNM * 10.0;
    double defocusSpreadA = std::sqrt(dzChromA * dzChromA + dzUserA * dzUserA);

    int N = (m_fftN > 0) ? m_fftN : 1024;
    double dxA = (m_pixelSize > 0) ? m_pixelSize : 1.0;
    double maxR = (N / 2.0) * std::sqrt(2.0);
    int nProf = std::max(64, (int)std::ceil(maxR) + 1);
    m_ctfProfile.assign(nProf, 0.0);
    m_ctfPhaseProfile.assign(nProf, 0.0);
    // Amplitude contrast B (from user, in %) and phase contrast A = √(1−B²).
    double B = ampContrastPct / 100.0;
    B = std::max(0.0, std::min(1.0, B));
    const double A = std::sqrt(1.0 - B * B);

    // Beam tilt as a 2D angle vector (radians), CCW from +x (EM convention).
    double tiltRad    = beamtiltMrad * 1.0e-3;
    double tiltDirRad = beamtiltDirDeg * M_PI / 180.0;

    double profAngleRad = m_ctfAngleDeg * M_PI / 180.0;
    double dfProf = dfA + astigA * std::cos(2.0 * (profAngleRad - astigAngleRad));
    double alphaRad = openAngleMrad * 1.0e-3;
    // Beam tilt as a spatial-frequency offset t = τ/λ (1/Å); see onCtfComputeImpl
    // for the full rationale. The even/odd split of the exact tilted-geometry
    // aberration gives the (elliptical) ring modulus and the coma phase.
    double tMag = tiltRad / lambdaA;
    double tx   = tMag * std::cos(tiltDirRad);
    double ty   = tMag * std::sin(tiltDirRad);
    auto chiRound = [&](double kx, double ky) {
        double k2 = kx * kx + ky * ky;
        return M_PI * lambdaA * dfA * k2
             + 0.5 * M_PI * CsA * lambdaA * lambdaA * lambdaA * k2 * k2;
    };
    double gcoef = 2.0 * M_PI * lambdaA * dfA
                 + 2.0 * M_PI * CsA * lambdaA * lambdaA * lambdaA * (tx * tx + ty * ty);
    auto chiTiltAt = [&](double ux, double uy) {
        return chiRound(ux + tx, uy + ty) - chiRound(tx, ty)
             - gcoef * (ux * tx + uy * ty);
    };
    for (int j = 0; j < nProf; j++) {
        double rPix = (double)j / (nProf - 1) * maxR;
        double q = rPix / (N * dxA);
        double q2 = q * q;
        double qx = q * std::cos(profAngleRad);
        double qy = q * std::sin(profAngleRad);
        double chiP = chiTiltAt(qx, qy);
        double chiM = chiTiltAt(-qx, -qy);
        double chiAstig = M_PI * lambdaA * (dfProf - dfA) * q2;  // user astig (even)
        double chiT    = chiP + chiAstig;                        // full tilted χ
        double chiEven = 0.5 * (chiP + chiM) + chiAstig;
        double chiOdd  = 0.5 * (chiP - chiM);                    // coma (odd)
        double tArg = M_PI * lambdaA * defocusSpreadA * q2;
        double envT = std::exp(-0.5 * tArg * tArg);
        // Spatial-coherence envelope from the finite gun opening angle:
        //   E_s(q) = exp(-π² α² q² (Δf + Cs·λ²·q²)²)
        double sArg = dfProf + CsA * lambdaA * lambdaA * q2;
        double envS = std::exp(-(M_PI * M_PI) * alphaRad * alphaRad * q2 * sArg * sArg);
        double E = envT * envS;
        // The profile must plot whichever model is selected, or it would
        // contradict what panel 2 shows. The formulae mirror ctfAt() in
        // onCtfComputeImpl(); panel 4 plots |C| and panel 3 arg(C) ∈ [−π,π].
        Complex c;
        switch (m_ctfModel) {
        case CtfModel::Pupil: {
            double ph = -(chiT + std::atan2(B, A));
            c = E * Complex(std::cos(ph), std::sin(ph));
            break;
        }
        case CtfModel::ComplexCTF: {
            double base = E * (A * std::sin(-chiEven) + B * std::cos(-chiEven));
            c = base * Complex(std::cos(chiOdd), -std::sin(chiOdd));
            break;
        }
        case CtfModel::RealCTF:
        default:
            c = Complex(E * (A * std::sin(-chiT) + B * std::cos(-chiT)), 0.0);
            break;
        }
        m_ctfProfile[j]      = std::abs(c);
        m_ctfPhaseProfile[j] = std::arg(c);
    }
}

void FtWindow::computeCtfWithModel(CtfModel model)
{
    m_ctfModel = model;
    onCtfCompute();
}

void FtWindow::onCtfCompute()
{
    if (!ensureCalcHeadroom(tr("compute the CTF"))) return;
    onCtfComputeImpl();
}

void FtWindow::onCtfComputeImpl()
{
    // Parse parameters
    bool okV = false, okE = false, okC = false, okD = false;
    bool okA = false, okAA = false, okDS = false, okOA = false;
    bool okAC = false, okBT = false, okBTD = false;
    double voltageKV    = m_ctfVoltageEdit->text().toDouble(&okV);
    double energyEV     = m_ctfEnergySpreadEdit->text().toDouble(&okE);
    double defocusSpreadNM = m_ctfDefocusSpreadEdit->text().toDouble(&okDS);
    double openAngleMrad = m_ctfOpenAngleEdit->text().toDouble(&okOA);
    double csMM         = m_ctfCsEdit->text().toDouble(&okC);
    double defocusNM    = m_ctfDefocusEdit->text().toDouble(&okD);
    double astigNM       = m_ctfAstigEdit->text().toDouble(&okA);
    double astigAngleDeg = m_ctfAstigAngleEdit->text().toDouble(&okAA);
    double ampContrastPct = m_ctfAmpContrastEdit->text().toDouble(&okAC);
    double beamtiltMrad   = m_ctfBeamtiltEdit->text().toDouble(&okBT);
    double beamtiltDirDeg = m_ctfBeamtiltDirEdit->text().toDouble(&okBTD);
    if (!okV || voltageKV <= 0) voltageKV = 300.0;
    if (!okE)                   energyEV  = 0.7;
    if (!okDS)                  defocusSpreadNM = 5.0;
    if (!okOA)                  openAngleMrad = 0.1;
    if (!okC)                   csMM      = 2.7;
    if (!okD)                   defocusNM = 1000.0;
    if (!okA)                   astigNM   = 0.0;
    if (!okAA)                  astigAngleDeg = 0.0;
    if (!okAC)                  ampContrastPct = 7.0;
    if (!okBT)                  beamtiltMrad   = 0.0;
    if (!okBTD)                 beamtiltDirDeg = 0.0;

    // Relativistic electron wavelength in Angstrom
    double V = voltageKV * 1000.0;  // volts
    double lambdaA = 12.2639 / std::sqrt(V * (1.0 + V * 0.978466e-6));

    // Convert to Angstrom consistent units
    double CsA = csMM * 1.0e7;       // mm -> Angstrom
    double dfA = - defocusNM * 10.0;   // nm -> Angstrom
    double astigA = - astigNM * 10.0;  // nm -> Angstrom (same sign convention as dfA)
    double astigAngleRad = astigAngleDeg * M_PI / 180.0;

    // Temporal-coherence defocus spread (Å). Chromatic contribution from the
    // energy spread (assuming Cc ≈ Cs) is combined in quadrature with the
    // user-supplied defocus spread.
    double dzChromA = CsA * (energyEV / V);
    double dzUserA  = defocusSpreadNM * 10.0;
    double defocusSpreadA = std::sqrt(dzChromA * dzChromA + dzUserA * dzUserA);

    // Force a 1024x1024 Fourier space (indices -512..+512 about the centre).
    int N = 1024;
    double dxA = (m_pixelSize > 0) ? m_pixelSize : 1.0;

    storeUndoSnapshot();

    // Build 1D profile: from r = 0 (center) to r = (N/2)*sqrt(2) (corners)
    double maxR = (N / 2.0) * std::sqrt(2.0);
    int nProf = std::max(64, (int)std::ceil(maxR) + 1);
    m_ctfProfile.assign(nProf, 0.0);
    m_ctfPhaseProfile.assign(nProf, 0.0);
    // Amplitude contrast B (from user, in %) and phase contrast A = √(1−B²),
    // so that A² + B² = 1.
    double B = ampContrastPct / 100.0;
    B = std::max(0.0, std::min(1.0, B));
    const double A = std::sqrt(1.0 - B * B);

    // Beam tilt as a 2D angle vector (radians), CCW from +x (EM convention).
    double tiltRad    = beamtiltMrad * 1.0e-3;
    double tiltDirRad = beamtiltDirDeg * M_PI / 180.0;

    double alphaRad = openAngleMrad * 1.0e-3;
    // Beam tilt expressed as a spatial-frequency offset t = τ/λ (1/Å), as a
    // 2D vector along the tilt azimuth.
    double tMag = tiltRad / lambdaA;
    double tx   = tMag * std::cos(tiltDirRad);
    double ty   = tMag * std::sin(tiltDirRad);

    // Show a left-to-right blue progress bar in the parameter-window
    // background while the (1024x1024) transfer function is being built,
    // matching Amyloid Filament and the other long-running tools.
    m_toolProgress = 0.1;
    update();

    // chiRound/ctfAt capture their inputs by value so they stay valid when the
    // chunked fill steps below run on later event-loop turns (on the web build
    // this function has already returned by then).
    // Round-lens (isotropic) wave aberration χ(k) = πλ·Δf·k² + ½πCs·λ³·k⁴,
    // evaluated for an arbitrary 2D spatial-frequency vector.
    auto chiRound = [lambdaA, dfA, CsA](double kx, double ky) {
        double k2 = kx * kx + ky * ky;
        return M_PI * lambdaA * dfA * k2
             + 0.5 * M_PI * CsA * lambdaA * lambdaA * lambdaA * k2 * k2;
    };
    const CtfModel model = m_ctfModel;
    auto ctfAt = [N, dxA, lambdaA, dfA, CsA, defocusSpreadA, alphaRad,
                  A, B, tx, ty, chiRound, model](double dfLocalA, double rPix,
                                                 double thetaRad) -> Complex {
        // Spatial frequency q (1/Å) for this radial pixel distance.
        double q = rPix / (N * dxA);
        double q2 = q * q;
        double qx = q * std::cos(thetaRad);
        double qy = q * std::sin(thetaRad);
        // Beam tilt evaluates the round-lens aberration at the tilted geometry,
        //   χ_tilt(q) = χ(q+t) − χ(t) − q·∇χ(t),
        // with the constant and linear (image-shift) terms removed. Evaluate it
        // at +q and −q so we can split it by parity in q.
        double gcoef = 2.0 * M_PI * lambdaA * dfA
                     + 2.0 * M_PI * CsA * lambdaA * lambdaA * lambdaA * (tx * tx + ty * ty);
        auto chiTiltAt = [&](double ux, double uy) {
            return chiRound(ux + tx, uy + ty) - chiRound(tx, ty)
                 - gcoef * (ux * tx + uy * ty);
        };
        // The tilted aberration at +q and −q. With a beam tilt these differ, and
        // that asymmetry is the whole physical effect of the tilt.
        double chiP = chiTiltAt(qx, qy);
        double chiM = chiTiltAt(-qx, -qy);
        // The user's lens astigmatism is even in q and rides along with defocus.
        double chiAstig = M_PI * lambdaA * (dfLocalA - dfA) * q2;
        // Full tilted aberration (no even/odd split).
        double chiT = chiP + chiAstig;
        // Even part: defocus, Cs, astigmatism and the defocus/astigmatism the
        // tilt itself induces. It alone sets the oscillating Thon rings.
        double chiEven = 0.5 * (chiP + chiM) + chiAstig;
        // Odd part: 1st-order coma. A pure phase aberration — it never changes
        // the modulus.
        double chiOdd = 0.5 * (chiP - chiM);
        // Temporal-coherence envelope from defocus spread.
        double tArg = M_PI * lambdaA * defocusSpreadA * q2;
        double envT = std::exp(-0.5 * tArg * tArg);
        // Spatial-coherence envelope from the finite gun opening angle:
        //   E_s(q) = exp(-π² α² q² (Δf + Cs·λ²·q²)²)
        double sArg = dfLocalA + CsA * lambdaA * lambdaA * q2;
        double envS = std::exp(-(M_PI * M_PI) * alphaRad * alphaRad * q2 * sArg * sArg);
        double E = envT * envS;

        switch (model) {
        case CtfModel::Pupil: {
            // Wave-optical pupil P(q) = E(q)·exp(−iχ_tilt(q)): the aberrated lens
            // acting on the electron wave. The modulus is only the coherence
            // envelope (no Thon rings — those belong to the intensity CTF); the
            // aberration lives entirely in the phase. Under tilt it is genuinely
            // non-Hermitian, so h = FT⁻¹[P] is complex and |h|² is the one-sided
            // coma comet. Amplitude contrast is folded in as the constant phase
            // atan2(B,A), which factors out of |h|² and so cannot affect the PSF.
            double ph = -(chiT + std::atan2(B, A));
            return E * Complex(std::cos(ph), std::sin(ph));
        }
        case CtfModel::ComplexCTF: {
            // Linear image-intensity transfer function for a weak-phase object:
            // the real oscillating transfer built from the even aberration, times
            // the coma phase factor exp(−iχ_odd). Hermitian by construction —
            // T(−q) = conj(T(q)) — because a real image demands it, so the Thon
            // rings stay symmetric and the real-space PSF comes out real. It is
            // Hermitian without being even, which is exactly what lets that real
            // PSF still show one-sided coma.
            double base = E * (A * std::sin(-chiEven) + B * std::cos(-chiEven));
            return base * Complex(std::cos(chiOdd), -std::sin(chiOdd));
        }
        case CtfModel::RealCTF:
        default: {
            // The transfer function evaluated at the full tilted aberration and
            // kept purely real: not Hermitian, so the Thon rings themselves go
            // one-sided. The price is the PSF — for a real C the inverse
            // transform obeys h(−r) = conj(h(r)), so the real part taken for
            // panel 1 is exactly centrosymmetric and shows no coma at all.
            return Complex(E * (A * std::sin(-chiT) + B * std::cos(-chiT)), 0.0);
        }
        }
    };
    std::vector<std::function<void()>> steps;

    // Step: 1D profile (direction-dependent defocus along m_ctfAngleDeg) and
    // allocation of the Fourier buffer. Panel-4 shows the CTF amplitude |C|
    // (the coma phase factor has unit modulus, so |C| is the rectified
    // envelope of the oscillating contrast transfer); panel-3 shows the
    // complementary phase arg(C) ∈ [−π,π].
    steps.push_back([this, ctfAt, nProf, maxR, dfA, astigA, astigAngleRad, N]() {
        double profAngleRad = m_ctfAngleDeg * M_PI / 180.0;
        double dfProf = dfA + astigA * std::cos(2.0 * (profAngleRad - astigAngleRad));
        for (int j = 0; j < nProf; j++) {
            double rPix = (double)j / (nProf - 1) * maxR;
            Complex c = ctfAt(dfProf, rPix, profAngleRad);
            m_ctfProfile[j]      = std::abs(c);
            m_ctfPhaseProfile[j] = std::arg(c);
        }
        m_fftN = N;
        m_fftData.assign((size_t)N * N, Complex(0.0, 0.0));
        m_toolProgress = 0.10;
    });

    // Steps: fill the direction-dependent Fourier transform in horizontal
    // bands so the blue bar advances smoothly through the (1024×1024)
    // transcendental evaluation instead of stalling on one monolithic loop.
    // Defocus varies azimuthally as Δf(θ) = Δf_avg + Δf_A·cos(2·(θ−α)), θ
    // measured CCW from the horizontal axis (standard EM convention); the
    // average defocus is recovered at θ = α ± 45°.
    const int nBands = 32;
    for (int b = 0; b < nBands; b++) {
        int y0 = (int)((long long)b       * N / nBands);
        int y1 = (int)((long long)(b + 1) * N / nBands);
        double prog = 0.10 + 0.80 * (double)(b + 1) / nBands;
        steps.push_back([this, ctfAt, N, dfA, astigA, astigAngleRad, y0, y1, prog]() {
            double half = N / 2.0;
            for (int y = y0; y < y1; y++) {
                double dy = y - half;
                for (int x = 0; x < N; x++) {
                    double dx = x - half;
                    double rPix = std::sqrt(dx * dx + dy * dy);
                    // Image y axis points downward, so flip it for the CCW angle.
                    double theta = std::atan2(-dy, dx);
                    double dfLocal = dfA + astigA * std::cos(2.0 * (theta - astigAngleRad));
                    m_fftData[y * N + x] = ctfAt(dfLocal, rPix, theta);
                }
            }
            m_toolProgress = prog;
        });
    }

    // Step: build the panel-2 display images.
    steps.push_back([this, N]() {
    m_ftComputed = true;
    if (m_origW <= 0 || m_origH <= 0) {
        m_origW = N;
        m_origH = N;
    }
    m_modeBtn->show();
    m_maskBtnVisible = true;
    m_zoom[1].reset(N, N);
    m_zoom[2].reset(N, N);
    // Build the panel-2 mode images (cos/sin, amp/phase, complex, power). The
    // current display mode is honoured here exactly as for a real transform.
    // Note: the synthetic CTF is stored with raw amplitudes of order 1, whereas
    // the FFT of an actual image has amplitudes orders of magnitude larger.
    // Because the amplitude panel shows
    // log(1+amp) — which is ~linear for amp≲1 but truly logarithmic for amp≫1 —
    // the CTF's envelope fall-off looks steeper than that of a transformed
    // image. This is expected (a log-scale regime difference), not a
    // wrong-display-mode bug.
    recomputeDisplayImages();
        m_toolProgress = 0.92;
    });

    // Step: inverse transform to real space for panel 1.
    steps.push_back([this, N, model]() {

    // Inverse-transform to real space for panel 1. The pupil is built in the
    // centred Fourier convention (zero frequency at (N/2, N/2)), and
    // computeInverseFFT() already returns the result in centred real-space form
    // — so the PSF lands at the image centre directly. No manual quadrant swap
    // is performed here: that extra fftShift is equivalent to multiplying the
    // Fourier data by (-1)^(x+y) (a 180° phase flip on every second pixel) and
    // would push the PSF back into the corners.
    //
    // How the wave becomes an image depends on the model:
    //  - Pupil: h = FT⁻¹[P] is a complex wave, and the microscope records its
    //    intensity |h|². That is the point spread function, and the only form in
    //    which the coma survives.
    //  - Complex CTF: the transform is Hermitian, so the image is already real —
    //    RealPart discards nothing, and the PSF is real and one-sided.
    //  - Real-valued CTF: the transform is real, so h(−r) = conj(h(r)) and the
    //    real part is exactly centrosymmetric. The discarded imaginary part is
    //    where all the asymmetry went; this is inherent to the model, not a bug.
    m_origW = N;
    m_origH = N;
    // Record what this buffer's Fourier data means, so the FT⁻¹ arrow (and any
    // later re-inversion) reproduces this image instead of reinterpreting it.
    m_ftInverseOutput = (model == CtfModel::Pupil) ? InverseOutput::Intensity
                                                   : InverseOutput::RealPart;
    computeInverseFFT(m_ftInverseOutput);
        m_toolProgress = 0.97;
    });

    // Step: store the result in the active history slot.
    steps.push_back([this, voltageKV, defocusNM, csMM, model]() {

    if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS) {
        const char *modelName = (model == CtfModel::Pupil)      ? "pupil"
                              : (model == CtfModel::ComplexCTF) ? "complex"
                                                                : "real";
        m_imagePath = QString("ctf[%1]: %2kV df=%3nm Cs=%4mm")
                          .arg(modelName).arg(voltageKV).arg(defocusNM).arg(csMM);
        m_history[m_activeSlot].image        = m_image;
        m_history[m_activeSlot].path         = m_imagePath;
        m_history[m_activeSlot].rawPixels    = m_imageRawPixels;
        m_history[m_activeSlot].minVal       = m_imageMinVal;
        m_history[m_activeSlot].maxVal       = m_imageMaxVal;
        m_history[m_activeSlot].pixelSize    = m_pixelSize;
        // Keep the simulated CTF itself as this buffer's transform. A beam-tilted
        // CTF is not Hermitian, so it cannot be recovered from the real-space
        // image: that image is the real part of the inverse transform, and any
        // forward FFT of it is Hermitian by construction and would silently
        // symmetrise the CTF (erasing the coma). Cache the transform and derive
        // the panel-4 thumbnail from it rather than from the image.
        m_history[m_activeSlot].fftData      = m_fftData;
        m_history[m_activeSlot].fftN         = m_fftN;
        m_history[m_activeSlot].fftOrigW     = m_origW;
        m_history[m_activeSlot].fftOrigH     = m_origH;
        m_history[m_activeSlot].ftComputed   = true;
        m_history[m_activeSlot].ftInverseOutput = m_ftInverseOutput;
        m_history[m_activeSlot].powerSpecImg = powerSpecFromCurrentFFT();
        m_history[m_activeSlot].occupied     = true;
    }

    saveHistory();
        m_toolProgress = -1;
    });

    chainSteps(std::move(steps));
}

// Default the fit band to 10%…90% of the Nyquist frequency of the current
// image. Nyquist is 2 * pixelSize (Å), so a fraction f of the Nyquist frequency
// corresponds to a d-spacing of nyquist / f: 90% of Nyquist is the fine (upper)
// limit and 10% the coarse (lower) one. Percentages are of the frequency, not of
// the d-spacing — taking 90% of the Nyquist *spacing* would place the limit
// beyond Nyquist, where the transform holds no data.
void FtWindow::updateCtfFitResolutionDefaults()
{
    if (!m_ctfFitResHiEdit || !m_ctfFitResLoEdit) return;

    double pixelSize = m_pixelSize;
    // Prefer the buffer actually being fitted, when it differs from the active one.
    if (m_ctfFitInputCombo) {
        int idx = m_ctfFitInputCombo->currentIndex();
        if (idx >= 0 && idx < HISTORY_SLOTS && m_history[idx].occupied)
            pixelSize = m_history[idx].pixelSize;
    }
    if (!(pixelSize > 0)) return;   // unknown scale: leave the existing defaults

    const double nyquistA = 2.0 * pixelSize;
    const double resHiA   = nyquistA / 0.9;   // finest included (small Å)
    const double resLoA   = nyquistA / 0.1;   // coarsest included (large Å)

    m_ctfFitResHiEdit->setText(QString::number(resHiA, 'g', 4));
    m_ctfFitResLoEdit->setText(QString::number(resLoA, 'g', 4));
}

void FtWindow::onCtfFitCancel()
{
    m_ctfFitActive = false;
    m_ctfFitHasResult = false;
    m_ctfFitVoltageEdit->hide();
    m_ctfFitCsEdit->hide();
    m_ctfFitInputCombo->hide();
    m_ctfFitResHiEdit->hide();
    m_ctfFitResLoEdit->hide();
    m_ctfFitCancelBtn->hide();
    m_ctfFitExecuteBtn->hide();
    update();
}

void FtWindow::onCtfFitExecute()
{
    if (!ensureCalcHeadroom(tr("fit the CTF"))) return;
    onCtfFitExecuteImpl();
}

// Fit a CTF (defocus + astigmatism) to the power spectrum |FFT|² of the current
// image, GCTFFIND-style, then synthesise the fitted transfer function into a
// user-chosen target buffer and show it on the Fourier side.
//
// The fit works on a background-subtracted, per-radius whitened polar sampling
// of the power spectrum, and maximises the normalised cross-correlation between
// that data and the theoretical Thon-ring intensity |CTF|². Search proceeds in
// three passes: a 1D coarse defocus scan, a 2D scan over defocus/astigmatism/
// astigmatism-angle, and a local refinement.
void FtWindow::onCtfFitExecuteImpl()
{
    // ---- parameters ----
    bool okV = false, okC = false, okRHi = false, okRLo = false;
    double voltageKV = m_ctfFitVoltageEdit->text().toDouble(&okV);
    double csMM      = m_ctfFitCsEdit->text().toDouble(&okC);
    double resHiA    = m_ctfFitResHiEdit->text().toDouble(&okRHi);  // upper limit (fine, small Å)
    double resLoA    = m_ctfFitResLoEdit->text().toDouble(&okRLo);  // lower limit (coarse, large Å)
    if (!okV || voltageKV <= 0) voltageKV = 300.0;
    if (!okC)                   csMM      = 2.7;
    if (!okRHi || resHiA <= 0)  resHiA    = 3.0;
    if (!okRLo || resLoA <= 0)  resLoA    = 30.0;
    if (resHiA > resLoA) std::swap(resHiA, resLoA);   // finer value = high-freq edge

    int inputIdx = m_ctfFitInputCombo->currentIndex();
    if (inputIdx < 0 || inputIdx >= HISTORY_SLOTS)
        inputIdx = (m_activeSlot >= 0) ? m_activeSlot : 0;
    // The fitted composite is written into the currently selected buffer.
    int outIdx = (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS) ? m_activeSlot : inputIdx;

    // Relativistic electron wavelength (Å) and Cs (Å).
    double V = voltageKV * 1000.0;
    double lambdaA = 12.2639 / std::sqrt(V * (1.0 + V * 0.978466e-6));
    double CsA = csMM * 1.0e7;

    // Fixed defaults for the quantities not exposed by the fit UI.
    const double B = 0.07;                       // amplitude contrast (7%)
    const double A = std::sqrt(1.0 - B * B);     // phase contrast
    const double alphaRad = 0.1e-3;              // gun opening half-angle
    double dzUserA  = 5.0 * 10.0;                // 5 nm defocus spread
    double dzChromA = CsA * (0.7 / V);           // chromatic term (energy 0.7 eV)
    double defocusSpreadA = std::sqrt(dzChromA * dzChromA + dzUserA * dzUserA);

    // ---- obtain the Fourier transform of the INPUT buffer ----
    // Prefer a cached transform; otherwise compute it from the buffer's image
    // (centered convention, matching computeFFT).
    auto computeCenteredFFT = [](const QImage &img, int &Nout) -> std::vector<Complex> {
        QImage gray = img.convertToFormat(QImage::Format_Grayscale8);
        int w = gray.width(), h = gray.height();
        int N = nextGoodFFTSize(std::max(w, h));
        Nout = N;
        double sum = 0.0;
        for (int y = 0; y < h; y++) {
            const uchar *row = gray.constScanLine(y);
            for (int x = 0; x < w; x++) sum += row[x];
        }
        double avg = (w > 0 && h > 0) ? sum / ((double)w * h) : 0.0;
        std::vector<Complex> data((size_t)N * N, Complex(avg, 0.0));
        for (int y = 0; y < h; y++) {
            const uchar *row = gray.constScanLine(y);
            for (int x = 0; x < w; x++) data[(size_t)y * N + x] = Complex(row[x], 0.0);
        }
        fftShift(data, N);
        fft2d(data, N, false);
        fftShift(data, N);
        return data;
    };

    std::vector<Complex> srcFFT;
    int    N   = 0;
    double dxA = 1.0;
    QImage inImage;
    std::vector<double> inRaw;
    double inMin = 0.0, inMax = 0.0;
    if (inputIdx == m_activeSlot && m_ftComputed && !m_fftData.empty()) {
        srcFFT = m_fftData; N = m_fftN; dxA = (m_pixelSize > 0) ? m_pixelSize : 1.0;
        inImage = m_image; inRaw = m_imageRawPixels; inMin = m_imageMinVal; inMax = m_imageMaxVal;
    } else if (inputIdx == m_activeSlot && !m_image.isNull()) {
        srcFFT = computeCenteredFFT(m_image, N); dxA = (m_pixelSize > 0) ? m_pixelSize : 1.0;
        inImage = m_image; inRaw = m_imageRawPixels; inMin = m_imageMinVal; inMax = m_imageMaxVal;
    } else if (inputIdx >= 0 && inputIdx < HISTORY_SLOTS && m_history[inputIdx].occupied) {
        HistoryEntry &e = m_history[inputIdx];
        dxA = (e.pixelSize > 0) ? e.pixelSize : 1.0;
        inImage = e.image; inRaw = e.rawPixels; inMin = e.minVal; inMax = e.maxVal;
        if (e.ftComputed && !e.fftData.empty()) { srcFFT = e.fftData; N = e.fftN; }
        else if (!e.image.isNull())             { srcFFT = computeCenteredFFT(e.image, N); }
    }
    if (srcFFT.empty() || N <= 0) {
        QMessageBox::warning(this, tr("CTF Fit"),
            tr("The selected input buffer has no image to fit."));
        return;
    }

    storeUndoSnapshot();

    double cx  = N / 2.0, cy = N / 2.0;

    // ---- polar sampling of the power spectrum |FFT|² ----
    // theta spans [0,pi): the power spectrum is centrosymmetric and the
    // astigmatism has period pi, so a half turn is sufficient. The fit is
    // restricted to the requested resolution band: the Fourier radius for a
    // resolution d (Å) is r = N·dxA / d, clamped below the Nyquist corner.
    const int nR = 96, nT = 120;
    const int nCells = nR * nT;
    double rMax = std::min(N * 0.49, N * dxA / resHiA);
    double rMin = std::max(2.0,      N * dxA / resLoA);
    if (rMax <= rMin) { rMin = N * 0.03; rMax = N * 0.48; }

    std::vector<double> C1(nR), C2(nR), rPixArr(nR);
    for (int iR = 0; iR < nR; iR++) {
        double rPix = rMin + (rMax - rMin) * iR / (nR - 1);
        rPixArr[iR] = rPix;
        double q = rPix / (N * dxA);
        double q2 = q * q;
        C1[iR] = M_PI * lambdaA * q2;                                   // ·Δf
        C2[iR] = 0.5 * M_PI * CsA * lambdaA * lambdaA * lambdaA * q2 * q2;
    }
    std::vector<double> cos2(nT), sin2(nT), cth(nT), sth(nT);
    for (int iT = 0; iT < nT; iT++) {
        double theta = M_PI * iT / nT;
        cth[iT] = std::cos(theta);
        sth[iT] = std::sin(theta);
        cos2[iT] = std::cos(2.0 * theta);
        sin2[iT] = std::sin(2.0 * theta);
    }

    auto sampleP = [&](double fx, double fy) -> double {
        int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
        if (x0 < 0 || y0 < 0 || x0 + 1 >= N || y0 + 1 >= N) return 0.0;
        double tx = fx - x0, ty = fy - y0;
        auto P = [&](int xx, int yy) { return std::norm(srcFFT[(size_t)yy * N + xx]); };
        double v00 = P(x0, y0), v10 = P(x0 + 1, y0);
        double v01 = P(x0, y0 + 1), v11 = P(x0 + 1, y0 + 1);
        double vx0 = v00 * (1.0 - tx) + v10 * tx;
        double vx1 = v01 * (1.0 - tx) + v11 * tx;
        return vx0 * (1.0 - ty) + vx1 * ty;
    };

    std::vector<double> Pdata(nCells);
    for (int iR = 0; iR < nR; iR++) {
        double rPix = rPixArr[iR];
        for (int iT = 0; iT < nT; iT++) {
            // Image y axis points down, so flip it (matches the synthesis).
            double fx = cx + rPix * cth[iT];
            double fy = cy - rPix * sth[iT];
            Pdata[iR * nT + iT] = sampleP(fx, fy);
        }
    }
    // ---- background subtraction that PRESERVES the isotropic Thon rings ----
    // Subtracting the exact azimuthal mean at each radius would erase the
    // isotropic rings (for a non-astigmatic image nothing would be left to
    // fit). Instead estimate a SMOOTH radial background from the rotational
    // average and subtract that; a smooth radial amplitude envelope then
    // whitens the residual so weak high-frequency rings weigh as much as strong
    // low-frequency ones. This mirrors the CTFFIND/GCtfFind preprocessing.
    auto movAvg = [](const std::vector<double> &in, int win) {
        int n = (int)in.size();
        std::vector<double> out(n, 0.0);
        if (win < 1) win = 1;
        for (int i = 0; i < n; i++) {
            int a = std::max(0, i - win), b = std::min(n - 1, i + win);
            double s = 0.0;
            for (int j = a; j <= b; j++) s += in[j];
            out[i] = s / (b - a + 1);
        }
        return out;
    };

    std::vector<double> Prad(nR, 0.0);
    for (int iR = 0; iR < nR; iR++) {
        double s = 0.0;
        for (int iT = 0; iT < nT; iT++) s += Pdata[iR * nT + iT];
        Prad[iR] = s / nT;
    }
    std::vector<double> Bg = movAvg(Prad, std::max(3, nR / 8));   // smooth envelope

    std::vector<double> res2(nR, 0.0);
    for (int iR = 0; iR < nR; iR++) {
        double s = 0.0;
        for (int iT = 0; iT < nT; iT++) {
            double d = Pdata[iR * nT + iT] - Bg[iR];
            s += d * d;
        }
        res2[iR] = s / nT;
    }
    std::vector<double> ampSm = movAvg(res2, std::max(3, nR / 6));
    std::vector<double> Amp(nR);
    for (int iR = 0; iR < nR; iR++) Amp[iR] = std::sqrt(ampSm[iR]);

    std::vector<double> &W = Pdata;          // whiten the 2D data in place
    for (int iR = 0; iR < nR; iR++) {
        double inv = (Amp[iR] > 1e-20) ? 1.0 / Amp[iR] : 0.0;
        for (int iT = 0; iT < nT; iT++)
            W[iR * nT + iT] = (W[iR * nT + iT] - Bg[iR]) * inv;
    }

    // Pearson-correlation precomputations for the whitened data.
    double wMean = 0.0; for (double v : W) wMean += v; wMean /= nCells;
    double wVar  = 0.0; for (double v : W) { double d = v - wMean; wVar += d * d; }
    if (wVar <= 0.0) wVar = 1.0;

    // Envelope-free model ring intensity o(χ) = (A·sin(−χ)+B·cos(−χ))², so the
    // (whitened) high-frequency rings keep full weight in the correlation.
    auto oIso = [A, B](double chi) {
        double s = A * std::sin(-chi) + B * std::cos(-chi);
        return s * s;
    };

    // 2D score: Pearson CC between the whitened spectrum and the astigmatic
    // model, over the resolution band.
    auto score2D = [&](double dfMeanA, double astigAmpA, double ca, double sa) -> double {
        double sumWM = 0.0, sumM = 0.0, sumM2 = 0.0;
        for (int iR = 0; iR < nR; iR++) {
            double c1 = C1[iR], c2 = C2[iR];
            const double *Wrow = &W[iR * nT];
            for (int iT = 0; iT < nT; iT++) {
                double cos2d = cos2[iT] * ca + sin2[iT] * sa;       // cos(2(θ-α))
                double dfLocal = dfMeanA + astigAmpA * cos2d;
                double M = oIso(c1 * dfLocal + c2);
                sumWM += Wrow[iT] * M;
                sumM  += M;
                sumM2 += M * M;
            }
        }
        double mVar = sumM2 - sumM * sumM / nCells;
        double num  = sumWM - wMean * sumM;                         // Σ(W−wMean)·M
        if (mVar <= 0.0) return -2.0;
        return num / std::sqrt(wVar * mVar);
    };

    // Pass 1: full-range 2D coarse grid over mean defocus, astigmatism
    // magnitude and angle. A 1D (rotational-average) prescan is NOT used: a
    // large astigmatism smears the rotational average and would mislead it, so
    // the coarse defocus is searched jointly with astigmatism instead. The
    // ranges span ~200 nm … ~10 µm defocus and up to ~1.8 µm astigmatism
    // amplitude, covering the extremes of typical cryo-EM data.
    const double kAstigMaxA = 18000.0;    // astigmatism amplitude search cap (Å)
    double bestDf = -10000.0, bestAst = 0.0, bestAng = 0.0, bestCC = -2.0;
    for (double df = -2000.0; df >= -100000.0; df -= 800.0)
        for (double ast = 0.0; ast <= kAstigMaxA; ast += 1500.0)
            for (int ai = 0; ai < 12; ai++) {
                double al = M_PI * ai / 12.0;
                double cc = score2D(df, ast, std::cos(2.0 * al), std::sin(2.0 * al));
                if (cc > bestCC) { bestCC = cc; bestDf = df; bestAst = ast; bestAng = al; }
            }

    // Pass 2: GCtfFind-style iterative univariate refinement with ranges that
    // shrink by half each iteration (mean defocus, astig magnitude, angle).
    {
        double dfR = 800.0, astR = 1500.0, angR = M_PI / 12.0;
        for (int it = 0; it < 14; it++) {
            double c = std::cos(2.0 * bestAng), s = std::sin(2.0 * bestAng);
            for (double df = bestDf - dfR; df <= bestDf + dfR; df += dfR / 5.0) {
                double cc = score2D(df, bestAst, c, s);
                if (cc > bestCC) { bestCC = cc; bestDf = df; }
            }
            for (double ast = std::max(0.0, bestAst - astR); ast <= bestAst + astR; ast += astR / 5.0) {
                double cc = score2D(bestDf, ast, c, s);
                if (cc > bestCC) { bestCC = cc; bestAst = ast; }
            }
            for (double al = bestAng - angR; al <= bestAng + angR; al += angR / 5.0) {
                double cc = score2D(bestDf, bestAst, std::cos(2.0 * al), std::sin(2.0 * al));
                if (cc > bestCC) { bestCC = cc; bestAng = al; }
            }
            dfR *= 0.5; astR *= 0.5; angR *= 0.5;
        }
    }

    // Fitted parameters (Å / rad) in the code's sign convention.
    double dfA           = bestDf;
    double astigA        = bestAst;
    double astigAngleRad = bestAng;
    while (astigAngleRad < 0)       astigAngleRad += M_PI;
    while (astigAngleRad >= M_PI)   astigAngleRad -= M_PI;

    // Physical values for reporting (nm / degrees). Positive defocus = underfocus.
    // The fit's `astigA` is the astigmatism AMPLITUDE (the defocus deviation from
    // the mean, i.e. (df1−df2)/2). The conventional astigmatism reported by
    // CTFFIND/GCtfFind — and the ground-truth used here — is the full peak-to-peak
    // difference df1−df2, which is twice the amplitude.
    double defocusNM   = -dfA / 10.0;
    double astigAmpNM  =  astigA / 10.0;         // amplitude (for the CTF SIM field)
    double astigFullNM =  2.0 * astigA / 10.0;   // df1−df2 (conventional, reported)
    double astigAngDeg =  astigAngleRad * 180.0 / M_PI;
    // The fit's azimuth is measured in image coordinates (y flipped), so it is
    // mirrored relative to the standard EM convention (CCW from +x). Report the
    // mirrored angle (validated against the Exercise_11 ground truth).
    double astigAngStdDeg = std::fmod(180.0 - astigAngDeg, 180.0);
    if (astigAngStdDeg < 0.0) astigAngStdDeg += 180.0;
    // CTF SIM negates the astigmatism term internally, so reproducing the fitted
    // ellipse from its fields needs the azimuth rotated by 90°.
    double astigAngSimDeg = std::fmod(astigAngDeg + 90.0, 180.0);

    // Echo the fitted values into the CTF SIM parameter fields so the user can
    // inspect and re-simulate them. CTF SIM's astigmatism field is the amplitude
    // (half the peak-to-peak difference) and uses its own azimuth convention.
    if (m_ctfVoltageEdit)   m_ctfVoltageEdit->setText(QString::number(voltageKV, 'f', 0));
    if (m_ctfCsEdit)        m_ctfCsEdit->setText(QString::number(csMM, 'f', 2));
    if (m_ctfDefocusEdit)   m_ctfDefocusEdit->setText(QString::number(defocusNM, 'f', 1));
    if (m_ctfAstigEdit)     m_ctfAstigEdit->setText(QString::number(astigAmpNM, 'f', 1));
    if (m_ctfAstigAngleEdit)m_ctfAstigAngleEdit->setText(QString::number(astigAngSimDeg, 'f', 1));

    // Isotropic (Cs) transfer function, evaluated per pixel; astigmatism enters
    // through the azimuthal local defocus computed in the fill loop below.
    auto ctfAt = [N, dxA, lambdaA, CsA, defocusSpreadA, alphaRad, A, B]
                 (double dfLocalA, double rPix) -> Complex {
        double q = rPix / (N * dxA);
        double q2 = q * q;
        double chiEven = M_PI * lambdaA * dfLocalA * q2
                       + 0.5 * M_PI * CsA * lambdaA * lambdaA * lambdaA * q2 * q2;
        double tArg = M_PI * lambdaA * defocusSpreadA * q2;
        double envT = std::exp(-0.5 * tArg * tArg);
        double sArg = dfLocalA + CsA * lambdaA * lambdaA * q2;
        double envS = std::exp(-(M_PI * M_PI) * alphaRad * alphaRad * q2 * sArg * sArg);
        double base = envT * envS * (A * std::sin(-chiEven) + B * std::cos(-chiEven));
        return Complex(base, 0.0);
    };

    // Effective (isotropic) radius that maps an astigmatic Thon-ring ellipse to a
    // circle: the mean-defocus radius r_eff at which the fitted CTF has the same
    // phase (chi) as this pixel. Averaging power over bins of r_eff therefore
    // pools pixels along the true elliptical Thon rings (defocus + astigmatism),
    // and reusing r_eff when filling the display makes the averaged rings
    // elliptical again, aligned with the original transform and the model.
    double aQ4  = 0.5 * M_PI * CsA * lambdaA * lambdaA * lambdaA;  // q^4 coeff of chi
    double bQ2  = M_PI * lambdaA * dfA;                            // q^2 coeff (mean df)
    double NdxA = N * dxA;
    auto effRadius = [aQ4, bQ2, NdxA, dfA, astigA, astigAngleRad, lambdaA]
                     (double dx, double dy) -> double {
        double rr = std::sqrt(dx * dx + dy * dy);
        if (rr < 1e-9) return 0.0;
        double theta = std::atan2(-dy, dx);
        double q = rr / NdxA;
        double dfLocal = dfA + astigA * std::cos(2.0 * (theta - astigAngleRad));
        double chi = M_PI * lambdaA * dfLocal * q * q + aQ4 * q * q * q * q;
        double u;
        if (std::fabs(aQ4) < 1e-30) {
            u = (std::fabs(bQ2) > 1e-30) ? chi / bQ2 : q * q;
        } else {
            double disc = bQ2 * bQ2 + 4.0 * aQ4 * chi;
            double qc2  = -bQ2 / (2.0 * aQ4);       // chi extremum (defocus/Cs crossover)
            if (disc < 0.0 || q * q > qc2) return rr;   // past crossover: geometric radius
            u = (-bQ2 - std::sqrt(disc)) / (2.0 * aQ4);
        }
        if (u <= 0.0) return rr;
        return std::sqrt(u) * NdxA;
    };

    // Build the experimental (left) half of the composite from the source FFT.
    // The left half keeps its native amplitudes (so it renders with the same
    // contrast the original transform would); the fitted model on the right is
    // instead scaled so its mean displayed grey matches the left half's.
    struct CtfFitComposite {
        std::vector<Complex> src;         // original transform (top-left)
        std::vector<double>  radAmp;      // elliptical radial average, indexed by r_eff
        double               modelScale = 1.0;
    };
    auto comp = std::make_shared<CtfFitComposite>();
    {
        int rMaxInt = (int)std::ceil((N / 2.0) * std::sqrt(2.0)) + 2;
        std::vector<double> radSum(rMaxInt, 0.0), radCnt(rMaxInt, 0.0);
        double half = N / 2.0;
        int halfN = N / 2;

        // Pass 1: elliptical radial average of the power |src|^2 over r_eff bins.
        for (int y = 0; y < N; y++) {
            double dy = y - half;
            for (int x = 0; x < N; x++) {
                double dx = x - half;
                double p = std::norm(srcFFT[(size_t)y * N + x]);
                int ri = (int)(effRadius(dx, dy) + 0.5);
                if (ri >= 0 && ri < rMaxInt) { radSum[ri] += p; radCnt[ri] += 1.0; }
            }
        }
        std::vector<double> radAmp(rMaxInt);
        for (int r = 0; r < rMaxInt; r++)
            radAmp[r] = (radCnt[r] > 0) ? std::sqrt(radSum[r] / radCnt[r]) : 0.0;

        // Mean displayed grey (log power) of the left half, as it will appear.
        double sumLogLeft = 0.0; long long cntLeft = 0;
        for (int y = 0; y < N; y++) {
            double dy = y - half;
            for (int x = 0; x < halfN; x++) {
                double dx = x - half;
                double val;
                if (y < halfN) {
                    val = std::norm(srcFFT[(size_t)y * N + x]);          // top-left
                } else {
                    int ri = (int)(effRadius(dx, dy) + 0.5);             // bottom-left
                    double amp = (ri >= 0 && ri < rMaxInt) ? radAmp[ri] : 0.0;
                    val = amp * amp;
                }
                sumLogLeft += std::log(1.0 + val);
                cntLeft++;
            }
        }
        double meanLogLeft = (cntLeft > 0) ? sumLogLeft / cntLeft : 0.0;

        // Histogram the model intensity base^2 over the right half so we can find
        // the amplitude gain g that lifts its mean log power to the left's.
        const int HB = 1000;
        std::vector<double> hist(HB, 0.0); long long cntRight = 0;
        for (int y = 0; y < N; y++) {
            double dy = y - half;
            for (int x = halfN; x < N; x++) {
                double dx = x - half;
                double rr = std::sqrt(dx * dx + dy * dy);
                double theta = std::atan2(-dy, dx);
                double dfLocal = dfA + astigA * std::cos(2.0 * (theta - astigAngleRad));
                double base = ctfAt(dfLocal, rr).real();
                double b2 = std::min(1.0, base * base);
                int k = (int)(b2 * (HB - 1));
                if (k < 0) k = 0; if (k >= HB) k = HB - 1;
                hist[k] += 1.0; cntRight++;
            }
        }
        auto meanLogModel = [&](double g) {
            double gg = g * g, s = 0.0;
            for (int k = 0; k < HB; k++)
                if (hist[k] > 0.0)
                    s += hist[k] * std::log(1.0 + gg * ((k + 0.5) / HB));
            return (cntRight > 0) ? s / cntRight : 0.0;
        };
        double glo = 1e-6, ghi = 1e12, g = 1.0;
        if (meanLogModel(ghi) <= meanLogLeft)      g = ghi;
        else if (meanLogModel(glo) >= meanLogLeft) g = glo;
        else for (int it = 0; it < 60; it++) {     // geometric bisection (g spans decades)
            g = std::sqrt(glo * ghi);
            if (meanLogModel(g) < meanLogLeft) glo = g; else ghi = g;
        }

        comp->modelScale = g;
        comp->radAmp = std::move(radAmp);
        comp->src = std::move(srcFFT);      // take the input FFT
    }

    // The output composite is placed into the currently selected buffer, whose
    // real-space side becomes the input image that was fitted.
    m_activeSlot     = outIdx;
    m_image          = inImage;
    m_imageRawPixels = inRaw;
    m_imageMinVal    = inMin;
    m_imageMaxVal    = inMax;
    m_imageDispMin   = inMin;
    m_imageDispMax   = inMax;
    m_pixelSize      = dxA;                 // sampled like the input image
    m_origW          = inImage.width();
    m_origH          = inImage.height();
    m_zoom[0].reset(inImage.width(), inImage.height());

    m_toolProgress = 0.1;
    update();

    std::vector<std::function<void()>> steps;

    steps.push_back([this, N]() {
        m_fftN = N;
        m_fftData.assign((size_t)N * N, Complex(0.0, 0.0));
        m_toolProgress = 0.12;
    });

    // Fill the composite Fourier display in horizontal bands:
    //   right half          -> the newly fitted, synthesised CTF (scaled to match
    //                          the left half's mean grey)
    //   top-left quarter    -> the original Fourier transform of the input image
    //   bottom-left quarter -> the elliptically radially averaged Fourier transform
    const int nBands = 32;
    for (int b = 0; b < nBands; b++) {
        int y0 = (int)((long long)b       * N / nBands);
        int y1 = (int)((long long)(b + 1) * N / nBands);
        double prog = 0.12 + 0.78 * (double)(b + 1) / nBands;
        steps.push_back([this, comp, ctfAt, effRadius, N, dfA, astigA, astigAngleRad, y0, y1, prog]() {
            double half = N / 2.0;
            int halfN = N / 2;
            int rMaxInt = (int)comp->radAmp.size();
            double g = comp->modelScale;
            for (int y = y0; y < y1; y++) {
                double dy = y - half;
                for (int x = 0; x < N; x++) {
                    double dx = x - half;
                    size_t idx = (size_t)y * N + x;
                    if (x >= halfN) {
                        // Right half: fitted CTF, scaled to match the left's grey.
                        double rPix = std::sqrt(dx * dx + dy * dy);
                        double theta = std::atan2(-dy, dx);
                        double dfLocal = dfA + astigA * std::cos(2.0 * (theta - astigAngleRad));
                        m_fftData[idx] = g * ctfAt(dfLocal, rPix);
                    } else if (y < halfN) {
                        // Top-left quarter: original Fourier transform (unscaled).
                        m_fftData[idx] = comp->src[idx];
                    } else {
                        // Bottom-left quarter: elliptical radial average (unscaled).
                        int ri = (int)(effRadius(dx, dy) + 0.5);
                        double amp = (ri >= 0 && ri < rMaxInt) ? comp->radAmp[ri] : 0.0;
                        m_fftData[idx] = Complex(amp, 0.0);
                    }
                }
            }
            m_toolProgress = prog;
        });
    }

    steps.push_back([this, N]() {
        m_ftComputed = true;
        m_modeBtn->show();
        m_maskBtnVisible = true;
        m_zoom[1].reset(N, N);
        m_zoom[2].reset(N, N);
        recomputeDisplayImages();
        m_toolProgress = 0.93;
    });

    steps.push_back([this, outIdx, defocusNM, astigFullNM, astigAngStdDeg]() {
        // Store into the selected output slot. Real space keeps the input image;
        // the Fourier side (cached transform + thumbnail) is the fit composite.
        m_imagePath = QString("ctffit: def=%1nm astig=%2nm ang=%3°")
                          .arg(defocusNM, 0, 'f', 1)
                          .arg(astigFullNM, 0, 'f', 1)
                          .arg(astigAngStdDeg, 0, 'f', 1);
        m_history[outIdx].image        = m_image;
        m_history[outIdx].path         = m_imagePath;
        m_history[outIdx].rawPixels    = m_imageRawPixels;
        m_history[outIdx].minVal       = m_imageMinVal;
        m_history[outIdx].maxVal       = m_imageMaxVal;
        m_history[outIdx].pixelSize    = m_pixelSize;
        m_history[outIdx].fftData      = m_fftData;
        m_history[outIdx].fftN         = m_fftN;
        m_history[outIdx].fftOrigW     = m_origW;
        m_history[outIdx].fftOrigH     = m_origH;
        m_history[outIdx].ftComputed   = true;
        m_history[outIdx].powerSpecImg = powerSpecFromCurrentFFT();
        m_history[outIdx].occupied     = true;
        saveHistory();

        // Record the fitted values and keep the parameter window open so they
        // are shown; the tool stays active for another fit.
        m_ctfFitResDefocusNM = defocusNM;
        m_ctfFitResAstigNM   = astigFullNM;
        m_ctfFitResAngleDeg  = astigAngStdDeg;
        m_ctfFitHasResult    = true;
        m_toolProgress = -1;
        update();
    });

    chainSteps(std::move(steps));
}

void FtWindow::onEditPixelSize()
{
    if (m_image.isNull()) return;

    // A small popup styled like the in-panel parameter windows (dark field,
    // outset buttons). Units are always Ångström.
    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setModal(true);
    dlg->setWindowTitle(tr("Edit pixel size"));
    dlg->setStyleSheet("QDialog { background:#333; }");

    auto *layout = new QVBoxLayout(dlg);
    auto *label = new QLabel(QString::fromUtf8("Pixel size (Å):"), dlg);
    label->setStyleSheet("color:#eee;");
    layout->addWidget(label);

    auto *edit = new QLineEdit(QString::number(m_pixelSize, 'g', 6), dlg);
    edit->setStyleSheet("background:#222; color:white; border:1px solid #888; padding:2px;");
    layout->addWidget(edit);
    edit->selectAll();
    edit->setFocus();

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    buttons->setStyleSheet(
        "QPushButton { background-color:#888; border:2px outset #aaa; color:#eee; padding:2px 12px; }");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    connect(edit, &QLineEdit::returnPressed, dlg, &QDialog::accept);

    connect(dlg, &QDialog::accepted, this, [this, edit]() {
        bool ok = false;
        double v = edit->text().toDouble(&ok);
        if (ok && v > 0.0) {
            m_pixelSize = v;
            if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS)
                m_history[m_activeSlot].pixelSize = v;
            saveHistory();
            update();
        }
    });

    dlg->show();
}

void FtWindow::onPhaseRampCancel()
{
    m_phaseRampActive = false;
    m_phaseRampSizeCombo->hide();
    m_phaseRampDirEdit->hide();
    m_phaseRampStepEdit->hide();
    m_phaseRampCancelBtn->hide();
    m_phaseRampComputeBtn->hide();
    update();
}

void FtWindow::onPhaseRampCompute()
{
    if (!ensureCalcHeadroom(tr("apply the phase ramp"))) return;
    onPhaseRampComputeImpl();
}

void FtWindow::onPhaseRampComputeImpl()
{
    int N = m_phaseRampSizeCombo->currentData().toInt();
    if (N <= 0) N = 1024;

    bool okDir = false, okStep = false;
    double dirDeg  = m_phaseRampDirEdit->text().toDouble(&okDir);
    double stepDeg = m_phaseRampStepEdit->text().toDouble(&okStep);
    if (!okDir)  dirDeg  = 30.0;
    if (!okStep) stepDeg = 10.0;

    storeUndoSnapshot();

    // Pick or create an active history slot, allocate a real-space buffer.
    if (m_activeSlot < 0) {
        m_activeSlot = HISTORY_SLOTS - 1;
        for (int i = 0; i < HISTORY_SLOTS; i++) {
            if (!m_history[i].occupied) { m_activeSlot = i; break; }
        }
    }
    m_image = QImage(N, N, QImage::Format_Grayscale8);
    m_image.fill(0);
    m_imageRawPixels.assign((size_t)N * N, 0.0);
    m_pixelSize = (m_pixelSize > 0) ? m_pixelSize : 1.0;

    // Build shifted FFT: amplitude 1, phase = stepRad * projection along
    // the chosen direction, with the origin at the array centre.
    double dirRad  = dirDeg  * M_PI / 180.0;
    double stepRad = stepDeg * M_PI / 180.0;
    double cd = std::cos(dirRad);
    double sd = std::sin(dirRad);

    // Left-to-right blue progress bar in the parameter-window background.
    m_toolProgress = 0.1;
    update();

    chainSteps({
        // --- Step 1: fill Fourier space with the phase ramp ---
        [this, N, stepRad, cd, sd]() {
    m_fftN = N;
    m_fftData.assign((size_t)N * N, Complex(0.0, 0.0));
    double half = N / 2.0;
    for (int y = 0; y < N; y++) {
        // Image y axis points downward; flip for the math CCW convention so
        // that "direction" matches the angle shown elsewhere in the UI.
        double dy = -(y - half);
        for (int x = 0; x < N; x++) {
            double dx = x - half;
            double phase = stepRad * (dx * cd + dy * sd);
            m_fftData[y * N + x] = Complex(std::cos(phase), std::sin(phase));
        }
    }
            m_toolProgress = 0.5;
        },
        // --- Step 2: build the panel-2 display images ---
        [this, N]() {
    m_ftComputed = true;
    m_origW = N;
    m_origH = N;
    m_modeBtn->show();
    m_maskBtnVisible = true;
    m_zoom[0].reset(N, N);
    m_zoom[1].reset(N, N);
    m_zoom[2].reset(N, N);
    recomputeDisplayImages();
            m_toolProgress = 0.7;
        },
        // --- Step 3: inverse transform to real space for panel 1 ---
        [this]() {

    // Inverse-transform to real space for panel 1. computeInverseFFT now
    // produces the result in centered real-space form, so the delta of a
    // phase ramp already lands at (N/2, N/2) plus the shift implied by the
    // ramp — no extra quadrant swap needed.
    computeInverseFFT();
            m_toolProgress = 0.9;
        },
        // --- Step 4: store the result in the active history slot ---
        [this, N, dirDeg, stepDeg]() {

    if (m_activeSlot >= 0 && m_activeSlot < HISTORY_SLOTS) {
        m_imagePath = QString("phase ramp: N=%1 dir=%2° step=%3°")
                          .arg(N).arg(dirDeg).arg(stepDeg);
        m_history[m_activeSlot].image        = m_image;
        m_history[m_activeSlot].path         = m_imagePath;
        m_history[m_activeSlot].rawPixels    = m_imageRawPixels;
        m_history[m_activeSlot].minVal       = m_imageMinVal;
        m_history[m_activeSlot].maxVal       = m_imageMaxVal;
        m_history[m_activeSlot].pixelSize    = m_pixelSize;
        m_history[m_activeSlot].powerSpecImg = computePowerSpecMasked(m_image);
        m_history[m_activeSlot].occupied     = true;
    }

    saveHistory();
            m_toolProgress = -1;
        }
    });
}
