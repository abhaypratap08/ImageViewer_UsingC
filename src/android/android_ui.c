#include "android_ui.h"
#include "../motion.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FINGERS 3
#define TAP_MS 290
#define CHROME_MS 3200
#define SLOP 12.0f
#define MAX_ZOOM 16.0f

typedef struct {
    int id;
    float x, y, sx, sy;
    int moved;
} Finger;
enum {
    HIT_NONE,
    HIT_OPEN,
    HIT_FOLDER,
    HIT_BACK,
    HIT_MORE,
    HIT_INFO,
    HIT_ZOOM,
    HIT_ROTATE,
    HIT_RAIL,
    HIT_PREV,
    HIT_NEXT,
    HIT_COPY,
    HIT_SHARE,
    HIT_DELETE,
    HIT_CANCEL,
    HIT_CONFIRM,
    HIT_MENU_ROTATE,
    HIT_MENU_COPY,
    HIT_MENU_SHARE,
    HIT_MENU_GUIDE,
    HIT_MENU_MOTION,
    HIT_MENU_CONTRAST,
    HIT_MENU_DELETE,
    HIT_ERROR_OPEN,
    HIT_ERROR_NEXT
};
enum { G_NONE, G_PAN, G_SWIPE, G_VERTICAL, G_PINCH };

struct AndroidUI {
    SDL_Renderer *renderer;
    PhotonUI *text;
    int width, height;
    float unit;
    int landscape, viewer, controls, info, menu, error, loading;
    int reduced_motion, reduced_transparency, high_contrast, rotation;
    int dialog_open;
    float zoom, pan_x, pan_y, swipe_x, swipe_y;
    int image_w, image_h, image_count;
    AndroidUIFrame frame;
    PhotonSpring pan_x_spring, pan_y_spring;
    PhotonVelocity vx, vy;
    Finger fingers[MAX_FINGERS];
    int finger_count, gesture, pressed, moved;
    float start_x, start_y, start_pan_x, start_pan_y, pinch_dist, pinch_zoom;
    double pending_tap_time;
    float pending_tap_x, pending_tap_y;
    int pending_tap;
    Uint32 chrome_until, guide_until, now;
    SDL_Rect open_rect, folder_rect, back_rect, more_rect, info_rect, zoom_rect, rotate_rect, rail_rect, info_sheet,
        error_open, error_next, dialog;
    SDL_Rect info_buttons[4];
    AndroidAction action;
};

static SDL_Color text_color = {242, 243, 247, 255}, muted = {180, 185, 197, 255}, accent = {224, 188, 217, 255};
static int clamp_i(int x, int lo, int hi) {
    return x < lo ? lo : x > hi ? hi : x;
}
static float clamp_f(float x, float lo, float hi) {
    return x < lo ? lo : x > hi ? hi : x;
}
static int hit(SDL_Rect r, float x, float y) {
    return r.w > 0 && r.h > 0 && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
static int u(const AndroidUI *ui, float x) {
    return (int)lroundf(x * ui->unit);
}
static SDL_Rect ru(const AndroidUI *ui, float x, float y, float w, float h) {
    return (SDL_Rect){u(ui, x), u(ui, y), u(ui, w), u(ui, h)};
}
static void text(AndroidUI *ui, int role, const char *s, int x, int y, int w, SDL_Color c) {
    if (s)
        photon_ui_text(ui->text, role, s, x, y, w, c);
}
static void center(AndroidUI *ui, int role, const char *s, SDL_Rect r, SDL_Color c) {
    if (s)
        photon_ui_centered(ui->text, role, s, r, c);
}
static void fill(AndroidUI *ui, SDL_Rect r, int radius, SDL_Color c) {
    photon_ui_round_rect(ui->renderer, r, radius, c);
}
static void button(AndroidUI *ui, SDL_Rect r, const char *s, int primary, int active) {
    photon_ui_button(ui->text, r, s, active, primary, 0, ui->pressed && hit(r, ui->start_x, ui->start_y), 0, 0);
}
static void queue(AndroidUI *ui, AndroidActionType type, int index, float x, float y) {
    if (ui->action.type == ANDROID_ACTION_NONE)
        ui->action = (AndroidAction){type, index, x, y};
}
static void kick(AndroidUI *ui) {
    ui->controls = 1;
    ui->chrome_until = ui->now + CHROME_MS;
}
static float fit_scale(const AndroidUI *ui) {
    if (ui->image_w <= 0 || ui->image_h <= 0)
        return 1;
    int w = ui->image_w, h = ui->image_h;
    if (ui->rotation == 90 || ui->rotation == 270) {
        w = ui->image_h;
        h = ui->image_w;
    }
    return fminf((float)ui->width / w, (float)ui->height / h);
}
static void pan_limits(AndroidUI *ui, float *x, float *y) {
    float f = fit_scale(ui), w = ui->image_w * f * ui->zoom, h = ui->image_h * f * ui->zoom;
    if (ui->rotation == 90 || ui->rotation == 270) {
        w = ui->image_h * f * ui->zoom;
        h = ui->image_w * f * ui->zoom;
    }
    *x = fmaxf(0, (w - ui->width) / 2);
    *y = fmaxf(0, (h - ui->height) / 2);
}
static void reset_pan(AndroidUI *ui) {
    ui->pan_x = ui->pan_y = 0;
    photon_spring_reset(&ui->pan_x_spring, 0);
    photon_spring_reset(&ui->pan_y_spring, 0);
}
static void clamp_pan(AndroidUI *ui) {
    float x, y;
    pan_limits(ui, &x, &y);
    ui->pan_x = photon_clamp(ui->pan_x, -x, x);
    ui->pan_y = photon_clamp(ui->pan_y, -y, y);
}
static void zoom_at(AndroidUI *ui, float next, float x, float y) {
    float f = fit_scale(ui), old = ui->zoom;
    float max = fmaxf(MAX_ZOOM, 1 / fmaxf(f, .01f));
    float ix = (x - ui->width / 2 - ui->pan_x) / (f * old), iy = (y - ui->height / 2 - ui->pan_y) / (f * old);
    next = clamp_f(next, 1, max);
    ui->zoom = next;
    ui->pan_x = x - ui->width / 2 - ix * f * next;
    ui->pan_y = y - ui->height / 2 - iy * f * next;
    clamp_pan(ui);
    photon_spring_reset(&ui->pan_x_spring, ui->pan_x);
    photon_spring_reset(&ui->pan_y_spring, ui->pan_y);
}
static void layout(AndroidUI *ui) {
    float s = ui->unit;
    int top = u(ui, ui->landscape ? 10 : 28), pad = u(ui, 16), size = u(ui, 48);
    ui->open_rect = ru(ui, 24, ui->landscape ? 294 : 714, 206, 54);
    ui->folder_rect = ru(ui, 240, ui->landscape ? 294 : 714, 126, 54);
    ui->back_rect = (SDL_Rect){pad, top, size, size};
    ui->more_rect = (SDL_Rect){ui->width - pad - size, top, size, size};
    ui->info_rect = ru(ui, 20, ui->landscape ? 304 : 698, 84, 42);
    ui->zoom_rect = ru(ui, ui->landscape ? 365 : 158, ui->landscape ? 304 : 698, 108, 42);
    ui->rotate_rect = ru(ui, ui->landscape ? 720 : 286, ui->landscape ? 304 : 698, 100, 42);
    ui->rail_rect = ru(ui, 20, ui->landscape ? 352 : 754, ui->landscape ? 804 : 350, 32);
    ui->info_sheet = ui->landscape ? (SDL_Rect){ui->width - u(ui, 320), 0, u(ui, 320), ui->height}
                                   : (SDL_Rect){0, ui->height - u(ui, 340), ui->width, u(ui, 340)};
    ui->error_next = ru(ui, 25, ui->landscape ? 245 : 570, 160, 48);
    ui->error_open = ru(ui, 205, ui->landscape ? 245 : 570, 160, 48);
    ui->dialog = ru(ui, 28, ui->landscape ? 115 : 260, 334, 210);
    (void)s;
    for (int i = 0; i < 4; i++)
        ui->info_buttons[i] =
            (SDL_Rect){u(ui, 20 + i * 88), ui->info_sheet.y + ui->info_sheet.h - u(ui, 62), u(ui, 80), u(ui, 42)};
}
static int hit_test(AndroidUI *ui, float x, float y) {
    if (!ui->viewer) {
        if (hit(ui->open_rect, x, y))
            return HIT_OPEN;
        if (hit(ui->folder_rect, x, y))
            return HIT_FOLDER;
        return HIT_NONE;
    }
    if (ui->dialog_open) {
        if (hit((SDL_Rect){ui->dialog.x + u(ui, 166), ui->dialog.y + u(ui, 154), u(ui, 74), u(ui, 44)}, x, y))
            return HIT_CANCEL;
        if (hit((SDL_Rect){ui->dialog.x + u(ui, 245), ui->dialog.y + u(ui, 154), u(ui, 82), u(ui, 44)}, x, y))
            return HIT_CONFIRM;
        return HIT_NONE;
    }
    if (ui->error) {
        if (hit(ui->error_next, x, y))
            return HIT_ERROR_NEXT;
        if (hit(ui->error_open, x, y))
            return HIT_ERROR_OPEN;
        return HIT_NONE;
    }
    /* Hidden chrome must not leave invisible hit regions over the image. */
    if (!ui->controls && !ui->info && !ui->menu)
        return HIT_NONE;
    if (ui->menu) {
        int menu_x = ui->width - u(ui, 236), menu_y = u(ui, 80);
        if (x < menu_x || x > ui->width - u(ui, 8) || y < menu_y ||
            y >= menu_y + u(ui, 8 * 46 + 20))
            return HIT_NONE;
        int row = (int)((y - menu_y - u(ui, 8)) / u(ui, 46));
        switch (row) {
        case 0:
            return HIT_MENU_ROTATE;
        case 1:
            return HIT_MENU_COPY;
        case 2:
            return HIT_MENU_SHARE;
        case 4:
            return HIT_MENU_GUIDE;
        case 5:
            return HIT_MENU_MOTION;
        case 6:
            return HIT_MENU_CONTRAST;
        case 7:
            return HIT_MENU_DELETE;
        default:
            return HIT_NONE;
        }
    }
    if (ui->info) {
        for (int i = 0; i < 4; i++) {
            if (!hit(ui->info_buttons[i], x, y))
                continue;
            if (i == 0) return HIT_ROTATE;
            if (i == 1) return HIT_COPY;
            if (i == 2) return HIT_SHARE;
            return HIT_DELETE;
        }
        if (hit(ui->info_sheet, x, y))
            return HIT_NONE;
    }
    if (hit(ui->back_rect, x, y))
        return HIT_BACK;
    if (hit(ui->more_rect, x, y))
        return HIT_MORE;
    if (hit(ui->info_rect, x, y))
        return HIT_INFO;
    if (hit(ui->zoom_rect, x, y))
        return HIT_ZOOM;
    if (hit(ui->rotate_rect, x, y))
        return HIT_ROTATE;
    if (hit(ui->rail_rect, x, y))
        return HIT_RAIL;
    return HIT_NONE;
}
static void layout_unit(AndroidUI *ui) {
    float wb = ui->landscape ? 844 : 390, hb = ui->landscape ? 390 : 844;
    ui->unit = clamp_f(fminf(ui->width / wb, ui->height / hb), .75f, 4);
    layout(ui);
}
AndroidUI *android_ui_create(SDL_Renderer *renderer, PhotonUI *text, int w, int h) {
    AndroidUI *ui = calloc(1, sizeof(*ui));
    if (!ui)
        return NULL;
    ui->renderer = renderer;
    ui->text = text;
    ui->width = w;
    ui->height = h;
    ui->viewer = 0;
    ui->controls = 1;
    ui->zoom = 1;
    layout_unit(ui);
    reset_pan(ui);
    return ui;
}
void android_ui_destroy(AndroidUI *ui) {
    free(ui);
}
void android_ui_resize(AndroidUI *ui, int w, int h) {
    if (!ui)
        return;
    ui->width = w;
    ui->height = h;
    ui->landscape = w > h;
    layout_unit(ui);
    clamp_pan(ui);
}
void android_ui_show_home(AndroidUI *ui) {
    if (!ui)
        return;
    ui->viewer = 0;
    ui->info = ui->menu = ui->dialog_open = ui->error = ui->loading = 0;
    ui->controls = 1;
    ui->finger_count = 0;
    ui->gesture = G_NONE;
    layout(ui);
}
void android_ui_show_viewer(AndroidUI *ui) {
    if (!ui)
        return;
    ui->viewer = 1;
    kick(ui);
    layout(ui);
}
void android_ui_set_image(AndroidUI *ui) {
    if (!ui)
        return;
    ui->viewer = 1;
    ui->error = ui->loading = 0;
    ui->rotation = 0;
    ui->zoom = 1;
    reset_pan(ui);
    kick(ui);
    layout(ui);
}
void android_ui_set_error(AndroidUI *ui, int error, const char *message) {
    if (!ui)
        return;
    ui->error = error != 0;
    ui->viewer = 1;
    ui->loading = 0;
    kick(ui);
    (void)message;
    layout(ui);
}
void android_ui_set_loading(AndroidUI *ui, int loading, const char *message) {
    if (ui) {
        ui->loading = loading != 0;
        (void)message;
    }
}
void android_ui_set_rotation(AndroidUI *ui, int r) {
    if (ui) {
        ui->rotation = ((r % 360) + 360) % 360;
        clamp_pan(ui);
    }
}
void android_ui_preferences(AndroidUI *ui, int motion, int transparency, int contrast) {
    if (ui) {
        ui->reduced_motion = motion != 0;
        ui->reduced_transparency = transparency != 0;
        ui->high_contrast = contrast != 0;
    }
}
int android_ui_is_immersive(const AndroidUI *ui) {
    return ui && ui->viewer && !ui->controls && !ui->info && !ui->menu && !ui->dialog_open && !ui->error &&
           !ui->loading;
}
int android_ui_next_action(AndroidUI *ui, AndroidAction *a) {
    if (!ui || !a || ui->action.type == ANDROID_ACTION_NONE)
        return 0;
    *a = ui->action;
    ui->action.type = ANDROID_ACTION_NONE;
    return 1;
}
int android_ui_action_back(AndroidUI *ui) {
    if (!ui)
        return 0;
    if (ui->dialog_open)
        ui->dialog_open = 0;
    else if (ui->menu)
        ui->menu = 0;
    else if (ui->info)
        ui->info = 0;
    else if (ui->viewer) {
        android_ui_show_home(ui);
        return 1;
    } else
        return 0;
    kick(ui);
    layout(ui);
    return 1;
}
static void perform(AndroidUI *ui, int h, float x, float y) {
    switch (h) {
    case HIT_OPEN:
        queue(ui, ANDROID_ACTION_OPEN_IMAGE, 0, x, y);
        break;
    case HIT_FOLDER:
        queue(ui, ANDROID_ACTION_OPEN_FOLDER, 0, x, y);
        break;
    case HIT_BACK:
        android_ui_show_home(ui);
        break;
    case HIT_MORE:
        ui->menu = 1;
        kick(ui);
        break;
    case HIT_INFO:
        ui->info = !ui->info;
        kick(ui);
        break;
    case HIT_ZOOM:
        if (ui->zoom > 1.05) {
            ui->zoom = 1;
            reset_pan(ui);
        } else {
            float f = fit_scale(ui);
            zoom_at(ui, f < 1 ? 1 / f : 2.5, ui->width / 2, ui->height / 2);
        }
        kick(ui);
        break;
    case HIT_ROTATE:
        ui->rotation = (ui->rotation + 90) % 360;
        reset_pan(ui);
        queue(ui, ANDROID_ACTION_ROTATE, ui->rotation, x, y);
        break;
    case HIT_RAIL: {
        int count = ui->image_count > 0 ? ui->image_count : 1;
        int i = clamp_i((int)((x - ui->rail_rect.x) / (float)ui->rail_rect.w * count), 0, count - 1);
        queue(ui, ANDROID_ACTION_NAVIGATE_INDEX, i, x, y);
        break;
    }
    case HIT_PREV:
        queue(ui, ANDROID_ACTION_PREVIOUS, 0, x, y);
        break;
    case HIT_NEXT:
        queue(ui, ANDROID_ACTION_NEXT, 0, x, y);
        break;
    case HIT_COPY:
        queue(ui, ANDROID_ACTION_COPY, 0, x, y);
        break;
    case HIT_SHARE:
        queue(ui, ANDROID_ACTION_SHARE, 0, x, y);
        break;
    case HIT_DELETE:
        ui->dialog_open = 1;
        break;
    case HIT_CANCEL:
        ui->dialog_open = 0;
        break;
    case HIT_CONFIRM:
        ui->dialog_open = 0;
        queue(ui, ANDROID_ACTION_DELETE_CONFIRM, 0, x, y);
        break;
    case HIT_MENU_ROTATE:
        ui->menu = 0;
        perform(ui, HIT_ROTATE, x, y);
        break;
    case HIT_MENU_COPY:
        ui->menu = 0;
        perform(ui, HIT_COPY, x, y);
        break;
    case HIT_MENU_SHARE:
        ui->menu = 0;
        perform(ui, HIT_SHARE, x, y);
        break;
    case HIT_MENU_GUIDE:
        ui->menu = 0;
        ui->guide_until = ui->now + 4500;
        break;
    case HIT_MENU_MOTION:
        ui->menu = 0;
        ui->reduced_motion = !ui->reduced_motion;
        break;
    case HIT_MENU_CONTRAST:
        ui->menu = 0;
        ui->high_contrast = !ui->high_contrast;
        break;
    case HIT_MENU_DELETE:
        ui->menu = 0;
        ui->dialog_open = 1;
        break;
    case HIT_ERROR_OPEN:
        queue(ui, ANDROID_ACTION_OPEN_IMAGE, 0, x, y);
        break;
    case HIT_ERROR_NEXT:
        queue(ui, ANDROID_ACTION_NEXT, 0, x, y);
        break;
    default:
        break;
    }
    layout(ui);
}
static Finger *finger(AndroidUI *ui, int id) {
    for (int i = 0; i < ui->finger_count; i++)
        if (ui->fingers[i].id == id)
            return &ui->fingers[i];
    return NULL;
}
void android_ui_touch_down(AndroidUI *ui, int id, float x, float y, double t) {
    if (!ui || ui->finger_count >= MAX_FINGERS)
        return;
    if (ui->finger_count == 0) {
        ui->start_x = x;
        ui->start_y = y;
        ui->start_pan_x = ui->pan_x;
        ui->start_pan_y = ui->pan_y;
        ui->moved = 0;
        ui->gesture = G_NONE;
        ui->pressed = hit_test(ui, x, y);
    }
    ui->fingers[ui->finger_count++] = (Finger){id, x, y, x, y, 0};
    if (ui->finger_count == 2) {
        ui->gesture = G_PINCH;
        ui->pressed = HIT_NONE;
        ui->pending_tap = 0;
        ui->pinch_zoom = ui->zoom;
        Finger *a = &ui->fingers[0], *b = &ui->fingers[1];
        ui->pinch_dist = hypotf(a->x - b->x, a->y - b->y);
    }
    if (t)
        ui->now = (Uint32)(t * 1000);
}
void android_ui_touch_move(AndroidUI *ui, int id, float x, float y, double t) {
    Finger *f;
    if (!ui || (f = finger(ui, id)) == NULL)
        return;
    f->x = x;
    f->y = y;
    ui->now = (Uint32)(t * 1000);
    if (ui->finger_count >= 2 && ui->gesture == G_PINCH) {
        Finger *a = &ui->fingers[0], *b = &ui->fingers[1];
        float d = hypotf(a->x - b->x, a->y - b->y), cx = (a->x + b->x) / 2, cy = (a->y + b->y) / 2;
        if (ui->pinch_dist > 1)
            zoom_at(ui, ui->pinch_zoom * d / ui->pinch_dist, cx, cy);
        return;
    }
    if (ui->finger_count != 1 || ui->pressed)
        return;
    float dx = x - ui->start_x, dy = y - ui->start_y;
    if (!ui->moved && hypotf(dx, dy) > SLOP) {
        ui->moved = 1;
        ui->pending_tap = 0;
        ui->gesture = ui->zoom > 1.05 ? G_PAN : (fabsf(dx) > fabsf(dy) ? G_SWIPE : G_VERTICAL);
    }
    if (!ui->moved)
        return;
    if (ui->gesture == G_PAN) {
        ui->pan_x = ui->start_pan_x + dx;
        ui->pan_y = ui->start_pan_y + dy;
        float lx, ly;
        pan_limits(ui, &lx, &ly);
        ui->pan_x = photon_bound_drag(ui->pan_x, lx, ui->width);
        ui->pan_y = photon_bound_drag(ui->pan_y, ly, ui->height);
    } else if (ui->gesture == G_SWIPE)
        ui->swipe_x = dx * .55f;
    else if (ui->gesture == G_VERTICAL)
        ui->swipe_y = dy * .55f;
}
void android_ui_touch_up(AndroidUI *ui, int id, float x, float y, double t) {
    Finger *f;
    if (!ui || (f = finger(ui, id)) == NULL)
        return;
    int moved = ui->moved, h = ui->pressed;
    float dx = x - ui->start_x, dy = y - ui->start_y;
    for (int i = 0; i < ui->finger_count; i++)
        if (ui->fingers[i].id == id) {
            for (int j = i + 1; j < ui->finger_count; j++)
                ui->fingers[j - 1] = ui->fingers[j];
            ui->finger_count--;
            break;
        }
    ui->now = (Uint32)(t * 1000);
    if (ui->finger_count > 0) {
        f->moved = 1;
        return;
    }
    if (ui->gesture == G_PINCH) {
        ui->gesture = G_NONE;
        return;
    }
    if (ui->gesture == G_PAN) {
        ui->pan_x = photon_clamp(ui->pan_x, -ui->pan_x, ui->pan_x);
        clamp_pan(ui);
    } else if (ui->gesture == G_SWIPE && fabsf(dx) > ui->width * .2)
        queue(ui, dx < 0 ? ANDROID_ACTION_NEXT : ANDROID_ACTION_PREVIOUS, 0, x, y);
    else if (ui->gesture == G_VERTICAL) {
        if (dy < -u(ui, 55))
            ui->info = 1;
        else if (dy > u(ui, 110))
            android_ui_show_home(ui);
    } else if (!moved && h) {
        if (h == hit_test(ui, x, y))
            perform(ui, h, x, y);
    } else if (!moved && ui->viewer) {
        if (ui->pending_tap && (ui->now - (Uint32)(ui->pending_tap_time * 1000)) < TAP_MS &&
            hypotf(x - ui->pending_tap_x, y - ui->pending_tap_y) < u(ui, 48)) {
            ui->pending_tap = 0;
            if (ui->zoom > 1.05) {
                ui->zoom = 1;
                reset_pan(ui);
            } else
                zoom_at(ui, fit_scale(ui) < 1 ? fminf(1 / fit_scale(ui), 3) : 2.5, x, y);
            kick(ui);
        } else {
            ui->pending_tap = 1;
            ui->pending_tap_x = x;
            ui->pending_tap_y = y;
            ui->pending_tap_time = t;
        }
    }
    ui->gesture = G_NONE;
    ui->pressed = HIT_NONE;
    ui->swipe_x = ui->swipe_y = 0;
    layout(ui);
}
void android_ui_update(AndroidUI *ui, float dt) {
    if (!ui)
        return;
    ui->now = SDL_GetTicks();
    if (ui->pending_tap && (ui->now - (Uint32)(ui->pending_tap_time * 1000)) > TAP_MS) {
        ui->pending_tap = 0;
        if (ui->controls)
            ui->controls = 0;
        else
            kick(ui);
    }
    if (ui->viewer && ui->controls && !ui->info && !ui->menu && !ui->dialog_open && !ui->error && !ui->loading &&
        ui->now >= ui->chrome_until)
        ui->controls = 0;
    if (!ui->finger_count && !ui->reduced_motion) {
        float lx, ly;
        pan_limits(ui, &lx, &ly);
        photon_spring_target(&ui->pan_x_spring, photon_clamp(ui->pan_x_spring.target, -lx, lx));
        photon_spring_target(&ui->pan_y_spring, photon_clamp(ui->pan_y_spring.target, -ly, ly));
        photon_spring_step(&ui->pan_x_spring, dt, .32, .86, 0);
        photon_spring_step(&ui->pan_y_spring, dt, .32, .86, 0);
        ui->pan_x = ui->pan_x_spring.value;
        ui->pan_y = ui->pan_y_spring.value;
    }
    layout(ui);
}
static void draw_image(AndroidUI *ui) {
    SDL_Texture *texture = ui->frame.image_texture;
    if (!texture)
        return;
    float scale = fit_scale(ui) * ui->zoom;
    float width = ui->image_w * scale, height = ui->image_h * scale;
    float x = (ui->width - width) / 2 + ui->pan_x + ui->swipe_x;
    float y = (ui->height - height) / 2 + ui->pan_y + ui->swipe_y;
#if SDL_VERSION_ATLEAST(2, 0, 10)
    SDL_FRect destination = {x, y, width, height};
    SDL_RenderCopyExF(ui->renderer, texture, NULL, &destination, ui->rotation, NULL, SDL_FLIP_NONE);
#else
    SDL_Rect destination = {(int)x, (int)y, (int)width, (int)height};
    SDL_RenderCopyEx(ui->renderer, texture, NULL, &destination, ui->rotation, NULL, SDL_FLIP_NONE);
#endif
}
static void draw_info(AndroidUI *ui) {
    SDL_Rect s = ui->info_sheet;
    fill(ui, s, ui->landscape ? 0 : u(ui, 26), (SDL_Color){27, 28, 33, 250});
    text(ui, PHOTON_FONT_LABEL, "Image info", s.x + u(ui, 22), s.y + u(ui, 20), s.w - u(ui, 44), text_color);
    text(ui, PHOTON_FONT_SMALL, ui->frame.image_name, s.x + u(ui, 22), s.y + u(ui, 47), s.w - u(ui, 44), muted);
    char b[96];
    const char *rows[] = {"Dimensions", "File size", "Zoom", "Rotation", "Position", "Format"};
    for (int i = 0; i < 6; i++) {
        int col = i % 2, row = i / 2, x = s.x + u(ui, 22) + col * (s.w / 2), y = s.y + u(ui, 76) + row * u(ui, 48);
        if (i == 0)
            snprintf(b, sizeof(b), "%d × %d px", ui->frame.image_width, ui->frame.image_height);
        else if (i == 1)
            snprintf(b, sizeof(b), "%.1f MB", ui->frame.image_size / 1048576.0);
        else if (i == 2)
            snprintf(b, sizeof(b), "%.0f%%", fit_scale(ui) * ui->zoom * 100);
        else if (i == 3)
            snprintf(b, sizeof(b), "%d°", ui->rotation);
        else if (i == 4)
            snprintf(b, sizeof(b), "%d of %d", ui->frame.image_index + 1, ui->frame.image_count);
        else
            snprintf(b, sizeof(b), "%s", ui->frame.image_format ? ui->frame.image_format : "Unknown");
        text(ui, PHOTON_FONT_SMALL, rows[i], x, y, s.w / 2 - u(ui, 25), muted);
        text(ui, PHOTON_FONT_BODY, b, x, y + u(ui, 17), s.w / 2 - u(ui, 25), text_color);
    }
    button(ui, ui->info_buttons[0], "Rotate", 0, 0);
    button(ui, ui->info_buttons[1], "Copy", 0, 0);
    button(ui, ui->info_buttons[2], "Share", 0, 0);
    button(ui, ui->info_buttons[3], "Delete", 0, 0);
}
static void draw_rail(AndroidUI *ui) {
    int n = ui->image_count > 0 ? ui->image_count : 1;
    int gap = u(ui, 3), tw = clamp_i((ui->rail_rect.w - gap * (n - 1)) / n, 2, ui->rail_rect.w);
    for (int i = 0; i < n && i < 4096; i++) {
        int x = ui->rail_rect.x + i * (tw + gap), h = i == ui->frame.image_index ? u(ui, 28) : u(ui, 12);
        fill(ui, (SDL_Rect){x, ui->rail_rect.y + (ui->rail_rect.h - h) / 2, tw, h}, u(ui, 2),
             i == ui->frame.image_index ? accent : (SDL_Color){255, 255, 255, 75});
    }
}
static void draw_home(AndroidUI *ui) {
    SDL_SetRenderDrawColor(ui->renderer, 18, 19, 23, 255);
    SDL_RenderClear(ui->renderer);
    SDL_Rect logo = ru(ui, 24, ui->landscape ? 20 : 50, 30, 30);
    fill(ui, logo, u(ui, 9), (SDL_Color){143, 220, 90, 255});
    center(ui, PHOTON_FONT_LABEL, "✦", logo, (SDL_Color){27, 36, 16, 255});
    text(ui, PHOTON_FONT_LABEL, "Photon", logo.x + u(ui, 40), logo.y + u(ui, 5), ui->width - logo.x - u(ui, 50),
         text_color);
    int y = u(ui, ui->landscape ? 112 : 190), x = u(ui, 24), w = ui->landscape ? ui->width / 2 : ui->width - u(ui, 48);
    text(ui, PHOTON_FONT_TITLE, "Your images.", x, y, w, text_color);
    text(ui, PHOTON_FONT_TITLE, "Nothing else.", x, y + u(ui, 31), w, text_color);
    text(ui, PHOTON_FONT_BODY, "Open one image. Swipe through the whole folder.", x, y + u(ui, 77), w, muted);
    if (ui->frame.image_name && ui->frame.image_name[0]) {
        text(ui, PHOTON_FONT_SMALL, "RECENT", x, u(ui, ui->landscape ? 245 : 580), w, muted);
        fill(ui, ru(ui, 20, ui->landscape ? 265 : 600, 350, 60), 12, (SDL_Color){27, 28, 33, 255});
        text(ui, PHOTON_FONT_BODY, ui->frame.image_name, x + u(ui, 16), u(ui, ui->landscape ? 278 : 613), w - u(ui, 30),
             text_color);
    }
    button(ui, ui->open_rect, "+  Open image", 1, 1);
    button(ui, ui->folder_rect, "Folder", 0, 0);
    center(ui, PHOTON_FONT_SMALL, "Private by default · Android chooses what Photon can read",
           ru(ui, 20, ui->landscape ? 356 : 782, 350, 24), muted);
}
void android_ui_render(AndroidUI *ui, const AndroidUIFrame *frame) {
    if (!ui)
        return;
    if (frame)
        ui->frame = *frame;
    ui->image_w = ui->frame.image_width;
    ui->image_h = ui->frame.image_height;
    ui->image_count = ui->frame.image_count;
    layout(ui);
    if (!ui->viewer) {
        draw_home(ui);
        return;
    }
    SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ui->renderer);
    if (!ui->error)
        draw_image(ui);
    if (ui->controls || ui->info || ui->menu || ui->dialog_open || ui->error) {
        fill(ui, (SDL_Rect){0, 0, ui->width, u(ui, 92)}, 0, (SDL_Color){0, 0, 0, 180});
        button(ui, ui->back_rect, "‹", 0, 0);
        center(ui, PHOTON_FONT_LABEL, ui->frame.image_name,
               (SDL_Rect){u(ui, 60), u(ui, 24), ui->width - u(ui, 120), u(ui, 25)}, text_color);
        button(ui, ui->more_rect, "⋮", 0, 0);
        if (!ui->error) {
            fill(ui, (SDL_Rect){0, ui->height - u(ui, 150), ui->width, u(ui, 150)}, 0, (SDL_Color){0, 0, 0, 180});
            char z[24];
            snprintf(z, sizeof(z), ui->zoom > 1.05 ? "%.0f%%" : "Fit", fit_scale(ui) * ui->zoom * 100);
            button(ui, ui->info_rect, "Info", 0, ui->info);
            button(ui, ui->zoom_rect, z, 1, ui->zoom <= 1.05);
            button(ui, ui->rotate_rect, "Rotate", 0, 0);
            draw_rail(ui);
        }
    }
    if (ui->error) {
        center(ui, PHOTON_FONT_TITLE, "Can’t decode this image", ru(ui, 25, ui->landscape ? 110 : 290, 340, 40),
               text_color);
        center(ui, PHOTON_FONT_BODY,
               ui->frame.error_message ? ui->frame.error_message : "The file is damaged or unsupported.",
               ru(ui, 35, ui->landscape ? 155 : 340, 320, 62), muted);
        button(ui, ui->error_next, "Skip to next", 0, 0);
        button(ui, ui->error_open, "Open another", 1, 1);
    }
    if (ui->info)
        draw_info(ui);
    if (ui->menu) {
        SDL_Rect m = {ui->width - u(ui, 236), u(ui, 80), u(ui, 220), u(ui, 8 * 46 + 20)};
        fill(ui, m, u(ui, 16), (SDL_Color){37, 38, 44, 255});
        const char *names[] = {"Rotate",        "Copy image",     "Share…",        "Open another",
                               "Gesture guide", "Reduced motion", "High contrast", "Delete image"};
        for (int i = 0; i < 8; i++)
            center(ui, PHOTON_FONT_BODY, names[i],
                   (SDL_Rect){m.x + u(ui, 8), m.y + u(ui, 8 + i * 46), m.w - u(ui, 16), u(ui, 44)}, text_color);
    }
    if (ui->dialog_open) {
        fill(ui, (SDL_Rect){0, 0, ui->width, ui->height}, 0, (SDL_Color){0, 0, 0, 170});
        fill(ui, ui->dialog, u(ui, 24), (SDL_Color){37, 38, 44, 255});
        text(ui, PHOTON_FONT_LABEL, "Remove this image from Photon?", ui->dialog.x + u(ui, 20),
             ui->dialog.y + u(ui, 22), ui->dialog.w - u(ui, 40), text_color);
        text(ui, PHOTON_FONT_BODY, "The original Android document stays safe.", ui->dialog.x + u(ui, 20),
             ui->dialog.y + u(ui, 65), ui->dialog.w - u(ui, 40), muted);
        button(ui, (SDL_Rect){ui->dialog.x + u(ui, 166), ui->dialog.y + u(ui, 154), u(ui, 74), u(ui, 44)}, "Cancel", 0,
               0);
        button(ui, (SDL_Rect){ui->dialog.x + u(ui, 245), ui->dialog.y + u(ui, 154), u(ui, 82), u(ui, 44)}, "Remove", 1,
               0);
    }
    if (ui->guide_until > ui->now)
        center(ui, PHOTON_FONT_SMALL, "Swipe ← → · double tap · pinch · drag",
               ru(ui, 35, ui->landscape ? 220 : 480, 320, 32), text_color);
}
