#ifndef FFT_H
#define FFT_H

#include <vector>
#include <complex>
#include <QImage>

using Complex = std::complex<double>;

int nextPow2(int n);
int nextGoodFFTSize(int n);
void fft1d(std::vector<Complex> &data, bool inverse);
void fft2d(std::vector<Complex> &data, int N, bool inverse);
void fftShift(std::vector<Complex> &data, int N);
QImage floatToImage(const std::vector<double> &vals, int N);

#endif // FFT_H
