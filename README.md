# Photon — Image Viewer

A lightweight, fast image viewer built in C with SDL2. Supports drag & drop, folder navigation, thumbnail strip, image info panel, rotation, clipboard copy, and more.

## Downloads

| Platform | File | Notes |
|----------|------|-------|
| Windows (Installer) | [`Photon-Setup.exe`](https://github.com/abhaypratap08/ImageViewer_UsingC/releases/latest) | Installs to Program Files, adds file associations & shortcuts |
| Windows (Portable)  | [`Photon-Windows.zip`](https://github.com/abhaypratap08/ImageViewer_UsingC/releases/latest) | Extract and run `photon.exe` — no install needed |
| Linux               | [`Photon-x86_64.AppImage`](https://github.com/abhaypratap08/ImageViewer_UsingC/releases/latest) | Runs on any x86_64 distro — no install needed |

**Linux quick start:**
```bash
chmod +x Photon-x86_64.AppImage && ./Photon-x86_64.AppImage
```

---

## Controls

| Key / Input       | Action                              |
|-------------------|-------------------------------------|
| `O`               | Open file dialog                    |
| `←` / `→`         | Previous / next image in folder     |
| `+` / `-`         | Zoom in / out                       |
| `Scroll wheel`    | Zoom                                |
| `F`               | Fit to window                       |
| `1`               | Actual size (100%)                  |
| `R`               | Rotate clockwise 90°                |
| `Shift+R`         | Rotate counter-clockwise 90°        |
| `I`               | Toggle info panel                   |
| `T`               | Toggle thumbnail strip              |
| `Ctrl+C`          | Copy image to clipboard             |
| `Del`             | Delete current image (with confirm) |
| `Mouse drag`      | Pan image                           |
| `Drag & drop`     | Open dropped image file             |
| `ESC`             | Exit                                |

---

## Build from Source

### Clone the Repository
```bash
git clone https://github.com/abhaypratap08/ImageViewer_UsingC.git
cd ImageViewer_UsingC
```

### Windows (MSYS2 / MinGW64)
1. Download and install MSYS2 from https://www.msys2.org/, then open the **MSYS2 MINGW64** terminal.
2. Install dependencies:
```bash
# pacman is MSYS2's package manager — run this inside the MSYS2 MINGW64 terminal
pacman -Syu
make install-deps-windows
```
3. Build:
```bash
make
```

### Linux

#### Arch Linux / Manjaro
```bash
sudo pacman -S gcc sdl2 sdl2_image sdl2_ttf make
```

#### Ubuntu / Debian / Linux Mint
```bash
make install-deps
# or manually:
sudo apt install build-essential libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev make
```

#### Fedora / RHEL / CentOS
```bash
sudo dnf install gcc SDL2-devel SDL2_image-devel SDL2_ttf-devel make
```

#### openSUSE
```bash
sudo zypper install gcc libSDL2-devel libSDL2_image-devel libSDL2_ttf-devel make
```

Then build:
```bash
make
```

### macOS (Homebrew)
```bash
make install-deps-mac
# or manually:
brew install sdl2 sdl2_image sdl2_ttf
```
Then build:
```bash
make
```

---

## Usage
```bash
./photon image.jpg        # Linux / macOS — open a specific image
./photon.exe image.jpg    # Windows

./photon                  # Launch with file dialog
```

---

## Build Targets

| Target                      | Description                                   |
|-----------------------------|-----------------------------------------------|
| `make`                      | Default build (optimized, security-hardened)  |
| `make debug`                | Debug build with AddressSanitizer + UBSan     |
| `make release`              | Stripped, maximum-optimization release build  |
| `make clean`                | Remove build artifacts                        |
| `make run`                  | Build and run with `test_image.png`           |
| `make install-deps`         | Install dependencies (Ubuntu/Debian)          |
| `make install-deps-mac`     | Install dependencies (macOS/Homebrew)         |
| `make install-deps-windows` | Install dependencies (MSYS2/Windows)          |
| `make format`               | Format source code (requires `clang-format`)  |

---

## Troubleshooting

**`gcc` not found** — Install GCC via your package manager (see Build from Source above).

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
├── src/main.c                    # Source code
├── Makefile                      # Build configuration
├── installer/
│   ├── windows/photon.nsi        # NSIS Windows installer script
│   └── linux/
│       ├── AppRun                # AppImage entry point
│       └── photon.desktop        # Linux desktop entry
├── .github/workflows/build.yml   # CI — builds on every push & publishes releases
├── LICENSE                       # MIT License
└── README.md                     # This file
```

---

## License

MIT License
