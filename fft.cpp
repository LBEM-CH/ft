#include "fft.h"
#include <algorithm>
#include <cmath>
#ifndef __EMSCRIPTEN__
#include <thread>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int nextPow2(int n) {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

// Find the smallest integer >= n whose prime factors are only 2, 3, and 5.
int nextGoodFFTSize(int n) {
    if (n <= 1) return 1;
    int best = nextPow2(n);  // fallback: always valid
    for (int p5 = 1; p5 <= best; p5 *= 5) {
        for (int p3 = p5; p3 <= best; p3 *= 3) {
            int p2 = p3;
            while (p2 < n) p2 *= 2;
            if (p2 < best) best = p2;
        }
    }
    return best;
}

// Radix-2 FFT for power-of-2 sizes (known working implementation)
static void fft1d_pow2(std::vector<Complex> &data, bool inverse) {
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

// Bluestein's algorithm: compute FFT of any size N by reducing to
// a power-of-2 convolution.
static void fft1d_bluestein(std::vector<Complex> &data, bool inverse) {
    int N = (int)data.size();
    if (N <= 1) return;

    // Chirp: w[k] = exp(-i * pi * k^2 / N)  (forward)
    //        w[k] = exp(+i * pi * k^2 / N)  (inverse)
    double sign = inverse ? 1.0 : -1.0;
    std::vector<Complex> chirp(N);
    for (int k = 0; k < N; k++) {
        double angle = sign * M_PI * ((long long)k * k % (2LL * N)) / N;
        chirp[k] = Complex(cos(angle), sin(angle));
    }

    // Convolution size: power of 2 >= 2N - 1
    int M = nextPow2(2 * N - 1);

    // a[k] = data[k] * chirp[k], zero-padded to M
    std::vector<Complex> a(M, Complex(0, 0));
    for (int k = 0; k < N; k++)
        a[k] = data[k] * chirp[k];

    // b[k] = conj(chirp[k]) with wrap-around, zero-padded to M
    std::vector<Complex> b(M, Complex(0, 0));
    b[0] = std::conj(chirp[0]);
    for (int k = 1; k < N; k++) {
        b[k]     = std::conj(chirp[k]);
        b[M - k] = std::conj(chirp[k]);
    }

    // Convolution via FFT: result = IFFT(FFT(a) * FFT(b))
    fft1d_pow2(a, false);
    fft1d_pow2(b, false);
    for (int i = 0; i < M; i++)
        a[i] *= b[i];
    fft1d_pow2(a, true);

    // Extract result: data[k] = a[k] * chirp[k]
    for (int k = 0; k < N; k++)
        data[k] = a[k] * chirp[k];

    if (inverse) {
        for (auto &x : data)
            x /= N;
    }
}

// Public FFT: dispatches to radix-2 or Bluestein
void fft1d(std::vector<Complex> &data, bool inverse) {
    int n = (int)data.size();
    if (n <= 1) return;
    // Check if n is a power of 2
    if ((n & (n - 1)) == 0)
        fft1d_pow2(data, inverse);
    else
        fft1d_bluestein(data, inverse);
}

void fft2d(std::vector<Complex> &data, int N, bool inverse) {
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

#ifdef __EMSCRIPTEN__
    // Single-threaded for WASM
    doRows(0, N);
    doCols(0, N);
#else
    int nThreads = (int)std::thread::hardware_concurrency();
    if (nThreads < 1) nThreads = 1;

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
#endif
}

void fftShift(std::vector<Complex> &data, int N) {
    int half = N / 2;
    std::vector<Complex> tmp(data.size());
    for (int y = 0; y < N; y++) {
        int ny = (y + half) % N;
        for (int x = 0; x < N; x++) {
            int nx = (x + half) % N;
            tmp[ny * N + nx] = data[y * N + x];
        }
    }
    data = std::move(tmp);
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
