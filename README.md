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

### Windows

1. **Install Qt 6** using the [Qt online installer](https://www.qt.io/download-qt-installer). During installation, select the **Qt 6.x** component matching your compiler (MSVC or MinGW). Note the installation path (e.g. `C:\Qt\6.8.3\msvc2022_64`).

2. **Install CMake** from [cmake.org](https://cmake.org/download/) or via `winget install Kitware.CMake`. Make sure CMake is on your PATH.

3. **Install a C++17 compiler** — either:
   - **MSVC**: Install [Visual Studio 2019 or later](https://visualstudio.microsoft.com/) with the "Desktop development with C++" workload, or
   - **MinGW**: Use the MinGW build shipped with Qt (selected during Qt installation).

## Building

### macOS / Linux

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

The executable `ft` will be created in the `build/` directory.

### Windows with MSVC

Open a **Developer Command Prompt for Visual Studio** (or "x64 Native Tools Command Prompt") and run:

```cmd
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build . --config Release
```

Replace the `CMAKE_PREFIX_PATH` with the actual path to your Qt installation. The executable `ft.exe` will be created in `build\Release\`.

### Windows with MinGW

Open a terminal and make sure the MinGW `bin` directory shipped with Qt is on your PATH:

```cmd
set PATH=C:\Qt\Tools\mingw1310_64\bin;%PATH%
mkdir build
cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\mingw_64
cmake --build .
```

The executable `ft.exe` will be created in the `build\` directory.

## Running

On macOS or Linux:

```bash
./build/ft
```

On Windows:

```cmd
build\Release\ft.exe
```

If `ft.exe` cannot find Qt DLLs at runtime, either add the Qt `bin` directory to your PATH or copy the required DLLs next to `ft.exe`. Qt provides the `windeployqt` tool to automate this:

```cmd
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe build\Release\ft.exe
```

Use the **Load image** button to open an image file, then click the **FT** arrow to compute the Fourier transform.

## License

See [LICENSE](LICENSE) for details.
