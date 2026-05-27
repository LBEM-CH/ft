#ifndef FFT_H
#define FFT_H

#include <vector>
#include <complex>
#include <QImage>

using Complex = std::complex<double>;

// Threading is available natively, and in a WebAssembly build only when it is
// compiled with -pthread (Emscripten then defines __EMSCRIPTEN_PTHREADS__).
// Gate every std::thread use on this single macro so one source tree serves the
// single-threaded and multi-threaded WASM builds alike.
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
#define FT_HAVE_THREADS 1
#else
#define FT_HAVE_THREADS 0
#endif

int nextPow2(int n);
int nextGoodFFTSize(int n);
void fft1d(std::vector<Complex> &data, bool inverse);
void fft2d(std::vector<Complex> &data, int N, bool inverse);
void fftShift(std::vector<Complex> &data, int N);
QImage floatToImage(const std::vector<double> &vals, int N);

#endif // FFT_H
