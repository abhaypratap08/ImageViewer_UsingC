#ifndef PHOTON_ANDROID_BRIDGE_H
#define PHOTON_ANDROID_BRIDGE_H

#ifdef __ANDROID__
/* SDL_USEREVENT codes. data1 is malloc-owned UTF-8 for DOCUMENT and MESSAGE;
 * the SDL consumer MUST free it, even when ignoring a message. BACK has NULL data1. */
#define PHOTON_ANDROID_EVENT_DOCUMENT 0x50484401
#define PHOTON_ANDROID_EVENT_BACK 0x50484402
#define PHOTON_ANDROID_EVENT_MESSAGE 0x50484403

const char *android_font_path(void);
void android_open_picker(int folder);
void android_set_immersive(int immersive);
void android_copy_image(const char *path);
void android_share_image(const char *path);
void android_finish_viewer(void);
/* Returns 1 only if a confirmation was offered for a write-granted SAF source.
 * Otherwise the caller must label its own local operation 'Remove from session'. */
int android_request_delete_document(const char *path);
/* Pixels: densityDpi, left, top, right, bottom. 1 on success. */
int android_display_metrics(int metrics[5]);
/* Heap strings: caller must free(). Paths are newline-delimited, capped at 128. */
char *android_last_session_paths(void);
char *android_document_name(const char *path);
char *android_document_location(const char *path);
/* JSON [{"uri":"content://...","name":"...","location":"..."}], <= 32 entries. */
char *android_recents_json(void);
void android_clear_recents(void);
#endif

#endif
