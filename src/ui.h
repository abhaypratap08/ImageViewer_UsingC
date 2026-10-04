#ifndef PHOTON_UI_H
#define PHOTON_UI_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

typedef struct PhotonUI PhotonUI;

enum {
    PHOTON_FONT_BODY = 0,
    PHOTON_FONT_LABEL = 1,
    PHOTON_FONT_TITLE = 2,
    PHOTON_FONT_SMALL = 3
};

/* The caller owns SDL/TTF initialization and the renderer. Destroy the UI
 * before destroying its renderer or calling TTF_Quit. A missing font leaves
 * a usable drawing context with text disabled. Text scale is clamped to 1..2. */
PhotonUI *photon_ui_create(SDL_Renderer *renderer, const char *font_path,
                           float text_scale);
void photon_ui_destroy(PhotonUI *ui);
void photon_ui_preferences(PhotonUI *ui, int reduced_transparency,
                           int high_contrast);

/* Borrowed font, for measurement only: do not close or change its settings.
 * Returns NULL for an invalid role or when no font could be loaded. */
TTF_Font *photon_ui_font(PhotonUI *ui, int role);

/* Single-line UTF-8 text. Ellipsis respects code-point boundaries; malformed
 * UTF-8 is ignored. Overflow draws nothing if even an ellipsis cannot fit.
 * Missing text/fonts and nonpositive widths also draw nothing. Centered text
 * clips vertically to rect. Both retain the caller's clip. */
void photon_ui_text(PhotonUI *ui, int role, const char *text,
                    int x, int y, int max_width, SDL_Color color);
void photon_ui_centered(PhotonUI *ui, int role, const char *text,
                        SDL_Rect rect, SDL_Color color);

/* Drawing retains the renderer's clip, draw color, and draw blend mode. */
void photon_ui_round_rect(SDL_Renderer *renderer, SDL_Rect rect,
                          int radius, SDL_Color color);
void photon_ui_surface(PhotonUI *ui, SDL_Rect rect, int radius,
                       int structural, float opacity);
void photon_ui_button(PhotonUI *ui, SDL_Rect rect, const char *label,
                      int active, int primary, int hovered, int pressed,
                      int focused, int disabled);

#endif
