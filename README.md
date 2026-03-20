# Photon - Image Viewer
A lightweight image viewer built with C and SDL2.

## Controls
| Key / Input   | Action              |
|---------------|---------------------|
| `ESC`         | Exit application    |
| `+` / `-`     | Zoom in / out       |
| `F`           | Fit to window       |
| `1`           | Actual size         |
| `I`           | Toggle info overlay |
| `Mouse wheel` | Zoom                |

---

## Installation

### Clone the Repository
```bash
git clone https://github.com/abhaypratap08/ImageViewer_UsingC.git
cd ImageViewer_UsingC
```

---

### Windows (MSYS2 / MinGW64)

1. Download and install MSYS2 from https://www.msys2.org/, then open the **MSYS2 MINGW64** terminal.
2. Install dependencies:
```bash
pacman -Syu
make install-deps-windows
```
3. Build:
```bash
make
```

---

### Linux

#### Arch Linux / Manjaro
```bash
sudo pacman -S gcc sdl2 sdl2_image make
```

#### Ubuntu / Debian / Linux Mint
```bash
make install-deps
# or manually:
sudo apt install build-essential libsdl2-dev libsdl2-image-dev make
```

#### Fedora / RHEL / CentOS
```bash
sudo dnf install gcc SDL2-devel SDL2_image-devel make
```

#### openSUSE
```bash
sudo zypper install gcc libSDL2-devel libSDL2_image-devel make
```

Then build:
```bash
make
```

---

### macOS (Homebrew)

```bash
make install-deps-mac
# or manually:
brew install sdl2 sdl2_image
```

Then build:
```bash
make
```

---

## Usage
```bash
./photon image.jpg        # Linux / macOS
./photon.exe image.jpg    # Windows

./photon                  # Launch without an image
```

---

## Build Targets

| Target                      | Description                                       |
|-----------------------------|---------------------------------------------------|
| `make`                      | Default build (optimized, security-hardened)      |
| `make debug`                | Debug build with AddressSanitizer + UBSan         |
| `make release`              | Stripped, maximum-optimization release build      |
| `make clean`                | Remove build artifacts                            |
| `make run`                  | Build and run with `test_image.png`               |
| `make install-deps`         | Install SDL2 dependencies (Ubuntu/Debian)         |
| `make install-deps-mac`     | Install SDL2 dependencies (macOS/Homebrew)        |
| `make install-deps-windows` | Install SDL2 dependencies (MSYS2/Windows)         |
| `make format`               | Format source code (requires `clang-format`)      |
| `make security-scan`        | Static analysis (requires `clang-static-analyzer`)|

---

## Troubleshooting

**`gcc` not found** — Install GCC via your package manager (see Installation above).

**`SDL2` not found** — Run the appropriate `make install-deps*` target for your platform.

**Build fails**
```bash
make clean && make
make debug    # build with sanitizers for more diagnostic info
```

**Executable not found after build**
```bash
ls -la photon      # Linux / macOS
ls -la photon.exe  # Windows
make               # rebuild if missing
```

---

## Project Structure
```
ImageViewer_UsingC/
├── src/main.c           # Source code
├── Makefile             # Build configuration
├── .github/workflows/   # CI workflows
├── LICENSE              # MIT License
└── README.md            # This file
```

---

## License
MIT License
