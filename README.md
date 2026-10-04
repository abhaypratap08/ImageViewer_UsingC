# Photon

> A minimal, fast image viewer written in C. No Electron. No Python. No nonsense.

Built on SDL2 with a focus on speed, simplicity, and staying out of your way.

---

## Install

**Windows** — [`Photon-Windows.zip`](https://github.com/abhaypratap08/ImageViewer_UsingC/releases/latest)

Extract anywhere, double-click `photon.exe`. No installation, no registry, no admin rights.

**Linux** — [`Photon-x86_64.AppImage`](https://github.com/abhaypratap08/ImageViewer_UsingC/releases/latest)

```bash
chmod +x Photon-x86_64.AppImage && ./Photon-x86_64.AppImage
```

---

## Features

- Browses entire folders — open one image, navigate the rest with arrow keys
- Thumbnail strip with a bounded, proximity-based cache and one decode per frame
- Info panel: filename, format, dimensions, aspect ratio, file size, date, zoom, rotation
- Rotate, zoom, pan — no menus, just keys
- Native file open dialog on Windows, Linux (zenity/kdialog), and macOS
- Drag & drop support
- Clipboard copy — image data on Windows, file path on Linux/macOS
- Delete with confirmation
- SDL_ttf text rendering — real fonts, not rectangles
- Immediate press feedback, cancellable toolbar clicks and keyboard focus
- Interruptible spring panels, momentum panning and soft image boundaries
- Floating native materials, system fonts, text scaling and contrast controls
- Compiled with `-fstack-protector`, `-D_FORTIFY_SOURCE=2`, `-fPIE`, `-Wl,-z,relro,-z,now`
- No console window on Windows (`-mwindows`)

---

## Controls

| Input | Action |
|-------|--------|
| `O` | Open file dialog |
| `← →` | Previous / next image |
| `+ -` | Zoom in / out |
| `Scroll` | Zoom around the pointer; scroll details when over the info panel |
| `F` | Fit to window |
| `1` | 100% zoom |
| `R` | Rotate 90° clockwise |
| `Shift+R` | Rotate 90° counter-clockwise |
| `I` | Toggle info panel |
| `T` | Toggle thumbnail strip |
| `Page Up / Down` | Scroll the info panel |
| `F1` | Show controls and accessibility shortcuts |
| `Ctrl+C` | Copy image on Windows, file path on Linux/macOS |
| `Del` | Delete image (with confirmation) |
| `Drag` | Pan |
| `Drop` | Open dropped file |
| `ESC` | Quit |

### Accessibility and motion

Photon keeps drag motion 1:1 with the pointer, carries release velocity into
momentum, and softly resists the image bounds. Panel transitions are
interruptible springs rather than fixed-duration animations. For users who
prefer a calmer or higher-contrast presentation:

| Shortcut / variable | Action |
|---------------------|--------|
| `Ctrl+Shift+M` | Toggle reduced motion (also `PHOTON_REDUCED_MOTION=1`) |
| `Ctrl+Shift+T` | Toggle reduced transparency (also `PHOTON_REDUCED_TRANSPARENCY=1`) |
| `Ctrl+Shift+H` | Toggle high contrast (also `PHOTON_HIGH_CONTRAST=1`) |
| `Tab` / `Shift+Tab`, `Enter`, `Space` | Move toolbar focus and activate controls |

`PHOTON_TEXT_SCALE=1.25` (clamped to `1`–`2`) increases UI text, control sizes,
and spacing. The toolbar wraps on narrow windows and image details scroll.
`PHOTON_FONT` or `--font /path/to/font.ttf` can override the detected system font.

```bash
PHOTON_REDUCED_MOTION=1 PHOTON_TEXT_SCALE=1.25 ./photon image.jpg
```

These are independent, explicit preferences: SDL2 does not expose the web's
`prefers-*` media queries, so Photon does not automatically read OS accessibility
settings. Keyboard toggles last for the current run; use environment variables
for subsequent launches. Reduced motion removes inertia, elasticity and panel
travel, while preserving immediate interaction feedback.

The guidance in `SKILL.md` is adapted to C/SDL2, not a new web frontend. Native
materials use layered tinting, not real backdrop blur. No sounds, haptics or
decorative looping motion are added. Zoom and quarter-turn rotation remain
direct operations; springs govern released drags and reversible panels.
SDL-drawn controls are keyboard-operable but do not expose a native screen-reader
accessibility tree. Image decoding is still synchronous (thumbnail work is
limited to one miss per frame), so large files can pause the interface.

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
| `make test` | Run all headless tests (Linux) |
| `make test-motion` | Run spring, projection, rubber-band, and velocity tests |
| `make test-ui` | Run SDL dummy-renderer UI/cache/accessibility smoke test |
| `make test-app` | Run real event-handler, layout and image-loading regression tests |
| `make run` | Build and run with `test_image.png` |
| `make install-deps` | Install SDL2 deps (Ubuntu/Debian) |
| `make install-deps-mac` | Install SDL2 deps (Homebrew) |
| `make install-deps-windows` | Install SDL2 deps (MSYS2) |
| `make format` | Run `clang-format` on source |

The UI tests require a system font (DejaVu Sans on Linux, or set
`PHOTON_TEST_FONT` for the standalone UI test). They create and remove their own
image fixtures under `tests/`, use SDL's dummy video driver, and never open file
dialogs, modify desktop integration, copy to the clipboard or delete user images.

```bash
make clean && make
make test
make test TEST_CFLAGS='-std=c99 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined'
```

Headless tests do not replace hands-on checks of GPU rendering, pointer capture
outside the window, native dialogs, or Windows/macOS behavior.

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
│   ├── main.c          # viewer, layout and SDL event integration
│   ├── motion.c/.h     # springs, momentum projection and velocity sampling
│   ├── ui.c/.h         # materials, font roles and bounded text texture cache
│   └── photon.rc       # Windows resource file (icon + version metadata)
├── tests/              # motion, software-rendered UI and event-flow regressions
├── assets/
│   └── icon.ico        # application icon
├── installer/
│   └── windows/
│       └── photon.nsi  # NSIS installer script
├── Makefile
├── .github/
│   └── workflows/
│       └── build.yml   # CI: builds Linux AppImage + Windows EXE on every push
├── LICENSE
└── README.md
```

---

## Stack

- **Language:** C99
- **Graphics:** SDL2, SDL2\_image, SDL2\_ttf
- **Build:** GCC + Make
- **CI:** GitHub Actions — builds and publishes releases automatically
- **Packaging:** AppImage (Linux), portable zip (Windows)

---

## License

MIT — do whatever you want with it.
