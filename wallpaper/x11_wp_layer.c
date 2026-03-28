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
#include <jpeglib.h>

enum {
    EXIT_OK = 0,
    EXIT_BAD_USAGE = 1,
    EXIT_RUNTIME_ERROR = 2
};

static volatile sig_atomic_t g_running = 1;

static void print_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s <image.jpg> [--foreground]\n", program_name);
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

static int create_desktop_wallpaper_window(
    Display *display,
    int screen,
    const char *image_path,
    Window *out_window,
    Pixmap *out_pixmap,
    XImage **out_image,
    unsigned char **out_pixels) {
    Window root = RootWindow(display, screen);

    int image_width = 0;
    int image_height = 0;
    unsigned char *pixels = read_jpeg_as_bgra(image_path, &image_width, &image_height);
    if (!pixels)
        return -1;

    XImage *image = XCreateImage(
        display,
        DefaultVisual(display, screen),
        DefaultDepth(display, screen),
        ZPixmap,
        0,
        (char *)pixels,
        image_width,
        image_height,
        32,
        0);
    if (!image) {
        free(pixels);
        return -1;
    }

    Pixmap pixmap = XCreatePixmap(
        display,
        root,
        (unsigned int)image_width,
        (unsigned int)image_height,
        DefaultDepth(display, screen));
    if (!pixmap) {
        image->data = NULL;
        XDestroyImage(image);
        free(pixels);
        return -1;
    }

    XPutImage(display, pixmap, DefaultGC(display, screen), image,
              0, 0, 0, 0, (unsigned int)image_width, (unsigned int)image_height);

    XSetWindowAttributes attrs;
    memset(&attrs, 0, sizeof(attrs));
    attrs.background_pixmap = pixmap;
    attrs.override_redirect = False;

    int screen_width = DisplayWidth(display, screen);
    int screen_height = DisplayHeight(display, screen);

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
    if (argc < 2 || argc > 3) {
        print_usage(argv[0]);
        return EXIT_BAD_USAGE;
    }

    bool foreground = (argc == 3 && strcmp(argv[2], "--foreground") == 0);
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

    Window wallpaper_window = 0;
    Pixmap wallpaper_pixmap = 0;
    XImage *wallpaper_image = NULL;
    unsigned char *wallpaper_pixels = NULL;

    if (create_desktop_wallpaper_window(
            display,
            DefaultScreen(display),
            argv[1],
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
    XCloseDisplay(display);
    return EXIT_OK;
}
