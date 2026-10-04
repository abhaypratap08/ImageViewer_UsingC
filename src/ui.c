#include "ui.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PHOTON_FONT_COUNT 4
#define PHOTON_TEXT_CACHE_SIZE 128

typedef struct {
    char *text;
    TTF_Font *font;
    SDL_Color color;
    SDL_Texture *texture;
    int width, height;
    int fit_width; /* Zero means the full text fits, independent of layout. */
    Uint64 used;
} TextEntry;

struct PhotonUI {
    SDL_Renderer *renderer;
    TTF_Font *fonts[PHOTON_FONT_COUNT];
    TextEntry text_cache[PHOTON_TEXT_CACHE_SIZE];
    Uint64 clock;
    float text_scale;
    int reduced_transparency;
    int high_contrast;
};

typedef struct {
    SDL_BlendMode blend;
    SDL_Color color;
} DrawState;

static void forget_text(TextEntry *entry) {
    if (entry->texture) SDL_DestroyTexture(entry->texture);
    free(entry->text);
    memset(entry, 0, sizeof(*entry));
}

PhotonUI *photon_ui_create(SDL_Renderer *renderer, const char *font_path,
                           float text_scale) {
    static const int sizes[PHOTON_FONT_COUNT] = {14, 14, 22, 12};
    PhotonUI *ui;
    if (!renderer) return NULL;
    ui = calloc(1, sizeof(*ui));
    if (!ui) return NULL;
    if (!(text_scale >= 1.0f)) text_scale = 1.0f; /* Includes NaN. */
    if (text_scale > 2.0f) text_scale = 2.0f;
    ui->renderer = renderer;
    ui->text_scale = text_scale;
    if (font_path && font_path[0] && TTF_WasInit()) {
        for (int i = 0; i < PHOTON_FONT_COUNT; i++) {
            ui->fonts[i] = TTF_OpenFont(font_path,
                                      (int)(sizes[i] * text_scale + 0.5f));
            if (ui->fonts[i] && i == PHOTON_FONT_LABEL)
                TTF_SetFontStyle(ui->fonts[i], TTF_STYLE_BOLD);
        }
    }
    return ui;
}

void photon_ui_destroy(PhotonUI *ui) {
    if (!ui) return;
    for (int i = 0; i < PHOTON_TEXT_CACHE_SIZE; i++)
        forget_text(&ui->text_cache[i]);
    for (int i = 0; i < PHOTON_FONT_COUNT; i++)
        if (ui->fonts[i]) TTF_CloseFont(ui->fonts[i]);
    free(ui);
}

void photon_ui_preferences(PhotonUI *ui, int reduced_transparency,
                           int high_contrast) {
    if (!ui) return;
    ui->reduced_transparency = reduced_transparency != 0;
    ui->high_contrast = high_contrast != 0;
}

TTF_Font *photon_ui_font(PhotonUI *ui, int role) {
    if (!ui || role < 0 || role >= PHOTON_FONT_COUNT) return NULL;
    return ui->fonts[role];
}

static char *copy_text(const char *text) {
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);
    if (copy) memcpy(copy, text, size);
    return copy;
}

/* Reject malformed input instead of ever handing SDL_ttf a partial sequence. */
static int utf8_count(const char *text, size_t length, size_t *count) {
    const unsigned char *s = (const unsigned char *)text;
    size_t offset = 0;
    *count = 0;
    while (offset < length) {
        unsigned char lead = s[offset];
        size_t bytes;
        if (lead < 0x80) bytes = 1;
        else if (lead >= 0xc2 && lead <= 0xdf) bytes = 2;
        else if (lead >= 0xe0 && lead <= 0xef) bytes = 3;
        else if (lead >= 0xf0 && lead <= 0xf4) bytes = 4;
        else return 0;
        if (bytes > length - offset) return 0;
        for (size_t i = 1; i < bytes; i++)
            if ((s[offset + i] & 0xc0) != 0x80) return 0;
        if (bytes >= 3 &&
            ((lead == 0xe0 && s[offset + 1] < 0xa0) ||
             (lead == 0xed && s[offset + 1] >= 0xa0) ||
             (lead == 0xf0 && s[offset + 1] < 0x90) ||
             (lead == 0xf4 && s[offset + 1] >= 0x90))) return 0;
        offset += bytes;
        (*count)++;
    }
    return 1;
}

static size_t utf8_prefix(const char *text, size_t characters) {
    size_t offset = 0;
    while (characters > 0) {
        offset++;
        while (((unsigned char)text[offset] & 0xc0) == 0x80) offset++;
        characters--;
    }
    return offset;
}

static char *fit_text(TTF_Font *font, const char *text, int max_width,
                      int *fit_width) {
    const char *ellipsis = TTF_GlyphIsProvided(font, 0x2026)
                         ? "\xe2\x80\xa6" : "...";
    size_t length = strlen(text), count, low, high;
    int width;
    char *result;
    if (length > SIZE_MAX - 4 || !utf8_count(text, length, &count)) return NULL;
    if (TTF_SizeUTF8(font, text, &width, NULL) != 0) return NULL;
    if (width <= max_width) {
        *fit_width = 0;
        return copy_text(text);
    }
    if (!count || TTF_SizeUTF8(font, ellipsis, &width, NULL) != 0 ||
        width > max_width) return NULL;
    result = malloc(length + 4);
    if (!result) return NULL;
    /* Search code-point counts, not arbitrary byte offsets. Fitting happens
     * on cache misses only, including when the layout width changes. */
    low = 0;
    high = count - 1;
    while (low < high) {
        size_t middle = low + (high - low + 1) / 2;
        size_t bytes = utf8_prefix(text, middle);
        memcpy(result, text, bytes);
        memcpy(result + bytes, ellipsis, 4);
        if (TTF_SizeUTF8(font, result, &width, NULL) == 0 && width <= max_width)
            low = middle;
        else
            high = middle - 1;
    }
    length = utf8_prefix(text, low);
    memcpy(result, text, length);
    memcpy(result + length, ellipsis, 4);
    *fit_width = max_width;
    return result;
}

static int same_color(SDL_Color a, SDL_Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static TextEntry *cached_text(PhotonUI *ui, int role, const char *text,
                              int max_width, SDL_Color color) {
    TTF_Font *font = photon_ui_font(ui, role);
    TextEntry *slot;
    SDL_Surface *surface;
    SDL_Color opaque = {color.r, color.g, color.b, 255};
    char *fitted, *key;
    int fit_width = 0;
    if (!font || !text || !text[0] || max_width <= 0 || color.a == 0) return NULL;
    if (++ui->clock == 0) {
        for (int i = 0; i < PHOTON_TEXT_CACHE_SIZE; i++)
            ui->text_cache[i].used = 0;
        ui->clock = 1;
    }
    slot = &ui->text_cache[0];
    for (int i = 0; i < PHOTON_TEXT_CACHE_SIZE; i++) {
        TextEntry *entry = &ui->text_cache[i];
        if (entry->text && entry->font == font && same_color(entry->color, color) &&
            (entry->fit_width == max_width ||
             (entry->fit_width == 0 && entry->width <= max_width)) &&
            strcmp(entry->text, text) == 0) {
            entry->used = ui->clock;
            return entry;
        }
        if (slot->text && (!entry->text || entry->used < slot->used)) slot = entry;
    }
    fitted = fit_text(font, text, max_width, &fit_width);
    if (!fitted) return NULL;
    key = copy_text(text); /* Keep the entire source, never a truncated key. */
    if (!key) { free(fitted); return NULL; }
    surface = TTF_RenderUTF8_Blended(font, fitted, opaque);
    free(fitted);
    if (!surface) { free(key); return NULL; }
    /* Evict before allocating the texture: even transiently, at most 128. */
    forget_text(slot);
    slot->texture = SDL_CreateTextureFromSurface(ui->renderer, surface);
    slot->width = surface->w;
    slot->height = surface->h;
    SDL_FreeSurface(surface);
    if (!slot->texture) { free(key); return NULL; }
    SDL_SetTextureBlendMode(slot->texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(slot->texture, color.a);
    slot->text = key;
    slot->font = font;
    slot->color = color;
    slot->fit_width = fit_width;
    slot->used = ui->clock;
    return slot;
}

static int valid_rect(SDL_Rect rect) {
    return rect.w > 0 && rect.h > 0 &&
           rect.x <= INT_MAX - rect.w && rect.y <= INT_MAX - rect.h;
}

static SDL_Rect inset_rect(SDL_Rect rect, int inset, int offset_y) {
    Sint64 x = (Sint64)rect.x + inset;
    Sint64 y = (Sint64)rect.y + inset + offset_y;
    Sint64 w = (Sint64)rect.w - 2 * (Sint64)inset;
    Sint64 h = (Sint64)rect.h - 2 * (Sint64)inset;
    SDL_Rect result = {0, 0, 0, 0};
    if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX ||
        w <= 0 || w > INT_MAX || h <= 0 || h > INT_MAX) return result;
    result = (SDL_Rect){(int)x, (int)y, (int)w, (int)h};
    return valid_rect(result) ? result : (SDL_Rect){0, 0, 0, 0};
}

static void copy_entry(PhotonUI *ui, TextEntry *entry, SDL_Rect dest,
                       SDL_Rect bounds) {
    SDL_Rect previous, clip = bounds;
    SDL_bool clipped;
    if (!entry || !valid_rect(dest) || !valid_rect(bounds)) return;
    clipped = SDL_RenderIsClipEnabled(ui->renderer);
    SDL_RenderGetClipRect(ui->renderer, &previous);
    if (clipped && !SDL_IntersectRect(&previous, &bounds, &clip)) return;
    if (SDL_RenderSetClipRect(ui->renderer, &clip) == 0)
        SDL_RenderCopy(ui->renderer, entry->texture, NULL, &dest);
    SDL_RenderSetClipRect(ui->renderer, clipped ? &previous : NULL);
}

void photon_ui_text(PhotonUI *ui, int role, const char *text,
                    int x, int y, int max_width, SDL_Color color) {
    TextEntry *entry = cached_text(ui, role, text, max_width, color);
    if (entry) {
        SDL_Rect dest = {x, y, entry->width, entry->height};
        SDL_Rect bounds = {x, y, max_width, entry->height};
        copy_entry(ui, entry, dest, bounds);
    }
}

void photon_ui_centered(PhotonUI *ui, int role, const char *text,
                        SDL_Rect rect, SDL_Color color) {
    TextEntry *entry;
    SDL_Rect dest;
    Sint64 y;
    if (!valid_rect(rect)) return;
    entry = cached_text(ui, role, text, rect.w, color);
    if (!entry) return;
    y = (Sint64)rect.y + (rect.h - entry->height) / 2;
    if (y < INT_MIN || y > INT_MAX) return;
    dest = (SDL_Rect){rect.x + (rect.w - entry->width) / 2, (int)y,
                      entry->width, entry->height};
    copy_entry(ui, entry, dest, rect);
}

static DrawState begin_drawing(SDL_Renderer *renderer) {
    DrawState state = {SDL_BLENDMODE_NONE, {0, 0, 0, 255}};
    SDL_GetRenderDrawBlendMode(renderer, &state.blend);
    SDL_GetRenderDrawColor(renderer, &state.color.r, &state.color.g,
                          &state.color.b, &state.color.a);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    return state;
}

static void end_drawing(SDL_Renderer *renderer, DrawState state) {
    SDL_SetRenderDrawColor(renderer, state.color.r, state.color.g,
                          state.color.b, state.color.a);
    SDL_SetRenderDrawBlendMode(renderer, state.blend);
}

static void draw_color(SDL_Renderer *renderer, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

static int round_radius(SDL_Rect rect, int radius) {
    if (radius < 0) return 0;
    if (radius > rect.w / 2) radius = rect.w / 2;
    if (radius > rect.h / 2) radius = rect.h / 2;
    return radius;
}

static int round_inset(int height, int radius, int row) {
    int edge = row < height - 1 - row ? row : height - 1 - row;
    double distance;
    if (edge >= radius) return 0;
    distance = radius - edge - 0.5;
    return radius - (int)(SDL_sqrt((double)radius * radius -
                                  distance * distance) + 0.5);
}

/* Scanline coverage visits each pixel once, so translucent corners have the
 * same weight as the center. SDL's primitives also work on software renderers. */
static void fill_round(SDL_Renderer *renderer, SDL_Rect rect,
                        int radius, SDL_Color color) {
    SDL_Rect middle;
    if (!valid_rect(rect) || !color.a) return;
    radius = round_radius(rect, radius);
    draw_color(renderer, color);
    if (!radius) {
        SDL_RenderFillRect(renderer, &rect);
        return;
    }
    middle = (SDL_Rect){rect.x, rect.y + radius, rect.w, rect.h - radius * 2};
    if (middle.h > 0) SDL_RenderFillRect(renderer, &middle);
    for (int row = 0; row < radius; row++) {
        int inset = round_inset(rect.h, radius, row);
        SDL_RenderDrawLine(renderer, rect.x + inset, rect.y + row,
                           rect.x + rect.w - 1 - inset, rect.y + row);
        SDL_RenderDrawLine(renderer, rect.x + inset, rect.y + rect.h - 1 - row,
                           rect.x + rect.w - 1 - inset, rect.y + rect.h - 1 - row);
    }
}

/* Draw the ring directly, without painting over its transparent interior. */
static void outline_round(SDL_Renderer *renderer, SDL_Rect rect, int radius,
                           int thickness, SDL_Color color) {
    SDL_Rect inner;
    int inner_radius;
    if (!valid_rect(rect) || thickness <= 0 || !color.a) return;
    radius = round_radius(rect, radius);
    inner = inset_rect(rect, thickness, 0);
    if (!valid_rect(inner)) {
        fill_round(renderer, rect, radius, color);
        return;
    }
    inner_radius = round_radius(inner, radius - thickness);
    draw_color(renderer, color);
    for (int row = 0; row < rect.h; row++) {
        int outer_inset = round_inset(rect.h, radius, row);
        int left = rect.x + outer_inset;
        int right = rect.x + rect.w - 1 - outer_inset;
        if (row < thickness || row >= rect.h - thickness) {
            SDL_RenderDrawLine(renderer, left, rect.y + row, right, rect.y + row);
        } else {
            int inner_inset = thickness +
                             round_inset(inner.h, inner_radius, row - thickness);
            if (inner_inset > outer_inset) {
                SDL_RenderDrawLine(renderer, left, rect.y + row,
                                   rect.x + inner_inset - 1, rect.y + row);
                SDL_RenderDrawLine(renderer, rect.x + rect.w - inner_inset,
                                   rect.y + row, right, rect.y + row);
            }
        }
    }
}

void photon_ui_round_rect(SDL_Renderer *renderer, SDL_Rect rect,
                          int radius, SDL_Color color) {
    DrawState state;
    if (!renderer || !valid_rect(rect) || !color.a) return;
    state = begin_drawing(renderer);
    fill_round(renderer, rect, radius, color);
    end_drawing(renderer, state);
}

static Uint8 opacity_alpha(int alpha, float opacity) {
    return (Uint8)(alpha * opacity + 0.5f);
}

static void top_edge(SDL_Renderer *renderer, SDL_Rect rect,
                      int radius, SDL_Color color) {
    radius = round_radius(rect, radius);
    if (!valid_rect(rect) || !color.a || rect.w <= radius * 2) return;
    draw_color(renderer, color);
    SDL_RenderDrawLine(renderer, rect.x + radius, rect.y,
                       rect.x + rect.w - radius - 1, rect.y);
}

void photon_ui_surface(PhotonUI *ui, SDL_Rect rect, int radius,
                       int structural, float opacity) {
    DrawState state;
    SDL_Color base;
    int solid;
    if (!ui || !valid_rect(rect) || !(opacity > 0.0f)) return;
    if (opacity > 1.0f) opacity = 1.0f;
    radius = round_radius(rect, radius);
    solid = ui->reduced_transparency || ui->high_contrast;
    state = begin_drawing(ui->renderer);
    base = structural ? (SDL_Color){26, 27, 32, 238}
                      : (SDL_Color){33, 35, 41, 216};
    if (solid) {
        /* Accessibility materials stay opaque whenever visible, even when a
         * caller supplies a fractional presentation opacity. */
        base.a = 255;
        if (ui->high_contrast) base = (SDL_Color){21, 22, 26, 255};
        fill_round(ui->renderer, rect, radius, base);
        outline_round(ui->renderer, rect, radius, ui->high_contrast ? 2 : 1,
                      ui->high_contrast ? (SDL_Color){176, 181, 193, 255}
                                        : (SDL_Color){76, 79, 88, 255});
    } else {
        int tint_height = rect.h / 3;
        /* Layered neutral tint is a native material approximation, NOT
         * backdrop blur. It never reads or filters the underlying image. */
        fill_round(ui->renderer, inset_rect(rect, -2, 3), radius + 2,
                   (SDL_Color){0, 0, 0, opacity_alpha(18, opacity)});
        fill_round(ui->renderer, inset_rect(rect, 0, 2), radius,
                   (SDL_Color){0, 0, 0, opacity_alpha(34, opacity)});
        base.a = opacity_alpha(base.a, opacity);
        fill_round(ui->renderer, rect, radius, base);
        if (tint_height > 28) tint_height = 28;
        for (int row = 1; row < tint_height; row++) {
            int inset = round_inset(rect.h, radius, row) + 1;
            int alpha = (structural ? 5 : 8) * (tint_height - row) / tint_height;
            if (rect.w <= inset * 2) continue;
            draw_color(ui->renderer, (SDL_Color){224, 229, 239,
                                                 opacity_alpha(alpha, opacity)});
            SDL_RenderDrawLine(ui->renderer, rect.x + inset, rect.y + row,
                               rect.x + rect.w - 1 - inset, rect.y + row);
        }
        outline_round(ui->renderer, rect, radius, 1,
                      (SDL_Color){200, 205, 216, opacity_alpha(24, opacity)});
        top_edge(ui->renderer, rect, radius,
                  (SDL_Color){237, 241, 249, opacity_alpha(40, opacity)});
    }
    end_drawing(ui->renderer, state);
}

void photon_ui_button(PhotonUI *ui, SDL_Rect rect, const char *label,
                      int active, int primary, int hovered, int pressed,
                      int focused, int disabled) {
    DrawState state;
    SDL_Rect face = rect;
    SDL_Color fill, border, text = {242, 243, 247, 255};
    int radius, padding;
    if (!ui || !valid_rect(rect)) return;
    pressed = pressed && !disabled;
    hovered = hovered && !disabled;
    radius = round_radius(rect, (int)(10.0f * ui->text_scale));
    padding = (int)(10.0f * ui->text_scale + 0.5f);

    if (disabled) {
        fill = (SDL_Color){37, 39, 45, 255};
        border = (SDL_Color){65, 68, 77, 255};
        text = (SDL_Color){172, 176, 187, 255};
    } else if (primary) {
        fill = pressed ? (SDL_Color){43, 72, 113, 255}
             : hovered ? (SDL_Color){65, 105, 155, 255}
                       : (SDL_Color){54, 91, 139, 255};
        border = (SDL_Color){91, 124, 166, 255};
    } else if (active) {
        fill = pressed ? (SDL_Color){35, 51, 71, 255}
             : hovered ? (SDL_Color){47, 70, 99, 255}
                       : (SDL_Color){42, 60, 84, 255};
        border = (SDL_Color){77, 107, 145, 255};
    } else {
        fill = pressed ? (SDL_Color){36, 38, 45, 255}
             : hovered ? (SDL_Color){60, 63, 72, 255}
                       : (SDL_Color){48, 50, 58, 255};
        border = (SDL_Color){79, 82, 92, 255};
    }
    if (ui->high_contrast) {
        border = disabled ? (SDL_Color){118, 122, 133, 255}
                          : (SDL_Color){192, 197, 208, 255};
        text = disabled ? (SDL_Color){194, 198, 209, 255}
                        : (SDL_Color){255, 255, 255, 255};
    }
    /* Pointer-down feedback is immediate and reversible. No fixed-duration
     * tween or clock is involved; the caller supplies the live input state. */
    if (pressed && rect.w > 4 && rect.h > 4) {
        face = inset_rect(rect, 1, 1);
        if (radius > 0) radius--;
    }
    state = begin_drawing(ui->renderer);
    if (!pressed && !disabled && !ui->reduced_transparency && !ui->high_contrast)
        fill_round(ui->renderer, inset_rect(face, 0, 1), radius,
                   (SDL_Color){0, 0, 0, 32});
    fill_round(ui->renderer, face, radius, fill);
    outline_round(ui->renderer, face, radius, ui->high_contrast ? 2 : 1, border);
    if (!pressed && !disabled && !ui->high_contrast)
        top_edge(ui->renderer, face, radius, (SDL_Color){245, 248, 255, 22});
    if (pressed && !ui->high_contrast)
        top_edge(ui->renderer, face, radius, (SDL_Color){0, 0, 0, 70});

    /* A neutral, separated ring keeps focus unmistakable without making
     * every focused secondary control another blue accent. */
    if (focused && !disabled)
        outline_round(ui->renderer, inset_rect(rect, -3, 0),
                       round_radius(rect, (int)(10.0f * ui->text_scale)) + 3,
                       2, (SDL_Color){222, 227, 238, 255});
    if (face.w > padding * 2 && face.h > 4) {
        SDL_Rect content = {face.x + padding, face.y + 2,
                            face.w - padding * 2, face.h - 4};
        photon_ui_centered(ui, PHOTON_FONT_LABEL, label, content, text);
    }
    end_drawing(ui->renderer, state);
}
