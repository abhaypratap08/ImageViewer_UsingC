/* Enable POSIX extensions: strdup, popen, pclose */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <ctype.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#define strcasecmp  _stricmp
#define PATH_SEP    '\\'
/* Hide the console window — GUI subsystem */
#pragma comment(linker, "/subsystem:windows")
#else
#include <dirent.h>
#define PATH_SEP    '/'
#endif

#define WINDOW_WIDTH    800
#define WINDOW_HEIGHT   600
#define WINDOW_TITLE    "Photon"
#define MAX_PATH_LENGTH 4096
#define MAX_FILE_SIZE   (100 * 1024 * 1024)
#define MAX_IMAGES      4096

#define THUMB_W         90
#define THUMB_H         68
#define THUMB_PAD       5
#define THUMB_SLOT_W    (THUMB_W + THUMB_PAD)
#define THUMB_STRIP_H   (THUMB_H + THUMB_PAD * 2 + 2)
#define THUMB_CACHE_MAX 32
#define THUMB_SCALE_MAX 128

#define INFO_W          300
#define INFO_LINE_H     22
#define INFO_PAD        12
#define INFO_LINES      9

#ifdef _WIN32
#undef main
#endif

// ── Types ─────────────────────────────────────────────────────────────────────
typedef enum {
    SECURITY_OK,
    SECURITY_ERROR_INVALID_INPUT,
    SECURITY_ERROR_PATH_TOO_LONG,
    SECURITY_ERROR_FILE_TOO_LARGE,
    SECURITY_ERROR_ACCESS_DENIED,
    SECURITY_ERROR_MEMORY_ALLOCATION
} SecurityResult;

typedef struct {
    char         path[MAX_PATH_LENGTH];
    SDL_Texture *tex;
    int          w, h;
} Thumb;

typedef struct {
    char **paths;
    int    count;
    int    current;
} FileList;

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *image_texture;
    TTF_Font     *font_regular;
    TTF_Font     *font_bold;
    int   window_width, window_height;
    int   image_width,  image_height;
    int   running;
    float zoom;
    int   pan_x,  pan_y;
    int   fit_to_window;
    int   show_info;
    int   show_thumbnails;
    int   rotation;
    int   is_panning;
    int   drag_start_x, drag_start_y;
    int   pan_start_x,  pan_start_y;
    FileList file_list;
    char  current_path[MAX_PATH_LENGTH];
    Thumb thumb_cache[THUMB_CACHE_MAX];
    long  current_file_size;
    time_t current_mod_time;
} App;

// ── Security helpers ──────────────────────────────────────────────────────────
SecurityResult validate_filepath(const char *fp) {
    if (!fp) return SECURITY_ERROR_INVALID_INPUT;
    size_t len = strlen(fp);
    if (len == 0 || len >= MAX_PATH_LENGTH) return SECURITY_ERROR_PATH_TOO_LONG;
    if (strstr(fp, "..")) return SECURITY_ERROR_ACCESS_DENIED;
    return SECURITY_OK;
}

SecurityResult sanitize_filename(char *fn, size_t max) {
    if (!fn || max == 0) return SECURITY_ERROR_INVALID_INPUT;
    size_t len = strlen(fn);
    if (len >= max) return SECURITY_ERROR_PATH_TOO_LONG;
    for (size_t i = 0; i < len; i++) {
        switch (fn[i]) {
            case '<': case '>': case ':': case '"':
            case '|': case '?': case '*': fn[i] = '_'; break;
            default:
                if (!isprint((unsigned char)fn[i]) && !isspace((unsigned char)fn[i]))
                    fn[i] = '_';
        }
    }
    fn[max - 1] = '\0';
    return SECURITY_OK;
}

SecurityResult validate_image_size(long sz) {
    if (sz < 0)             return SECURITY_ERROR_INVALID_INPUT;
    if (sz > MAX_FILE_SIZE) return SECURITY_ERROR_FILE_TOO_LARGE;
    return SECURITY_OK;
}

void secure_strncpy(char *dst, const char *src, size_t n) {
    if (!dst || !src || n == 0) return;
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

void secure_memzero(void *p, size_t n) {
    volatile char *v = (volatile char *)p;
    for (size_t i = 0; i < n; i++) v[i] = 0;
}

// ── Utility ───────────────────────────────────────────────────────────────────
const char* get_format_name(const char *fp) {
    if (!fp) return "Unknown";
    const char *ext = strrchr(fp, '.');
    if (!ext || strlen(ext) > 10) return "Unknown";
    ext++;
    if (strcasecmp(ext, "png")  == 0) return "PNG";
    if (strcasecmp(ext, "jpg")  == 0 || strcasecmp(ext, "jpeg") == 0) return "JPEG";
    if (strcasecmp(ext, "bmp")  == 0) return "BMP";
    if (strcasecmp(ext, "gif")  == 0) return "GIF";
    if (strcasecmp(ext, "tga")  == 0) return "TGA";
    if (strcasecmp(ext, "webp") == 0) return "WEBP";
    return "Unknown";
}

char* format_file_size(long bytes) {
    static char buf[64];
    const char *u[] = {"B", "KB", "MB", "GB"};
    int unit = 0;
    double s = (double)bytes;
    if (bytes < 0) { secure_strncpy(buf, "Unknown", sizeof(buf)); return buf; }
    while (s >= 1024.0 && unit < 3) { s /= 1024.0; unit++; }
    snprintf(buf, sizeof(buf), "%.1f %s", s, u[unit]);
    return buf;
}

static int is_image_file(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext) return 0;
    ext++;
    return (strcasecmp(ext, "png")  == 0 || strcasecmp(ext, "jpg")  == 0 ||
            strcasecmp(ext, "jpeg") == 0 || strcasecmp(ext, "bmp")  == 0 ||
            strcasecmp(ext, "gif")  == 0 || strcasecmp(ext, "tga")  == 0 ||
            strcasecmp(ext, "webp") == 0);
}

// ── Font helpers ──────────────────────────────────────────────────────────────
static const char* find_font(void) {
    static const char *candidates[] = {
#ifdef _WIN32
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf",
        "C:\\Windows\\Fonts\\verdana.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
#endif
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        FILE *f = fopen(candidates[i], "rb");
        if (f) { fclose(f); return candidates[i]; }
    }
    return NULL;
}

SDL_Texture* render_text(App *app, TTF_Font *font,
                         const char *text, SDL_Color color) {
    if (!font || !text) return NULL;
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, color);
    if (!surf) return NULL;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(app->renderer, surf);
    SDL_FreeSurface(surf);
    return tex;
}

void draw_text(App *app, TTF_Font *font,
               const char *text, int x, int y, SDL_Color color) {
    SDL_Texture *tex = render_text(app, font, text, color);
    if (!tex) return;
    int w, h;
    SDL_QueryTexture(tex, NULL, NULL, &w, &h);
    SDL_Rect dst = {x, y, w, h};
    SDL_RenderCopy(app->renderer, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

// ── Window title ──────────────────────────────────────────────────────────────
void update_window_title(App *app) {
    if (!app) return;
    if (app->file_list.count > 0) {
        const char *p = app->file_list.paths[app->file_list.current];
        const char *f = strrchr(p, PATH_SEP);
        f = f ? f + 1 : p;
        char t[512];
        snprintf(t, sizeof(t), "Photon - %s  [%d / %d]",
                 f, app->file_list.current + 1, app->file_list.count);
        SDL_SetWindowTitle(app->window, t);
    } else {
        SDL_SetWindowTitle(app->window, "Photon");
    }
}

// ── FileList ──────────────────────────────────────────────────────────────────
void free_file_list(FileList *list) {
    if (!list) return;
    for (int i = 0; i < list->count; i++) free(list->paths[i]);
    free(list->paths);
    list->paths = NULL;
    list->count = 0;
    list->current = 0;
}

static int path_cmp(const void *a, const void *b) {
    return strcasecmp(*(const char **)a, *(const char **)b);
}

int scan_folder(const char *filepath, FileList *list) {
    if (!filepath || !list) return 0;

    char dir[MAX_PATH_LENGTH];
    secure_strncpy(dir, filepath, sizeof(dir));
    char *sep = strrchr(dir, '/');
#ifdef _WIN32
    char *sep2 = strrchr(dir, '\\');
    if (!sep || (sep2 && sep2 > sep)) sep = sep2;
#endif
    if (sep) *sep = '\0';
    else secure_strncpy(dir, ".", sizeof(dir));

    list->paths = malloc(MAX_IMAGES * sizeof(char *));
    if (!list->paths) return 0;
    list->count = 0;
    list->current = 0;

#ifdef _WIN32
    char search[MAX_PATH_LENGTH];
    snprintf(search, sizeof(search), "%s\\*", dir);
    WIN32_FIND_DATA ffd;
    HANDLE h = FindFirstFile(search, &ffd);
    if (h == INVALID_HANDLE_VALUE) { free(list->paths); list->paths = NULL; return 0; }
    do {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
             is_image_file(ffd.cFileName) && list->count < MAX_IMAGES) {
            char full[MAX_PATH_LENGTH];
            int full_len = snprintf(full, sizeof(full), "%s\\%s", dir, ffd.cFileName);
            if (full_len < 0 || full_len >= (int)sizeof(full)) continue;
            list->paths[list->count] = strdup(full);
            if (list->paths[list->count]) list->count++;
        }
    } while (FindNextFile(h, &ffd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) { free(list->paths); list->paths = NULL; return 0; }
    struct dirent *e;
    while ((e = readdir(d)) != NULL && list->count < MAX_IMAGES) {
        if (is_image_file(e->d_name)) {
            char full[MAX_PATH_LENGTH];
            int full_len = snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
            if (full_len < 0 || full_len >= (int)sizeof(full)) continue;
            list->paths[list->count] = strdup(full);
            if (list->paths[list->count]) list->count++;
        }
    }
    closedir(d);
#endif

    qsort(list->paths, list->count, sizeof(char *), path_cmp);

    for (int i = 0; i < list->count; i++) {
#ifdef _WIN32
        if (_stricmp(list->paths[i], filepath) == 0) { list->current = i; break; }
#else
        if (strcmp(list->paths[i], filepath) == 0) { list->current = i; break; }
#endif
    }
    return list->count > 0;
}

// ── Thumbnail cache ───────────────────────────────────────────────────────────
void free_thumb_cache(App *app) {
    for (int i = 0; i < THUMB_CACHE_MAX; i++) {
        if (app->thumb_cache[i].tex) {
            SDL_DestroyTexture(app->thumb_cache[i].tex);
            app->thumb_cache[i].tex = NULL;
            app->thumb_cache[i].path[0] = '\0';
        }
    }
}

SDL_Texture* get_thumb(App *app, int index) {
    if (index < 0 || index >= app->file_list.count) return NULL;
    const char *path = app->file_list.paths[index];

    for (int i = 0; i < THUMB_CACHE_MAX; i++)
        if (app->thumb_cache[i].tex && strcmp(app->thumb_cache[i].path, path) == 0)
            return app->thumb_cache[i].tex;

    int slot = -1;
    for (int i = 0; i < THUMB_CACHE_MAX; i++)
        if (!app->thumb_cache[i].tex) { slot = i; break; }
    if (slot == -1) {
        int worst = 0, worst_dist = 0;
        for (int i = 0; i < THUMB_CACHE_MAX; i++) {
            int idx = -1;
            for (int j = 0; j < app->file_list.count; j++)
                if (strcmp(app->thumb_cache[i].path, app->file_list.paths[j]) == 0)
                    { idx = j; break; }
            int dist = (idx < 0) ? 99999 : abs(idx - app->file_list.current);
            if (dist > worst_dist) { worst_dist = dist; worst = i; }
        }
        slot = worst;
        SDL_DestroyTexture(app->thumb_cache[slot].tex);
        app->thumb_cache[slot].tex = NULL;
    }

    SDL_Surface *full = IMG_Load(path);
    if (!full) return NULL;

    float ar = (float)full->w / full->h;
    int tw = (ar >= 1.f) ? THUMB_SCALE_MAX : (int)(THUMB_SCALE_MAX * ar);
    int th = (ar <  1.f) ? THUMB_SCALE_MAX : (int)(THUMB_SCALE_MAX / ar);
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;

    SDL_Surface *scaled = SDL_CreateRGBSurface(0, tw, th, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (scaled) {
        SDL_BlitScaled(full, NULL, scaled, NULL);
        SDL_FreeSurface(full);
        full = scaled;
    }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(app->renderer, full);
    SDL_FreeSurface(full);
    if (!tex) return NULL;

    secure_strncpy(app->thumb_cache[slot].path, path, MAX_PATH_LENGTH);
    app->thumb_cache[slot].tex = tex;
    return tex;
}

// ── File dialog ───────────────────────────────────────────────────────────────
char* open_file_dialog(void) {
    static char fp[MAX_PATH_LENGTH];
    fp[0] = '\0';
#ifdef _WIN32
    OPENFILENAME ofn = {0};
    ofn.lStructSize  = sizeof(ofn);
    ofn.lpstrFile    = fp;
    ofn.nMaxFile     = sizeof(fp);
    ofn.lpstrFilter  = "Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tga;*.webp\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle   = "Open Image";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileName(&ofn) ? fp : NULL;
#elif defined(__APPLE__)
    FILE *f = popen("osascript -e 'POSIX path of (choose file of type "
                    "{\"public.image\"} with prompt \"Open Image\")'", "r");
    if (!f) return NULL;
    if (fgets(fp, sizeof(fp), f)) {
        size_t l = strlen(fp);
        if (l && fp[l - 1] == '\n') fp[l - 1] = '\0';
    }
    pclose(f);
    return fp[0] ? fp : NULL;
#else
    FILE *f = popen(
        "zenity --file-selection --title='Open Image' "
        "--file-filter='Images | *.png *.jpg *.jpeg *.bmp *.gif *.tga *.webp' "
        "2>/dev/null", "r");
    if (!f)
        f = popen(
            "kdialog --getopenfilename . "
            "'Image files (*.png *.jpg *.jpeg *.bmp *.gif *.tga *.webp)' 2>/dev/null", "r");
    if (f) {
        if (fgets(fp, sizeof(fp), f)) {
            size_t l = strlen(fp);
            if (l && fp[l - 1] == '\n') fp[l - 1] = '\0';
        }
        pclose(f);
        return fp[0] ? fp : NULL;
    }
    return NULL;
#endif
}

// ── Image loading ─────────────────────────────────────────────────────────────
int load_image(App *app, const char *path) {
    if (!app || !path) return 0;
    if (validate_filepath(path) != SECURITY_OK) return 0;

    struct stat st;
    if (stat(path, &st) != 0 || validate_image_size(st.st_size) != SECURITY_OK) return 0;

    SDL_Surface *surf = IMG_Load(path);
    if (!surf) { SDL_Log("IMG_Load: %s", IMG_GetError()); return 0; }
    if (surf->w <= 0 || surf->h <= 0 || surf->w > 32768 || surf->h > 32768) {
        SDL_FreeSurface(surf); return 0;
    }

    if (app->image_texture) {
        SDL_DestroyTexture(app->image_texture);
        app->image_texture = NULL;
    }
    app->image_texture     = SDL_CreateTextureFromSurface(app->renderer, surf);
    app->image_width       = surf->w;
    app->image_height      = surf->h;
    app->current_file_size = st.st_size;
    app->current_mod_time  = st.st_mtime;
    SDL_FreeSurface(surf);

    if (!app->image_texture) return 0;
    SDL_Log("Loaded: %s (%dx%d)", path, app->image_width, app->image_height);
    return 1;
}

// ── Navigation ────────────────────────────────────────────────────────────────
void navigate_to(App *app, int index) {
    if (!app || app->file_list.count == 0) return;
    if (index < 0) index = app->file_list.count - 1;
    if (index >= app->file_list.count) index = 0;
    app->file_list.current = index;
    const char *path = app->file_list.paths[index];
    if (load_image(app, path)) {
        secure_strncpy(app->current_path, path, sizeof(app->current_path));
        app->fit_to_window = 1;
        app->zoom = 1.0f;
        app->pan_x = 0;
        app->pan_y = 0;
        app->rotation = 0;
        update_window_title(app);
    }
}

void navigate_image(App *app, int dir) {
    if (!app || app->file_list.count <= 1) return;
    navigate_to(app, app->file_list.current + dir);
}

// ── Open ──────────────────────────────────────────────────────────────────────
void open_image_path(App *app, const char *path) {
    if (!app || !path) return;
    if (validate_filepath(path) != SECURITY_OK) return;
    if (!is_image_file(path)) { SDL_Log("Not an image: %s", path); return; }
    if (load_image(app, path)) {
        secure_strncpy(app->current_path, path, sizeof(app->current_path));
        free_file_list(&app->file_list);
        free_thumb_cache(app);
        scan_folder(path, &app->file_list);
        app->fit_to_window = 1;
        app->zoom = 1.0f;
        app->pan_x = 0;
        app->pan_y = 0;
        app->rotation = 0;
        update_window_title(app);
    }
}

void open_image(App *app) {
    char *path = open_file_dialog();
    if (path) open_image_path(app, path);
}

// ── Clipboard ─────────────────────────────────────────────────────────────────
void copy_to_clipboard(App *app) {
    if (!app || !app->current_path[0]) return;
#ifdef _WIN32
    SDL_Surface *surf = IMG_Load(app->current_path);
    if (!surf) return;
    SDL_Surface *bgr = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_BGR24, 0);
    SDL_FreeSurface(surf);
    if (!bgr) return;

    int w = bgr->w, h = bgr->h;
    int dst_pitch = (w * 3 + 3) & ~3;
    BITMAPINFOHEADER bih = {0};
    bih.biSize        = sizeof(bih);
    bih.biWidth       = w;
    bih.biHeight      = h;
    bih.biPlanes      = 1;
    bih.biBitCount    = 24;
    bih.biCompression = BI_RGB;
    bih.biSizeImage   = h * dst_pitch;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(bih) + bih.biSizeImage);
    if (!hMem) { SDL_FreeSurface(bgr); return; }
    BYTE *mem = (BYTE*)GlobalLock(hMem);
    memcpy(mem, &bih, sizeof(bih));
    BYTE *src = (BYTE*)bgr->pixels;
    BYTE *dst = mem + sizeof(bih);
    for (int y = h - 1; y >= 0; y--)
        memcpy(dst + (h - 1 - y) * dst_pitch, src + y * bgr->pitch, w * 3);
    GlobalUnlock(hMem);
    SDL_FreeSurface(bgr);

    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        SetClipboardData(CF_DIB, hMem);
        CloseClipboard();
        SDL_Log("Image copied to clipboard");
    } else {
        GlobalFree(hMem);
    }
#else
    SDL_SetClipboardText(app->current_path);
    SDL_Log("Path copied: %s", app->current_path);
#endif
}

// ── Delete ────────────────────────────────────────────────────────────────────
void delete_current_image(App *app) {
    if (!app || app->file_list.count == 0) return;
    const char *path  = app->file_list.paths[app->file_list.current];
    const char *fname = strrchr(path, PATH_SEP);
    fname = fname ? fname + 1 : path;

    char msg[MAX_PATH_LENGTH + 64];
    snprintf(msg, sizeof(msg), "Delete \"%s\"?\nThis cannot be undone.", fname);

    SDL_MessageBoxButtonData btns[] = {
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,  0, "Cancel"},
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,  1, "Delete"},
    };
    SDL_MessageBoxData mbd = {
        SDL_MESSAGEBOX_WARNING, app->window,
        "Delete Image", msg, 2, btns, NULL
    };
    int btn = 0;
    SDL_ShowMessageBox(&mbd, &btn);
    if (btn != 1) return;

    remove(path);

    for (int i = 0; i < THUMB_CACHE_MAX; i++) {
        if (strcmp(app->thumb_cache[i].path, path) == 0) {
            SDL_DestroyTexture(app->thumb_cache[i].tex);
            app->thumb_cache[i].tex = NULL;
            app->thumb_cache[i].path[0] = '\0';
            break;
        }
    }

    free(app->file_list.paths[app->file_list.current]);
    for (int i = app->file_list.current; i < app->file_list.count - 1; i++)
        app->file_list.paths[i] = app->file_list.paths[i + 1];
    app->file_list.count--;

    if (app->file_list.count == 0) {
        if (app->image_texture) {
            SDL_DestroyTexture(app->image_texture);
            app->image_texture = NULL;
        }
        app->current_path[0] = '\0';
        SDL_SetWindowTitle(app->window, "Photon");
    } else {
        if (app->file_list.current >= app->file_list.count)
            app->file_list.current = app->file_list.count - 1;
        navigate_to(app, app->file_list.current);
    }
}

// ── Rendering ─────────────────────────────────────────────────────────────────
void render_image(App *app) {
    if (!app) return;
    SDL_SetRenderDrawColor(app->renderer, 25, 25, 35, 255);
    SDL_RenderClear(app->renderer);

    if (!app->image_texture) {
        if (app->font_regular) {
            SDL_Color c = {160, 160, 200, 255};
            draw_text(app, app->font_regular,
                      "Press O to open an image, or drag & drop a file here",
                      app->window_width / 2 - 200,
                      app->window_height / 2 - 10, c);
        }
        return;
    }

    int eff_w = (app->rotation == 90 || app->rotation == 270)
                ? app->image_height : app->image_width;
    int eff_h = (app->rotation == 90 || app->rotation == 270)
                ? app->image_width  : app->image_height;
    int area_h = app->show_thumbnails
                 ? app->window_height - THUMB_STRIP_H
                 : app->window_height;

    SDL_Rect dest;
    if (app->fit_to_window) {
        float ar  = (float)eff_w / eff_h;
        float war = (float)app->window_width / area_h;
        if (ar > war) {
            dest.w = app->window_width;
            dest.h = (int)(app->window_width / ar);
            dest.x = 0;
            dest.y = (area_h - dest.h) / 2;
        } else {
            dest.h = area_h;
            dest.w = (int)(area_h * ar);
            dest.x = (app->window_width - dest.w) / 2;
            dest.y = 0;
        }
    } else {
        dest.w = (int)(eff_w * app->zoom);
        dest.h = (int)(eff_h * app->zoom);
        if (dest.w <= 0 || dest.h <= 0 || dest.w > 65536 || dest.h > 65536) return;
        dest.x = app->pan_x + (app->window_width - dest.w) / 2;
        dest.y = app->pan_y + (area_h - dest.h) / 2;
    }

    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 60);
    SDL_Rect shadow = {dest.x + 4, dest.y + 4, dest.w, dest.h};
    SDL_RenderFillRect(app->renderer, &shadow);

    SDL_RenderCopyEx(app->renderer, app->image_texture, NULL, &dest,
                     (double)app->rotation, NULL, SDL_FLIP_NONE);

    SDL_SetRenderDrawColor(app->renderer, 80, 80, 100, 255);
    SDL_RenderDrawRect(app->renderer, &dest);
}

void render_info_panel(App *app) {
    if (!app || !app->show_info || !app->image_texture || !app->font_regular) return;

    int panel_h = INFO_PAD * 2 + INFO_LINE_H * INFO_LINES + 8;
    SDL_Rect panel = {15, 15, INFO_W, panel_h};

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 10, 12, 24, 220);
    SDL_RenderFillRect(app->renderer, &panel);

    SDL_SetRenderDrawColor(app->renderer, 65, 105, 225, 255);
    SDL_RenderDrawRect(app->renderer, &panel);

    SDL_Rect accent_bar = {panel.x, panel.y, panel.w, 4};
    SDL_SetRenderDrawColor(app->renderer, 65, 105, 225, 255);
    SDL_RenderFillRect(app->renderer, &accent_bar);

    int tx = panel.x + INFO_PAD;
    int ty = panel.y + INFO_PAD + 6;

    SDL_Color white      = {255, 255, 255, 255};
    SDL_Color grey       = {160, 170, 200, 255};
    SDL_Color accent_col = {100, 160, 255, 255};

    draw_text(app, app->font_bold, "Image Info", tx, ty, accent_col);
    ty += INFO_LINE_H + 4;

    SDL_SetRenderDrawColor(app->renderer, 50, 70, 120, 255);
    SDL_RenderDrawLine(app->renderer, tx, ty, panel.x + panel.w - INFO_PAD, ty);
    ty += 8;

    const char *full_name = strrchr(app->current_path, PATH_SEP);
    full_name = full_name ? full_name + 1 : app->current_path;
    char trunc[48];
    if (strlen(full_name) > 34) {
        snprintf(trunc, sizeof(trunc), "%.31s...", full_name);
        full_name = trunc;
    }
    draw_text(app, app->font_bold,    "File",     tx,      ty, grey);
    draw_text(app, app->font_regular, full_name,  tx + 70, ty, white);
    ty += INFO_LINE_H;

    char fmt_buf[64];
    snprintf(fmt_buf, sizeof(fmt_buf), "%s", get_format_name(app->current_path));
    draw_text(app, app->font_bold,    "Format",   tx,      ty, grey);
    draw_text(app, app->font_regular, fmt_buf,    tx + 70, ty, white);
    ty += INFO_LINE_H;

    char dim_buf[32];
    snprintf(dim_buf, sizeof(dim_buf), "%d x %d px", app->image_width, app->image_height);
    draw_text(app, app->font_bold,    "Size",     tx,      ty, grey);
    draw_text(app, app->font_regular, dim_buf,    tx + 70, ty, white);
    ty += INFO_LINE_H;

    float ar = (float)app->image_width / (float)app->image_height;
    char ar_buf[32];
    snprintf(ar_buf, sizeof(ar_buf), "%.2f : 1", ar);
    draw_text(app, app->font_bold,    "Aspect",   tx,      ty, grey);
    draw_text(app, app->font_regular, ar_buf,     tx + 70, ty, white);
    ty += INFO_LINE_H;

    char fs_buf[32];
    snprintf(fs_buf, sizeof(fs_buf), "%s", format_file_size(app->current_file_size));
    draw_text(app, app->font_bold,    "Filesize", tx,      ty, grey);
    draw_text(app, app->font_regular, fs_buf,     tx + 70, ty, white);
    ty += INFO_LINE_H;

    if (app->current_mod_time > 0) {
        char date_buf[32];
        strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M",
                 localtime(&app->current_mod_time));
        draw_text(app, app->font_bold,    "Modified", tx,      ty, grey);
        draw_text(app, app->font_regular, date_buf,   tx + 70, ty, white);
        ty += INFO_LINE_H;
    }

    char zoom_buf[32];
    snprintf(zoom_buf, sizeof(zoom_buf), "%.0f%%", app->zoom * 100.f);
    draw_text(app, app->font_bold,    "Zoom",     tx,      ty, grey);
    draw_text(app, app->font_regular, zoom_buf,   tx + 70, ty, white);
    ty += INFO_LINE_H;

    char rot_buf[16];
    snprintf(rot_buf, sizeof(rot_buf), "%d deg", app->rotation);
    draw_text(app, app->font_bold,    "Rotation", tx,      ty, grey);
    draw_text(app, app->font_regular, rot_buf,    tx + 70, ty, white);
    ty += INFO_LINE_H;

    char idx_buf[32];
    snprintf(idx_buf, sizeof(idx_buf), "%d of %d",
             app->file_list.current + 1, app->file_list.count);
    draw_text(app, app->font_bold,    "Index",    tx,      ty, grey);
    draw_text(app, app->font_regular, idx_buf,    tx + 70, ty, white);
}

void render_thumbnail_strip(App *app) {
    if (!app || !app->show_thumbnails || app->file_list.count == 0) return;

    int strip_y = app->window_height - THUMB_STRIP_H;

    SDL_SetRenderDrawColor(app->renderer, 12, 12, 22, 230);
    SDL_Rect bg = {0, strip_y, app->window_width, THUMB_STRIP_H};
    SDL_RenderFillRect(app->renderer, &bg);

    SDL_SetRenderDrawColor(app->renderer, 55, 75, 125, 255);
    SDL_Rect sep = {0, strip_y, app->window_width, 2};
    SDL_RenderFillRect(app->renderer, &sep);

    int visible = app->window_width / THUMB_SLOT_W;
    if (visible < 1) visible = 1;
    int start = app->file_list.current - visible / 2;
    if (start < 0) start = 0;
    if (start + visible > app->file_list.count)
        start = app->file_list.count - visible;
    if (start < 0) start = 0;

    int x = THUMB_PAD;
    for (int i = start; i < start + visible && i < app->file_list.count; i++) {
        int cur = (i == app->file_list.current);

        if (cur) {
            SDL_SetRenderDrawColor(app->renderer, 70, 120, 255, 255);
            SDL_Rect hl = {x - 2, strip_y + THUMB_PAD - 2, THUMB_W + 4, THUMB_H + 4};
            SDL_RenderFillRect(app->renderer, &hl);
        }

        SDL_Rect slot = {x, strip_y + THUMB_PAD, THUMB_W, THUMB_H};
        SDL_SetRenderDrawColor(app->renderer, 28, 28, 40, 255);
        SDL_RenderFillRect(app->renderer, &slot);

        SDL_Texture *tex = get_thumb(app, i);
        if (tex) {
            int tw, th;
            SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
            float tar = (float)tw / th;
            SDL_Rect dst;
            if (tar > (float)THUMB_W / THUMB_H) {
                dst.w = THUMB_W;
                dst.h = (int)(THUMB_W / tar);
                dst.x = x;
                dst.y = strip_y + THUMB_PAD + (THUMB_H - dst.h) / 2;
            } else {
                dst.h = THUMB_H;
                dst.w = (int)(THUMB_H * tar);
                dst.x = x + (THUMB_W - dst.w) / 2;
                dst.y = strip_y + THUMB_PAD;
            }
            SDL_RenderCopy(app->renderer, tex, NULL, &dst);
        }

        SDL_SetRenderDrawColor(app->renderer,
            cur ? 70 : 48, cur ? 120 : 48, cur ? 255 : 65, 255);
        SDL_RenderDrawRect(app->renderer, &slot);
        x += THUMB_SLOT_W;
    }
}

void render_hint_bar(App *app) {
    if (!app || !app->font_regular) return;
    static const char *hints =
        "O: Open   </> : Navigate   R: Rotate   I: Info   "
        "T: Thumbnails   Ctrl+C: Copy   Del: Delete";
    SDL_Color c = {90, 100, 140, 255};
    int y = app->show_thumbnails
            ? app->window_height - THUMB_STRIP_H - 18
            : app->window_height - 18;
    draw_text(app, app->font_regular, hints, 8, y, c);
}

void render(App *app) {
    render_image(app);
    render_thumbnail_strip(app);
    render_info_panel(app);
    render_hint_bar(app);
    SDL_RenderPresent(app->renderer);
}

// ── Events ────────────────────────────────────────────────────────────────────
void handle_events(App *app) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {

        case SDL_QUIT:
            app->running = 0;
            break;

        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                app->window_width  = ev.window.data1;
                app->window_height = ev.window.data2;
            }
            break;

        case SDL_DROPFILE:
            if (ev.drop.file) {
                open_image_path(app, ev.drop.file);
                SDL_free(ev.drop.file);
            }
            break;

        case SDL_KEYDOWN: {
            int ctrl  = (ev.key.keysym.mod & KMOD_CTRL)  != 0;
            int shift = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
            switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE:
                    app->running = 0;
                    break;
                case SDLK_PLUS:
                case SDLK_EQUALS:
                    app->zoom *= 1.2f;
                    app->fit_to_window = 0;
                    break;
                case SDLK_MINUS:
                    app->zoom /= 1.2f;
                    app->fit_to_window = 0;
                    break;
                case SDLK_f:
                    app->fit_to_window = 1;
                    app->zoom = 1.f;
                    app->pan_x = 0;
                    app->pan_y = 0;
                    break;
                case SDLK_1:
                    app->fit_to_window = 0;
                    app->zoom = 1.f;
                    app->pan_x = 0;
                    app->pan_y = 0;
                    break;
                case SDLK_i:
                    app->show_info = !app->show_info;
                    break;
                case SDLK_t:
                    app->show_thumbnails = !app->show_thumbnails;
                    break;
                case SDLK_o:
                    open_image(app);
                    break;
                case SDLK_LEFT:
                    navigate_image(app, -1);
                    break;
                case SDLK_RIGHT:
                    navigate_image(app, 1);
                    break;
                case SDLK_r:
                    app->rotation = (app->rotation + (shift ? 270 : 90)) % 360;
                    break;
                case SDLK_c:
                    if (ctrl) { copy_to_clipboard(app); }
                    break;
                case SDLK_DELETE:
                    delete_current_image(app);
                    break;
            }
            break;
        }

        case SDL_MOUSEBUTTONDOWN:
            if (ev.button.button == SDL_BUTTON_LEFT) {
                if (app->show_thumbnails &&
                    ev.button.y > app->window_height - THUMB_STRIP_H &&
                    app->file_list.count > 0) {
                    int visible = app->window_width / THUMB_SLOT_W;
                    if (visible < 1) visible = 1;
                    int start = app->file_list.current - visible / 2;
                    if (start < 0) start = 0;
                    if (start + visible > app->file_list.count)
                        start = app->file_list.count - visible;
                    if (start < 0) start = 0;
                    int clicked = start + (ev.button.x - THUMB_PAD) / THUMB_SLOT_W;
                    if (clicked >= 0 && clicked < app->file_list.count)
                        navigate_to(app, clicked);
                } else {
                    app->is_panning   = 1;
                    app->drag_start_x = ev.button.x;
                    app->drag_start_y = ev.button.y;
                    app->pan_start_x  = app->pan_x;
                    app->pan_start_y  = app->pan_y;
                    app->fit_to_window = 0;
                    if (app->zoom < 0.05f) app->zoom = 0.05f;
                }
            }
            break;

        case SDL_MOUSEBUTTONUP:
            if (ev.button.button == SDL_BUTTON_LEFT)
                app->is_panning = 0;
            break;

        case SDL_MOUSEMOTION:
            if (app->is_panning) {
                app->pan_x = app->pan_start_x + (ev.motion.x - app->drag_start_x);
                app->pan_y = app->pan_start_y + (ev.motion.y - app->drag_start_y);
            }
            break;

        case SDL_MOUSEWHEEL:
            if (ev.wheel.y > 0)      { app->zoom *= 1.1f; app->fit_to_window = 0; }
            else if (ev.wheel.y < 0) { app->zoom /= 1.1f; app->fit_to_window = 0; }
            break;
        }
    }
}

// ── SDL init / cleanup ────────────────────────────────────────────────────────
int initialize_sdl(App *app) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        SDL_Log("SDL init: %s", SDL_GetError()); return 0;
    }
    int img_flags = IMG_INIT_PNG | IMG_INIT_JPG;
    if (!(IMG_Init(img_flags) & img_flags)) {
        SDL_Log("SDL_image init: %s", IMG_GetError()); SDL_Quit(); return 0;
    }
    if (TTF_Init() < 0) {
        SDL_Log("SDL_ttf init: %s", TTF_GetError()); IMG_Quit(); SDL_Quit(); return 0;
    }

    app->window = SDL_CreateWindow(WINDOW_TITLE,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!app->window) {
        SDL_Log("Window: %s", SDL_GetError());
        TTF_Quit(); IMG_Quit(); SDL_Quit(); return 0;
    }

    app->renderer = SDL_CreateRenderer(app->window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!app->renderer) {
        SDL_Log("Renderer: %s", SDL_GetError());
        SDL_DestroyWindow(app->window); TTF_Quit(); IMG_Quit(); SDL_Quit(); return 0;
    }

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    SDL_GetWindowSize(app->window, &app->window_width, &app->window_height);

    const char *font_path = find_font();
    if (font_path) {
        app->font_regular = TTF_OpenFont(font_path, 13);
        app->font_bold    = TTF_OpenFont(font_path, 13);
        if (app->font_bold)
            TTF_SetFontStyle(app->font_bold, TTF_STYLE_BOLD);
    }
    if (!app->font_regular)
        SDL_Log("Warning: No font found. Text disabled. (%s)", TTF_GetError());

    app->running         = 1;
    app->zoom            = 1.0f;
    app->fit_to_window   = 1;
    app->show_info       = 0;
    app->show_thumbnails = 1;
    app->rotation        = 0;
    return 1;
}

void cleanup(App *app) {
    if (!app) return;
    free_file_list(&app->file_list);
    free_thumb_cache(app);
    if (app->image_texture) SDL_DestroyTexture(app->image_texture);
    if (app->font_bold)     TTF_CloseFont(app->font_bold);
    if (app->font_regular)  TTF_CloseFont(app->font_regular);
    if (app->renderer)      SDL_DestroyRenderer(app->renderer);
    if (app->window)        SDL_DestroyWindow(app->window);
    TTF_Quit(); IMG_Quit(); SDL_Quit();
}

// ── Entry point ───────────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {
    App app = {0};
    if (!initialize_sdl(&app)) return 1;

    if (argc >= 2) open_image_path(&app, argv[1]);
    else           open_image(&app);

    while (app.running) {
        handle_events(&app);
        render(&app);
        SDL_Delay(16);
    }
    cleanup(&app);
    return 0;
}
