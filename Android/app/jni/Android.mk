LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := main

LOCAL_SRC_FILES := \
    ../../../src/main.c \
    ../../../src/motion.c \
    ../../../src/ui.c \
    ../../../src/android/android_ui.c \
    ../../../src/android/android_bridge.c

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/SDL/include/SDL2 \
    $(LOCAL_PATH)/SDL_image/include/SDL2 \
    $(LOCAL_PATH)/SDL_ttf \
    $(LOCAL_PATH)/SDL_ttf/external/freetype/include \
    $(LOCAL_PATH)/SDL_ttf/external/harfbuzz/src \
    $(LOCAL_PATH)/../../../src

LOCAL_CFLAGS := \
    -std=c99 \
    -O2 \
    -Wall \
    -Wextra \
    -fPIC \
    -D_POSIX_C_SOURCE=200809L

LOCAL_SHARED_LIBRARIES := \
    SDL2 \
    SDL2_image \
    SDL2_ttf

LOCAL_LDLIBS := \
    -llog \
    -landroid

include $(BUILD_SHARED_LIBRARY)

$(call import-add-path,$(LOCAL_PATH))
$(call import-add-path,$(LOCAL_PATH)/SDL_ttf)

$(call import-module,SDL)
$(call import-module,SDL_image)
$(call import-module,SDL_ttf)
