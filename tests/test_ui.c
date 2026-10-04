#include "../src/ui.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static const char *find_test_font(void) {
    const char *override = getenv("PHOTON_TEST_FONT");
    if (override && override[0]) return override;
    const char *candidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "C:\\Windows\\Fonts\\segoeui.ttf"
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        FILE *file = fopen(candidates[i], "rb");
        if (file) { fclose(file); return candidates[i]; }
    }
    return NULL;
}

int main(void) {
    const char *font = find_test_font();
    SDL_Window *window;
    SDL_Renderer *renderer;
    PhotonUI *ui;

    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    assert(TTF_Init() == 0);
    window = SDL_CreateWindow("Photon UI test", 0, 0, 320, 240,
                              SDL_WINDOW_HIDDEN);
    assert(window);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    assert(renderer);
    ui = photon_ui_create(renderer, font, 1.0f);
    assert(ui && photon_ui_font(ui, PHOTON_FONT_BODY));
    photon_ui_preferences(ui, 0, 0);
    SDL_SetRenderDrawColor(renderer, 12, 13, 16, 255);
    SDL_RenderClear(renderer);
    photon_ui_surface(ui, (SDL_Rect){8, 8, 304, 224}, 16, 1, 1.0f);
    SDL_Rect sample = {30, 44, 1, 1};
    Uint32 normal_pixel, pressed_pixel;
    photon_ui_button(ui, (SDL_Rect){24, 24, 96, 36}, "Open", 0, 1,
                     0, 0, 0, 0);
    assert(SDL_RenderReadPixels(renderer, &sample, SDL_PIXELFORMAT_RGBA32, &normal_pixel, 4) == 0);
    photon_ui_button(ui, (SDL_Rect){24, 24, 96, 36}, "Open", 0, 1,
                     1, 1, 1, 0);
    assert(SDL_RenderReadPixels(renderer, &sample, SDL_PIXELFORMAT_RGBA32, &pressed_pixel, 4) == 0);
    assert(normal_pixel != pressed_pixel);
    photon_ui_text(ui, PHOTON_FONT_BODY,
                   "UTF-8: café — image viewer", 24, 76, 272,
                   (SDL_Color){242, 243, 247, 255});
    photon_ui_text(ui, PHOTON_FONT_BODY,
                   "A deliberately long label that must be ellipsized safely",
                   24, 104, 140, (SDL_Color){180, 185, 197, 255});
    photon_ui_preferences(ui, 1, 1);
    photon_ui_surface(ui, (SDL_Rect){24, 144, 272, 60}, 12, 0, 0.4f);
    /* Exercise cache hits, evictions and multi-byte fitting under sanitizers. */
    for (int i = 0; i < 300; i++) {
        char label[96];
        snprintf(label, sizeof(label), "Café — 日本語 — frame %d", i);
        photon_ui_text(ui, PHOTON_FONT_BODY, label, 24, 104, 90,
                       (SDL_Color){242, 243, 247, 255});
        photon_ui_text(ui, PHOTON_FONT_BODY, label, 24, 104, 90,
                       (SDL_Color){242, 243, 247, 255});
    }
    SDL_Rect clip = {12, 12, 100, 100}, after;
    SDL_RenderSetClipRect(renderer, &clip);
    SDL_SetRenderDrawColor(renderer, 19, 27, 35, 123);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
    photon_ui_button(ui, (SDL_Rect){24, 24, 96, 36}, "Open", 0, 1, 0, 0, 1, 0);
    photon_ui_text(ui, PHOTON_FONT_BODY, "UTF-8 café…", 24, 80, 20,
                   (SDL_Color){242, 243, 247, 255});
    SDL_RenderGetClipRect(renderer, &after);
    assert(SDL_RectEquals(&clip, &after));
    SDL_BlendMode mode;
    Uint8 r, g, b, a;
    SDL_GetRenderDrawBlendMode(renderer, &mode);
    SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
    assert(mode == SDL_BLENDMODE_ADD && r == 19 && g == 27 && b == 35 && a == 123);
    SDL_RenderPresent(renderer);
    photon_ui_destroy(ui);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    puts("ui tests passed");
    return 0;
}
