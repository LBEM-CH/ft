#include "fft.h"
#include <algorithm>
#include <cmath>
#include <thread>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int nextPow2(int n) {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

void fft1d(std::vector<Complex> &data, bool inverse) {
    int n = (int)data.size();
    if (n <= 1) return;

    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }

    for (int len = 2; len <= n; len <<= 1) {
        double angle = 2.0 * M_PI / len * (inverse ? -1.0 : 1.0);
        Complex wn(cos(angle), sin(angle));
        for (int i = 0; i < n; i += len) {
            Complex w(1.0);
            for (int j = 0; j < len / 2; j++) {
                Complex u = data[i + j];
                Complex v = data[i + j + len / 2] * w;
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w *= wn;
            }
        }
    }

    if (inverse) {
        for (auto &x : data)
            x /= n;
    }
}

void fft2d(std::vector<Complex> &data, int N, bool inverse) {
    int nThreads = (int)std::thread::hardware_concurrency();
    if (nThreads < 1) nThreads = 1;

    // Row-wise FFT (each thread gets its own temp buffer)
    auto doRows = [&](int yStart, int yEnd) {
        std::vector<Complex> row(N);
        for (int y = yStart; y < yEnd; y++) {
            for (int x = 0; x < N; x++)
                row[x] = data[y * N + x];
            fft1d(row, inverse);
            for (int x = 0; x < N; x++)
                data[y * N + x] = row[x];
        }
    };

    {
        std::vector<std::thread> threads;
        int perThread = (N + nThreads - 1) / nThreads;
        for (int t = 0; t < nThreads; t++) {
            int y0 = t * perThread;
            int y1 = std::min(y0 + perThread, N);
            if (y0 < y1)
                threads.emplace_back(doRows, y0, y1);
        }
        for (auto &t : threads) t.join();
    }

    // Column-wise FFT
    auto doCols = [&](int xStart, int xEnd) {
        std::vector<Complex> col(N);
        for (int x = xStart; x < xEnd; x++) {
            for (int y = 0; y < N; y++)
                col[y] = data[y * N + x];
            fft1d(col, inverse);
            for (int y = 0; y < N; y++)
                data[y * N + x] = col[y];
        }
    };

    {
        std::vector<std::thread> threads;
        int perThread = (N + nThreads - 1) / nThreads;
        for (int t = 0; t < nThreads; t++) {
            int x0 = t * perThread;
            int x1 = std::min(x0 + perThread, N);
            if (x0 < x1)
                threads.emplace_back(doCols, x0, x1);
        }
        for (auto &t : threads) t.join();
    }
}

void fftShift(std::vector<Complex> &data, int N) {
    int half = N / 2;
    for (int y = 0; y < half; y++) {
        for (int x = 0; x < half; x++) {
            std::swap(data[y * N + x], data[(y + half) * N + (x + half)]);
            std::swap(data[y * N + (x + half)], data[(y + half) * N + x]);
        }
    }
}

QImage floatToImage(const std::vector<double> &vals, int N) {
    double mn = *std::min_element(vals.begin(), vals.end());
    double mx = *std::max_element(vals.begin(), vals.end());
    double scale = (mx > mn) ? 255.0 / (mx - mn) : 1.0;
    QImage img(N, N, QImage::Format_Grayscale8);
    for (int y = 0; y < N; y++) {
        uchar *row = img.scanLine(y);
        for (int x = 0; x < N; x++)
            row[x] = static_cast<uchar>(std::clamp((vals[y * N + x] - mn) * scale, 0.0, 255.0));
    }
    return img;
}
