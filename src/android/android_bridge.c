#include "android_bridge.h"

#ifdef __ANDROID__

#include <jni.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_system.h>

static JNIEnv *jni_env(void) {
    return (JNIEnv *)SDL_AndroidGetJNIEnv();
}

/* SDL_AndroidGetActivity returns a local JNI reference, not a global one. */
static jclass activity_class(JNIEnv *env, jobject *activity) {
    *activity = (jobject)SDL_AndroidGetActivity();
    return *activity ? (*env)->GetObjectClass(env, *activity) : NULL;
}

static void clear_exception(JNIEnv *env) {
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
}

static void call_static_void(const char *name, const char *signature, jvalue *args) {
    JNIEnv *env = jni_env();
    jobject activity = NULL;
    jclass cls;
    jmethodID method;
    if (!env)
        return;
    cls = activity_class(env, &activity);
    if (!cls) {
        if (activity)
            (*env)->DeleteLocalRef(env, activity);
        return;
    }
    method = (*env)->GetStaticMethodID(env, cls, name, signature);
    if (method)
        (*env)->CallStaticVoidMethodA(env, cls, method, args);
    clear_exception(env);
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, activity);
}

/* Caller owns the copied string; no Java reference can outlive this JNI call. */
static char *call_static_string_result(const char *name, const char *argument) {
    JNIEnv *env = jni_env();
    jobject activity = NULL;
    jclass cls;
    jmethodID method;
    jstring param = NULL, result = NULL;
    const char *utf;
    char *copy = NULL;
    if (!env)
        return NULL;
    cls = activity_class(env, &activity);
    if (!cls) {
        if (activity)
            (*env)->DeleteLocalRef(env, activity);
        return NULL;
    }
    method = (*env)->GetStaticMethodID(env, cls, name,
                                       argument ? "(Ljava/lang/String;)Ljava/lang/String;" : "()Ljava/lang/String;");
    if (argument)
        param = (*env)->NewStringUTF(env, argument);
    if (method && (!argument || param))
        result = (jstring)(argument ? (*env)->CallStaticObjectMethod(env, cls, method, param)
                                    : (*env)->CallStaticObjectMethod(env, cls, method));
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        result = NULL;
    }
    if (result) {
        utf = (*env)->GetStringUTFChars(env, result, NULL);
        if (utf) {
            size_t length = strlen(utf);
            if (length <= 1024 * 1024) {
                copy = malloc(length + 1);
                if (copy)
                    memcpy(copy, utf, length + 1);
            }
            (*env)->ReleaseStringUTFChars(env, result, utf);
        }
        clear_exception(env);
        (*env)->DeleteLocalRef(env, result);
    }
    if (param)
        (*env)->DeleteLocalRef(env, param);
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, activity);
    return copy;
}

const char *android_font_path(void) {
    static char path[4096];
    char *result = call_static_string_result("getFontPath", NULL);
    if (!result)
        return NULL;
    strncpy(path, result, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    free(result);
    return path[0] ? path : NULL;
}
char *android_last_session_paths(void) {
    return call_static_string_result("getLastSessionPaths", NULL);
}
char *android_document_name(const char *path) {
    return call_static_string_result("getDocumentName", path);
}
char *android_document_location(const char *path) {
    return call_static_string_result("getDocumentLocation", path);
}
char *android_recents_json(void) {
    return call_static_string_result("getRecents", NULL);
}

void android_clear_recents(void) {
    call_static_void("clearRecents", "()V", NULL);
}
void android_finish_viewer(void) {
    call_static_void("finishViewer", "()V", NULL);
}

void android_open_picker(int folder) {
    jvalue args[1];
    args[0].z = folder ? JNI_TRUE : JNI_FALSE;
    call_static_void("openPicker", "(Z)V", args);
}

void android_set_immersive(int immersive) {
    jvalue args[1];
    args[0].z = immersive ? JNI_TRUE : JNI_FALSE;
    call_static_void("setPhotonImmersive", "(Z)V", args);
}

static void call_static_string(const char *name, const char *path) {
    JNIEnv *env = jni_env();
    jobject activity = NULL;
    jclass cls;
    jmethodID method;
    jstring value;
    if (!env || !path)
        return;
    cls = activity_class(env, &activity);
    if (!cls) {
        if (activity)
            (*env)->DeleteLocalRef(env, activity);
        return;
    }
    method = (*env)->GetStaticMethodID(env, cls, name, "(Ljava/lang/String;)V");
    value = (*env)->NewStringUTF(env, path);
    if (method && value)
        (*env)->CallStaticVoidMethod(env, cls, method, value);
    clear_exception(env);
    if (value)
        (*env)->DeleteLocalRef(env, value);
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, activity);
}

void android_copy_image(const char *path) {
    call_static_string("copyImage", path);
}
void android_share_image(const char *path) {
    call_static_string("shareImage", path);
}

int android_request_delete_document(const char *path) {
    JNIEnv *env = jni_env();
    jobject activity = NULL;
    jclass cls;
    jmethodID method;
    jstring value;
    int result = 0;
    if (!env || !path)
        return 0;
    cls = activity_class(env, &activity);
    if (!cls) {
        if (activity)
            (*env)->DeleteLocalRef(env, activity);
        return 0;
    }
    method = (*env)->GetStaticMethodID(env, cls, "requestDeleteDocument", "(Ljava/lang/String;)Z");
    value = (*env)->NewStringUTF(env, path);
    if (method && value)
        result = (*env)->CallStaticBooleanMethod(env, cls, method, value) == JNI_TRUE;
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        result = 0;
    }
    if (value)
        (*env)->DeleteLocalRef(env, value);
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, activity);
    return result;
}

int android_display_metrics(int metrics[5]) {
    JNIEnv *env = jni_env();
    jobject activity = NULL;
    jclass cls;
    jmethodID method;
    jintArray values = NULL;
    if (!env || !metrics)
        return 0;
    cls = activity_class(env, &activity);
    if (!cls) {
        if (activity)
            (*env)->DeleteLocalRef(env, activity);
        return 0;
    }
    method = (*env)->GetStaticMethodID(env, cls, "getDisplayMetrics", "()[I");
    if (method)
        values = (jintArray)(*env)->CallStaticObjectMethod(env, cls, method);
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        values = NULL;
    }
    if (values && (*env)->GetArrayLength(env, values) >= 5)
        (*env)->GetIntArrayRegion(env, values, 0, 5, (jint *)metrics);
    else {
        if (values)
            (*env)->DeleteLocalRef(env, values);
        values = NULL;
    }
    clear_exception(env);
    if (values)
        (*env)->DeleteLocalRef(env, values);
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, activity);
    return values != NULL;
}

static int push_event(int code, const char *payload) {
    SDL_Event event;
    char *copy = NULL;
    if (payload) {
        size_t length = strlen(payload);
        if (length > 1024 * 1024)
            return 0;
        copy = malloc(length + 1);
        if (!copy)
            return 0;
        memcpy(copy, payload, length + 1);
    }
    memset(&event, 0, sizeof(event));
    event.type = SDL_USEREVENT;
    event.user.code = code;
    event.user.data1 = copy; /* SDL consumer owns and must free this (including error events). */
    if (SDL_PushEvent(&event) == 1)
        return 1;
    free(copy);
    return 0;
}

JNIEXPORT void JNICALL Java_com_abhaypratap_photon_PhotonActivity_nativeDocumentSelected(JNIEnv *env, jclass clazz,
                                                                                         jstring paths) {
    const char *utf;
    (void)clazz;
    if (!paths)
        return;
    utf = (*env)->GetStringUTFChars(env, paths, NULL);
    if (utf) {
        push_event(PHOTON_ANDROID_EVENT_DOCUMENT, utf);
        (*env)->ReleaseStringUTFChars(env, paths, utf);
    }
}

JNIEXPORT void JNICALL Java_com_abhaypratap_photon_PhotonActivity_nativeDocumentMessage(JNIEnv *env, jclass clazz,
                                                                                        jstring message) {
    const char *utf;
    (void)clazz;
    if (!message)
        return;
    utf = (*env)->GetStringUTFChars(env, message, NULL);
    if (utf) {
        push_event(PHOTON_ANDROID_EVENT_MESSAGE, utf);
        (*env)->ReleaseStringUTFChars(env, message, utf);
    }
}

JNIEXPORT jboolean JNICALL Java_com_abhaypratap_photon_PhotonActivity_nativeBackPressed(JNIEnv *env, jclass clazz) {
    (void)env;
    (void)clazz;
    return push_event(PHOTON_ANDROID_EVENT_BACK, NULL) ? JNI_TRUE : JNI_FALSE;
}

#endif
