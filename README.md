# Photon

> A minimal, fast image viewer written in C. No Electron. No Python. No nonsense.

Built on SDL2 with a focus on speed, simplicity, and staying out of your way.

---

## Install

| Platform | File | |
|----------|------|-|
| Windows (Installer) | [`Photon-Setup.exe`](https://github.com/abhaypratap08/ImageViewer_UsingC/releases/latest) | Registers file associations, adds Start Menu & Desktop shortcuts |
| Windows (Portable)  | [`Photon-Windows.zip`](https://github.com/abhaypratap08/ImageViewer_UsingC/releases/latest) | Extract anywhere, run `photon.exe` — zero installation |
| Linux (AppImage)    | [`Photon-x86_64.AppImage`](https://github.com/abhaypratap08/ImageViewer_UsingC/releases/latest) | Single binary, runs on any x86\_64 distro |

```bash
# Linux
chmod +x Photon-x86_64.AppImage && ./Photon-x86_64.AppImage
```

---

## Features

- Browses entire folders — open one image, navigate the rest with arrow keys
- Thumbnail strip with lazy loading and LRU cache
- Info panel: filename, format, dimensions, aspect ratio, file size, date, zoom, rotation
- Rotate, zoom, pan — no menus, just keys
- Native file open dialog on Windows, Linux (zenity/kdialog), and macOS
- Drag & drop support
- Clipboard copy — pastes as actual image data on Windows
- Delete with confirmation
- SDL_ttf text rendering — real fonts, not rectangles
- Compiled with `-fstack-protector`, `-D_FORTIFY_SOURCE=2`, `-fPIE`, `-Wl,-z,relro,-z,now`
- No console window on Windows (`-mwindows`)

---

## Controls

| Input | Action |
|-------|--------|
| `O` | Open file dialog |
| `← →` | Previous / next image |
| `+ -` | Zoom in / out |
| `Scroll` | Zoom |
| `F` | Fit to window |
| `1` | 100% zoom |
| `R` | Rotate 90° clockwise |
| `Shift+R` | Rotate 90° counter-clockwise |
| `I` | Toggle info panel |
| `T` | Toggle thumbnail strip |
| `Ctrl+C` | Copy to clipboard |
| `Del` | Delete image (with confirmation) |
| `Drag` | Pan |
| `Drop` | Open dropped file |
| `ESC` | Quit |

---

## Build from Source

```bash
git clone https://github.com/abhaypratap08/ImageViewer_UsingC.git
cd ImageViewer_UsingC
```

### Dependencies

**Ubuntu / Debian**
```bash
make install-deps
# or: sudo apt install build-essential libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev make
```

**Arch / Manjaro**
```bash
sudo pacman -S gcc sdl2 sdl2_image sdl2_ttf make
```

**Fedora**
```bash
sudo dnf install gcc SDL2-devel SDL2_image-devel SDL2_ttf-devel make
```

**openSUSE**
```bash
sudo zypper install gcc libSDL2-devel libSDL2_image-devel libSDL2_ttf-devel make
```

**macOS**
```bash
make install-deps-mac
# or: brew install sdl2 sdl2_image sdl2_ttf
```

**Windows (MSYS2 MINGW64)**
```bash
pacman -Syu
make install-deps-windows
```

### Compile

```bash
make          # optimized build
make debug    # AddressSanitizer + UBSan
make release  # stripped, -O3
make clean    # remove artifacts
```

### Run

```bash
./photon image.jpg       # Linux / macOS
./photon.exe image.jpg   # Windows
./photon                 # opens file dialog
```

---

## Build Targets

| Target | Description |
|--------|-------------|
| `make` | Default — optimized, security-hardened |
| `make debug` | Debug build with ASan + UBSan |
| `make release` | Stripped, `-O3` release build |
| `make clean` | Remove object files and binary |
| `make run` | Build and run with `test_image.png` |
| `make install-deps` | Install SDL2 deps (Ubuntu/Debian) |
| `make install-deps-mac` | Install SDL2 deps (Homebrew) |
| `make install-deps-windows` | Install SDL2 deps (MSYS2) |
| `make format` | Run `clang-format` on source |

---

## Troubleshooting

**`gcc` not found** — install GCC via your package manager.

**`SDL2` not found** — run the appropriate `make install-deps*` for your platform.

**Build fails**
```bash
make clean && make
make debug  # sanitizers give better error output
```

**Binary not found after build**
```bash
ls -la photon       # Linux / macOS
ls -la photon.exe   # Windows
make                # rebuild
```

---

## Project Structure

```
ImageViewer_UsingC/
├── src/
│   ├── main.c          # everything
│   ├── photon.rc       # Windows resource file (icon + version metadata)
├── assets/
│   └── icon.ico        # application icon
├── installer/
│   └── windows/
│       └── photon.nsi  # NSIS installer script
├── Makefile
├── .github/
│   └── workflows/
│       └── build.yml   # CI: builds Linux AppImage + Windows EXE/installer on every push
├── LICENSE
└── README.md
```

---

## Stack

- **Language:** C99
- **Graphics:** SDL2, SDL2\_image, SDL2\_ttf
- **Build:** GCC + Make
- **CI:** GitHub Actions — builds and publishes releases automatically
- **Installer:** NSIS (Windows)
- **Packaging:** AppImage (Linux)

---

## License

MIT — do whatever you want with it.
