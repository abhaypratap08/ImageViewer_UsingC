# Photon - Image Viewer (V1.0)

A image viewer built with C and SDL2.

## Windows Setup Guide

### 1. Install MSYS2

1. Download MSYS2 from https://www.msys2.org/
2. Run installer and follow setup
3. Open MSYS2 MINGW64 terminal

### 2. Install Development Tools

```bash
# Update package database
pacman -Syu

# Install GCC compiler
pacman -S mingw-w64-x86_64-gcc

# Install SDL2 libraries
pacman -S mingw-w64-x86_64-SDL2
pacman -S mingw-w64-x86_64-SDL2_image

# Install build tools
pacman -S make
```

### 3. Clone Repository

```bash
cd /c/Users/prata/OneDrive/Documents/
git clone https://github.com/yourusername/photon.git
cd photon
```

### 4. Build Application

```bash
# Compile the application
make

# Check if executable was created
ls -la photon.exe
```

### 5. Run Application

```bash
# View an image
./photon.exe image.jpg

# Run without image
./photon.exe
```

## Windows Controls

- `ESC` - Exit application
- `+/-` - Zoom in/out
- `F` - Fit to window
- `1` - Actual size
- `I` - Toggle info overlay
- `Mouse wheel` - Zoom

## Windows Troubleshooting

### Common Issues

**"gcc not found"**
```bash
# Check GCC installation
gcc --version

# Reinstall GCC if needed
pacman -S mingw-w64-x86_64-gcc
```

**"SDL2 not found"**
```bash
# Check SDL2 installation
pacman -Qs mingw-w64-x86_64-SDL2

# Reinstall SDL2 if needed
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image
```

**Build fails**
```bash
# Clean and rebuild
make clean
make

# Build with debug info
make debug
```

**"photon.exe not found"**
```bash
# Check current directory
ls -la

# Rebuild if missing
make
```

## Windows Project Structure

```
photon/
├── src/main.c    # Source code
├── Makefile       # Build configuration
├── LICENSE        # MIT License
└── README.md      # This file
```

## License

MIT License
