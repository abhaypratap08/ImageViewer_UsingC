/* Enable POSIX extensions: strdup, popen, pclose */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
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
#else
#include <dirent.h>
#include <unistd.h>
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

#define UI_MARGIN       16
#define UI_GAP          14
#define TOOLBAR_H       54
#define TOOLBAR_BTN_H   30
#define TOOLBAR_BTN_PAD 14
#define STATUS_BAR_H    34
#define PANEL_MIN_W     280
#define PANEL_MAX_W     380
#define PANEL_MIN_CANVAS_W 360
#define INFO_PAD        14
#define INFO_ROW_H      48

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
    char  custom_font_path[MAX_PATH_LENGTH];
    Thumb thumb_cache[THUMB_CACHE_MAX];
    long  current_file_size;
    time_t current_mod_time;
    SDL_Rect open_button_rect;
    SDL_Rect info_button_rect;
    SDL_Rect thumbs_button_rect;
    SDL_Rect fit_button_rect;
    SDL_Rect actual_button_rect;
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
    size_t len = strlen(src);
    if (len >= n) len = n - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
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

// ── Desktop Integration (Linux) ─────────────────────────────────────────────
#if !defined(_WIN32) && !defined(__APPLE__)
static void integrate_desktop(void) {
    const char *appimage = getenv("APPIMAGE");
    if (!appimage) return;

    char home[MAX_PATH_LENGTH];
    const char *h = getenv("HOME");
    if (!h) return;
    secure_strncpy(home, h, sizeof(home));

    char desktop_path[MAX_PATH_LENGTH];
    snprintf(desktop_path, sizeof(desktop_path), 
             "%s/.local/share/applications/photon.desktop", home);

    if (access(desktop_path, F_OK) == 0) return;

    char dir[MAX_PATH_LENGTH];
    snprintf(dir, sizeof(dir), "%s/.local/share/applications", home);
    
    /* Ensure directory exists */
    char mkdir_cmd[MAX_PATH_LENGTH + 16];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", dir);
    system(mkdir_cmd);

    FILE *f = fopen(desktop_path, "w");
    if (!f) return;

    fprintf(f, "[Desktop Entry]\n");
    fprintf(f, "Name=Photon Image Viewer\n");
    fprintf(f, "Comment=A lightweight image viewer built with C and SDL2\n");
    fprintf(f, "Exec=%s %%f\n", appimage);
    fprintf(f, "Icon=photon\n");
    fprintf(f, "Terminal=false\n");
    fprintf(f, "Type=Application\n");
    fprintf(f, "Categories=Graphics;Viewer;\n");
    fprintf(f, "MimeType=image/jpeg;image/png;image/bmp;image/gif;image/webp;image/x-tga;\n");
    fprintf(f, "StartupNotify=true\n");
    fclose(f);

    SDL_Log("Desktop integration complete: %s", desktop_path);
}
#endif

// ── Font helpers ──────────────────────────────────────────────────────────────
static const char* find_font(App *app) {
    static char detected_path[MAX_PATH_LENGTH];
    detected_path[0] = '\0';

    /* 1. Priority: CLI argument */
    if (app && app->custom_font_path[0]) {
        FILE *f = fopen(app->custom_font_path, "rb");
        if (f) { fclose(f); return app->custom_font_path; }
        SDL_Log("Warning: Custom font not found: %s", app->custom_font_path);
    }

    /* 2. Priority: Environment variable */
    const char *env_font = getenv("PHOTON_FONT");
    if (env_font) {
        FILE *f = fopen(env_font, "rb");
        if (f) { fclose(f); return env_font; }
    }

    /* 3. Priority: Dynamic System Detection (Linux/Unix) */
#if !defined(_WIN32) && !defined(__APPLE__)
    FILE *fp = popen("fc-match -f '%{file}' sans-serif 2>/dev/null", "r");
    if (fp) {
        if (fgets(detected_path, sizeof(detected_path), fp)) {
            size_t len = strlen(detected_path);
            if (len > 0 && detected_path[len - 1] == '\n') detected_path[len - 1] = '\0';
            pclose(fp);
            if (detected_path[0]) {
                FILE *f = fopen(detected_path, "rb");
                if (f) { fclose(f); return detected_path; }
            }
        } else {
            pclose(fp);
        }
    }
#endif

    /* 4. Priority: Hardcoded Fallbacks */
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

static int point_in_rect(int x, int y, const SDL_Rect *rect) {
    return rect && rect->w > 0 && rect->h > 0 &&
           x >= rect->x && x < rect->x + rect->w &&
           y >= rect->y && y < rect->y + rect->h;
}

static int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static int text_width(TTF_Font *font, const char *text) {
    int w = 0;
    if (!font || !text) return 0;
    if (TTF_SizeUTF8(font, text, &w, NULL) != 0) return 0;
    return w;
}

static void fit_text_to_width(TTF_Font *font, const char *text, int max_w,
                              char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!text) return;

    secure_strncpy(out, text, out_sz);
    if (!font || max_w <= 0) return;

    if (text_width(font, out) <= max_w) return;
    if (text_width(font, "...") > max_w) {
        out[0] = '\0';
        return;
    }

    size_t len = strlen(text);
    while (len > 0) {
        len--;
        snprintf(out, out_sz, "%.*s...", (int)len, text);
        if (text_width(font, out) <= max_w) return;
    }

    secure_strncpy(out, "...", out_sz);
}

static void draw_text_centered(App *app, TTF_Font *font,
                               const char *text, SDL_Rect rect,
                               SDL_Color color) {
    if (!app || !font || !text || rect.w <= 0 || rect.h <= 0) return;
    int w = 0, h = 0;
    if (TTF_SizeUTF8(font, text, &w, &h) != 0) return;
    draw_text(app, font, text,
              rect.x + (rect.w - w) / 2,
              rect.y + (rect.h - h) / 2, color);
}

static void draw_text_fitted(App *app, TTF_Font *font,
                             const char *text, int x, int y,
                             int max_w, SDL_Color color) {
    char clipped[512];
    fit_text_to_width(font, text, max_w, clipped, sizeof(clipped));
    if (clipped[0]) draw_text(app, font, clipped, x, y, color);
}

static const char* filename_from_path(const char *path) {
    const char *name;
    if (!path || !path[0]) return "No Image Selected";
    name = strrchr(path, PATH_SEP);
    return name ? name + 1 : path;
}

static void directory_from_path(const char *path, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!path || !path[0]) {
        secure_strncpy(out, ".", out_sz);
        return;
    }

    secure_strncpy(out, path, out_sz);
    char *sep = strrchr(out, PATH_SEP);
    if (sep) *sep = '\0';
    else secure_strncpy(out, ".", out_sz);
}

static int get_info_panel_width(const App *app) {
    if (!app || !app->show_info) return 0;
    int width = clamp_int(app->window_width / 3, PANEL_MIN_W, PANEL_MAX_W);
    int allowed = app->window_width - UI_MARGIN * 2 - PANEL_MIN_CANVAS_W;
    if (allowed < 180) allowed = app->window_width / 2;
    if (allowed < 180) allowed = 180;
    if (width > allowed) width = allowed;
    return width;
}

static SDL_Rect get_toolbar_rect(const App *app) {
    SDL_Rect rect = {0, 0, 0, 0};
    if (!app) return rect;
    rect.x = UI_MARGIN;
    rect.y = UI_MARGIN;
    rect.w = app->window_width - UI_MARGIN * 2;
    rect.h = TOOLBAR_H;
    if (rect.w < 0) rect.w = 0;
    return rect;
}

static SDL_Rect get_workspace_rect(const App *app) {
    SDL_Rect toolbar = get_toolbar_rect(app);
    SDL_Rect rect = {0, 0, 0, 0};
    if (!app) return rect;
    rect.x = UI_MARGIN;
    rect.y = toolbar.y + toolbar.h + UI_GAP;
    rect.w = app->window_width - UI_MARGIN * 2;
    rect.h = app->window_height - rect.y - UI_MARGIN;
    if (rect.w < 0) rect.w = 0;
    if (rect.h < 0) rect.h = 0;
    return rect;
}

static SDL_Rect get_info_panel_rect(const App *app) {
    SDL_Rect workspace = get_workspace_rect(app);
    SDL_Rect rect = {0, 0, 0, 0};
    int panel_w = get_info_panel_width(app);
    if (!panel_w) return rect;
    rect.x = workspace.x + workspace.w - panel_w;
    rect.y = workspace.y;
    rect.w = panel_w;
    rect.h = workspace.h;
    return rect;
}

static int get_content_right_offset(const App *app) {
    int panel_w = get_info_panel_width(app);
    return panel_w ? panel_w + UI_GAP : 0;
}

static SDL_Rect get_thumbnail_rect(const App *app) {
    SDL_Rect workspace = get_workspace_rect(app);
    SDL_Rect rect = {0, 0, 0, 0};
    if (!app || !app->show_thumbnails) return rect;
    rect.x = workspace.x;
    rect.w = workspace.w - get_content_right_offset(app);
    rect.h = THUMB_STRIP_H;
    rect.y = workspace.y + workspace.h - rect.h;
    if (rect.w < THUMB_W + THUMB_PAD * 2 || rect.h <= 0) rect = (SDL_Rect){0, 0, 0, 0};
    return rect;
}

static SDL_Rect get_status_rect(const App *app) {
    SDL_Rect workspace = get_workspace_rect(app);
    SDL_Rect thumb = get_thumbnail_rect(app);
    SDL_Rect rect = {0, 0, 0, 0};
    if (!app) return rect;
    rect.x = workspace.x;
    rect.w = workspace.w - get_content_right_offset(app);
    rect.h = STATUS_BAR_H;
    rect.y = thumb.h > 0
           ? thumb.y - UI_GAP - rect.h
           : workspace.y + workspace.h - rect.h;
    if (rect.w < 140 || rect.y < workspace.y) rect = (SDL_Rect){0, 0, 0, 0};
    return rect;
}

static SDL_Rect get_canvas_rect(const App *app) {
    SDL_Rect workspace = get_workspace_rect(app);
    SDL_Rect status = get_status_rect(app);
    SDL_Rect rect = {0, 0, 0, 0};
    int bottom = status.h > 0 ? status.y - UI_GAP : workspace.y + workspace.h;
    if (!app) return rect;
    rect.x = workspace.x;
    rect.y = workspace.y;
    rect.w = workspace.w - get_content_right_offset(app);
    rect.h = bottom - workspace.y;
    if (rect.w < 0) rect.w = 0;
    if (rect.h < 0) rect.h = 0;
    return rect;
}

static int get_button_width(TTF_Font *font, const char *label) {
    int w = text_width(font, label);
    if (w < 42) w = 42;
    return w + TOOLBAR_BTN_PAD * 2;
}

static void draw_toolbar_button(App *app, SDL_Rect rect, const char *label,
                                int active, int primary) {
    SDL_Color text = {220, 228, 255, 255};
    SDL_Color border = active
                     ? (SDL_Color){95, 155, 255, 255}
                     : (SDL_Color){68, 78, 110, 255};

    if (!app || rect.w <= 0 || rect.h <= 0) return;

    if (primary) {
        SDL_SetRenderDrawColor(app->renderer, 72, 116, 255, 255);
    } else if (active) {
        SDL_SetRenderDrawColor(app->renderer, 36, 52, 96, 255);
    } else {
        SDL_SetRenderDrawColor(app->renderer, 24, 28, 44, 230);
    }
    SDL_RenderFillRect(app->renderer, &rect);

    SDL_SetRenderDrawColor(app->renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(app->renderer, &rect);

    if (app->font_regular) {
        char clipped[64];
        fit_text_to_width(app->font_regular, label, rect.w - 12, clipped, sizeof(clipped));
        draw_text_centered(app, app->font_regular, clipped, rect, text);
    }
}

static void draw_info_row(App *app, SDL_Rect rect,
                          const char *label, const char *value) {
    SDL_Color label_col = {135, 150, 188, 255};
    SDL_Color value_col = {244, 247, 255, 255};

    if (!app || rect.w <= 0 || rect.h <= 0) return;

    SDL_SetRenderDrawColor(app->renderer, 18, 22, 36, 225);
    SDL_RenderFillRect(app->renderer, &rect);
    SDL_SetRenderDrawColor(app->renderer, 52, 66, 104, 255);
    SDL_RenderDrawRect(app->renderer, &rect);

    if (!app->font_regular) return;

    draw_text(app, app->font_regular, label, rect.x + 12, rect.y + 7, label_col);
    draw_text_fitted(app, app->font_bold ? app->font_bold : app->font_regular,
                     value, rect.x + 12, rect.y + 24, rect.w - 24, value_col);
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
    if (SDL_SetClipboardText(app->current_path) == 0)
        SDL_Log("Image path copied to clipboard: %s", app->current_path);
    else
        SDL_Log("Clipboard copy failed: %s", SDL_GetError());
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

    if (remove(path) != 0) {
        char err[MAX_PATH_LENGTH + 128];
        snprintf(err, sizeof(err), "Failed to delete \"%s\": %s",
                 fname, strerror(errno));
        SDL_Log("%s", err);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                 "Delete Failed", err, app->window);
        return;
    }

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
    SDL_SetRenderDrawColor(app->renderer, 12, 14, 24, 255);
    SDL_RenderClear(app->renderer);

    SDL_Rect canvas = get_canvas_rect(app);
    if (canvas.w > 0 && canvas.h > 0) {
        SDL_SetRenderDrawColor(app->renderer, 18, 20, 32, 255);
        SDL_RenderFillRect(app->renderer, &canvas);
        SDL_SetRenderDrawColor(app->renderer, 38, 44, 64, 255);
        SDL_RenderDrawRect(app->renderer, &canvas);
    }

    if (canvas.w <= 0 || canvas.h <= 0) return;

    if (!app->image_texture) {
        SDL_Rect empty = {
            canvas.x + clamp_int((canvas.w - 420) / 2, 18, canvas.w / 4),
            canvas.y + clamp_int((canvas.h - 156) / 2, 18, canvas.h / 3),
            clamp_int(canvas.w - 80, 280, 460),
            156
        };
        SDL_Color title_col = {238, 242, 255, 255};
        SDL_Color body_col = {149, 160, 198, 255};

        SDL_SetRenderDrawColor(app->renderer, 16, 19, 31, 238);
        SDL_RenderFillRect(app->renderer, &empty);
        SDL_SetRenderDrawColor(app->renderer, 78, 118, 255, 255);
        SDL_RenderDrawRect(app->renderer, &empty);

        SDL_Rect glow = {empty.x, empty.y, empty.w, 4};
        SDL_SetRenderDrawColor(app->renderer, 78, 118, 255, 255);
        SDL_RenderFillRect(app->renderer, &glow);

        if (app->font_regular) {
            draw_text(app, app->font_bold ? app->font_bold : app->font_regular,
                      "Drop an image or press Open", empty.x + 18, empty.y + 24,
                      title_col);
            draw_text_fitted(app, app->font_regular,
                             "Photon keeps things practical for ' THE ABSURDIST '.",
                             empty.x + 18, empty.y + 58, empty.w - 36, body_col);
            draw_text_fitted(app, app->font_regular,
                             "Drag files here, click Open, or use O to start browsing.",
                             empty.x + 18, empty.y + 84, empty.w - 36, body_col);
            draw_text_fitted(app, app->font_regular,
                             "Toolbar buttons are clickable now, including Info and Strip.",
                             empty.x + 18, empty.y + 110, empty.w - 36, body_col);
        }
        return;
    }

    SDL_Rect viewport = {
        canvas.x + 12,
        canvas.y + 12,
        canvas.w - 24,
        canvas.h - 24
    };
    if (viewport.w <= 0 || viewport.h <= 0) return;

    int eff_w = (app->rotation == 90 || app->rotation == 270)
                ? app->image_height : app->image_width;
    int eff_h = (app->rotation == 90 || app->rotation == 270)
                ? app->image_width  : app->image_height;

    SDL_Rect dest;
    if (app->fit_to_window) {
        float ar  = (float)eff_w / eff_h;
        float war = (float)viewport.w / viewport.h;
        if (ar > war) {
            dest.w = viewport.w;
            dest.h = (int)(viewport.w / ar);
            dest.x = viewport.x;
            dest.y = viewport.y + (viewport.h - dest.h) / 2;
        } else {
            dest.h = viewport.h;
            dest.w = (int)(viewport.h * ar);
            dest.x = viewport.x + (viewport.w - dest.w) / 2;
            dest.y = viewport.y;
        }
    } else {
        dest.w = (int)(eff_w * app->zoom);
        dest.h = (int)(eff_h * app->zoom);
        if (dest.w <= 0 || dest.h <= 0 || dest.w > 65536 || dest.h > 65536) return;
        dest.x = app->pan_x + viewport.x + (viewport.w - dest.w) / 2;
        dest.y = app->pan_y + viewport.y + (viewport.h - dest.h) / 2;
    }

    SDL_RenderSetClipRect(app->renderer, &viewport);
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 60);
    SDL_Rect shadow = {dest.x + 4, dest.y + 4, dest.w, dest.h};
    SDL_RenderFillRect(app->renderer, &shadow);

    SDL_RenderCopyEx(app->renderer, app->image_texture, NULL, &dest,
                     (double)app->rotation, NULL, SDL_FLIP_NONE);

    SDL_SetRenderDrawColor(app->renderer, 80, 80, 100, 255);
    SDL_RenderDrawRect(app->renderer, &dest);
    SDL_RenderSetClipRect(app->renderer, NULL);
}

void render_toolbar(App *app) {
    SDL_Rect bar = get_toolbar_rect(app);
    SDL_Color title_col = {245, 247, 255, 255};
    SDL_Color sub_col   = {142, 154, 192, 255};

    if (!app || bar.w <= 0 || bar.h <= 0) return;

    app->open_button_rect   = (SDL_Rect){0, 0, 0, 0};
    app->info_button_rect   = (SDL_Rect){0, 0, 0, 0};
    app->thumbs_button_rect = (SDL_Rect){0, 0, 0, 0};
    app->fit_button_rect    = (SDL_Rect){0, 0, 0, 0};
    app->actual_button_rect = (SDL_Rect){0, 0, 0, 0};

    SDL_SetRenderDrawColor(app->renderer, 16, 18, 30, 235);
    SDL_RenderFillRect(app->renderer, &bar);
    SDL_SetRenderDrawColor(app->renderer, 48, 58, 90, 255);
    SDL_RenderDrawRect(app->renderer, &bar);

    SDL_Rect accent = {bar.x, bar.y, 5, bar.h};
    SDL_SetRenderDrawColor(app->renderer, 78, 118, 255, 255);
    SDL_RenderFillRect(app->renderer, &accent);

    int btn_y = bar.y + (bar.h - TOOLBAR_BTN_H) / 2;
    int cursor_x = bar.x + bar.w - 12;
    int w = get_button_width(app->font_regular, "1:1");

    app->actual_button_rect = (SDL_Rect){cursor_x - w, btn_y, w, TOOLBAR_BTN_H};
    cursor_x = app->actual_button_rect.x - 8;

    w = get_button_width(app->font_regular, "Fit");
    app->fit_button_rect = (SDL_Rect){cursor_x - w, btn_y, w, TOOLBAR_BTN_H};
    cursor_x = app->fit_button_rect.x - 8;

    w = get_button_width(app->font_regular, "Strip");
    app->thumbs_button_rect = (SDL_Rect){cursor_x - w, btn_y, w, TOOLBAR_BTN_H};
    cursor_x = app->thumbs_button_rect.x - 8;

    w = get_button_width(app->font_regular, "Info");
    app->info_button_rect = (SDL_Rect){cursor_x - w, btn_y, w, TOOLBAR_BTN_H};
    cursor_x = app->info_button_rect.x - 8;

    w = get_button_width(app->font_regular, "Open");
    app->open_button_rect = (SDL_Rect){cursor_x - w, btn_y, w, TOOLBAR_BTN_H};

    draw_toolbar_button(app, app->actual_button_rect, "1:1",
                        !app->fit_to_window &&
                        app->zoom > 0.99f && app->zoom < 1.01f, 0);
    draw_toolbar_button(app, app->fit_button_rect, "Fit", app->fit_to_window, 0);
    draw_toolbar_button(app, app->thumbs_button_rect, "Strip", app->show_thumbnails, 0);
    draw_toolbar_button(app, app->info_button_rect, "Info", app->show_info, 0);
    draw_toolbar_button(app, app->open_button_rect, "Open", 1, 1);

    if (app->font_regular) {
        char title[256];
        char subtitle[512];
        int text_x = bar.x + 18;
        int text_w = app->open_button_rect.x - text_x - 18;

        if (app->current_path[0]) {
            secure_strncpy(title, filename_from_path(app->current_path), sizeof(title));
            snprintf(subtitle, sizeof(subtitle), "%d of %d  •  %s  •  %d x %d",
                     app->file_list.count > 0 ? app->file_list.current + 1 : 1,
                     app->file_list.count > 0 ? app->file_list.count : 1,
                     get_format_name(app->current_path),
                     app->image_width, app->image_height);
        } else {
            snprintf(title, sizeof(title), "Photon");
            snprintf(subtitle, sizeof(subtitle),
                     "Minimal image viewer for ' THE ABSURDIST '");
        }

        draw_text_fitted(app, app->font_bold ? app->font_bold : app->font_regular,
                         title, text_x, bar.y + 10, text_w, title_col);
        draw_text_fitted(app, app->font_regular, subtitle,
                         text_x, bar.y + 30, text_w, sub_col);
    }
}

void render_info_panel(App *app) {
    SDL_Rect panel = get_info_panel_rect(app);
    SDL_Color title_col  = {245, 247, 255, 255};
    SDL_Color text_col   = {162, 174, 208, 255};
    SDL_Color badge_text = {233, 239, 255, 255};

    if (!app || !app->show_info || panel.w <= 0 || panel.h <= 0) return;

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 12, 15, 26, 230);
    SDL_RenderFillRect(app->renderer, &panel);

    SDL_SetRenderDrawColor(app->renderer, 52, 66, 104, 255);
    SDL_RenderDrawRect(app->renderer, &panel);

    SDL_Rect header = {panel.x + 1, panel.y + 1, panel.w - 2, 88};
    SDL_SetRenderDrawColor(app->renderer, 19, 24, 40, 245);
    SDL_RenderFillRect(app->renderer, &header);

    SDL_Rect accent = {header.x, header.y, header.w, 4};
    SDL_SetRenderDrawColor(app->renderer, 78, 118, 255, 255);
    SDL_RenderFillRect(app->renderer, &accent);

    if (app->font_regular) {
        if (app->image_texture) {
            char folder[MAX_PATH_LENGTH];
            char format[32];
            int badge_w;

            directory_from_path(app->current_path, folder, sizeof(folder));
            snprintf(format, sizeof(format), "%s", get_format_name(app->current_path));

            draw_text(app, app->font_bold ? app->font_bold : app->font_regular,
                      "Image Info", header.x + INFO_PAD, header.y + 14,
                      (SDL_Color){126, 170, 255, 255});
            draw_text_fitted(app, app->font_bold ? app->font_bold : app->font_regular,
                             filename_from_path(app->current_path),
                             header.x + INFO_PAD, header.y + 38,
                             header.w - INFO_PAD * 2 - 74, title_col);
            draw_text_fitted(app, app->font_regular, folder,
                             header.x + INFO_PAD, header.y + 60,
                             header.w - INFO_PAD * 2, text_col);

            badge_w = clamp_int(text_width(app->font_regular, format) + 24, 58, 84);
            SDL_Rect badge = {header.x + header.w - badge_w - INFO_PAD,
                              header.y + 14, badge_w, 26};
            SDL_SetRenderDrawColor(app->renderer, 45, 66, 126, 255);
            SDL_RenderFillRect(app->renderer, &badge);
            SDL_SetRenderDrawColor(app->renderer, 86, 132, 255, 255);
            SDL_RenderDrawRect(app->renderer, &badge);
            draw_text_centered(app, app->font_regular, format, badge, badge_text);
        } else {
            draw_text(app, app->font_bold ? app->font_bold : app->font_regular,
                      "Image Info", header.x + INFO_PAD, header.y + 14,
                      (SDL_Color){126, 170, 255, 255});
            draw_text(app, app->font_bold ? app->font_bold : app->font_regular,
                      "No image selected", header.x + INFO_PAD, header.y + 42,
                      title_col);
            draw_text_fitted(app, app->font_regular,
                             "Open a file, drag one in, or let ' THE ABSURDIST ' choose the next mystery.",
                             header.x + INFO_PAD, header.y + 64,
                             header.w - INFO_PAD * 2, text_col);
        }
    }

    if (!app->image_texture) return;

    int y = header.y + header.h + 12;
    int row_w = panel.w - INFO_PAD * 2;
    char value[128];
    char modified[64];
    char folder[MAX_PATH_LENGTH];
    char aspect[64];

    snprintf(value, sizeof(value), "%d x %d px", app->image_width, app->image_height);
    draw_info_row(app, (SDL_Rect){panel.x + INFO_PAD, y, row_w, INFO_ROW_H},
                  "Dimensions", value);
    y += INFO_ROW_H + 10;

    snprintf(value, sizeof(value), "%s", format_file_size(app->current_file_size));
    draw_info_row(app, (SDL_Rect){panel.x + INFO_PAD, y, row_w, INFO_ROW_H},
                  "File Size", value);
    y += INFO_ROW_H + 10;

    if (app->fit_to_window) snprintf(value, sizeof(value), "Fit to window");
    else snprintf(value, sizeof(value), "%.0f%%", app->zoom * 100.f);
    draw_info_row(app, (SDL_Rect){panel.x + INFO_PAD, y, row_w, INFO_ROW_H},
                  "Zoom", value);
    y += INFO_ROW_H + 10;

    snprintf(value, sizeof(value), "%d deg", app->rotation);
    draw_info_row(app, (SDL_Rect){panel.x + INFO_PAD, y, row_w, INFO_ROW_H},
                  "Rotation", value);
    y += INFO_ROW_H + 10;

    snprintf(aspect, sizeof(aspect), "%.2f : 1",
             (float)app->image_width / (float)app->image_height);
    draw_info_row(app, (SDL_Rect){panel.x + INFO_PAD, y, row_w, INFO_ROW_H},
                  "Aspect", aspect);
    y += INFO_ROW_H + 10;

    if (app->current_mod_time > 0) {
        strftime(modified, sizeof(modified), "%Y-%m-%d %H:%M",
                 localtime(&app->current_mod_time));
    } else {
        secure_strncpy(modified, "Unknown", sizeof(modified));
    }
    draw_info_row(app, (SDL_Rect){panel.x + INFO_PAD, y, row_w, INFO_ROW_H},
                  "Modified", modified);
    y += INFO_ROW_H + 10;

    if (app->file_list.count > 0)
        snprintf(value, sizeof(value), "%d of %d",
                 app->file_list.current + 1, app->file_list.count);
    else
        snprintf(value, sizeof(value), "Standalone");
    draw_info_row(app, (SDL_Rect){panel.x + INFO_PAD, y, row_w, INFO_ROW_H},
                  "Position", value);
    y += INFO_ROW_H + 10;

    directory_from_path(app->current_path, folder, sizeof(folder));
    draw_info_row(app, (SDL_Rect){panel.x + INFO_PAD, y, row_w, INFO_ROW_H},
                  "Folder", folder);
}

void render_thumbnail_strip(App *app) {
    SDL_Rect strip = get_thumbnail_rect(app);
    if (!app || !app->show_thumbnails || app->file_list.count == 0 || strip.w <= 0) return;

    SDL_SetRenderDrawColor(app->renderer, 12, 12, 22, 230);
    SDL_RenderFillRect(app->renderer, &strip);

    SDL_SetRenderDrawColor(app->renderer, 55, 75, 125, 255);
    SDL_Rect sep = {strip.x, strip.y, strip.w, 2};
    SDL_RenderFillRect(app->renderer, &sep);
    SDL_SetRenderDrawColor(app->renderer, 48, 58, 90, 255);
    SDL_RenderDrawRect(app->renderer, &strip);

    int visible = (strip.w - THUMB_PAD * 2) / THUMB_SLOT_W;
    if (visible < 1) visible = 1;
    int start = app->file_list.current - visible / 2;
    if (start < 0) start = 0;
    if (start + visible > app->file_list.count)
        start = app->file_list.count - visible;
    if (start < 0) start = 0;

    int x = strip.x + THUMB_PAD;
    for (int i = start; i < start + visible && i < app->file_list.count; i++) {
        int cur = (i == app->file_list.current);

        if (cur) {
            SDL_SetRenderDrawColor(app->renderer, 70, 120, 255, 255);
            SDL_Rect hl = {x - 2, strip.y + THUMB_PAD - 2, THUMB_W + 4, THUMB_H + 4};
            SDL_RenderFillRect(app->renderer, &hl);
        }

        SDL_Rect slot = {x, strip.y + THUMB_PAD, THUMB_W, THUMB_H};
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
                dst.y = strip.y + THUMB_PAD + (THUMB_H - dst.h) / 2;
            } else {
                dst.h = THUMB_H;
                dst.w = (int)(THUMB_H * tar);
                dst.x = x + (THUMB_W - dst.w) / 2;
                dst.y = strip.y + THUMB_PAD;
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
    SDL_Rect bar = get_status_rect(app);
    if (!app || bar.w <= 0 || bar.h <= 0) return;

    SDL_SetRenderDrawColor(app->renderer, 15, 17, 28, 238);
    SDL_RenderFillRect(app->renderer, &bar);
    SDL_SetRenderDrawColor(app->renderer, 48, 58, 90, 255);
    SDL_RenderDrawRect(app->renderer, &bar);

    if (!app->font_regular) return;

#ifdef _WIN32
    static const char *copy_hint = "Ctrl+C copy image";
#else
    static const char *copy_hint = "Ctrl+C copy path";
#endif
    SDL_Color c = {122, 135, 176, 255};
    char summary[512];

    if (app->image_texture) {
        snprintf(summary, sizeof(summary),
                 "%s  •  R rotate  •  %s  •  Del delete  •  %s",
                 app->fit_to_window ? "Fit mode" : "Drag pan + scroll zoom",
                 copy_hint,
                 app->show_info ? "Info open" : "I opens info");
    } else {
        snprintf(summary, sizeof(summary),
                 "O open  •  drag files here  •  I info  •  T strip  •  %s",
                 copy_hint);
    }

    draw_text_fitted(app, app->font_regular, summary,
                     bar.x + 12, bar.y + 8, bar.w - 24, c);
}

void render(App *app) {
    render_image(app);
    render_toolbar(app);
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
                SDL_Rect thumbs_rect = get_thumbnail_rect(app);
                SDL_Rect canvas = get_canvas_rect(app);

                if (point_in_rect(ev.button.x, ev.button.y, &app->open_button_rect)) {
                    open_image(app);
                } else if (point_in_rect(ev.button.x, ev.button.y, &app->info_button_rect)) {
                    app->show_info = !app->show_info;
                } else if (point_in_rect(ev.button.x, ev.button.y, &app->thumbs_button_rect)) {
                    app->show_thumbnails = !app->show_thumbnails;
                } else if (point_in_rect(ev.button.x, ev.button.y, &app->fit_button_rect)) {
                    app->fit_to_window = 1;
                    app->zoom = 1.f;
                    app->pan_x = 0;
                    app->pan_y = 0;
                } else if (point_in_rect(ev.button.x, ev.button.y, &app->actual_button_rect)) {
                    app->fit_to_window = 0;
                    app->zoom = 1.f;
                    app->pan_x = 0;
                    app->pan_y = 0;
                } else if (app->show_thumbnails &&
                           point_in_rect(ev.button.x, ev.button.y, &thumbs_rect) &&
                           app->file_list.count > 0) {
                    int visible = (thumbs_rect.w - THUMB_PAD * 2) / THUMB_SLOT_W;
                    if (visible < 1) visible = 1;
                    int start = app->file_list.current - visible / 2;
                    if (start < 0) start = 0;
                    if (start + visible > app->file_list.count)
                        start = app->file_list.count - visible;
                    if (start < 0) start = 0;
                    int clicked = start +
                                (ev.button.x - (thumbs_rect.x + THUMB_PAD)) / THUMB_SLOT_W;
                    if (clicked >= 0 && clicked < app->file_list.count)
                        navigate_to(app, clicked);
                } else if (!app->image_texture && point_in_rect(ev.button.x, ev.button.y, &canvas)) {
                    open_image(app);
                } else if (app->image_texture && point_in_rect(ev.button.x, ev.button.y, &canvas)) {
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

    const char *font_path = find_font(app);
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
    int arg_idx = 1;

    while (arg_idx < argc) {
        if (strcmp(argv[arg_idx], "--font") == 0 && arg_idx + 1 < argc) {
            secure_strncpy(app.custom_font_path, argv[arg_idx + 1], MAX_PATH_LENGTH);
            arg_idx += 2;
        } else {
            break;
        }
    }

    if (!initialize_sdl(&app)) return 1;

#if !defined(_WIN32) && !defined(__APPLE__)
    integrate_desktop();
#endif

    if (arg_idx < argc) open_image_path(&app, argv[arg_idx]);
    else                open_image(&app);

    while (app.running) {
        handle_events(&app);
        render(&app);
        SDL_Delay(16);
    }
    cleanup(&app);
    return 0;
}
