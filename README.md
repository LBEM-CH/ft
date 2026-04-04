# ft — Fourier space exploration

A Qt6-based interactive viewer for images and their 2D Fourier transforms.
Supports MRC, TIFF, JPEG and PNG image formats.

## Features

- Load and display images with pixel-coordinate axes
- Compute and display the 2D FFT with animated progress
- Four display modes: cosine/sine, amplitude/phase, power spectrum, complex (phase-colored) Fourier transform
- Axis labels in both pixel and physical units (Angstrom / reciprocal Angstrom)
- Mouse-wheel zoom on any displayed image, with axes that update to the visible region
- Grey-value histograms below each displayed image
- Live pixel-value readout under the mouse cursor
- MRC file support with automatic byte-order detection and header debug output
- Optional center-masking of the Fourier transform for display

## Requirements

- **C++17** compiler (GCC >= 8, Clang >= 7, or MSVC >= 2017)
- **CMake** >= 3.16
- **Qt 6** (Widgets module)

## Installing dependencies

### macOS

Install Qt 6 and CMake via [Homebrew](https://brew.sh):

```bash
brew install qt@6 cmake
```

If CMake cannot find Qt, point it to the Homebrew prefix:

```bash
export CMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
```

### Ubuntu (22.04 / 24.04)

```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev
```

On Ubuntu 22.04, if `qt6-base-dev` is not available in the default repositories, add the official Qt PPA or install from the [Qt online installer](https://www.qt.io/download-qt-installer).

## Building

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

The executable `ft` will be created in the `build/` directory.

## Running

```bash
./build/ft
```

Use the **Load image** button to open an image file, then click the **FT** arrow to compute the Fourier transform.

## License

See [LICENSE](LICENSE) for details.
