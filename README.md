# Photon - Image Viewer (V1.0)
A lightweight image viewer built with C and SDL2.

## Controls
| Key / Input     | Action           |
|-----------------|------------------|
| `ESC`           | Exit application |
| `+` / `-`       | Zoom in / out    |
| `F`             | Fit to window    |
| `1`             | Actual size      |
| `I`             | Toggle info overlay |
| `Mouse wheel`   | Zoom             |

---

## Installation

### Windows (MSYS2 / MinGW64)

#### 1. Install MSYS2
Download and install MSYS2 from https://www.msys2.org/, then open the **MSYS2 MINGW64** terminal.

#### 2. Install Dependencies
```bash
pacman -Syu
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image make
```

#### 3. Build & Run
```bash
git clone https://github.com/yourusername/photon.git && cd photon
make
./photon.exe image.jpg
```

---

### Linux

#### Arch Linux / Manjaro
```bash
sudo pacman -Syu
sudo pacman -S gcc sdl2 sdl2_image make
```

#### Ubuntu / Debian / Linux Mint
```bash
sudo apt update && sudo apt upgrade
sudo apt install build-essential libsdl2-dev libsdl2-image-dev make
```

#### Fedora / RHEL / CentOS
```bash
sudo dnf update
sudo dnf install gcc SDL2-devel SDL2_image-devel make
```

#### openSUSE
```bash
sudo zypper update
sudo zypper install gcc libSDL2-devel libSDL2_image-devel make
```

#### Build & Run (all Linux distros)
```bash
git clone https://github.com/yourusername/photon.git && cd photon
make
./photon image.jpg
```

---

### macOS (Homebrew)

#### 1. Install Homebrew (if not already installed)
```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

#### 2. Install Dependencies
```bash
brew update
brew install sdl2 sdl2_image
```

#### 3. Build & Run
```bash
git clone https://github.com/yourusername/photon.git && cd photon
make
./photon image.jpg
```

---

## Usage
```bash
# Open an image
./photon image.jpg        # Linux / macOS
./photon.exe image.jpg    # Windows

# Launch without an image
./photon
```

---

## Troubleshooting

**`gcc` not found**
Install GCC via your package manager (see Installation section for your distro).

**`SDL2` not found**
Install SDL2 and SDL2_image via your package manager (see Installation section for your distro).

**Build fails**
```bash
make clean
make
# Or build with debug info
make debug
```

**Executable not found after build**
```bash
ls -la          # Linux / macOS
ls -la *.exe    # Windows
make            # Rebuild if missing
```

---

## Project Structure
```
photon/
├── src/main.c    # Source code
├── Makefile      # Build configuration
├── LICENSE       # MIT License
└── README.md     # This file
```

---

## License
MIT License
