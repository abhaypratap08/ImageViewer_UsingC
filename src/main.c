/* Enable POSIX extensions: strdup, popen, pclose */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <ctype.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include "motion.h"
#include "ui.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#define strcasecmp  _stricmp
#define PATH_SEP    '\\'
#else
#include <dirent.h>
#include <strings.h>
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

#define MOTION_RESPONSE 0.4f
#define MOTION_DAMPING   1.0f
#define MOMENTUM_DECAY   0.998f
#define MIN_ZOOM         0.05f
#define MAX_ZOOM         16.0f
#define DRAG_THRESHOLD   10

enum {
    BUTTON_OPEN = 0,
    BUTTON_INFO,
    BUTTON_THUMBS,
    BUTTON_FIT,
    BUTTON_ACTUAL,
    BUTTON_COUNT
};

#ifdef _WIN32
#ifndef PHOTON_TESTING
#undef main
#endif
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
    int          failed;
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
    float pan_x, pan_y;
    int   fit_to_window;
    int   show_info;
    int   show_thumbnails;
    int   rotation;
    int   is_panning;
    int   drag_start_x, drag_start_y;
    float pan_start_x, pan_start_y;
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
    PhotonUI *ui;
    PhotonSpring pan_spring_x;
    PhotonSpring pan_spring_y;
    PhotonVelocity pan_velocity_x;
    PhotonVelocity pan_velocity_y;
    Uint32 last_frame_ticks;
    int hover_button;
    int pressed_button;
    int focus_button;
    int drag_moved;
    int reduced_motion;
    int reduced_transparency;
    int high_contrast;
    int renderer_vsync;
    float text_scale;
    PhotonSpring info_spring;
    PhotonSpring thumbs_spring;
    int info_scroll;
    float pan_damping;
    int pressed_thumbnail;
    int pressed_empty;
    char feedback[160];
    Uint32 feedback_until;
    int thumb_loaded_this_frame;
} App;

static void cancel_pointer(App *app);

static void feedback(App *app, const char *message) {
    if (!app) return;
    snprintf(app->feedback, sizeof(app->feedback), "%s", message);
    app->feedback_until = SDL_GetTicks() + 5000;
}

static int ui_px(const App *app, int value) {
    return (int)lroundf(value * (app->text_scale >= 1 ? app->text_scale : 1));
}

static int font_height(TTF_Font *font, int fallback) {
    return font ? TTF_FontHeight(font) : fallback;
}

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
    {
        int r = snprintf(desktop_path, sizeof(desktop_path),
                         "%s/.local/share/applications/photon.desktop", home);
        if (r < 0 || r >= (int)sizeof(desktop_path)) return;
    }

    if (access(desktop_path, F_OK) == 0) return;

    /* Ensure directory exists — use mkdir() directly, never system() */
    char dir[MAX_PATH_LENGTH];
    {
        int r;
        r = snprintf(dir, sizeof(dir), "%s/.local", home);
        if (r < 0 || r >= (int)sizeof(dir)) return;
        mkdir(dir, 0755);
        r = snprintf(dir, sizeof(dir), "%s/.local/share", home);
        if (r < 0 || r >= (int)sizeof(dir)) return;
        mkdir(dir, 0755);
        r = snprintf(dir, sizeof(dir), "%s/.local/share/applications", home);
        if (r < 0 || r >= (int)sizeof(dir)) return;
        mkdir(dir, 0755);
    }

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
    static char detected_path[MAX_PATH_LENGTH];
    detected_path[0] = '\0';
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

static int ui_role_for_font(const App *app, TTF_Font *font) {
    if (!app || !app->ui || !font) return PHOTON_FONT_BODY;
    if (font == photon_ui_font(app->ui, PHOTON_FONT_LABEL))
        return PHOTON_FONT_LABEL;
    if (font == photon_ui_font(app->ui, PHOTON_FONT_TITLE))
        return PHOTON_FONT_TITLE;
    if (font == photon_ui_font(app->ui, PHOTON_FONT_SMALL))
        return PHOTON_FONT_SMALL;
    return PHOTON_FONT_BODY;
}

void draw_text(App *app, TTF_Font *font,
               const char *text, int x, int y, SDL_Color color) {
    if (app && app->ui) {
        int max_width = app->window_width - x + UI_MARGIN;
        if (max_width > 0)
            photon_ui_text(app->ui, ui_role_for_font(app, font), text,
                           x, y, max_width, color);
        return;
    }
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
    if (app->ui) {
        photon_ui_centered(app->ui, ui_role_for_font(app, font), text,
                           rect, color);
        return;
    }
    int w = 0, h = 0;
    if (TTF_SizeUTF8(font, text, &w, &h) != 0) return;
    draw_text(app, font, text,
              rect.x + (rect.w - w) / 2,
              rect.y + (rect.h - h) / 2, color);
}

static void draw_text_fitted(App *app, TTF_Font *font,
                             const char *text, int x, int y,
                             int max_w, SDL_Color color) {
    if (app && app->ui) {
        photon_ui_text(app->ui, ui_role_for_font(app, font), text,
                       x, y, max_w, color);
        return;
    }
    char clipped[512];
    fit_text_to_width(font, text, max_w, clipped, sizeof(clipped));
    if (clipped[0]) draw_text(app, font, clipped, x, y, color);
}

static const char* filename_from_path(const char *path) {
    const char *name;
    if (!path || !path[0]) return "No Image Selected";
    name = strrchr(path, PATH_SEP);
#ifdef _WIN32
    const char *slash = strrchr(path, '/');
    if (!name || (slash && slash > name)) name = slash;
#endif
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

static float presentation_progress(const PhotonSpring *spring) {
    return photon_clamp(spring->value, 0.0f, 1.0f);
}

static void set_info_visible(App *app, int visible) {
    if (!app) return;
    app->show_info = visible != 0;
    photon_spring_target(&app->info_spring, app->show_info ? 1.0f : 0.0f);
    if (app->reduced_motion)
        photon_spring_reset(&app->info_spring, app->info_spring.target);
}

static void set_thumbnails_visible(App *app, int visible) {
    if (!app) return;
    app->show_thumbnails = visible != 0;
    photon_spring_target(&app->thumbs_spring,
                         app->show_thumbnails ? 1.0f : 0.0f);
    if (app->reduced_motion)
        photon_spring_reset(&app->thumbs_spring, app->thumbs_spring.target);
}

static int get_info_panel_width(const App *app) {
    if (!app) return 0;
    return clamp_int(ui_px(app, 300), 180, app->window_width - UI_MARGIN * 2);
}

static const char *button_labels[BUTTON_COUNT] = {"Open", "Info", "Strip", "Fit", "1:1"};

/* One layout serves rendering and hit testing, including large text and resize. */
static SDL_Rect layout_toolbar(const App *app, SDL_Rect *buttons, SDL_Rect *title) {
    SDL_Rect bar = {UI_MARGIN, UI_MARGIN, app->window_width - UI_MARGIN * 2, 0};
    int widths[BUTTON_COUNT], total = 0, gap = ui_px(app, 6), pad = ui_px(app, 12);
    int height = font_height(app->font_bold, ui_px(app, 18)) + ui_px(app, 16);
    int title_height = font_height(app->font_bold, ui_px(app, 18)) +
                       font_height(photon_ui_font(app->ui, PHOTON_FONT_SMALL), ui_px(app, 16));
    int available = bar.w - pad * 2;
    for (int i = 0; i < BUTTON_COUNT; i++) {
        widths[i] = text_width(app->font_bold, button_labels[i]) + ui_px(app, 24);
        if (widths[i] < ui_px(app, 54)) widths[i] = ui_px(app, 54);
        total += widths[i] + (i ? gap : 0);
    }
    int compact = available < total + ui_px(app, 150);
    int x = compact ? bar.x + pad : bar.x + bar.w - pad - total;
    int y = bar.y + pad + (compact ? title_height + gap : 0);
    if (title) *title = (SDL_Rect){bar.x + pad, bar.y + pad,
                                 compact ? available : x - bar.x - pad - gap,
                                 title_height};
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (i && x + widths[i] > bar.x + bar.w - pad) {
            x = bar.x + pad;
            y += height + gap;
        }
        if (buttons) buttons[i] = (SDL_Rect){x, y, widths[i], height};
        x += widths[i] + gap;
    }
    bar.h = y + height + pad - bar.y;
    return bar;
}

static SDL_Rect get_toolbar_rect(const App *app) {
    return layout_toolbar(app, NULL, NULL);
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
    if (presentation_progress(&app->info_spring) <= 0.001f) return rect;
    rect.x = workspace.x + workspace.w - panel_w;
    rect.x += (int)lroundf((panel_w + UI_MARGIN) *
                          (1 - presentation_progress(&app->info_spring)));
    rect.y = workspace.y;
    rect.w = panel_w;
    rect.h = workspace.h;
    return rect;
}

static int get_content_right_offset(const App *app) {
    int panel_w = get_info_panel_width(app);
    /* In compact windows the inspector overlays instead of crushing the image. */
    if (app->window_width < panel_w + ui_px(app, 480)) return 0;
    return (int)lroundf((panel_w + UI_GAP) * presentation_progress(&app->info_spring));
}

static SDL_Rect get_thumbnail_rect(const App *app) {
    SDL_Rect workspace = get_workspace_rect(app);
    SDL_Rect rect = {0, 0, 0, 0};
    if (!app || app->file_list.count == 0 ||
        presentation_progress(&app->thumbs_spring) <= 0.001f) return rect;
    if (workspace.h < THUMB_STRIP_H + ui_px(app, 72)) return rect;
    rect.x = workspace.x;
    rect.w = workspace.w - get_content_right_offset(app);
    rect.h = THUMB_STRIP_H;
    rect.y = workspace.y + workspace.h - rect.h +
             (int)lroundf((rect.h + UI_MARGIN) *
                          (1 - presentation_progress(&app->thumbs_spring)));
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
    rect.h = font_height(photon_ui_font(app->ui, PHOTON_FONT_SMALL), 16) + ui_px(app, 16);
    rect.y = thumb.h > 0
           ? workspace.y + workspace.h - rect.h -
             (int)lroundf((THUMB_STRIP_H + UI_GAP) * presentation_progress(&app->thumbs_spring))
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

static SDL_Rect get_image_viewport(const App *app) {
    SDL_Rect canvas = get_canvas_rect(app);
    SDL_Rect viewport = {canvas.x + 12, canvas.y + 12,
                         canvas.w - 24, canvas.h - 24};
    if (viewport.w < 0) viewport.w = 0;
    if (viewport.h < 0) viewport.h = 0;
    return viewport;
}

static float get_fit_scale(const App *app, SDL_Rect viewport) {
    int eff_w, eff_h;
    float scale_x, scale_y;
    if (!app || app->image_width <= 0 || app->image_height <= 0 ||
        viewport.w <= 0 || viewport.h <= 0) return 1.0f;
    eff_w = (app->rotation == 90 || app->rotation == 270)
          ? app->image_height : app->image_width;
    eff_h = (app->rotation == 90 || app->rotation == 270)
          ? app->image_width : app->image_height;
    scale_x = (float)viewport.w / (float)eff_w;
    scale_y = (float)viewport.h / (float)eff_h;
    return scale_x < scale_y ? scale_x : scale_y;
}

static void get_image_size_at_zoom(const App *app, float zoom,
                                   int *width, int *height) {
    int eff_w, eff_h;
    if (!app || !width || !height) return;
    eff_w = (app->rotation == 90 || app->rotation == 270)
          ? app->image_height : app->image_width;
    eff_h = (app->rotation == 90 || app->rotation == 270)
          ? app->image_width : app->image_height;
    *width = (int)photon_clamp((float)eff_w * zoom, 1.0f, 65536.0f);
    *height = (int)photon_clamp((float)eff_h * zoom, 1.0f, 65536.0f);
}

static void get_pan_limits(const App *app, SDL_Rect viewport,
                           float *limit_x, float *limit_y) {
    int image_w = 0, image_h = 0;
    if (limit_x) *limit_x = 0.0f;
    if (limit_y) *limit_y = 0.0f;
    if (!app || !limit_x || !limit_y || viewport.w <= 0 || viewport.h <= 0)
        return;
    get_image_size_at_zoom(app, app->zoom, &image_w, &image_h);
    *limit_x = fmaxf(0.0f, ((float)image_w - viewport.w) * 0.5f);
    *limit_y = fmaxf(0.0f, ((float)image_h - viewport.h) * 0.5f);
}

static void reset_pan_motion(App *app, float x, float y) {
    if (!app) return;
    photon_spring_reset(&app->pan_spring_x, x);
    photon_spring_reset(&app->pan_spring_y, y);
    app->pan_x = x;
    app->pan_y = y;
}

static void retarget_pan_motion(App *app, float x, float y,
                                float velocity_x, float velocity_y) {
    if (!app) return;
    photon_spring_reset(&app->pan_spring_x, app->pan_x);
    photon_spring_reset(&app->pan_spring_y, app->pan_y);
    app->pan_spring_x.velocity = velocity_x;
    app->pan_spring_y.velocity = velocity_y;
    photon_spring_target(&app->pan_spring_x, x);
    photon_spring_target(&app->pan_spring_y, y);
}

static void set_fit_view(App *app) {
    if (!app) return;
    cancel_pointer(app);
    app->fit_to_window = 1;
    app->zoom = 1.0f;
    reset_pan_motion(app, 0.0f, 0.0f);
}

static void set_actual_view(App *app) {
    if (!app) return;
    cancel_pointer(app);
    app->fit_to_window = 0;
    app->zoom = 1.0f;
    reset_pan_motion(app, 0.0f, 0.0f);
}

static void update_pan_motion(App *app, float dt) {
    if (!app) return;
    photon_spring_step(&app->info_spring, dt, 0.30f, 1.0f,
                       app->reduced_motion);
    photon_spring_step(&app->thumbs_spring, dt, 0.30f, 1.0f,
                       app->reduced_motion);
    if (app->is_panning) return;
    SDL_Rect viewport = get_image_viewport(app);
    float limit_x, limit_y;
    get_pan_limits(app, viewport, &limit_x, &limit_y);
    photon_spring_target(&app->pan_spring_x,
                         photon_clamp(app->pan_spring_x.target, -limit_x, limit_x));
    photon_spring_target(&app->pan_spring_y,
                         photon_clamp(app->pan_spring_y.target, -limit_y, limit_y));
    photon_spring_step(&app->pan_spring_x, dt, MOTION_RESPONSE,
                       app->pan_damping, app->reduced_motion);
    photon_spring_step(&app->pan_spring_y, dt, MOTION_RESPONSE,
                       app->pan_damping, app->reduced_motion);
    app->pan_x = app->pan_spring_x.value;
    app->pan_y = app->pan_spring_y.value;
}

static void zoom_at(App *app, float factor, int x, int y) {
    SDL_Rect viewport;
    float old_zoom, new_zoom, center_x, center_y;
    float image_x, image_y, next_pan_x, next_pan_y;
    float limit_x, limit_y;
    if (!app || !app->image_texture || !(factor > 0.0f) ||
        !isfinite(factor)) return;
    viewport = get_image_viewport(app);
    if (viewport.w <= 0 || viewport.h <= 0) return;
    old_zoom = app->fit_to_window ? get_fit_scale(app, viewport) : app->zoom;
    if (!(old_zoom > 0.0f) || !isfinite(old_zoom)) old_zoom = 1.0f;
    float fit = get_fit_scale(app, viewport);
    float maximum = fminf(fmaxf(MAX_ZOOM, fit),
                          65536.0f / fmaxf(app->image_width, app->image_height));
    new_zoom = photon_clamp(old_zoom * factor, fminf(MIN_ZOOM, fit), maximum);
    center_x = viewport.x + viewport.w * 0.5f;
    center_y = viewport.y + viewport.h * 0.5f;
    image_x = ((float)x - center_x - (app->fit_to_window ? 0.0f : app->pan_x)) /
              old_zoom;
    image_y = ((float)y - center_y - (app->fit_to_window ? 0.0f : app->pan_y)) /
              old_zoom;
    next_pan_x = (float)x - center_x - image_x * new_zoom;
    next_pan_y = (float)y - center_y - image_y * new_zoom;
    app->fit_to_window = 0;
    app->zoom = new_zoom;
    get_pan_limits(app, viewport, &limit_x, &limit_y);
    next_pan_x = photon_clamp(next_pan_x, -limit_x, limit_x);
    next_pan_y = photon_clamp(next_pan_y, -limit_y, limit_y);
    reset_pan_motion(app, next_pan_x, next_pan_y);
    app->pan_damping = MOTION_DAMPING;
}

static void draw_toolbar_button(App *app, SDL_Rect rect, const char *label,
                                int button_id, int active, int primary,
                                int disabled) {
    if (!app || rect.w <= 0 || rect.h <= 0) return;
    photon_ui_button(app->ui, rect, label, active, primary,
                     app->hover_button == button_id,
                     app->pressed_button == button_id &&
                     app->hover_button == button_id,
                     app->focus_button == button_id, disabled);
}

static void draw_info_row(App *app, SDL_Rect rect,
                          const char *label, const char *value) {
    SDL_Color label_col = {180, 185, 197, 255};
    SDL_Color value_col = {242, 243, 247, 255};

    if (!app || rect.w <= 0 || rect.h <= 0) return;

    if (!app->font_regular) return;
    photon_ui_text(app->ui, PHOTON_FONT_SMALL, label, rect.x, rect.y,
                   rect.w, label_col);
    draw_text_fitted(app, app->font_bold ? app->font_bold : app->font_regular,
                     value, rect.x, rect.y +
                     font_height(photon_ui_font(app->ui, PHOTON_FONT_SMALL), 16) + ui_px(app, 2),
                     rect.w, value_col);
}

static int info_header_height(const App *app) {
    return ui_px(app, 20) + font_height(app->font_bold, ui_px(app, 18)) * 2 +
           font_height(photon_ui_font(app->ui, PHOTON_FONT_SMALL), ui_px(app, 16));
}

static int info_row_height(const App *app) {
    return font_height(app->font_bold, ui_px(app, 18)) +
           font_height(photon_ui_font(app->ui, PHOTON_FONT_SMALL), ui_px(app, 16)) + ui_px(app, 16);
}

static int info_scroll_limit(const App *app) {
    SDL_Rect panel = get_info_panel_rect(app);
    int height = panel.h - info_header_height(app) - ui_px(app, 24);
    return app->image_texture ? (int)fmaxf(0, info_row_height(app) * 8 - height) : 0;
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
    if (sep == dir) sep[1] = '\0';
#ifdef _WIN32
    else if (sep == dir + 2 && dir[1] == ':') sep[1] = '\0';
#endif
    else if (sep) *sep = '\0';
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
        int matches = strcasecmp(filename_from_path(list->paths[i]), filename_from_path(filepath)) == 0;
#else
        int matches = strcmp(filename_from_path(list->paths[i]), filename_from_path(filepath)) == 0;
#endif
        if (matches) { list->current = i; return 1; }
    }
    /* If enumeration was capped or the file disappeared, never associate the
     * displayed image with an unrelated selection (especially for Delete). */
    free_file_list(list);
    return 0;
}

// ── Thumbnail cache ───────────────────────────────────────────────────────────
void free_thumb_cache(App *app) {
    for (int i = 0; i < THUMB_CACHE_MAX; i++) {
        if (app->thumb_cache[i].tex) {
            SDL_DestroyTexture(app->thumb_cache[i].tex);
            app->thumb_cache[i].tex = NULL;
        }
        app->thumb_cache[i].path[0] = '\0';
        app->thumb_cache[i].failed = 0;
    }
}

SDL_Texture* get_thumb(App *app, int index) {
    if (index < 0 || index >= app->file_list.count) return NULL;
    const char *path = app->file_list.paths[index];

    for (int i = 0; i < THUMB_CACHE_MAX; i++)
        if (app->thumb_cache[i].path[0] && strcmp(app->thumb_cache[i].path, path) == 0)
            return app->thumb_cache[i].tex;

    /* Bound decoding work per frame, including corrupt files. Cached misses
     * prevent repeated attempts from blocking pointer feedback. */
    if (app->thumb_loaded_this_frame || app->is_panning ||
        app->pressed_button >= 0 || app->pressed_thumbnail >= 0) return NULL;
    app->thumb_loaded_this_frame = 1;

    int slot = -1;
    for (int i = 0; i < THUMB_CACHE_MAX; i++)
        if (!app->thumb_cache[i].path[0]) { slot = i; break; }
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

    secure_strncpy(app->thumb_cache[slot].path, path, MAX_PATH_LENGTH);
    app->thumb_cache[slot].failed = 1;
    struct stat st;
    if (stat(path, &st) != 0 || validate_image_size(st.st_size) != SECURITY_OK) return NULL;
    SDL_Surface *full = IMG_Load(path);
    if (!full) return NULL;
    if (full->w <= 0 || full->h <= 0 || full->w > 32768 || full->h > 32768) {
        SDL_FreeSurface(full);
        return NULL;
    }

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
    app->thumb_cache[slot].failed = 0;
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

    SDL_Texture *next_texture = SDL_CreateTextureFromSurface(app->renderer, surf);
    if (!next_texture) { SDL_FreeSurface(surf); return 0; }
    if (app->image_texture) {
        SDL_DestroyTexture(app->image_texture);
    }
    app->image_texture     = next_texture;
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
    const char *path = app->file_list.paths[index];
    if (load_image(app, path)) {
        app->file_list.current = index;
        secure_strncpy(app->current_path, path, sizeof(app->current_path));
        set_fit_view(app);
        app->rotation = 0;
        app->info_scroll = 0;
        app->feedback[0] = '\0';
        update_window_title(app);
    } else {
        feedback(app, "Couldn't open that image. The current image is unchanged.");
    }
}

void navigate_image(App *app, int dir) {
    if (!app || app->file_list.count <= 1) return;
    navigate_to(app, app->file_list.current + dir);
}

// ── Open ──────────────────────────────────────────────────────────────────────
void open_image_path(App *app, const char *path) {
    if (!app || !path) return;
    if (validate_filepath(path) != SECURITY_OK || !is_image_file(path)) {
        feedback(app, "Choose a PNG, JPEG, BMP, GIF, TGA or WebP image.");
        return;
    }
    if (load_image(app, path)) {
        secure_strncpy(app->current_path, path, sizeof(app->current_path));
        free_file_list(&app->file_list);
        free_thumb_cache(app);
        scan_folder(path, &app->file_list);
        set_fit_view(app);
        app->rotation = 0;
        app->info_scroll = 0;
        app->feedback[0] = '\0';
        update_window_title(app);
    } else {
        feedback(app, "Couldn't open the image. Check its format, size and permissions.");
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
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Cancel"},
        {0, 1, "Delete"},
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
    SDL_SetRenderDrawColor(app->renderer, 18, 19, 23, 255);
    SDL_RenderClear(app->renderer);

    SDL_Rect canvas = get_canvas_rect(app);
    if (canvas.w <= 0 || canvas.h <= 0) return;

    if (!app->image_texture) {
        int title_h = font_height(photon_ui_font(app->ui, PHOTON_FONT_TITLE), ui_px(app, 28));
        int body_h = font_height(app->font_regular, ui_px(app, 18));
        int y = canvas.y + (canvas.h - title_h - body_h * 2 - ui_px(app, 12)) / 2;
        SDL_RenderSetClipRect(app->renderer, &canvas);
        draw_text_centered(app, photon_ui_font(app->ui, PHOTON_FONT_TITLE),
                           "Your images. Nothing else.",
                           (SDL_Rect){canvas.x, y, canvas.w, title_h},
                           (SDL_Color){242, 243, 247, 255});
        y += title_h + ui_px(app, 12);
        draw_text_centered(app, app->font_regular, "Drop an image here, or choose Open.",
                           (SDL_Rect){canvas.x, y, canvas.w, body_h},
                           (SDL_Color){180, 185, 197, 255});
        draw_text_centered(app, photon_ui_font(app->ui, PHOTON_FONT_SMALL),
                           "Browse a whole folder with the arrow keys.",
                           (SDL_Rect){canvas.x, y + body_h, canvas.w, body_h},
                           (SDL_Color){180, 185, 197, 255});
        SDL_RenderSetClipRect(app->renderer, NULL);
        return;
    }

    SDL_Rect viewport = get_image_viewport(app);
    if (viewport.w <= 0 || viewport.h <= 0) return;

    float scale = app->fit_to_window ? get_fit_scale(app, viewport) : app->zoom;
    float width = app->image_width * scale, height = app->image_height * scale;
    float x = viewport.x + (viewport.w - width) * 0.5f;
    float y = viewport.y + (viewport.h - height) * 0.5f;
    if (!app->fit_to_window) { x += app->pan_x; y += app->pan_y; }
    /* Rotate the original aspect ratio, not the already-swapped bounding box.
     * The image may travel underneath the floating chrome while panning. */
#if SDL_VERSION_ATLEAST(2, 0, 10)
    SDL_FRect dest = {x, y, width, height};
    SDL_RenderCopyExF(app->renderer, app->image_texture, NULL, &dest,
                      app->rotation, NULL, SDL_FLIP_NONE);
#else
    SDL_Rect dest = {(int)lroundf(x), (int)lroundf(y),
                     (int)fmaxf(1, width), (int)fmaxf(1, height)};
    SDL_RenderCopyEx(app->renderer, app->image_texture, NULL, &dest,
                     app->rotation, NULL, SDL_FLIP_NONE);
#endif
}

void render_toolbar(App *app) {
    SDL_Rect buttons[BUTTON_COUNT], title_rect;
    SDL_Rect bar = layout_toolbar(app, buttons, &title_rect);
    SDL_Color title_col = {242, 243, 247, 255};
    SDL_Color sub_col   = {180, 185, 197, 255};

    if (!app || bar.w <= 0 || bar.h <= 0) return;

    app->open_button_rect   = buttons[BUTTON_OPEN];
    app->info_button_rect   = buttons[BUTTON_INFO];
    app->thumbs_button_rect = buttons[BUTTON_THUMBS];
    app->fit_button_rect    = buttons[BUTTON_FIT];
    app->actual_button_rect = buttons[BUTTON_ACTUAL];

    photon_ui_surface(app->ui, bar, 16, 1, 1.0f);

    draw_toolbar_button(app, app->actual_button_rect, "1:1", BUTTON_ACTUAL,
                        !app->fit_to_window &&
                        app->zoom > 0.99f && app->zoom < 1.01f, 0,
                        !app->image_texture);
    draw_toolbar_button(app, app->fit_button_rect, "Fit", BUTTON_FIT,
                        app->fit_to_window, 0, !app->image_texture);
    draw_toolbar_button(app, app->thumbs_button_rect, "Strip", BUTTON_THUMBS,
                        app->show_thumbnails, 0, app->file_list.count == 0);
    draw_toolbar_button(app, app->info_button_rect, "Info", BUTTON_INFO,
                        app->show_info, 0, 0);
    draw_toolbar_button(app, app->open_button_rect, "Open", BUTTON_OPEN,
                        1, 1, 0);

    if (app->font_regular) {
        const char *title = app->current_path[0] ? filename_from_path(app->current_path) : "Photon";
        char subtitle[512];

        if (app->current_path[0]) {
            snprintf(subtitle, sizeof(subtitle), "%d of %d  •  %s  •  %d x %d",
                     app->file_list.count > 0 ? app->file_list.current + 1 : 1,
                     app->file_list.count > 0 ? app->file_list.count : 1,
                     get_format_name(app->current_path),
                     app->image_width, app->image_height);
        } else {
            snprintf(subtitle, sizeof(subtitle), "A little space for your images");
        }

        draw_text_fitted(app, app->font_bold ? app->font_bold : app->font_regular,
                         title, title_rect.x, title_rect.y, title_rect.w, title_col);
        photon_ui_text(app->ui, PHOTON_FONT_SMALL, subtitle, title_rect.x,
                       title_rect.y + font_height(app->font_bold, ui_px(app, 18)),
                       title_rect.w, sub_col);
    }
}

void render_info_panel(App *app) {
    SDL_Rect panel = get_info_panel_rect(app);
    SDL_Rect previous_clip;
    SDL_bool had_clip;
    SDL_Color title_col  = {242, 243, 247, 255};
    SDL_Color text_col   = {180, 185, 197, 255};

    if (!app || panel.w <= 0 || panel.h <= 0) return;
    had_clip = SDL_RenderIsClipEnabled(app->renderer);
    SDL_RenderGetClipRect(app->renderer, &previous_clip);
    SDL_RenderSetClipRect(app->renderer, &panel);

    photon_ui_surface(app->ui, panel, 16, 1, 1.0f);

    int pad = ui_px(app, 18), line = font_height(app->font_bold, ui_px(app, 18));
    SDL_Rect header = {panel.x + pad, panel.y + pad, panel.w - pad * 2,
                       info_header_height(app)};
    photon_ui_text(app->ui, PHOTON_FONT_LABEL, "Image info", header.x,
                   header.y, header.w, title_col);
    photon_ui_text(app->ui, PHOTON_FONT_BODY, filename_from_path(app->current_path),
                   header.x, header.y + line + ui_px(app, 6), header.w, text_col);
    photon_ui_text(app->ui, PHOTON_FONT_SMALL,
                   app->image_texture ? get_format_name(app->current_path) : "Open an image to see its details.",
                   header.x, header.y + line * 2 + ui_px(app, 6), header.w, text_col);

    if (!app->image_texture) {
        SDL_RenderSetClipRect(app->renderer, had_clip ? &previous_clip : NULL);
        return;
    }

    int row_height = info_row_height(app);
    SDL_Rect content = {header.x, panel.y + header.h + ui_px(app, 12),
                        header.w, panel.h - header.h - ui_px(app, 24)};
    if (content.h <= 0) {
        SDL_RenderSetClipRect(app->renderer, had_clip ? &previous_clip : NULL);
        return;
    }
    app->info_scroll = clamp_int(app->info_scroll, 0, info_scroll_limit(app));
    SDL_RenderSetClipRect(app->renderer, &content);
    int y = content.y - app->info_scroll;
    int row_w = content.w;
    char value[128];
    char modified[64];
    char folder[MAX_PATH_LENGTH];
    char aspect[64];

    snprintf(value, sizeof(value), "%d x %d px", app->image_width, app->image_height);
    draw_info_row(app, (SDL_Rect){content.x, y, row_w, row_height},
                  "Dimensions", value);
    y += row_height;

    snprintf(value, sizeof(value), "%s", format_file_size(app->current_file_size));
    draw_info_row(app, (SDL_Rect){content.x, y, row_w, row_height},
                  "File Size", value);
    y += row_height;

    if (app->fit_to_window) snprintf(value, sizeof(value), "Fit to window");
    else snprintf(value, sizeof(value), "%.0f%%", app->zoom * 100.f);
    draw_info_row(app, (SDL_Rect){content.x, y, row_w, row_height},
                  "Zoom", value);
    y += row_height;

    snprintf(value, sizeof(value), "%d deg", app->rotation);
    draw_info_row(app, (SDL_Rect){content.x, y, row_w, row_height},
                  "Rotation", value);
    y += row_height;

    snprintf(aspect, sizeof(aspect), "%.2f : 1",
             (float)app->image_width / (float)app->image_height);
    draw_info_row(app, (SDL_Rect){content.x, y, row_w, row_height},
                  "Aspect", aspect);
    y += row_height;

    if (app->current_mod_time > 0) {
        strftime(modified, sizeof(modified), "%Y-%m-%d %H:%M",
                 localtime(&app->current_mod_time));
    } else {
        secure_strncpy(modified, "Unknown", sizeof(modified));
    }
    draw_info_row(app, (SDL_Rect){content.x, y, row_w, row_height},
                  "Modified", modified);
    y += row_height;

    if (app->file_list.count > 0)
        snprintf(value, sizeof(value), "%d of %d",
                 app->file_list.current + 1, app->file_list.count);
    else
        snprintf(value, sizeof(value), "Standalone");
    draw_info_row(app, (SDL_Rect){content.x, y, row_w, row_height},
                  "Position", value);
    y += row_height;

    directory_from_path(app->current_path, folder, sizeof(folder));
    draw_info_row(app, (SDL_Rect){content.x, y, row_w, row_height},
                  "Folder", folder);
    SDL_RenderSetClipRect(app->renderer, had_clip ? &previous_clip : NULL);
    if (info_scroll_limit(app) > 0) {
        int travel = content.h - ui_px(app, 28);
        SDL_Rect thumb = {panel.x + panel.w - ui_px(app, 7),
                          content.y + (int)((float)travel * app->info_scroll / info_scroll_limit(app)),
                          ui_px(app, 3), ui_px(app, 28)};
        photon_ui_round_rect(app->renderer, thumb, 2, text_col);
    }
}

void render_thumbnail_strip(App *app) {
    SDL_Rect strip = get_thumbnail_rect(app);
    SDL_Rect visual;
    SDL_Rect previous_clip;
    SDL_bool had_clip;
    float progress;
    if (!app || app->file_list.count == 0 || strip.w <= 0) return;

    progress = presentation_progress(&app->thumbs_spring);
    if (progress <= 0.001f) return;
    visual = strip;
    had_clip = SDL_RenderIsClipEnabled(app->renderer);
    SDL_RenderGetClipRect(app->renderer, &previous_clip);
    SDL_RenderSetClipRect(app->renderer, &strip);
    photon_ui_surface(app->ui, visual, 14, 0, 0.98f);

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
            SDL_Rect hl = {x - 2, visual.y + THUMB_PAD - 2, THUMB_W + 4, THUMB_H + 4};
            photon_ui_round_rect(app->renderer, hl, 9,
                                 (SDL_Color){64, 112, 190, 255});
        }

        SDL_Rect slot = {x, visual.y + THUMB_PAD, THUMB_W, THUMB_H};
        photon_ui_round_rect(app->renderer, slot, 7,
                             (SDL_Color){28, 29, 35, 255});

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
                dst.y = visual.y + THUMB_PAD + (THUMB_H - dst.h) / 2;
            } else {
                dst.h = THUMB_H;
                dst.w = (int)(THUMB_H * tar);
                dst.x = x + (THUMB_W - dst.w) / 2;
                dst.y = visual.y + THUMB_PAD;
            }
            SDL_RenderCopy(app->renderer, tex, NULL, &dst);
        }
        if (app->pressed_thumbnail == i)
            photon_ui_round_rect(app->renderer, slot, 7, (SDL_Color){255, 255, 255, 40});

        x += THUMB_SLOT_W;
    }
    SDL_RenderSetClipRect(app->renderer, had_clip ? &previous_clip : NULL);
}

void render_hint_bar(App *app) {
    SDL_Rect bar = get_status_rect(app);
    if (!app || bar.w <= 0 || bar.h <= 0) return;

    photon_ui_surface(app->ui, bar, 12, 0, 0.96f);

    if (!app->font_regular) return;

#ifdef _WIN32
    static const char *copy_hint = "Ctrl+C copy image";
#else
    static const char *copy_hint = "Ctrl+C copy path";
#endif
    SDL_Color c = {180, 185, 197, 255};
    char summary[512];

    if (app->image_texture) {
        snprintf(summary, sizeof(summary),
                  "%s  •  R rotate  •  %s  •  Del delete  •  %s  •  Ctrl+Shift+H contrast",
                 app->fit_to_window ? "Fit mode" : "Drag pan + scroll zoom",
                 copy_hint,
                 app->show_info ? "Info open" : "I opens info");
    } else {
        snprintf(summary, sizeof(summary),
                  "O open  •  drag files here  •  I info  •  T strip  •  %s  •  Tab focus",
                 copy_hint);
    }

    const char *text = app->feedback[0] && (Sint32)(app->feedback_until - SDL_GetTicks()) > 0
                     ? app->feedback : summary;
    photon_ui_text(app->ui, PHOTON_FONT_SMALL, text,
                   bar.x + ui_px(app, 12), bar.y + ui_px(app, 8),
                   bar.w - ui_px(app, 24), c);
}

void render(App *app) {
    int width, height;
    if (SDL_GetRendererOutputSize(app->renderer, &width, &height) == 0 &&
        app->window_width > 0 && app->window_height > 0)
        SDL_RenderSetScale(app->renderer, (float)width / app->window_width,
                           (float)height / app->window_height);
    app->thumb_loaded_this_frame = 0;
    render_image(app);
    render_toolbar(app);
    render_thumbnail_strip(app);
    render_hint_bar(app);
    render_info_panel(app);
    SDL_RenderPresent(app->renderer);
}

// ── Events ────────────────────────────────────────────────────────────────────
static SDL_Rect button_rect_for(const App *app, int button) {
    SDL_Rect buttons[BUTTON_COUNT];
    if (!app || button < 0 || button >= BUTTON_COUNT) return (SDL_Rect){0, 0, 0, 0};
    layout_toolbar(app, buttons, NULL);
    return buttons[button];
}

static int button_disabled(const App *app, int button) {
    if (!app) return 1;
    if (button == BUTTON_FIT || button == BUTTON_ACTUAL)
        return !app->image_texture;
    if (button == BUTTON_THUMBS)
        return app->file_list.count == 0;
    return 0;
}

static void apply_accessibility_preferences(App *app) {
    if (!app) return;
    photon_ui_preferences(app->ui, app->reduced_transparency,
                          app->high_contrast);
}

static int environment_flag(const char *name) {
    const char *value = getenv(name);
    return value && (strcmp(value, "1") == 0 ||
                     strcasecmp(value, "true") == 0 ||
                     strcasecmp(value, "yes") == 0);
}

static void activate_button(App *app, int button) {
    if (!app || button < 0 || button >= BUTTON_COUNT ||
        button_disabled(app, button)) return;
    app->focus_button = button;
    switch (button) {
        case BUTTON_OPEN:
            open_image(app);
            break;
        case BUTTON_INFO:
            set_info_visible(app, !app->show_info);
            break;
        case BUTTON_THUMBS:
            set_thumbnails_visible(app, !app->show_thumbnails);
            break;
        case BUTTON_FIT:
            set_fit_view(app);
            break;
        case BUTTON_ACTUAL:
            set_actual_view(app);
            break;
        default:
            break;
    }
}

static void update_hover_button(App *app, int x, int y) {
    if (!app) return;
    app->hover_button = -1;
    if (app->pressed_button >= 0) {
        SDL_Rect rect = button_rect_for(app, app->pressed_button);
        rect.x -= 8; rect.y -= 8; rect.w += 16; rect.h += 16;
        if (point_in_rect(x, y, &rect)) {
            app->hover_button = app->pressed_button;
            return;
        }
    }
    for (int button = 0; button < BUTTON_COUNT; ++button) {
        SDL_Rect rect = button_rect_for(app, button);
        if (!button_disabled(app, button) && point_in_rect(x, y, &rect)) {
            app->hover_button = button;
            break;
        }
    }
}

static void finish_pan(App *app, double now) {
    SDL_Rect viewport;
    float limit_x, limit_y, velocity_x, velocity_y;
    float target_x, target_y;
    if (!app) return;
    viewport = get_image_viewport(app);
    get_pan_limits(app, viewport, &limit_x, &limit_y);
    velocity_x = photon_clamp(photon_velocity_get(&app->pan_velocity_x, now), -8000, 8000);
    velocity_y = photon_clamp(photon_velocity_get(&app->pan_velocity_y, now), -8000, 8000);
    app->pan_damping = fabsf(velocity_x) + fabsf(velocity_y) > 20 ? 0.82f : 1.0f;
    target_x = photon_clamp(app->pan_x + photon_project(velocity_x, MOMENTUM_DECAY),
                            -limit_x, limit_x);
    target_y = photon_clamp(app->pan_y + photon_project(velocity_y, MOMENTUM_DECAY),
                            -limit_y, limit_y);
    if (app->reduced_motion) {
        /* Reduced motion has no inertial travel or projected jump on release. */
        reset_pan_motion(app, photon_clamp(app->pan_x, -limit_x, limit_x),
                         photon_clamp(app->pan_y, -limit_y, limit_y));
    } else {
        retarget_pan_motion(app, target_x, target_y, velocity_x, velocity_y);
    }
}

static int thumbnail_at(App *app, int x, int y) {
    SDL_Rect thumbs_rect;
    int visible, start, clicked;
    if (!app || app->file_list.count == 0) return -1;
    thumbs_rect = get_thumbnail_rect(app);
    if (!point_in_rect(x, y, &thumbs_rect)) return -1;
    visible = (thumbs_rect.w - THUMB_PAD * 2) / THUMB_SLOT_W;
    if (visible < 1) visible = 1;
    start = app->file_list.current - visible / 2;
    if (start < 0) start = 0;
    if (start + visible > app->file_list.count)
        start = app->file_list.count - visible;
    if (start < 0) start = 0;
    int local_x = x - thumbs_rect.x - THUMB_PAD;
    int local_y = y - thumbs_rect.y - THUMB_PAD;
    if (local_x < 0 || local_y < 0 || local_y >= THUMB_H ||
        local_x % THUMB_SLOT_W >= THUMB_W || local_x / THUMB_SLOT_W >= visible) return -1;
    clicked = start + local_x / THUMB_SLOT_W;
    return clicked >= 0 && clicked < app->file_list.count ? clicked : -1;
}

/* Undo boundary compression when grabbing a spring outside the resting bounds.
 * Reapplying rubber-banding to an already-compressed value would visibly jump. */
static float unbound_drag(float position, float limit, float dimension) {
    float over = fabsf(position) - limit;
    if (over <= 0 || dimension <= 0) return position;
    float raw = over * dimension / (0.55f * fmaxf(1, dimension - over));
    return copysignf(limit + raw, position);
}

static void cancel_pointer(App *app) {
    app->is_panning = app->pressed_empty = 0;
    app->pressed_button = app->pressed_thumbnail = -1;
    SDL_CaptureMouse(SDL_FALSE);
}

static void handle_event(App *app, SDL_Event ev) {
        switch (ev.type) {

        case SDL_QUIT:
            app->running = 0;
            break;

        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                cancel_pointer(app);
                app->hover_button = -1;
                reset_pan_motion(app, app->pan_x, app->pan_y);
            }
            if (ev.window.event == SDL_WINDOWEVENT_RESIZED ||
                ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                cancel_pointer(app);
                app->window_width  = ev.window.data1;
                app->window_height = ev.window.data2;
                if (!app->is_panning) {
                    SDL_Rect viewport = get_image_viewport(app);
                    float limit_x, limit_y;
                    get_pan_limits(app, viewport, &limit_x, &limit_y);
                    photon_spring_target(&app->pan_spring_x,
                                         photon_clamp(app->pan_x, -limit_x, limit_x));
                    photon_spring_target(&app->pan_spring_y,
                                         photon_clamp(app->pan_y, -limit_y, limit_y));
                }
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
            if (ev.key.repeat && ev.key.keysym.sym != SDLK_PLUS &&
                ev.key.keysym.sym != SDLK_EQUALS && ev.key.keysym.sym != SDLK_MINUS &&
                ev.key.keysym.sym != SDLK_LEFT && ev.key.keysym.sym != SDLK_RIGHT &&
                ev.key.keysym.sym != SDLK_PAGEDOWN && ev.key.keysym.sym != SDLK_PAGEUP) break;
            if (ctrl && shift && ev.key.keysym.sym == SDLK_m) {
                app->reduced_motion = !app->reduced_motion;
                if (app->reduced_motion) {
                    cancel_pointer(app);
                    reset_pan_motion(app, app->pan_x, app->pan_y);
                }
                feedback(app, app->reduced_motion ? "Reduced motion on" : "Reduced motion off");
                apply_accessibility_preferences(app);
                break;
            }
            if (ctrl && shift && ev.key.keysym.sym == SDLK_t) {
                app->reduced_transparency = !app->reduced_transparency;
                feedback(app, app->reduced_transparency ? "Solid materials on" : "Solid materials off");
                apply_accessibility_preferences(app);
                break;
            }
            if (ctrl && shift && ev.key.keysym.sym == SDLK_h) {
                app->high_contrast = !app->high_contrast;
                feedback(app, app->high_contrast ? "High contrast on" : "High contrast off");
                apply_accessibility_preferences(app);
                break;
            }
            switch (ev.key.keysym.sym) {
                case SDLK_F1:
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Photon controls",
                        "O: Open    Left / Right: Browse    R / Shift+R: Rotate\n"
                        "Scroll / + / -: Zoom    F: Fit    1: Actual size\n"
                        "I: Info    T: Strip    Page Up / Down: Scroll info\n"
                        "Tab / Shift+Tab: Focus    Enter / Space: Activate\n"
                        "Ctrl+C: Copy    Delete: Delete with confirmation\n\n"
                        "Ctrl+Shift+M: Reduced motion\nCtrl+Shift+T: Solid materials\n"
                        "Ctrl+Shift+H: High contrast\n"
                        "PHOTON_TEXT_SCALE=1.25: Larger text at startup", app->window);
                    break;
                case SDLK_PAGEUP:
                case SDLK_PAGEDOWN:
                    if (app->show_info)
                        app->info_scroll = clamp_int(app->info_scroll +
                            (ev.key.keysym.sym == SDLK_PAGEUP ? -1 : 1) * info_row_height(app) * 3,
                            0, info_scroll_limit(app));
                    break;
                case SDLK_ESCAPE:
                    app->running = 0;
                    break;
                case SDLK_TAB:
                    for (int tries = 0; tries < BUTTON_COUNT; ++tries) {
                        int next = app->focus_button + (shift ? -1 : 1);
                        if (next < 0) next = BUTTON_COUNT - 1;
                        if (next >= BUTTON_COUNT) next = 0;
                        app->focus_button = next;
                        if (!button_disabled(app, next)) break;
                    }
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                    activate_button(app, app->focus_button);
                    break;
                case SDLK_PLUS:
                case SDLK_EQUALS: {
                    SDL_Rect viewport = get_image_viewport(app);
                    cancel_pointer(app);
                    zoom_at(app, 1.2f, viewport.x + viewport.w / 2,
                            viewport.y + viewport.h / 2);
                    break;
                }
                case SDLK_MINUS: {
                    SDL_Rect viewport = get_image_viewport(app);
                    cancel_pointer(app);
                    zoom_at(app, 1.0f / 1.2f, viewport.x + viewport.w / 2,
                            viewport.y + viewport.h / 2);
                    break;
                }
                case SDLK_f:
                    set_fit_view(app);
                    break;
                case SDLK_1:
                    set_actual_view(app);
                    break;
                case SDLK_i:
                    set_info_visible(app, !app->show_info);
                    break;
                case SDLK_t:
                    set_thumbnails_visible(app, !app->show_thumbnails);
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
                    cancel_pointer(app);
                    app->rotation = (app->rotation + (shift ? 270 : 90)) % 360;
                    reset_pan_motion(app, 0.0f, 0.0f);
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
                SDL_Rect canvas = get_canvas_rect(app);
                SDL_Rect panel = get_info_panel_rect(app);
                cancel_pointer(app);
                update_hover_button(app, ev.button.x, ev.button.y);
                for (int button = 0; button < BUTTON_COUNT; ++button) {
                    SDL_Rect rect = button_rect_for(app, button);
                    if (!button_disabled(app, button) &&
                        point_in_rect(ev.button.x, ev.button.y, &rect)) {
                        app->pressed_button = button;
                        app->focus_button = button;
                        SDL_CaptureMouse(SDL_TRUE);
                        break;
                    }
                }
                if (app->pressed_button >= 0) {
                    break;
                }
                if (point_in_rect(ev.button.x, ev.button.y, &panel)) break;
                app->pressed_thumbnail = thumbnail_at(app, ev.button.x, ev.button.y);
                if (app->pressed_thumbnail >= 0) {
                    SDL_CaptureMouse(SDL_TRUE);
                } else if (!app->image_texture &&
                           point_in_rect(ev.button.x, ev.button.y, &canvas)) {
                    app->pressed_empty = 1;
                    SDL_CaptureMouse(SDL_TRUE);
                } else if (app->image_texture && point_in_rect(ev.button.x, ev.button.y, &canvas)) {
                    SDL_Rect viewport = get_image_viewport(app);
                    float limit_x, limit_y;
                    app->is_panning   = 1;
                    app->drag_moved   = 0;
                    app->drag_start_x = ev.button.x;
                    app->drag_start_y = ev.button.y;
                    if (app->fit_to_window) {
                        app->zoom = get_fit_scale(app, viewport);
                        reset_pan_motion(app, 0, 0);
                    }
                    get_pan_limits(app, viewport, &limit_x, &limit_y);
                    app->pan_start_x = unbound_drag(app->pan_x, limit_x, viewport.w);
                    app->pan_start_y = unbound_drag(app->pan_y, limit_y, viewport.h);
                    reset_pan_motion(app, app->pan_x, app->pan_y);
                    /* Keep the presentation scale when grabbing a fitted image,
                     * including very large images fitted below the wheel limit. */
                    photon_velocity_reset(&app->pan_velocity_x, app->pan_x,
                                          (double)ev.button.timestamp / 1000.0);
                    photon_velocity_reset(&app->pan_velocity_y, app->pan_y,
                                          (double)ev.button.timestamp / 1000.0);
                    SDL_CaptureMouse(SDL_TRUE);
                }
            }
            break;

        case SDL_MOUSEBUTTONUP:
            if (ev.button.button == SDL_BUTTON_LEFT) {
                update_hover_button(app, ev.button.x, ev.button.y);
                if (app->pressed_button >= 0) {
                    int button = app->pressed_button;
                    cancel_pointer(app);
                    if (app->hover_button == button)
                        activate_button(app, button);
                } else if (app->pressed_thumbnail >= 0) {
                    int pressed = app->pressed_thumbnail;
                    cancel_pointer(app);
                    if (thumbnail_at(app, ev.button.x, ev.button.y) == pressed)
                        navigate_to(app, pressed);
                } else if (app->pressed_empty) {
                    SDL_Rect canvas = get_canvas_rect(app);
                    SDL_Rect panel = get_info_panel_rect(app);
                    cancel_pointer(app);
                    if (point_in_rect(ev.button.x, ev.button.y, &canvas) &&
                        !point_in_rect(ev.button.x, ev.button.y, &panel)) open_image(app);
                } else if (app->is_panning) {
                    cancel_pointer(app);
                    finish_pan(app, (double)ev.button.timestamp / 1000.0);
                }
            }
            break;

        case SDL_MOUSEMOTION:
            update_hover_button(app, ev.motion.x, ev.motion.y);
            if (app->is_panning) {
                SDL_Rect viewport = get_image_viewport(app);
                float limit_x, limit_y, raw_x, raw_y;
                get_pan_limits(app, viewport, &limit_x, &limit_y);
                if (abs(ev.motion.x - app->drag_start_x) >= DRAG_THRESHOLD ||
                    abs(ev.motion.y - app->drag_start_y) >= DRAG_THRESHOLD)
                    app->drag_moved = 1;
                if (!app->drag_moved) break;
                app->fit_to_window = 0;
                raw_x = app->pan_start_x + (ev.motion.x - app->drag_start_x);
                raw_y = app->pan_start_y + (ev.motion.y - app->drag_start_y);
                app->pan_x = app->reduced_motion ? photon_clamp(raw_x, -limit_x, limit_x)
                            : photon_bound_drag(raw_x, limit_x, (float)viewport.w);
                app->pan_y = app->reduced_motion ? photon_clamp(raw_y, -limit_y, limit_y)
                            : photon_bound_drag(raw_y, limit_y, (float)viewport.h);
                photon_spring_reset(&app->pan_spring_x, app->pan_x);
                photon_spring_reset(&app->pan_spring_y, app->pan_y);
                photon_velocity_add(&app->pan_velocity_x, app->pan_x,
                                    (double)ev.motion.timestamp / 1000.0);
                photon_velocity_add(&app->pan_velocity_y, app->pan_y,
                                    (double)ev.motion.timestamp / 1000.0);
            }
            break;

        case SDL_MOUSEWHEEL:
            {
                int mouse_x, mouse_y;
                float delta = (float)ev.wheel.y;
                SDL_Rect panel = get_info_panel_rect(app), canvas = get_canvas_rect(app);
#if SDL_VERSION_ATLEAST(2, 0, 18)
                delta = ev.wheel.preciseY;
#endif
                if (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) delta = -delta;
#if SDL_VERSION_ATLEAST(2, 26, 0)
                mouse_x = ev.wheel.mouseX; mouse_y = ev.wheel.mouseY;
#else
                SDL_GetMouseState(&mouse_x, &mouse_y);
#endif
                if (point_in_rect(mouse_x, mouse_y, &panel)) {
                    app->info_scroll = clamp_int(app->info_scroll - (int)(delta * ui_px(app, 36)),
                                                0, info_scroll_limit(app));
                } else if (point_in_rect(mouse_x, mouse_y, &canvas) && delta != 0) {
                    cancel_pointer(app);
                    zoom_at(app, powf(1.1f, photon_clamp(delta, -20, 20)), mouse_x, mouse_y);
                }
            }
            break;
        }
}

void handle_events(App *app) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) handle_event(app, ev);
}

// ── SDL init / cleanup ────────────────────────────────────────────────────────
int initialize_sdl(App *app) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        SDL_Log("SDL init: %s", SDL_GetError()); return 0;
    }
    int img_flags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(img_flags) & img_flags) != img_flags) {
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
    if (!app->renderer)
        app->renderer = SDL_CreateRenderer(app->window, -1, SDL_RENDERER_SOFTWARE);
    if (!app->renderer) {
        SDL_Log("Renderer: %s", SDL_GetError());
        SDL_DestroyWindow(app->window); TTF_Quit(); IMG_Quit(); SDL_Quit(); return 0;
    }

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_RendererInfo renderer_info;
    SDL_GetRendererInfo(app->renderer, &renderer_info);
    app->renderer_vsync = (renderer_info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    SDL_GetWindowSize(app->window, &app->window_width, &app->window_height);

    const char *font_path = find_font(app);
    const char *scale = getenv("PHOTON_TEXT_SCALE");
    if (app->text_scale < 1.0f)
        app->text_scale = scale ? photon_clamp(strtof(scale, NULL), 1.0f, 2.0f) : 1.0f;
    app->ui = photon_ui_create(app->renderer, font_path, app->text_scale);
    if (!app->ui) {
        SDL_DestroyRenderer(app->renderer);
        SDL_DestroyWindow(app->window);
        TTF_Quit(); IMG_Quit(); SDL_Quit(); return 0;
    }
    app->font_regular = photon_ui_font(app->ui, PHOTON_FONT_BODY);
    app->font_bold = photon_ui_font(app->ui, PHOTON_FONT_LABEL);
    SDL_SetWindowMinimumSize(app->window, 480, ui_px(app, 360));
    SDL_GetWindowSize(app->window, &app->window_width, &app->window_height);
    if (!app->font_regular)
        SDL_Log("Warning: No font found. Text disabled. (%s)", TTF_GetError());

    app->running         = 1;
    app->zoom            = 1.0f;
    app->fit_to_window   = 1;
    app->show_info       = 0;
    app->show_thumbnails = 1;
    app->rotation        = 0;
    app->hover_button = app->pressed_button = app->focus_button = -1;
    app->pressed_thumbnail = -1;
    app->pan_damping = MOTION_DAMPING;
    app->reduced_motion |= environment_flag("PHOTON_REDUCED_MOTION");
    app->reduced_transparency |= environment_flag("PHOTON_REDUCED_TRANSPARENCY");
    app->high_contrast |= environment_flag("PHOTON_HIGH_CONTRAST");
    apply_accessibility_preferences(app);
    reset_pan_motion(app, 0, 0);
    photon_spring_reset(&app->info_spring, 0.0f);
    photon_spring_reset(&app->thumbs_spring, 1.0f);
    app->last_frame_ticks = SDL_GetTicks();
    return 1;
}

void cleanup(App *app) {
    if (!app) return;
    free_file_list(&app->file_list);
    free_thumb_cache(app);
    if (app->image_texture) SDL_DestroyTexture(app->image_texture);
    photon_ui_destroy(app->ui);
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
        Uint32 frame_start = SDL_GetTicks();
        float dt = (float)(frame_start - app.last_frame_ticks) / 1000.0f;
        app.last_frame_ticks = frame_start;
        update_pan_motion(&app, fminf(dt, 0.05f));
        handle_events(&app);
        render(&app);
        if (!app.renderer_vsync) {
            Uint32 elapsed = SDL_GetTicks() - frame_start;
            if (elapsed < 16) SDL_Delay(16 - elapsed);
        }
    }
    cleanup(&app);
    return 0;
}
