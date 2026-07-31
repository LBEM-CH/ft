#include "mrcloader.h"
#include <QFile>
#include <QDebug>
#include <QtEndian>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

QByteArray saveMrcToData(const std::vector<double> &pixels,
                         int nx, int ny, double pixelSize)
{
    if (nx <= 0 || ny <= 0 || (qint64)pixels.size() < (qint64)nx * ny)
        return QByteArray();
    if (!(pixelSize > 0.0)) pixelSize = 1.0;

    const qint64 count = (qint64)nx * ny;

    // Gather statistics for the header (order-independent).
    float dmin = (float)pixels[0], dmax = (float)pixels[0];
    double dsum = 0.0;
    for (qint64 i = 0; i < count; i++) {
        const float v = (float)pixels[(size_t)i];
        dmin = std::min(dmin, v);
        dmax = std::max(dmax, v);
        dsum += v;
    }
    const float dmean = (float)(dsum / (double)count);

    // 1024-byte header, zero-filled. All little-endian (the only byte order this
    // build runs on); loadMrcFromData() reads it back via the MAP/machine stamp.
    QByteArray hdr(1024, '\0');
    char *h = hdr.data();
    auto putI32 = [&](int off, qint32 v) {
        v = qToLittleEndian(v);
        memcpy(h + off, &v, 4);
    };
    auto putF32 = [&](int off, float f) {
        quint32 bits;
        memcpy(&bits, &f, 4);
        bits = qToLittleEndian(bits);
        memcpy(h + off, &bits, 4);
    };

    putI32(0,  nx);           // columns
    putI32(4,  ny);           // rows
    putI32(8,  1);            // sections (single slice)
    putI32(12, 2);            // mode 2 = 32-bit float
    putI32(16, 0);            // nxStart
    putI32(20, 0);            // nyStart
    putI32(24, 0);            // nzStart
    putI32(28, nx);           // mx (sampling) — cellA/mx recovers the pixel size
    putI32(32, ny);           // my
    putI32(36, 1);            // mz
    putF32(40, (float)(nx * pixelSize));   // cellA (Å)
    putF32(44, (float)(ny * pixelSize));   // cellB (Å)
    putF32(48, (float)pixelSize);          // cellC (Å)
    putF32(52, 90.0f);        // cellAlpha
    putF32(56, 90.0f);        // cellBeta
    putF32(60, 90.0f);        // cellGamma
    putI32(64, 1);            // mapc (columns = x)
    putI32(68, 2);            // mapr (rows = y)
    putI32(72, 3);            // maps (sections = z)
    putF32(76, dmin);
    putF32(80, dmax);
    putF32(84, dmean);
    putI32(88, 0);            // ispg (image / stack of images)
    putI32(92, 0);            // nsymbt (no extended header)
    // "MAP " stamp at 208 and little-endian machine stamp at 212.
    h[208] = 'M'; h[209] = 'A'; h[210] = 'P'; h[211] = ' ';
    h[212] = 0x44; h[213] = 0x41; h[214] = 0x00; h[215] = 0x00;

    // Serialise the float data little-endian, bottom-up: MRC stores row 0 at the
    // bottom, and loadMrc*() flips rows back to top-down on read, so `pixels`
    // (top-down display order) must be written with the row order reversed to
    // round-trip correctly.
    // The body is count*4 bytes. Cap it before multiplying, so the size can
    // neither overflow qint64 nor exceed what QByteArray can hold on this
    // platform (qsizetype is 32-bit in WASM): a truncated size would produce
    // an undersized buffer that the row loop below writes past.
    const qint64 maxBody = qint64(std::numeric_limits<qsizetype>::max()) - 1024;
    if (count > maxBody / 4) return QByteArray();
    QByteArray body(qsizetype(count * 4), '\0');
    char *b = body.data();
    for (int y = 0; y < ny; y++) {
        const double *srcRow = pixels.data() + (qint64)(ny - 1 - y) * nx;
        char *dstRow = b + (qint64)y * nx * 4;
        for (int x = 0; x < nx; x++) {
            float f = (float)srcRow[x];
            quint32 bits;
            memcpy(&bits, &f, 4);
            bits = qToLittleEndian(bits);
            memcpy(dstRow + x * 4, &bits, 4);
        }
    }

    return hdr + body;
}

MrcResult loadMrc(const QString &path)
{
    QFile f(path);

    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "MRC: Failed to open file:" << path;
        return MrcResult();
    }

    // qDebug() << "MRC: Opening file:" << path;
    // qDebug() << "MRC: File size:" << f.size() << "bytes";

    QByteArray fileData = f.readAll();
    f.close();

    return loadMrcFromData(fileData);
}

bool readMrcDimensions(const QString &path, int &nx, int &ny)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    QByteArray hdr = f.read(1024);
    f.close();
    if (hdr.size() < 1024)
        return false;

    // Byte-order detection, same rules as loadMrcFromData().
    bool needSwap = false;
    bool hasMapStamp = (hdr[208] == 'M' && hdr[209] == 'A' &&
                        hdr[210] == 'P' && hdr[211] == ' ');
    if (hasMapStamp) {
        quint8 machst0 = (quint8)hdr[212];
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
        needSwap = (machst0 == 0x11);
#else
        needSwap = (machst0 == 0x44);
#endif
    } else {
        qint32 rawMode;
        memcpy(&rawMode, hdr.constData() + 12, 4);
        needSwap = (rawMode < 0 || rawMode > 16);
    }

    auto readI32 = [&](int offset) -> qint32 {
        qint32 v;
        memcpy(&v, hdr.constData() + offset, 4);
        if (needSwap) v = qbswap(v);
        return v;
    };

    qint32 w = readI32(0);
    qint32 h = readI32(4);
    if (w <= 0 || h <= 0)
        return false;

    nx = w;
    ny = h;
    return true;
}

MrcResult loadMrcFromData(const QByteArray &fileData)
{
    MrcResult result;

    // qDebug() << "MRC: Data size:" << fileData.size() << "bytes";

    if (fileData.size() < 1024) {
        qDebug() << "MRC: Data too small for MRC header (need >= 1024 bytes)";
        return result;
    }

    QByteArray hdr = fileData.left(1024);

    // --- byte-order detection ---------------------------------------------------
    bool needSwap = false;

    // Check MAP stamp at bytes 208-211 (should be ASCII "MAP ")
    bool hasMapStamp = (hdr[208] == 'M' && hdr[209] == 'A' &&
                        hdr[210] == 'P' && hdr[211] == ' ');

    // qDebug() << "MRC: MAP stamp bytes:" << QString("%1 %2 %3 %4")
    //             .arg((quint8)hdr[208], 2, 16, QChar('0'))
    //             .arg((quint8)hdr[209], 2, 16, QChar('0'))
    //             .arg((quint8)hdr[210], 2, 16, QChar('0'))
    //             .arg((quint8)hdr[211], 2, 16, QChar('0'))
    //          << (hasMapStamp ? "(valid)" : "(MISSING or invalid)");

    if (hasMapStamp) {
        quint8 machst0 = (quint8)hdr[212];
        quint8 machst1 = (quint8)hdr[213];
        // qDebug() << "MRC: Machine stamp:" << QString("0x%1 0x%2")
        //             .arg(machst0, 2, 16, QChar('0'))
        //             .arg(machst1, 2, 16, QChar('0'));

        // 0x44 = little-endian (Intel / ARM), 0x11 = big-endian
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
        if (machst0 == 0x11) {
            needSwap = true;
            // qDebug() << "MRC: Big-endian file detected – will byte-swap";
        }
#else
        if (machst0 == 0x44) {
            needSwap = true;
            // qDebug() << "MRC: Little-endian file on big-endian host – will byte-swap";
        }
#endif
    } else {
        // No MAP stamp – use heuristic: read mode in native order
        qint32 rawMode;
        memcpy(&rawMode, hdr.constData() + 12, 4);
        if (rawMode < 0 || rawMode > 16) {
            needSwap = true;
            // qDebug() << "MRC: No MAP stamp; raw mode =" << rawMode
            //          << "looks wrong – trying byte-swap";
        } else {
            // qDebug() << "MRC: No MAP stamp; raw mode =" << rawMode
            //          << "looks plausible – assuming native byte order";
        }
    }

    // helper lambdas
    auto readI32 = [&](int offset) -> qint32 {
        qint32 v;
        memcpy(&v, hdr.constData() + offset, 4);
        if (needSwap) v = qbswap(v);
        return v;
    };
    auto readFloat = [&](int offset) -> float {
        quint32 raw;
        memcpy(&raw, hdr.constData() + offset, 4);
        if (needSwap) raw = qbswap(raw);
        float v;
        memcpy(&v, &raw, 4);
        return v;
    };

    // --- read header fields -----------------------------------------------------
    int nx     = readI32(0);
    int ny     = readI32(4);
    int nz     = readI32(8);
    int mode   = readI32(12);
    int nxStart = readI32(16);
    int nyStart = readI32(20);
    int nzStart = readI32(24);
    int mx     = readI32(28);
    int my     = readI32(32);
    int mz     = readI32(36);
    float cellA     = readFloat(40);
    float cellB     = readFloat(44);
    float cellC     = readFloat(48);
    float cellAlpha = readFloat(52);
    float cellBeta  = readFloat(56);
    float cellGamma = readFloat(60);
    int mapc   = readI32(64);
    int mapr   = readI32(68);
    int maps   = readI32(72);
    float dmin  = readFloat(76);
    float dmax  = readFloat(80);
    float dmean = readFloat(84);
    int ispg    = readI32(88);
    int nsymbt  = readI32(92);

    // qDebug() << "MRC: ---- Header ----";
    // qDebug() << "MRC:  nx =" << nx << " ny =" << ny << " nz =" << nz;
    // qDebug() << "MRC:  mode =" << mode;
    // qDebug() << "MRC:  nxStart =" << nxStart << " nyStart =" << nyStart << " nzStart =" << nzStart;
    // qDebug() << "MRC:  mx =" << mx << " my =" << my << " mz =" << mz;
    // qDebug() << "MRC:  cellA =" << cellA << " cellB =" << cellB << " cellC =" << cellC;
    // qDebug() << "MRC:  cellAlpha =" << cellAlpha << " cellBeta =" << cellBeta << " cellGamma =" << cellGamma;
    // qDebug() << "MRC:  mapc =" << mapc << " mapr =" << mapr << " maps =" << maps;
    // qDebug() << "MRC:  dmin =" << dmin << " dmax =" << dmax << " dmean =" << dmean;
    // qDebug() << "MRC:  ispg =" << ispg << " nsymbt (extended header bytes) =" << nsymbt;

    if (mx > 0 && cellA > 0) {
        result.pixelSize = cellA / mx;
        result.pixelSizeKnown = true;
        // qDebug() << "MRC:  pixel size X =" << result.pixelSize << "Angstrom";
    } else if (nx > 0 && cellA > 0) {
        result.pixelSize = cellA / nx;
        result.pixelSizeKnown = true;
        qDebug() << "MRC:  pixel size X (from cellA/nx) =" << result.pixelSize << "Angstrom";
    } else {
        result.pixelSize = 1.0;
        // qDebug() << "MRC:  pixel size not available in header, assuming 1.0 Angstrom";
    }
    if (my > 0 && cellB > 0)
        // qDebug() << "MRC:  pixel size Y =" << (cellB / my) << "Angstrom";

    // --- sanity checks ----------------------------------------------------------
    if (nx <= 0 || ny <= 0) {
        qDebug() << "MRC: ERROR – invalid dimensions nx=" << nx << " ny=" << ny;
        return result;
    }
    if (nz < 0) nz = 1;            // treat nz<=0 as single slice
    if (nz == 0) nz = 1;

    if (nsymbt < 0 || nsymbt > fileData.size() - 1024) {
        qDebug() << "MRC: ERROR – nsymbt looks invalid:" << nsymbt
                 << "(data size =" << fileData.size() << ")";
        return result;
    }

    if (mode != 0 && mode != 1 && mode != 2 && mode != 6) {
        qDebug() << "MRC: ERROR – unsupported mode:" << mode
                 << "(supported: 0, 1, 2, 6)";
        return result;
    }

    // --- read pixel data --------------------------------------------------------
    qint64 dataOffset = 1024 + nsymbt;
    // qDebug() << "MRC: Data offset =" << dataOffset;

    qint64 pixelCount = (qint64)nx * ny;


    // Guard the allocation against a header claiming more pixels than the file can hold.
    // Every supported mode uses >= 1 byte per pixel, so pixelCount can never legitimately
    // exceed the number of data bytes available; bail before the (potentially huge) resize.
    qint64 available = fileData.size() - dataOffset;
    if (available <= 0 || pixelCount > available) {
        qDebug() << "MRC: ERROR – header dimensions exceed available data (pixels ="
                 << pixelCount << ", data bytes =" << available << ")";
        return result;
    }
    
    result.nx = nx;
    result.ny = ny;
    result.rawPixels.resize(pixelCount);

    QImage img(nx, ny, QImage::Format_Grayscale8);

    if (mode == 0) {
        // 8-bit unsigned integers
        QByteArray data = fileData.mid(dataOffset, pixelCount);
        if (data.size() < pixelCount) {
            qDebug() << "MRC: ERROR – not enough data for mode 0: expected"
                     << pixelCount << "got" << data.size();
            return result;
        }
        const quint8 *src = reinterpret_cast<const quint8 *>(data.constData());
        for (qint64 i = 0; i < pixelCount; i++)
            result.rawPixels[i] = src[i];

        double mn = *std::min_element(result.rawPixels.begin(), result.rawPixels.end());
        double mx = *std::max_element(result.rawPixels.begin(), result.rawPixels.end());
        result.minVal = mn;
        result.maxVal = mx;
        double scale = (mx > mn) ? 255.0 / (mx - mn) : 1.0;

        for (int y = 0; y < ny; y++) {
            uchar *row = img.scanLine(ny - 1 - y);
            for (int x = 0; x < nx; x++)
                row[x] = static_cast<uchar>(std::clamp((result.rawPixels[y * nx + x] - mn) * scale, 0.0, 255.0));
        }
    } else if (mode == 1) {
        // 16-bit signed integers
        QByteArray data = fileData.mid(dataOffset, pixelCount * 2);
        if (data.size() < pixelCount * 2) {
            qDebug() << "MRC: ERROR – not enough data for mode 1: expected"
                     << pixelCount * 2 << "got" << data.size();
            return result;
        }
        const qint16 *src = reinterpret_cast<const qint16 *>(data.constData());
        for (qint64 i = 0; i < pixelCount; i++) {
            qint16 v = src[i];
            if (needSwap) v = qbswap(v);
            result.rawPixels[i] = v;
        }

        double mn = *std::min_element(result.rawPixels.begin(), result.rawPixels.end());
        double mx = *std::max_element(result.rawPixels.begin(), result.rawPixels.end());
        result.minVal = mn;
        result.maxVal = mx;
        double scale = (mx > mn) ? 255.0 / (mx - mn) : 1.0;

        for (int y = 0; y < ny; y++) {
            uchar *row = img.scanLine(ny - 1 - y);
            for (int x = 0; x < nx; x++)
                row[x] = static_cast<uchar>(std::clamp((result.rawPixels[y * nx + x] - mn) * scale, 0.0, 255.0));
        }
    } else if (mode == 2) {
        // 32-bit floats
        QByteArray data = fileData.mid(dataOffset, pixelCount * 4);
        if (data.size() < pixelCount * 4) {
            qDebug() << "MRC: ERROR – not enough data for mode 2: expected"
                     << pixelCount * 4 << "got" << data.size();
            return result;
        }
        const quint32 *raw = reinterpret_cast<const quint32 *>(data.constData());
        for (qint64 i = 0; i < pixelCount; i++) {
            quint32 bits = raw[i];
            if (needSwap) bits = qbswap(bits);
            float v;
            memcpy(&v, &bits, 4);
            result.rawPixels[i] = v;
        }

        double mn = *std::min_element(result.rawPixels.begin(), result.rawPixels.end());
        double mx = *std::max_element(result.rawPixels.begin(), result.rawPixels.end());
        result.minVal = mn;
        result.maxVal = mx;
        double scale = (mx > mn) ? 255.0 / (mx - mn) : 1.0;

        for (int y = 0; y < ny; y++) {
            uchar *row = img.scanLine(ny - 1 - y);
            for (int x = 0; x < nx; x++)
                row[x] = static_cast<uchar>(std::clamp((result.rawPixels[y * nx + x] - mn) * scale, 0.0, 255.0));
        }
    } else if (mode == 6) {
        // 16-bit unsigned integers
        QByteArray data = fileData.mid(dataOffset, pixelCount * 2);
        if (data.size() < pixelCount * 2) {
            qDebug() << "MRC: ERROR – not enough data for mode 6: expected"
                     << pixelCount * 2 << "got" << data.size();
            return result;
        }
        const quint16 *src = reinterpret_cast<const quint16 *>(data.constData());
        for (qint64 i = 0; i < pixelCount; i++) {
            quint16 v = src[i];
            if (needSwap) v = qbswap(v);
            result.rawPixels[i] = v;
        }

        double mn = *std::min_element(result.rawPixels.begin(), result.rawPixels.end());
        double mx = *std::max_element(result.rawPixels.begin(), result.rawPixels.end());
        result.minVal = mn;
        result.maxVal = mx;
        double scale = (mx > mn) ? 255.0 / (mx - mn) : 1.0;

        for (int y = 0; y < ny; y++) {
            uchar *row = img.scanLine(ny - 1 - y);
            for (int x = 0; x < nx; x++)
                row[x] = static_cast<uchar>(std::clamp((result.rawPixels[y * nx + x] - mn) * scale, 0.0, 255.0));
        }
    }

    // MRC stores pixel data bottom-up; the QImage above was built with a
    // vertical flip so it displays in the conventional top-down screen
    // orientation. Flip rawPixels the same way so its index order matches
    // the QImage scanlines (rawPixels[y*nx + x] == scanLine(y)[x]). All
    // downstream code (eraser, brush, rebuildImageFromRaw, ...) assumes
    // this top-down ordering.
    for (int y = 0; y < ny / 2; y++) {
        double *rowA = result.rawPixels.data() + y * nx;
        double *rowB = result.rawPixels.data() + (ny - 1 - y) * nx;
        for (int x = 0; x < nx; x++) std::swap(rowA[x], rowB[x]);
    }

    result.image = img;
    result.valid = true;

    // qDebug() << "MRC: Successfully loaded" << nx << "x" << ny
    //          << "image, mode" << mode
    //          << ", pixel range [" << result.minVal << "," << result.maxVal << "]";

    return result;
}
