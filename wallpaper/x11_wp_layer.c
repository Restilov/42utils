#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <jpeglib.h>
#include <png.h>

#define DEFAULT_FPS 10
#define MAX_FPS 60
#define MAX_FRAMES 300

enum {
    EXIT_OK = 0,
    EXIT_BAD_USAGE = 1,
    EXIT_RUNTIME_ERROR = 2
};

static volatile sig_atomic_t g_running = 1;
static char g_temp_dir[256] = {0};

static void print_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s <image.(jpg|jpeg|png)|video.(mp4|mkv|avi|webm|mov|gif)|directory> [--fps N] [--foreground]\n", program_name);
}

static void signal_handler(int signum) {
    (void)signum;
    g_running = 0;
}

static int install_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;

    if (sigaction(SIGTERM, &sa, NULL) != 0)
        return -1;
    if (sigaction(SIGINT, &sa, NULL) != 0)
        return -1;
    if (sigaction(SIGHUP, &sa, NULL) != 0)
        return -1;
    return 0;
}

static int daemonize_process(void) {
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid > 0)
        _exit(EXIT_OK);

    if (setsid() < 0)
        return -1;

    pid = fork();
    if (pid < 0)
        return -1;
    if (pid > 0)
        _exit(EXIT_OK);

    umask(0);
    if (chdir("/") < 0)
        return -1;

    int null_fd = open("/dev/null", O_RDWR);
    if (null_fd < 0)
        return -1;

    if (dup2(null_fd, STDIN_FILENO) < 0 ||
        dup2(null_fd, STDOUT_FILENO) < 0 ||
        dup2(null_fd, STDERR_FILENO) < 0) {
        close(null_fd);
        return -1;
    }

    if (null_fd > STDERR_FILENO)
        close(null_fd);
    return 0;
}

static unsigned char *read_jpeg_as_bgra(const char *filename, int *width, int *height) {
    struct jpeg_decompress_struct jpeg;
    struct jpeg_error_mgr jpeg_error;
    FILE *input_file = fopen(filename, "rb");
    if (!input_file)
        return NULL;

    jpeg.err = jpeg_std_error(&jpeg_error);
    jpeg_create_decompress(&jpeg);
    jpeg_stdio_src(&jpeg, input_file);
    jpeg_read_header(&jpeg, TRUE);
    jpeg_start_decompress(&jpeg);

    *width = (int)jpeg.output_width;
    *height = (int)jpeg.output_height;

    size_t pixel_count = (size_t)(*width) * (size_t)(*height);
    unsigned char *pixels = malloc(pixel_count * 4u);
    if (!pixels) {
        jpeg_finish_decompress(&jpeg);
        jpeg_destroy_decompress(&jpeg);
        fclose(input_file);
        return NULL;
    }

    JSAMPARRAY scanline = (*jpeg.mem->alloc_sarray)(
        (j_common_ptr)&jpeg,
        JPOOL_IMAGE,
        (JDIMENSION)(*width * 3),
        1);

    size_t out_index = 0;
    while (jpeg.output_scanline < jpeg.output_height) {
        jpeg_read_scanlines(&jpeg, scanline, 1);
        for (int x = 0; x < *width; ++x) {
            pixels[out_index + 0] = scanline[0][x * 3 + 2];
            pixels[out_index + 1] = scanline[0][x * 3 + 1];
            pixels[out_index + 2] = scanline[0][x * 3 + 0];
            pixels[out_index + 3] = 0;
            out_index += 4;
        }
    }

    jpeg_finish_decompress(&jpeg);
    jpeg_destroy_decompress(&jpeg);
    fclose(input_file);
    return pixels;
}

static unsigned char *read_png_as_bgra(const char *filename, int *width, int *height) {
    FILE *input_file = fopen(filename, "rb");
    if (!input_file)
        return NULL;

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(input_file);
        return NULL;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(input_file);
        return NULL;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(input_file);
        return NULL;
    }

    png_init_io(png, input_file);
    png_read_info(png, info);

    *width = (int)png_get_image_width(png, info);
    *height = (int)png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    if (bit_depth == 16)
        png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    if (!(color_type & PNG_COLOR_MASK_ALPHA))
        png_set_add_alpha(png, 0xff, PNG_FILLER_AFTER);

    png_read_update_info(png, info);

    size_t pixel_count = (size_t)(*width) * (size_t)(*height);
    unsigned char *pixels = malloc(pixel_count * 4u);
    png_bytep *rows = malloc((size_t)(*height) * sizeof(*rows));
    if (!pixels || !rows) {
        free(rows);
        free(pixels);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(input_file);
        return NULL;
    }

    for (int y = 0; y < *height; ++y)
        rows[y] = pixels + (size_t)y * (size_t)(*width) * 4u;

    png_read_image(png, rows);
    png_read_end(png, NULL);

    for (size_t i = 0; i < pixel_count; ++i) {
        unsigned char *pixel = pixels + i * 4u;
        unsigned char red = pixel[0];
        pixel[0] = pixel[2];
        pixel[2] = red;
        pixel[3] = 0;
    }

    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(input_file);
    return pixels;
}

static const char *get_file_extension(const char *filename) {
    const char *dot = strrchr(filename, '.');
    return dot ? dot + 1 : NULL;
}

static bool extension_equals(const char *extension, const char *value) {
    if (!extension || !value)
        return false;

    while (*extension && *value) {
        char a = *extension;
        char b = *value;
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');
        if (a != b)
            return false;
        ++extension;
        ++value;
    }

    return *extension == '\0' && *value == '\0';
}

static bool is_supported_image(const char *filename) {
    const char *ext = get_file_extension(filename);
    return extension_equals(ext, "jpg") ||
           extension_equals(ext, "jpeg") ||
           extension_equals(ext, "png");
}

static bool is_video_file(const char *filename) {
    const char *ext = get_file_extension(filename);
    return extension_equals(ext, "mp4") ||
           extension_equals(ext, "mkv") ||
           extension_equals(ext, "avi") ||
           extension_equals(ext, "webm") ||
           extension_equals(ext, "mov") ||
           extension_equals(ext, "gif");
}

static int extract_video_frames(const char *video_path, const char *out_dir, int fps) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "ffmpeg -i \"%s\" -vf fps=%d \"%s/frame_%%04d.png\" -y 2>/dev/null",
             video_path, fps, out_dir);
    return system(cmd);
}

static unsigned char *read_image_as_bgra(const char *filename, int *width, int *height) {
    const char *extension = get_file_extension(filename);

    if (extension_equals(extension, "jpg") || extension_equals(extension, "jpeg"))
        return read_jpeg_as_bgra(filename, width, height);
    if (extension_equals(extension, "png"))
        return read_png_as_bgra(filename, width, height);

    return NULL;
}

static unsigned char *scale_bgra_cover(
    const unsigned char *src_pixels,
    int src_width,
    int src_height,
    int dst_width,
    int dst_height) {
    if (!src_pixels || src_width <= 0 || src_height <= 0 || dst_width <= 0 || dst_height <= 0)
        return NULL;

    size_t dst_pixel_count = (size_t)dst_width * (size_t)dst_height;
    unsigned char *dst_pixels = malloc(dst_pixel_count * 4u);
    if (!dst_pixels)
        return NULL;

    double scale_x = (double)dst_width / (double)src_width;
    double scale_y = (double)dst_height / (double)src_height;
    double scale = (scale_x > scale_y) ? scale_x : scale_y;

    double sampled_width = (double)dst_width / scale;
    double sampled_height = (double)dst_height / scale;
    double offset_x = ((double)src_width - sampled_width) * 0.5;
    double offset_y = ((double)src_height - sampled_height) * 0.5;

    for (int y = 0; y < dst_height; ++y) {
        double src_y = offset_y + ((double)y + 0.5) / scale - 0.5;
        if (src_y < 0.0)
            src_y = 0.0;
        if (src_y > (double)(src_height - 1))
            src_y = (double)(src_height - 1);

        int y0 = (int)src_y;
        int y1 = (y0 + 1 < src_height) ? (y0 + 1) : y0;
        double wy = src_y - (double)y0;

        for (int x = 0; x < dst_width; ++x) {
            double src_x = offset_x + ((double)x + 0.5) / scale - 0.5;
            if (src_x < 0.0)
                src_x = 0.0;
            if (src_x > (double)(src_width - 1))
                src_x = (double)(src_width - 1);

            int x0 = (int)src_x;
            int x1 = (x0 + 1 < src_width) ? (x0 + 1) : x0;
            double wx = src_x - (double)x0;

            size_t src_index_00 = ((size_t)y0 * (size_t)src_width + (size_t)x0) * 4u;
            size_t src_index_10 = ((size_t)y0 * (size_t)src_width + (size_t)x1) * 4u;
            size_t src_index_01 = ((size_t)y1 * (size_t)src_width + (size_t)x0) * 4u;
            size_t src_index_11 = ((size_t)y1 * (size_t)src_width + (size_t)x1) * 4u;
            size_t dst_index = ((size_t)y * (size_t)dst_width + (size_t)x) * 4u;

            for (int channel = 0; channel < 4; ++channel) {
                double top = (double)src_pixels[src_index_00 + (size_t)channel] * (1.0 - wx) +
                             (double)src_pixels[src_index_10 + (size_t)channel] * wx;
                double bottom = (double)src_pixels[src_index_01 + (size_t)channel] * (1.0 - wx) +
                                (double)src_pixels[src_index_11 + (size_t)channel] * wx;
                double value = top * (1.0 - wy) + bottom * wy;
                dst_pixels[dst_index + (size_t)channel] = (unsigned char)(value + 0.5);
            }
        }
    }

    return dst_pixels;
}

static int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

static int load_frame_paths(const char *dirpath, char ***out_paths, int *out_count) {
    DIR *dir = opendir(dirpath);
    if (!dir)
        return -1;

    char **paths = NULL;
    int count = 0;
    int capacity = 0;
    size_t dirlen = strlen(dirpath);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;
        if (!is_supported_image(entry->d_name))
            continue;

        if (count >= capacity) {
            capacity = capacity ? capacity * 2 : 64;
            char **tmp = realloc(paths, (size_t)capacity * sizeof(*paths));
            if (!tmp) {
                for (int i = 0; i < count; ++i)
                    free(paths[i]);
                free(paths);
                closedir(dir);
                return -1;
            }
            paths = tmp;
        }

        size_t namelen = strlen(entry->d_name);
        bool need_slash = (dirlen > 0 && dirpath[dirlen - 1] != '/');
        size_t pathlen = dirlen + (need_slash ? 1 : 0) + namelen + 1;
        char *fullpath = malloc(pathlen);
        if (!fullpath) {
            for (int i = 0; i < count; ++i)
                free(paths[i]);
            free(paths);
            closedir(dir);
            return -1;
        }

        if (need_slash)
            snprintf(fullpath, pathlen, "%s/%s", dirpath, entry->d_name);
        else
            snprintf(fullpath, pathlen, "%s%s", dirpath, entry->d_name);

        paths[count++] = fullpath;
    }
    closedir(dir);

    if (count == 0) {
        free(paths);
        return -1;
    }

    qsort(paths, (size_t)count, sizeof(*paths), compare_strings);

    if (count > MAX_FRAMES) {
        fprintf(stderr, "Warning: %d frames found, using first %d.\n", count, MAX_FRAMES);
        for (int i = MAX_FRAMES; i < count; ++i)
            free(paths[i]);
        count = MAX_FRAMES;
    }

    *out_paths = paths;
    *out_count = count;
    return 0;
}

static Window find_desktop_window(Display *display, int screen) {
    Atom net_client_list = XInternAtom(display, "_NET_CLIENT_LIST", False);
    Atom net_wm_window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom net_wm_type_desktop = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;

    if (XGetWindowProperty(display, RootWindow(display, screen),
                           net_client_list, 0, 1024, False,
                           XA_WINDOW, &actual_type, &actual_format,
                           &nitems, &bytes_after, &prop) != Success || !prop)
        return 0;

    Window *windows = (Window *)prop;
    Window result = 0;
    for (unsigned long i = 0; i < nitems; i++) {
        unsigned char *type_prop = NULL;
        unsigned long type_nitems, type_bytes;
        int type_format;
        Atom type_actual;
        if (XGetWindowProperty(display, windows[i],
                               net_wm_window_type, 0, 1, False,
                               XA_ATOM, &type_actual, &type_format,
                               &type_nitems, &type_bytes, &type_prop) == Success && type_prop) {
            if (*(Atom *)type_prop == net_wm_type_desktop)
                result = windows[i];
            XFree(type_prop);
            if (result)
                break;
        }
    }
    XFree(prop);
    return result;
}

static Pixmap update_window_frame(
    Display *display,
    int screen,
    Window window,
    const char *image_path,
    Pixmap old_pixmap) {
    Window root = RootWindow(display, screen);
    int screen_width = DisplayWidth(display, screen);
    int screen_height = DisplayHeight(display, screen);

    int image_width = 0;
    int image_height = 0;
    unsigned char *source_pixels = read_image_as_bgra(image_path, &image_width, &image_height);
    if (!source_pixels)
        return 0;

    unsigned char *pixels = scale_bgra_cover(
        source_pixels, image_width, image_height,
        screen_width, screen_height);
    free(source_pixels);
    if (!pixels)
        return 0;

    XImage *image = XCreateImage(
        display,
        DefaultVisual(display, screen),
        DefaultDepth(display, screen),
        ZPixmap, 0,
        (char *)pixels,
        screen_width, screen_height,
        32, 0);
    if (!image) {
        free(pixels);
        return 0;
    }

    Pixmap pixmap = XCreatePixmap(
        display, root,
        (unsigned int)screen_width,
        (unsigned int)screen_height,
        DefaultDepth(display, screen));
    if (!pixmap) {
        image->data = NULL;
        XDestroyImage(image);
        free(pixels);
        return 0;
    }

    XPutImage(display, pixmap, DefaultGC(display, screen), image,
              0, 0, 0, 0,
              (unsigned int)screen_width,
              (unsigned int)screen_height);

    XCopyArea(display, pixmap, window, DefaultGC(display, screen),
              0, 0,
              (unsigned int)screen_width,
              (unsigned int)screen_height,
              0, 0);
    XFlush(display);

    if (old_pixmap)
        XFreePixmap(display, old_pixmap);

    image->data = NULL;
    XDestroyImage(image);
    free(pixels);

    return pixmap;
}

static int create_desktop_wallpaper_window(
    Display *display,
    int screen,
    const char *image_path,
    Window *out_window,
    Pixmap *out_pixmap,
    XImage **out_image,
    unsigned char **out_pixels) {
    Window root = RootWindow(display, screen);
    int screen_width = DisplayWidth(display, screen);
    int screen_height = DisplayHeight(display, screen);

    int image_width = 0;
    int image_height = 0;
    unsigned char *source_pixels = read_image_as_bgra(image_path, &image_width, &image_height);
    if (!source_pixels)
        return -1;

    unsigned char *pixels = scale_bgra_cover(
        source_pixels,
        image_width,
        image_height,
        screen_width,
        screen_height);
    free(source_pixels);
    if (!pixels)
        return -1;

    XImage *image = XCreateImage(
        display,
        DefaultVisual(display, screen),
        DefaultDepth(display, screen),
        ZPixmap,
        0,
        (char *)pixels,
        screen_width,
        screen_height,
        32,
        0);
    if (!image) {
        free(pixels);
        return -1;
    }

    Pixmap pixmap = XCreatePixmap(
        display,
        root,
        (unsigned int)screen_width,
        (unsigned int)screen_height,
        DefaultDepth(display, screen));
    if (!pixmap) {
        image->data = NULL;
        XDestroyImage(image);
        free(pixels);
        return -1;
    }

    XPutImage(display, pixmap, DefaultGC(display, screen), image,
              0, 0, 0, 0, (unsigned int)screen_width, (unsigned int)screen_height);

    XSetWindowAttributes attrs;
    memset(&attrs, 0, sizeof(attrs));
    attrs.background_pixmap = pixmap;
    attrs.override_redirect = False;

    Window desktop_window = XCreateWindow(
        display,
        root,
        0,
        0,
        (unsigned int)screen_width,
        (unsigned int)screen_height,
        0,
        DefaultDepth(display, screen),
        InputOutput,
        DefaultVisual(display, screen),
        CWBackPixmap | CWOverrideRedirect,
        &attrs);
    if (!desktop_window) {
        image->data = NULL;
        XDestroyImage(image);
        free(pixels);
        XFreePixmap(display, pixmap);
        return -1;
    }

    Atom wm_window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom wm_type_desktop = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    XChangeProperty(display, desktop_window, wm_window_type, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&wm_type_desktop, 1);

    Atom wm_state = XInternAtom(display, "_NET_WM_STATE", False);
    Atom wm_state_below = XInternAtom(display, "_NET_WM_STATE_BELOW", False);
    XChangeProperty(display, desktop_window, wm_state, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&wm_state_below, 1);

    XShapeCombineRectangles(display, desktop_window, ShapeInput,
                            0, 0, NULL, 0, ShapeSet, Unsorted);

    XMapWindow(display, desktop_window);
    XLowerWindow(display, desktop_window);
    XFlush(display);

    *out_window = desktop_window;
    *out_pixmap = pixmap;
    *out_image = image;
    *out_pixels = pixels;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_BAD_USAGE;
    }

    const char *target = argv[1];
    bool foreground = false;
    int fps = DEFAULT_FPS;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--foreground") == 0) {
            foreground = true;
        } else if (strcmp(argv[i], "--fps") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --fps requires a value.\n");
                return EXIT_BAD_USAGE;
            }
            fps = atoi(argv[++i]);
            if (fps < 1)
                fps = 1;
            if (fps > MAX_FPS)
                fps = MAX_FPS;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_BAD_USAGE;
        }
    }

    struct stat st;
    if (stat(target, &st) != 0) {
        perror(target);
        return EXIT_RUNTIME_ERROR;
    }

    bool animated = S_ISDIR(st.st_mode);

    if (!animated && is_video_file(target)) {
        if (system("command -v ffmpeg >/dev/null 2>&1") != 0) {
            fprintf(stderr, "Error: ffmpeg not found.\n");
            fprintf(stderr, "\n");
            fprintf(stderr, "Video files cannot be used directly without ffmpeg.\n");
            fprintf(stderr, "You can extract frames using ffmpeg or an online frame extractor.\n");
            fprintf(stderr, "\n");
            fprintf(stderr, "With ffmpeg:\n");
            fprintf(stderr, "  mkdir -p ~/frames\n");
            fprintf(stderr, "  ffmpeg -i \"%s\" -vf fps=%d ~/frames/frame_%%04d.png\n", target, fps);
            fprintf(stderr, "\n");
            fprintf(stderr, "With an online tool: search 'video to frames extractor' in your browser,\n");
            fprintf(stderr, "download the frames as a zip, extract them into a folder (e.g. ~/frames/).\n");
            fprintf(stderr, "\n");
            fprintf(stderr, "Then run:\n");
            fprintf(stderr, "  xwpbg ~/frames/ --fps %d\n", fps);
            return EXIT_RUNTIME_ERROR;
        }
        snprintf(g_temp_dir, sizeof(g_temp_dir), "/tmp/xwpbg_XXXXXX");
        if (!mkdtemp(g_temp_dir)) {
            perror("mkdtemp");
            return EXIT_RUNTIME_ERROR;
        }
        fprintf(stderr, "Extracting frames from video at %d fps...\n", fps);
        if (extract_video_frames(target, g_temp_dir, fps) != 0) {
            fprintf(stderr, "ffmpeg failed.\n");
            rmdir(g_temp_dir);
            return EXIT_RUNTIME_ERROR;
        }
        target = g_temp_dir;
        animated = true;
    }

    if (!foreground && daemonize_process() != 0) {
        perror("daemonize");
        return EXIT_RUNTIME_ERROR;
    }

    if (install_signal_handlers() != 0) {
        perror("sigaction");
        return EXIT_RUNTIME_ERROR;
    }

    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Failed to open X display.\n");
        return EXIT_RUNTIME_ERROR;
    }

    int screen = DefaultScreen(display);

    if (animated) {
        char **frame_paths = NULL;
        int frame_count = 0;

        if (load_frame_paths(target, &frame_paths, &frame_count) != 0) {
            fprintf(stderr, "No valid image frames found in: %s\n", target);
            XCloseDisplay(display);
            return EXIT_RUNTIME_ERROR;
        }

        fprintf(stderr, "Found %d frames, running at %d fps...\n", frame_count, fps);

        Window anim_window = find_desktop_window(display, screen);
        if (!anim_window) {
            fprintf(stderr, "Failed to find desktop window.\n");
            for (int i = 0; i < frame_count; ++i)
                free(frame_paths[i]);
            free(frame_paths);
            XCloseDisplay(display);
            return EXIT_RUNTIME_ERROR;
        }
        fprintf(stderr, "Drawing to desktop window: 0x%lx\n", anim_window);

        long frame_usec = 1000000L / fps;
        int current_frame = 0;
        Pixmap current_pixmap = 0;

        while (g_running) {
            struct timespec t0;
            clock_gettime(CLOCK_MONOTONIC, &t0);

            Pixmap new_pixmap = update_window_frame(
                display, screen, anim_window,
                frame_paths[current_frame],
                current_pixmap);

            if (new_pixmap)
                current_pixmap = new_pixmap;

            current_frame = (current_frame + 1) % frame_count;

            struct timespec t1;
            clock_gettime(CLOCK_MONOTONIC, &t1);
            long elapsed = (t1.tv_sec - t0.tv_sec) * 1000000L
                         + (t1.tv_nsec - t0.tv_nsec) / 1000L;
            long remaining = frame_usec - elapsed;
            if (remaining > 0)
                usleep((useconds_t)remaining);
        }

        if (current_pixmap)
            XFreePixmap(display, current_pixmap);

        for (int i = 0; i < frame_count; ++i)
            free(frame_paths[i]);
        free(frame_paths);
    } else {
        Window wallpaper_window = 0;
        Pixmap wallpaper_pixmap = 0;
        XImage *wallpaper_image = NULL;
        unsigned char *wallpaper_pixels = NULL;

        if (create_desktop_wallpaper_window(
                display, screen, target,
                &wallpaper_window,
                &wallpaper_pixmap,
                &wallpaper_image,
                &wallpaper_pixels) != 0) {
            fprintf(stderr, "Failed to create desktop wallpaper window.\n");
            XCloseDisplay(display);
            return EXIT_RUNTIME_ERROR;
        }

        while (g_running) {
            XLowerWindow(display, wallpaper_window);
            XFlush(display);
            sleep(5);
        }

        XDestroyWindow(display, wallpaper_window);
        XFreePixmap(display, wallpaper_pixmap);
        wallpaper_image->data = NULL;
        XDestroyImage(wallpaper_image);
        free(wallpaper_pixels);
    }

    XCloseDisplay(display);

    if (g_temp_dir[0]) {
        char cmd[300];
        snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", g_temp_dir);
        system(cmd);
    }

    return EXIT_OK;
}
