#ifndef PHOTON_ANDROID_UI_H
#define PHOTON_ANDROID_UI_H

#ifdef __ANDROID__
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif
#include "../ui.h"
#include <time.h>

typedef struct AndroidUI AndroidUI;
typedef enum {
    ANDROID_ACTION_NONE = 0,
    ANDROID_ACTION_OPEN_IMAGE,
    ANDROID_ACTION_OPEN_FOLDER,
    ANDROID_ACTION_PREVIOUS,
    ANDROID_ACTION_NEXT,
    ANDROID_ACTION_NAVIGATE_INDEX,
    ANDROID_ACTION_ROTATE,
    ANDROID_ACTION_COPY,
    ANDROID_ACTION_SHARE,
    ANDROID_ACTION_DELETE_CONFIRM
} AndroidActionType;
typedef struct {
    AndroidActionType type;
    int index;
    float x, y;
} AndroidAction;
typedef struct {
    SDL_Texture *image_texture;
    int image_width, image_height;
    const char *image_name, *image_format, *image_folder, *error_message;
    int image_index, image_count;
    long image_size;
    time_t image_modified;
} AndroidUIFrame;
AndroidUI *android_ui_create(SDL_Renderer *, PhotonUI *, int, int);
void android_ui_destroy(AndroidUI *);
void android_ui_resize(AndroidUI *, int, int);
void android_ui_update(AndroidUI *, float);
void android_ui_render(AndroidUI *, const AndroidUIFrame *);
void android_ui_touch_down(AndroidUI *, int, float, float, double);
void android_ui_touch_move(AndroidUI *, int, float, float, double);
void android_ui_touch_up(AndroidUI *, int, float, float, double);
int android_ui_next_action(AndroidUI *, AndroidAction *);
void android_ui_show_home(AndroidUI *);
void android_ui_show_viewer(AndroidUI *);
void android_ui_set_image(AndroidUI *);
void android_ui_set_error(AndroidUI *, int, const char *);
void android_ui_set_loading(AndroidUI *, int, const char *);
void android_ui_set_rotation(AndroidUI *, int);
int android_ui_action_back(AndroidUI *);
int android_ui_is_immersive(const AndroidUI *);
void android_ui_preferences(AndroidUI *, int, int, int);
#endif
