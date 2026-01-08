CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Werror -O2 -D_FORTIFY_SOURCE=2
LIBS = -lSDL2 -lSDL2_image
TARGET = photon
SRCDIR = src
SOURCES = $(SRCDIR)/main.c
OBJECTS = $(SOURCES:.c=.o)

# Security hardening flags
SECURITY_FLAGS = -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -pie -Wl,-z,relro,-z,now

# Default target
all: $(TARGET)

# Build the executable with security features
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LIBS) $(SECURITY_FLAGS)

# Compile source files with security flags
%.o: %.c
	$(CC) $(CFLAGS) $(SECURITY_FLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(OBJECTS) $(TARGET)

# Install dependencies (Ubuntu/Debian)
install-deps:
	sudo apt-get update
	sudo apt-get install libsdl2-dev libsdl2-image-dev

# Install dependencies (macOS with Homebrew)
install-deps-mac:
	brew install sdl2 sdl2_image

# Install dependencies (Windows with MSYS2)
install-deps-windows:
	pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image

# Run with a test image (if available)
run: $(TARGET)
	./$(TARGET) test_image.png

# Debug build
debug: CFLAGS += -g -DDEBUG -fsanitize=address -fsanitize=undefined
debug: SECURITY_FLAGS += -fsanitize=address -fsanitize=undefined
debug: $(TARGET)

# Release build
release: CFLAGS += -DNDEBUG -s -O3
release: $(TARGET)

# Security scan (requires clang-static-analyzer)
security-scan:
	clang-static-analyzer --analyze $(SOURCES)

# Format code (requires clang-format)
format:
	clang-format -i $(SOURCES)

# Create project structure
setup:
	mkdir -p $(SRCDIR)

.PHONY: all clean install-deps install-deps-mac install-deps-windows run debug release security-scan format setup
