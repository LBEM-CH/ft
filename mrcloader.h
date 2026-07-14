#ifndef MRCLOADER_H
#define MRCLOADER_H

#include <QString>
#include <QImage>
#include <vector>

struct MrcResult {
    QImage image;
    std::vector<double> rawPixels;  // original pixel values (not scaled to 0-255)
    double minVal = 0;
    double maxVal = 0;
    double pixelSize = 1.0;         // in Angstrom (default 1.0 if not in header)
    int nx = 0;
    int ny = 0;
    bool valid = false;
};

MrcResult loadMrc(const QString &path);
MrcResult loadMrcFromData(const QByteArray &fileData);

// Read only the 1024-byte MRC header to obtain the image dimensions, without
// reading the (potentially huge) pixel data. Returns false if the file cannot
// be opened or does not carry a usable header.
bool readMrcDimensions(const QString &path, int &nx, int &ny);

#endif // MRCLOADER_H
