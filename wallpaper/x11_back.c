#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
​
int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Kullanım: %s <ppm_dosyası>\n", argv[0]);
        return 1;
    }
​
    Display *d = XOpenDisplay(NULL);
    if (!d) return 1;
​
    int s = DefaultScreen(d);
    int scr_w = 3840; 
    int scr_h = 2160; 
​
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("Dosya açılamadı"); return 1; }
​
    char header[3];
    int img_w, img_h, max_val;
    fscanf(f, "%s %d %d %d", header, &img_w, &img_h, &max_val);
    fgetc(f); 
​
    unsigned char *data = malloc(img_w * img_h * 4);
    for (int i = 0; i < img_w * img_h; i++) {
        unsigned char rgb[3];
        fread(rgb, 1, 3, f);
        data[i*4+2] = rgb[0];
        data[i*4+1] = rgb[1];
        data[i*4+0] = rgb[2];
        data[i*4+3] = 0;      
    }
    fclose(f);
    XImage *xi = XCreateImage(d, DefaultVisual(d, s), DefaultDepth(d, s), ZPixmap, 0, (char *)data, img_w, img_h, 32, 0);
    Pixmap pix = XCreatePixmap(d, RootWindow(d, s), img_w, img_h, DefaultDepth(d, s));
    XPutImage(d, pix, DefaultGC(d, s), xi, 0, 0, 0, 0, img_w, img_h);
    XSetWindowAttributes attr;
    attr.background_pixmap = pix;
    attr.override_redirect = False; 
    Window win = XCreateWindow(d, RootWindow(d, s), 0, 0, scr_w, scr_h, 0, 
                               DefaultDepth(d, s), InputOutput, DefaultVisual(d, s), 
                               CWBackPixmap | CWOverrideRedirect, &attr);
    Atom wm_type = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False);
    Atom type_desktop = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    XChangeProperty(d, win, wm_type, XA_ATOM, 32, PropModeReplace, (unsigned char *)&type_desktop, 1);
    Atom wm_state = XInternAtom(d, "_NET_WM_STATE", False);
    Atom state_below = XInternAtom(d, "_NET_WM_STATE_BELOW", False);
    XChangeProperty(d, win, wm_state, XA_ATOM, 32, PropModeReplace, (unsigned char *)&state_below, 1);
    XShapeCombineRectangles(d, win, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);
    XMapWindow(d, win);
    XLowerWindow(d, win);
    XFlush(d);
    if (fork() > 0) exit(0); 
    while (1) {
        XLowerWindow(d, win);
        XFlush(d);
        sleep(5); 
    }
    return 0;
}