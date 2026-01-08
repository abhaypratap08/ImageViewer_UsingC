#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <ctype.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define WINDOW_TITLE "Photon"
#define MAX_PATH_LENGTH 4096
#define MAX_FILENAME_LENGTH 256
#define MAX_FILE_SIZE (100 * 1024 * 1024) // 100MB limit

#ifdef _WIN32
#undef main
#endif

typedef enum {
    SECURITY_OK,
    SECURITY_ERROR_INVALID_INPUT,
    SECURITY_ERROR_PATH_TOO_LONG,
    SECURITY_ERROR_FILE_TOO_LARGE,
    SECURITY_ERROR_ACCESS_DENIED,
    SECURITY_ERROR_MEMORY_ALLOCATION
} SecurityResult;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *image_texture;
    int window_width;
    int window_height;
    int image_width;
    int image_height;
    int running;
    float zoom;
    int pan_x;
    int pan_y;
    int fit_to_window;
    int show_info;
} App;

typedef struct {
    char filename[256];
    char filepath[512];
    int width;
    int height;
    long file_size;
    int bits_per_pixel;
    char format[32];
    time_t creation_time;
    time_t modification_time;
} ImageMetadata;

// Security functions
SecurityResult validate_filepath(const char *filepath) {
    if (!filepath) {
        return SECURITY_ERROR_INVALID_INPUT;
    }

    size_t len = strlen(filepath);
    if (len == 0 || len >= MAX_PATH_LENGTH) {
        return SECURITY_ERROR_PATH_TOO_LONG;
    }

    if (strstr(filepath, "..") != NULL) {
        return SECURITY_ERROR_ACCESS_DENIED;
    }

    if (memchr(filepath, '\0', len) != NULL) {
        return SECURITY_ERROR_INVALID_INPUT;
    }

    return SECURITY_OK;
}

SecurityResult sanitize_filename(char *filename, size_t max_len) {
    if (!filename || max_len == 0) {
        return SECURITY_ERROR_INVALID_INPUT;
    }

    size_t len = strlen(filename);
    if (len >= max_len) {
        return SECURITY_ERROR_PATH_TOO_LONG;
    }

    for (size_t i = 0; i < len && i < max_len - 1; i++) {
        switch (filename[i]) {
            case '<':
            case '>':
            case ':':
            case '"':
            case '|':
            case '?':
            case '*':
                filename[i] = '_';
                break;
            default:
                if (!isprint(filename[i]) && !isspace(filename[i])) {
                    filename[i] = '_';
                }
        }
    }

    filename[max_len - 1] = '\0';
    return SECURITY_OK;
}

SecurityResult validate_image_size(long file_size) {
    if (file_size < 0) {
        return SECURITY_ERROR_INVALID_INPUT;
    }

    if (file_size > MAX_FILE_SIZE) {
        return SECURITY_ERROR_FILE_TOO_LARGE;
    }

    return SECURITY_OK;
}

void secure_strncpy(char *dest, const char *src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) {
        return;
    }

    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

void secure_memzero(void *ptr, size_t size) {
    if (ptr && size > 0) {
        volatile char *p = (volatile char *)ptr;
        for (size_t i = 0; i < size; i++) {
            p[i] = 0;
        }
    }
}

SecurityResult safe_malloc(void **ptr, size_t size) {
    if (!ptr) {
        return SECURITY_ERROR_INVALID_INPUT;
    }

    if (size == 0 || size > SIZE_MAX / 2) {
        return SECURITY_ERROR_MEMORY_ALLOCATION;
    }

    *ptr = malloc(size);
    if (!*ptr) {
        return SECURITY_ERROR_MEMORY_ALLOCATION;
    }

    memset(*ptr, 0, size);
    return SECURITY_OK;
}

void safe_free(void **ptr) {
    if (ptr && *ptr) {
        secure_memzero(*ptr, 0);
        free(*ptr);
        *ptr = NULL;
    }
}

// Utility functions
void log_message(const char *message) {
    if (!message) {
        SDL_Log("Warning: Attempted to log NULL message");
        return;
    }
    
    size_t len = strlen(message);
    if (len > 1024) {
        SDL_Log("Warning: Message too long, truncating");
        char truncated[1025];
        secure_strncpy(truncated, message, sizeof(truncated));
        SDL_Log("%s", truncated);
        secure_memzero(truncated, sizeof(truncated));
    } else {
        SDL_Log("%s", message);
    }
}

const char* get_format_name(const char *filepath) {
    if (!filepath) return "Unknown";
    
    size_t len = strlen(filepath);
    if (len == 0 || len > MAX_PATH_LENGTH) return "Unknown";
    
    const char *ext = strrchr(filepath, '.');
    if (!ext) return "Unknown";
    
    ext++;
    if (strlen(ext) > 10) return "Unknown";
    
    if (strcasecmp(ext, "png") == 0) return "PNG";
    if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) return "JPEG";
    if (strcasecmp(ext, "bmp") == 0) return "BMP";
    if (strcasecmp(ext, "gif") == 0) return "GIF";
    
    return "Unknown";
}

char* format_file_size(long bytes) {
    static char buffer[64];
    const char *units[] = {"B", "KB", "MB", "GB"};
    int unit = 0;
    double size = (double)bytes;
    
    if (bytes < 0) {
        secure_strncpy(buffer, "Unknown", sizeof(buffer));
        return buffer;
    }
    
    while (size >= 1024.0 && unit < 3) {
        size /= 1024.0;
        unit++;
    }
    
    snprintf(buffer, sizeof(buffer), "%.1f %s", size, units[unit]);
    return buffer;
}

// Image loading functions
SecurityResult load_image_secure(App *app, const char *image_path) {
    if (!app || !image_path) {
        return SECURITY_ERROR_INVALID_INPUT;
    }

    SecurityResult result = validate_filepath(image_path);
    if (result != SECURITY_OK) {
        return result;
    }

    struct stat file_stat;
    if (stat(image_path, &file_stat) != 0) {
        return SECURITY_ERROR_ACCESS_DENIED;
    }

    result = validate_image_size(file_stat.st_size);
    if (result != SECURITY_OK) {
        return result;
    }

    SDL_Surface *surface = IMG_Load(image_path);
    if (!surface) {
        return SECURITY_ERROR_ACCESS_DENIED;
    }

    if (surface->w <= 0 || surface->h <= 0 || surface->w > 32768 || surface->h > 32768) {
        SDL_FreeSurface(surface);
        return SECURITY_ERROR_INVALID_INPUT;
    }

    if (app->image_texture) {
        SDL_DestroyTexture(app->image_texture);
        app->image_texture = NULL;
    }

    app->image_texture = SDL_CreateTextureFromSurface(app->renderer, surface);
    if (!app->image_texture) {
        SDL_FreeSurface(surface);
        return SECURITY_ERROR_MEMORY_ALLOCATION;
    }

    app->image_width = surface->w;
    app->image_height = surface->h;
    SDL_FreeSurface(surface);

    return SECURITY_OK;
}

int load_image(App *app, const char *image_path) {
    SecurityResult result = load_image_secure(app, image_path);
    
    switch (result) {
        case SECURITY_OK:
            SDL_Log("Loaded image: %s (%dx%d)", image_path, app->image_width, app->image_height);
            return 1;
        case SECURITY_ERROR_INVALID_INPUT:
            SDL_Log("Security error: Invalid input parameters");
            return 0;
        case SECURITY_ERROR_PATH_TOO_LONG:
            SDL_Log("Security error: File path too long");
            return 0;
        case SECURITY_ERROR_FILE_TOO_LARGE:
            SDL_Log("Security error: File size exceeds limit");
            return 0;
        case SECURITY_ERROR_ACCESS_DENIED:
            SDL_Log("Security error: Access denied or file not found");
            return 0;
        case SECURITY_ERROR_MEMORY_ALLOCATION:
            SDL_Log("Security error: Memory allocation failed");
            return 0;
        default:
            SDL_Log("Security error: Unknown error occurred");
            return 0;
    }
}

// Metadata functions
int extract_metadata(const char *filepath, ImageMetadata *metadata) {
    if (!filepath || !metadata) {
        return 0;
    }

    SecurityResult sec_result = validate_filepath(filepath);
    if (sec_result != SECURITY_OK) {
        return 0;
    }

    secure_strncpy(metadata->filepath, filepath, sizeof(metadata->filepath));

    const char *filename = strrchr(filepath, '/');
    if (!filename) {
        filename = strrchr(filepath, '\\');
    }
    if (filename) {
        filename++;
    } else {
        filename = filepath;
    }
    
    secure_strncpy(metadata->filename, filename, sizeof(metadata->filename));
    sec_result = sanitize_filename(metadata->filename, sizeof(metadata->filename));
    if (sec_result != SECURITY_OK) {
        return 0;
    }

    secure_strncpy(metadata->format, get_format_name(filepath), sizeof(metadata->format));

    struct stat file_stat;
    if (stat(filepath, &file_stat) == 0) {
        sec_result = validate_image_size(file_stat.st_size);
        if (sec_result != SECURITY_OK) {
            return 0;
        }
        
        metadata->file_size = file_stat.st_size;
        metadata->creation_time = file_stat.st_ctime;
        metadata->modification_time = file_stat.st_mtime;
    } else {
        metadata->file_size = 0;
        metadata->creation_time = 0;
        metadata->modification_time = 0;
    }

    SDL_Surface *surface = IMG_Load(filepath);
    if (surface) {
        metadata->width = surface->w;
        metadata->height = surface->h;
        metadata->bits_per_pixel = surface->format->BitsPerPixel;
        SDL_FreeSurface(surface);
    } else {
        metadata->width = 0;
        metadata->height = 0;
        metadata->bits_per_pixel = 0;
    }

    return 1;
}

void render_metadata_overlay(App *app, const ImageMetadata *metadata) {
    if (!app || !metadata || !app->show_info) {
        return;
    }

    // Create semi-transparent overlay background
    SDL_SetRenderDrawColor(app->renderer, 20, 20, 30, 230);
    SDL_Rect info_rect = {15, 15, 380, 200};
    SDL_RenderFillRect(app->renderer, &info_rect);

    // Draw border with gradient effect
    SDL_SetRenderDrawColor(app->renderer, 100, 150, 255, 255);
    SDL_RenderDrawRect(app->renderer, &info_rect);
    
    // Add inner border for depth
    SDL_SetRenderDrawColor(app->renderer, 150, 200, 255, 255);
    SDL_Rect inner_rect = {17, 17, 376, 196};
    SDL_RenderDrawRect(app->renderer, &inner_rect);

    // Title section
    SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
    SDL_Rect title_rect = {25, 25, 360, 30};
    SDL_RenderFillRect(app->renderer, &title_rect);
    
    // Render title text (simulated with rectangles for now)
    SDL_SetRenderDrawColor(app->renderer, 30, 30, 50, 255);
    SDL_Rect title_text_rect = {30, 30, 200, 20};
    SDL_RenderFillRect(app->renderer, &title_text_rect);

    char info_lines[8][300];
    int line_count = 0;
    int y_offset = 70;

    snprintf(info_lines[line_count++], 290, "File: %s", metadata->filename);
    snprintf(info_lines[line_count++], 290, "Format: %s", metadata->format);
    snprintf(info_lines[line_count++], 290, "Dimensions: %dx%d", metadata->width, metadata->height);
    snprintf(info_lines[line_count++], 290, "Size: %s", format_file_size(metadata->file_size));
    snprintf(info_lines[line_count++], 290, "Color Depth: %d bpp", metadata->bits_per_pixel);
    
    if (metadata->modification_time > 0) {
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", localtime(&metadata->modification_time));
        snprintf(info_lines[line_count++], 290, "Modified: %s", time_str);
    }

    snprintf(info_lines[line_count++], 290, "Zoom: %.1fx", app->zoom);

    // Render info lines with background for readability
    for (int i = 0; i < line_count; i++) {
        // Background for each line
        SDL_SetRenderDrawColor(app->renderer, 40, 40, 50, 200);
        SDL_Rect line_bg = {25, y_offset + i * 22 - 2, 350, 18};
        SDL_RenderFillRect(app->renderer, &line_bg);
        
        // Text placeholder (simulated with small rectangles)
        SDL_SetRenderDrawColor(app->renderer, 200, 200, 220, 255);
        SDL_Rect text_placeholder = {30, y_offset + i * 22, 8, 12};
        SDL_RenderFillRect(app->renderer, &text_placeholder);
        
        SDL_Log("Info: %s", info_lines[i]);
    }
    
    // Add decorative elements
    SDL_SetRenderDrawColor(app->renderer, 100, 150, 255, 255);
    SDL_Rect decor1 = {25, 175, 360, 2};
    SDL_RenderFillRect(app->renderer, &decor1);
    
    secure_memzero(info_lines, sizeof(info_lines));
}

// UI functions
void render_image(App *app) {
    if (!app) return;
    
    // Create gradient background
    SDL_SetRenderDrawColor(app->renderer, 25, 25, 35, 255);
    SDL_RenderClear(app->renderer);

    if (app->image_texture) {
        SDL_Rect dest_rect;
        
        if (app->fit_to_window) {
            float aspect_ratio = (float)app->image_width / app->image_height;
            float window_aspect_ratio = (float)app->window_width / app->window_height;

            if (aspect_ratio > window_aspect_ratio) {
                dest_rect.w = app->window_width;
                dest_rect.h = (int)(app->window_width / aspect_ratio);
                dest_rect.x = 0;
                dest_rect.y = (app->window_height - dest_rect.h) / 2;
            } else {
                dest_rect.h = app->window_height;
                dest_rect.w = (int)(app->window_height * aspect_ratio);
                dest_rect.x = (app->window_width - dest_rect.w) / 2;
                dest_rect.y = 0;
            }
        } else {
            dest_rect.w = (int)(app->image_width * app->zoom);
            dest_rect.h = (int)(app->image_height * app->zoom);
            
            if (dest_rect.w <= 0 || dest_rect.h <= 0 || 
                dest_rect.w > 65536 || dest_rect.h > 65536) {
                return;
            }
            
            dest_rect.x = app->pan_x + (app->window_width - dest_rect.w) / 2;
            dest_rect.y = app->pan_y + (app->window_height - dest_rect.h) / 2;
        }

        // Add subtle shadow effect
        SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 50);
        SDL_Rect shadow_rect = {dest_rect.x + 3, dest_rect.y + 3, dest_rect.w, dest_rect.h};
        SDL_RenderFillRect(app->renderer, &shadow_rect);
        
        // Render main image
        SDL_RenderCopy(app->renderer, app->image_texture, NULL, &dest_rect);
        
        // Add elegant border
        SDL_SetRenderDrawColor(app->renderer, 80, 80, 100, 255);
        SDL_RenderDrawRect(app->renderer, &dest_rect);
    }
}

void render_info_overlay(App *app) {
    if (!app || !app->show_info || !app->image_texture) {
        return;
    }

    // Create modern info overlay with glass effect
    SDL_SetRenderDrawColor(app->renderer, 10, 10, 20, 180);
    SDL_Rect info_rect = {15, 15, 200, 80};
    SDL_RenderFillRect(app->renderer, &info_rect);

    // Add glass border effect
    SDL_SetRenderDrawColor(app->renderer, 80, 120, 200, 255);
    SDL_RenderDrawRect(app->renderer, &info_rect);
    
    // Inner highlight
    SDL_SetRenderDrawColor(app->renderer, 120, 160, 255, 100);
    SDL_Rect highlight = {17, 17, 196, 76};
    SDL_RenderFillRect(app->renderer, &highlight);
    
    // Info text background
    SDL_SetRenderDrawColor(app->renderer, 30, 30, 40, 200);
    SDL_Rect text_bg = {22, 22, 186, 66};
    SDL_RenderFillRect(app->renderer, &text_bg);
    
    // Simulated text indicators
    SDL_SetRenderDrawColor(app->renderer, 200, 200, 220, 255);
    SDL_Rect text_indicators[] = {
        {25, 25, 60, 8},   // "Image:"
        {90, 25, 40, 8},   // "Info"
        {130, 25, 80, 8},  // dimensions
        {25, 45, 60, 8},   // "Size:"
        {90, 45, 40, 8},   // "Zoom:"
        {130, 45, 40, 8}   // zoom value
    };
    
    for (int i = 0; i < 6; i++) {
        SDL_RenderFillRect(app->renderer, &text_indicators[i]);
    }
    
    char info_text[256];
    secure_strncpy(info_text, "", sizeof(info_text));
    snprintf(info_text, sizeof(info_text), "Image: %dx%d | Zoom: %.1fx", 
            app->image_width, app->image_height, app->zoom);
    
    SDL_Log("Info: %s", info_text);
    secure_memzero(info_text, sizeof(info_text));
}

void handle_events(App *app) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                app->running = 0;
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    app->window_width = event.window.data1;
                    app->window_height = event.window.data2;
                }
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        app->running = 0;
                        break;
                    case SDLK_PLUS:
                    case SDLK_EQUALS:
                        app->zoom *= 1.2f;
                        app->fit_to_window = 0;
                        break;
                    case SDLK_MINUS:
                        app->zoom /= 1.2f;
                        app->fit_to_window = 0;
                        break;
                    case SDLK_f:
                        app->fit_to_window = 1;
                        app->zoom = 1.0f;
                        app->pan_x = 0;
                        app->pan_y = 0;
                        break;
                    case SDLK_1:
                        app->fit_to_window = 0;
                        app->zoom = 1.0f;
                        app->pan_x = 0;
                        app->pan_y = 0;
                        break;
                    case SDLK_i:
                        app->show_info = !app->show_info;
                        break;
                    case SDLK_LEFT:
                        break;
                    case SDLK_RIGHT:
                        break;
                }
                break;
            case SDL_MOUSEWHEEL:
                if (event.wheel.y > 0) {
                    app->zoom *= 1.1f;
                    app->fit_to_window = 0;
                } else if (event.wheel.y < 0) {
                    app->zoom /= 1.1f;
                    app->fit_to_window = 0;
                }
                break;
        }
    }
}

// Main application functions
int initialize_sdl(App *app) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
        return 0;
    }

    int img_flags = IMG_INIT_PNG | IMG_INIT_JPG;
    if (!(IMG_Init(img_flags) & img_flags)) {
        SDL_Log("Failed to initialize SDL_image: %s", IMG_GetError());
        SDL_Quit();
        return 0;
    }

    app->window = SDL_CreateWindow(
        WINDOW_TITLE,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI
    );

    if (!app->window) {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        IMG_Quit();
        SDL_Quit();
        return 0;
    }

    app->renderer = SDL_CreateRenderer(
        app->window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if (!app->renderer) {
        SDL_Log("Failed to create renderer: %s", SDL_GetError());
        SDL_DestroyWindow(app->window);
        IMG_Quit();
        SDL_Quit();
        return 0;
    }

    SDL_GetWindowSize(app->window, &app->window_width, &app->window_height);
    app->image_texture = NULL;
    app->image_width = 0;
    app->image_height = 0;
    app->running = 1;
    app->zoom = 1.0f;
    app->pan_x = 0;
    app->pan_y = 0;
    app->fit_to_window = 1;
    app->show_info = 0;

    return 1;
}

void cleanup(App *app) {
    if (!app) return;
    
    if (app->image_texture) {
        SDL_DestroyTexture(app->image_texture);
        app->image_texture = NULL;
    }
    if (app->renderer) {
        SDL_DestroyRenderer(app->renderer);
        app->renderer = NULL;
    }
    if (app->window) {
        SDL_DestroyWindow(app->window);
        app->window = NULL;
    }
    
    secure_memzero(app, sizeof(App));
    IMG_Quit();
    SDL_Quit();
}

void render(App *app) {
    render_image(app);
    render_info_overlay(app);
    SDL_RenderPresent(app->renderer);
}

int main(int argc, char *argv[]) {
    App app = {0};
    ImageMetadata metadata = {0};

    if (!initialize_sdl(&app)) {
        return 1;
    }

    if (argc > 1) {
        SecurityResult sec_result = validate_filepath(argv[1]);
        if (sec_result != SECURITY_OK) {
            SDL_Log("Security error: Invalid file path");
            cleanup(&app);
            return 1;
        }

        if (!load_image(&app, argv[1])) {
            SDL_Log("Failed to load specified image. Starting with empty viewer.");
        } else {
            extract_metadata(argv[1], &metadata);
        }
    } else {
        SDL_Log("Photon started - No image specified. Use command line argument to load an image.");
        SDL_Log("Controls: ESC=Exit, +/-=Zoom, F=Fit, 1=Actual Size, I=Toggle Info");
    }

    SDL_Log("Press ESC to exit");

    while (app.running) {
        handle_events(&app);
        render(&app);
        if (app.show_info && app.image_texture) {
            render_metadata_overlay(&app, &metadata);
        }
    }

    secure_memzero(&metadata, sizeof(metadata));
    cleanup(&app);
    return 0;
}
