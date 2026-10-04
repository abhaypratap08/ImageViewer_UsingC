/* Exercise the actual SDL event and rendering flow without launching dialogs. */
#define PHOTON_TESTING
#define main photon_program_main
#include "../src/main.c"
#undef main

#include <assert.h>

static void key_event(App *app, SDL_Keycode key, Uint16 modifiers, int repeat) {
    SDL_Event event = {0};
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = key;
    event.key.keysym.mod = modifiers;
    event.key.repeat = repeat;
    handle_event(app, event);
}

static void pointer(App *app, Uint32 type, int x, int y, Uint32 milliseconds) {
    SDL_Event event = {0};
    event.type = type;
    if (type == SDL_MOUSEMOTION) {
        event.motion.x = x;
        event.motion.y = y;
        event.motion.timestamp = milliseconds;
    } else {
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = x;
        event.button.y = y;
        event.button.timestamp = milliseconds;
    }
    handle_event(app, event);
}

static void settle(App *app) {
    for (int i = 0; i < 300; i++) update_pan_motion(app, 1.0f / 120.0f);
}

static void test_controls(App *app) {
    SDL_Rect info = button_rect_for(app, BUTTON_INFO);
    int x = info.x + info.w / 2, y = info.y + info.h / 2;
    pointer(app, SDL_MOUSEBUTTONDOWN, x, y, 1000);
    assert(app->pressed_button == BUTTON_INFO && !app->show_info);
    pointer(app, SDL_MOUSEMOTION, 2, 300, 1010);
    assert(app->hover_button == -1);
    pointer(app, SDL_MOUSEBUTTONUP, 2, 300, 1020);
    assert(!app->show_info && app->pressed_button == -1);

    pointer(app, SDL_MOUSEBUTTONDOWN, x, y, 1100);
    pointer(app, SDL_MOUSEMOTION, 2, 300, 1110);
    pointer(app, SDL_MOUSEMOTION, x, y, 1120);
    pointer(app, SDL_MOUSEBUTTONUP, x, y, 1130);
    assert(app->show_info && app->info_spring.value == 0);
    update_pan_motion(app, 0.04f);
    float value = app->info_spring.value, velocity = app->info_spring.velocity;
    assert(value > 0 && value < 1);
    key_event(app, SDLK_i, 0, 0);
    assert(!app->show_info);
    assert(app->info_spring.value == value && app->info_spring.velocity == velocity);
    key_event(app, SDLK_i, 0, 1);
    assert(!app->show_info); /* Key repeat must not toggle repeatedly. */
    key_event(app, SDLK_i, 0, 0);
    settle(app);
    assert(app->info_spring.value == 1);
    set_info_visible(app, 0);
    settle(app);
    assert(app->info_spring.value == 0);

    app->focus_button = -1;
    key_event(app, SDLK_TAB, 0, 0);
    assert(app->focus_button == BUTTON_OPEN);
    key_event(app, SDLK_TAB, 0, 0);
    assert(app->focus_button == BUTTON_INFO);
    key_event(app, SDLK_TAB, 0, 0);
    assert(app->focus_button == BUTTON_OPEN); /* Empty-image actions skipped. */
    key_event(app, SDLK_TAB, KMOD_SHIFT, 0);
    assert(app->focus_button == BUTTON_INFO);
    key_event(app, SDLK_SPACE, 0, 0);
    assert(app->show_info);
    set_info_visible(app, 0);
    settle(app);

    pointer(app, SDL_MOUSEBUTTONDOWN, x, y, 1200);
    SDL_Event lost = {0};
    lost.type = SDL_WINDOWEVENT;
    lost.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    handle_event(app, lost);
    pointer(app, SDL_MOUSEBUTTONUP, x, y, 1210);
    assert(!app->show_info && app->pressed_button == -1);
}

static void write_image(const char *path, int width, int height) {
    SDL_Surface *image = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
    assert(image);
    for (int y = 0; y < height; y++) {
        SDL_Rect row = {0, y, width, 1};
        int blue = 130 + y * 70 / height;
        SDL_FillRect(image, &row, SDL_MapRGB(image->format, 35 + y * 70 / height,
                                            70 + y * 80 / height, blue));
    }
    SDL_Rect subject = {width / 4, height / 4, width / 2, height / 2};
    SDL_FillRect(image, &subject, SDL_MapRGB(image->format, 228, 192, 144));
    assert(SDL_SaveBMP(image, path) == 0);
    SDL_FreeSurface(image);
}

static void test_drag_and_zoom(App *app) {
    set_actual_view(app);
    SDL_Rect viewport = get_image_viewport(app);
    int x = viewport.x + viewport.w / 2, y = viewport.y + viewport.h / 2;
    reset_pan_motion(app, 12.5f, 0);
    pointer(app, SDL_MOUSEBUTTONDOWN, x, y, 2000);
    pointer(app, SDL_MOUSEMOTION, x + 4, y, 2010);
    assert(app->pan_x == 12.5f);
    pointer(app, SDL_MOUSEMOTION, x + 30, y, 2030);
    assert(fabsf(app->pan_x - 42.5f) < 0.001f);
    pointer(app, SDL_MOUSEBUTTONUP, x + 30, y, 2040);
    assert(app->pan_spring_x.velocity > 0);
    assert(app->pan_spring_x.target > app->pan_x);
    update_pan_motion(app, 0.016f);
    float presentation = app->pan_x;
    assert(presentation > 42.5f);
    pointer(app, SDL_MOUSEBUTTONDOWN, x, y, 2060);
    assert(app->pan_x == presentation);
    pointer(app, SDL_MOUSEMOTION, x - 30, y, 2090);
    assert(fabsf(app->pan_x - (presentation - 30)) < 0.001f);
    pointer(app, SDL_MOUSEBUTTONUP, x - 30, y, 2250);
    assert(app->pan_spring_x.velocity == 0); /* Paused release doesn't fling. */

    float limit_x, limit_y;
    get_pan_limits(app, viewport, &limit_x, &limit_y);
    reset_pan_motion(app, limit_x + 45, 0);
    pointer(app, SDL_MOUSEBUTTONDOWN, x, y, 2300);
    assert(app->pan_x == limit_x + 45);
    pointer(app, SDL_MOUSEMOTION, x + 12, y, 2310);
    assert(app->pan_x > limit_x + 45 && app->pan_x < limit_x + 57);
    pointer(app, SDL_MOUSEBUTTONUP, x + 12, y, 2450);
    settle(app);
    assert(fabsf(app->pan_x - limit_x) < 0.001f);

    key_event(app, SDLK_m, KMOD_CTRL | KMOD_SHIFT, 0);
    assert(app->reduced_motion);
    reset_pan_motion(app, 0, 0);
    pointer(app, SDL_MOUSEBUTTONDOWN, x, y, 2500);
    pointer(app, SDL_MOUSEMOTION, x + 60, y, 2520);
    pointer(app, SDL_MOUSEBUTTONUP, x + 60, y, 2530);
    assert(app->pan_x == 60 && app->pan_spring_x.velocity == 0);
    settle(app);
    assert(app->pan_x == 60);
    key_event(app, SDLK_m, KMOD_CTRL | KMOD_SHIFT, 0);

    set_fit_view(app);
    float fit = get_fit_scale(app, viewport);
    pointer(app, SDL_MOUSEBUTTONDOWN, x, y, 2600);
    assert(app->fit_to_window); /* Tap doesn't silently leave Fit. */
    pointer(app, SDL_MOUSEMOTION, x + 30, y, 2630);
    assert(!app->fit_to_window && fabsf(app->zoom - fit) < 0.0001f);
    pointer(app, SDL_MOUSEBUTTONUP, x + 30, y, 2640);
    settle(app);

    set_actual_view(app);
    zoom_at(app, 1.2f, x + 40, y);
    assert(fabsf((40 - app->pan_x) / app->zoom - 40) < 0.001f);
    for (int i = 0; i < 150; i++) zoom_at(app, 1.2f, x, y);
    assert(app->zoom <= MAX_ZOOM && isfinite(app->pan_x));
    for (int i = 0; i < 150; i++) zoom_at(app, 1 / 1.2f, x, y);
    assert(app->zoom >= fminf(MIN_ZOOM, fit));
    set_fit_view(app);
}

static void test_rotated_render(App *app) {
    set_fit_view(app);
    key_event(app, SDLK_r, 0, 0);
    assert(app->rotation == 90);
    SDL_RenderSetScale(app->renderer, 1, 1);
    render_image(app);
    SDL_Surface *pixels = SDL_CreateRGBSurfaceWithFormat(0, WINDOW_WIDTH, WINDOW_HEIGHT,
                                                        32, SDL_PIXELFORMAT_RGBA32);
    assert(pixels);
    assert(SDL_RenderReadPixels(app->renderer, NULL, pixels->format->format,
                                pixels->pixels, pixels->pitch) == 0);
    int left = WINDOW_WIDTH, top = WINDOW_HEIGHT, right = 0, bottom = 0;
    for (int y = 0; y < WINDOW_HEIGHT; y++) {
        Uint32 *row = (Uint32 *)((Uint8 *)pixels->pixels + y * pixels->pitch);
        for (int x = 0; x < WINDOW_WIDTH; x++) {
            Uint8 r, g, b;
            SDL_GetRGB(row[x], pixels->format, &r, &g, &b);
            if (r != 18 || g != 19 || b != 23) {
                if (x < left) left = x;
                if (x > right) right = x;
                if (y < top) top = y;
                if (y > bottom) bottom = y;
            }
        }
    }
    float fit = get_fit_scale(app, get_image_viewport(app));
    assert(fabsf(right - left + 1 - app->image_height * fit) <= 2);
    assert(fabsf(bottom - top + 1 - app->image_width * fit) <= 2);
    SDL_FreeSurface(pixels);
    key_event(app, SDLK_r, KMOD_SHIFT, 0);
    assert(app->rotation == 0);
}

static void test_panels_and_thumbnails(App *app) {
    SDL_Rect strip = get_thumbnail_rect(app);
    assert(thumbnail_at(app, strip.x, strip.y + 20) == -1);
    assert(thumbnail_at(app, strip.x + THUMB_PAD + THUMB_W, strip.y + 20) == -1);
    int x = strip.x + THUMB_PAD + THUMB_SLOT_W + 20, y = strip.y + 20;
    assert(thumbnail_at(app, x, y) == 1);
    pointer(app, SDL_MOUSEBUTTONDOWN, x, y, 3000);
    assert(app->file_list.current == 0 && app->pressed_thumbnail == 1);
    pointer(app, SDL_MOUSEBUTTONUP, 0, 0, 3020);
    assert(app->file_list.current == 0);
    pointer(app, SDL_MOUSEBUTTONDOWN, x, y, 3100);
    pointer(app, SDL_MOUSEBUTTONUP, x, y, 3120);
    assert(app->file_list.current == 1);
    navigate_to(app, 0);

    set_info_visible(app, 1);
    settle(app);
    SDL_Rect panel = get_info_panel_rect(app);
    pointer(app, SDL_MOUSEBUTTONDOWN, panel.x + 10, panel.y + 20, 3200);
    assert(!app->is_panning);
    float zoom = app->zoom;
    int old_height = app->window_height;
    app->window_height = 400;
    key_event(app, SDLK_PAGEDOWN, 0, 0);
    assert(app->info_scroll > 0 && app->info_scroll <= info_scroll_limit(app));
    assert(app->zoom == zoom);
    key_event(app, SDLK_h, KMOD_CTRL | KMOD_SHIFT, 0);
    key_event(app, SDLK_t, KMOD_CTRL | KMOD_SHIFT, 0);
    assert(app->high_contrast && app->reduced_transparency && app->show_thumbnails);
    render(app);
    key_event(app, SDLK_h, KMOD_CTRL | KMOD_SHIFT, 0);
    key_event(app, SDLK_t, KMOD_CTRL | KMOD_SHIFT, 0);
    app->window_height = old_height;
    set_info_visible(app, 0);
    settle(app);
    set_thumbnails_visible(app, 0);
    update_pan_motion(app, 0.04f);
    float value = app->thumbs_spring.value, velocity = app->thumbs_spring.velocity;
    assert(value > 0 && value < 1);
    set_thumbnails_visible(app, 1);
    assert(value == app->thumbs_spring.value && velocity == app->thumbs_spring.velocity);
    settle(app);
}

static void test_layout(App *app) {
    const char *font = find_font(app);
    photon_ui_destroy(app->ui);
    app->text_scale = 2;
    app->ui = photon_ui_create(app->renderer, font, app->text_scale);
    assert(app->ui);
    app->font_regular = photon_ui_font(app->ui, PHOTON_FONT_BODY);
    app->font_bold = photon_ui_font(app->ui, PHOTON_FONT_LABEL);
    for (int width = 480; width <= 1400; width += 230) {
        app->window_width = width;
        app->window_height = 600;
        SDL_Rect buttons[BUTTON_COUNT], title;
        SDL_Rect bar = layout_toolbar(app, buttons, &title);
        assert(bar.h < app->window_height);
        for (int i = 0; i < BUTTON_COUNT; i++) {
            assert(buttons[i].x >= bar.x && buttons[i].x + buttons[i].w <= bar.x + bar.w);
            assert(buttons[i].y >= title.y + title.h || buttons[i].x >= title.x + title.w);
            assert(buttons[i].h >= TTF_FontHeight(app->font_bold));
            for (int j = i + 1; j < BUTTON_COUNT; j++)
                assert(!SDL_HasIntersection(&buttons[i], &buttons[j]));
        }
        render(app);
    }
    photon_ui_destroy(app->ui);
    app->text_scale = 1;
    app->ui = photon_ui_create(app->renderer, font, 1);
    app->font_regular = photon_ui_font(app->ui, PHOTON_FONT_BODY);
    app->font_bold = photon_ui_font(app->ui, PHOTON_FONT_LABEL);
    app->window_width = WINDOW_WIDTH;
    app->window_height = WINDOW_HEIGHT;
}

static void capture(App *app, const char *path) {
    app->window_width = 1000;
    app->window_height = 740;
    SDL_SetWindowSize(app->window, app->window_width, app->window_height);
    app->feedback[0] = '\0';
    app->focus_button = app->hover_button = -1;
    app->info_scroll = 0;
    set_info_visible(app, 1);
    settle(app);
    for (int i = 0; i < 4; i++) render(app);
    SDL_Surface *image = SDL_CreateRGBSurfaceWithFormat(0, 1000, 740, 32, SDL_PIXELFORMAT_RGBA32);
    assert(image);
    assert(SDL_RenderReadPixels(app->renderer, NULL, image->format->format,
                                image->pixels, image->pitch) == 0);
    assert(IMG_SavePNG(image, path) == 0);
    SDL_FreeSurface(image);
}

int main(int argc, char **argv) {
    App app = {0};
    char directory[] = "tests/.fixtures-XXXXXX";
    char landscape[128], portrait[128], invalid[128];
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("PHOTON_REDUCED_MOTION", "0", 1);
    SDL_setenv("PHOTON_REDUCED_TRANSPARENCY", "0", 1);
    SDL_setenv("PHOTON_HIGH_CONTRAST", "0", 1);
    app.text_scale = 1;
    assert(initialize_sdl(&app));
    assert(app.font_regular && app.font_bold);
    render(&app);
    test_controls(&app);

    assert(mkdtemp(directory));
    snprintf(landscape, sizeof(landscape), "%s/00-landscape.bmp", directory);
    snprintf(portrait, sizeof(portrait), "%s/01-portrait.bmp", directory);
    snprintf(invalid, sizeof(invalid), "%s/02-invalid.bmp", directory);
    write_image(landscape, 1200, 800);
    write_image(portrait, 300, 900);
    FILE *file = fopen(invalid, "wb");
    assert(file);
    assert(fputs("Not an image", file) >= 0);
    assert(fclose(file) == 0);

    open_image_path(&app, landscape);
    assert(app.image_texture && app.file_list.current == 0 && app.file_list.count == 3);
    SDL_Texture *texture = app.image_texture;
    navigate_to(&app, 2);
    assert(app.image_texture == texture && app.file_list.current == 0 && app.feedback[0]);
    open_image_path(&app, "tests/missing.bmp");
    assert(app.image_texture == texture && strcmp(app.current_path, landscape) == 0);
    test_drag_and_zoom(&app);
    test_rotated_render(&app);
    test_panels_and_thumbnails(&app);
    test_layout(&app);
    if (argc > 1) capture(&app, argv[1]);

    cleanup(&app);
    assert(remove(landscape) == 0 && remove(portrait) == 0 && remove(invalid) == 0);
    assert(rmdir(directory) == 0);
    puts("app interaction tests passed");
    return 0;
}
