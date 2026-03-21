CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -Werror -O2 -D_FORTIFY_SOURCE=2
SRCDIR  = src
SOURCES = $(SRCDIR)/main.c
OBJECTS = $(SOURCES:.c=.o)

# ── OS detection ──────────────────────────────────────────────────────────────
ifeq ($(OS),Windows_NT)
    TARGET         = photon.exe
    LIBS           = -lSDL2 -lSDL2_image -lSDL2_ttf -lcomdlg32 -lgdi32
    SECURITY_FLAGS = -fstack-protector-strong -D_FORTIFY_SOURCE=2 -mwindows
    RC_OBJ         = src/photon_res.o
else
    TARGET         = photon
    LIBS           = -lSDL2 -lSDL2_image -lSDL2_ttf
    SECURITY_FLAGS = -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -pie -Wl,-z,relro,-z,now
    RC_OBJ         =
endif

# ── Targets ───────────────────────────────────────────────────────────────────
all: $(TARGET)

$(TARGET): $(OBJECTS) $(RC_OBJ)
	$(CC) $(OBJECTS) $(RC_OBJ) -o $(TARGET) $(LIBS) $(SECURITY_FLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(SECURITY_FLAGS) -c $< -o $@

# Compile Windows resource file (icon)
src/photon_res.o: src/photon.rc assets/icon.ico
	windres src/photon.rc -O coff -o src/photon_res.o

clean:
	rm -f $(OBJECTS) $(RC_OBJ) $(TARGET)

# ── Dependency installation ───────────────────────────────────────────────────
install-deps:
	sudo apt-get update
	sudo apt-get install libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev

install-deps-mac:
	brew install sdl2 sdl2_image sdl2_ttf

install-deps-windows:
	pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image mingw-w64-x86_64-SDL2_ttf

# ── Dev targets ───────────────────────────────────────────────────────────────
run: $(TARGET)
	./$(TARGET) test_image.png

debug: CFLAGS         += -g -DDEBUG -fsanitize=address -fsanitize=undefined
debug: SECURITY_FLAGS += -fsanitize=address -fsanitize=undefined
debug: $(TARGET)

release: CFLAGS += -DNDEBUG -s -O3
release: $(TARGET)

format:
	clang-format -i $(SOURCES)

setup:
	mkdir -p $(SRCDIR)

.PHONY: all clean install-deps install-deps-mac install-deps-windows \
        run debug release format setup
