#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CURVE_STEPS 64
#define POINT_CAPACITY (CURVE_STEPS * 4 + 5)

struct app {
	Display *display;
	int screen;
	Window window;
	Pixmap buffer;
	GC gc;
	Atom wm_delete;
	XFontStruct *font;
	unsigned long background;
	unsigned long foreground;
	unsigned long accent;
	unsigned long shadow;
	int width;
	int height;
	int dragging;
	int drag_x;
	int drag_y;
	double drag_size;
	double drag_spread;
	double size;
	double spread;
};

static double clamp_double(double value, double low, double high)
{
	if (value < low)
		return low;
	if (value > high)
		return high;
	return value;
}

static double min_double(double left, double right)
{
	return left < right ? left : right;
}

static unsigned long allocate_color(struct app *app, const char *name,
				    unsigned long fallback)
{
	XColor exact;
	XColor screen;
	Colormap map = DefaultColormap(app->display, app->screen);

	if (XAllocNamedColor(app->display, map, name, &screen, &exact))
		return screen.pixel;
	return fallback;
}

static void append_bezier(XPoint *points, int *count,
			  double x0, double y0, double x1, double y1,
			  double x2, double y2, double x3, double y3)
{
	int first = *count == 0 ? 0 : 1;
	int step;

	for (step = first; step <= CURVE_STEPS; step++) {
		double t = (double)step / CURVE_STEPS;
		double u = 1.0 - t;
		double uu = u * u;
		double tt = t * t;
		double x = uu * u * x0 + 3.0 * uu * t * x1 +
			3.0 * u * tt * x2 + tt * t * x3;
		double y = uu * u * y0 + 3.0 * uu * t * y1 +
			3.0 * u * tt * y2 + tt * t * y3;

		if (*count >= POINT_CAPACITY)
			return;
		points[*count].x = (short)(x + 0.5);
		points[*count].y = (short)(y + 0.5);
		(*count)++;
	}
}

static void recreate_buffer(struct app *app)
{
	if (app->buffer)
		XFreePixmap(app->display, app->buffer);
	app->buffer = XCreatePixmap(app->display, app->window,
				    (unsigned int)app->width,
				    (unsigned int)app->height,
				    (unsigned int)DefaultDepth(app->display,
							       app->screen));
}

static void draw_text(struct app *app, int x, int y, const char *text,
			      unsigned long color)
{
	XSetForeground(app->display, app->gc, color);
	XDrawString(app->display, app->buffer, app->gc, x, y, text,
		    (int)strlen(text));
}

static void draw_scene(struct app *app)
{
	XPoint points[POINT_CAPACITY];
	char status[96];
	double center_x = app->width * 0.5;
	double base_y = app->height * 0.27;
	double lobe_width;
	double lobe_height;
	double outer_left;
	double outer_right;
	double notch_y;
	double tail;
	int count = 0;

	if (!app->buffer)
		return;

	XSetForeground(app->display, app->gc, app->background);
	XFillRectangle(app->display, app->buffer, app->gc, 0, 0,
		       (unsigned int)app->width, (unsigned int)app->height);

	lobe_width = app->width * 0.27 * app->spread *
		(0.82 + 0.18 * app->size);
	lobe_width = clamp_double(lobe_width, 72.0, app->width * 0.42);
	lobe_height = app->height * 0.29 * app->size;
	lobe_height = clamp_double(lobe_height, 52.0, app->height * 0.50);
	outer_left = center_x - lobe_width;
	outer_right = center_x + lobe_width;
	notch_y = base_y + lobe_height * (0.10 + 0.08 / app->size);
	tail = min_double(38.0, app->width * 0.06);

	append_bezier(points, &count,
		outer_left - tail, base_y - 4.0,
		outer_left - tail * 0.55, base_y - 8.0,
		outer_left - tail * 0.22, base_y,
		outer_left, base_y);
	append_bezier(points, &count,
		outer_left, base_y,
		outer_left - lobe_width * 0.06, base_y + lobe_height * 0.82,
		center_x - lobe_width * 0.28, base_y + lobe_height * 1.17,
		center_x, notch_y);
	append_bezier(points, &count,
		center_x, notch_y,
		center_x + lobe_width * 0.28, base_y + lobe_height * 1.17,
		outer_right + lobe_width * 0.06, base_y + lobe_height * 0.82,
		outer_right, base_y);
	append_bezier(points, &count,
		outer_right, base_y,
		outer_right + tail * 0.22, base_y,
		outer_right + tail * 0.55, base_y - 8.0,
		outer_right + tail, base_y - 4.0);

	XSetLineAttributes(app->display, app->gc, 9, LineSolid, CapRound,
			   JoinRound);
	XSetForeground(app->display, app->gc, app->shadow);
	XDrawLines(app->display, app->buffer, app->gc, points, count,
		   CoordModeOrigin);
	XSetLineAttributes(app->display, app->gc, 4, LineSolid, CapRound,
			   JoinRound);
	XSetForeground(app->display, app->gc, app->accent);
	XDrawLines(app->display, app->buffer, app->gc, points, count,
		   CoordModeOrigin);

	XSetLineAttributes(app->display, app->gc, 1, LineSolid, CapButt,
			   JoinMiter);
	draw_text(app, 18, 28, "XOPPAI - interactive omega curve",
		  app->foreground);
	draw_text(app, 18, 48,
		  "Drag up/down: size   left/right: width   R/right click: reset   Q/Esc: quit",
		  app->foreground);
	snprintf(status, sizeof(status), "size %3d%%   width %3d%%",
		 (int)(app->size * 100.0 + 0.5),
		 (int)(app->spread * 100.0 + 0.5));
	draw_text(app, 18, app->height - 18, status, app->accent);

	XCopyArea(app->display, app->buffer, app->window, app->gc, 0, 0,
		  (unsigned int)app->width, (unsigned int)app->height, 0, 0);
	XFlush(app->display);
}

static void reset_shape(struct app *app)
{
	app->size = 1.0;
	app->spread = 1.0;
}

static int handle_key(struct app *app, XKeyEvent *event)
{
	KeySym key = XLookupKeysym(event, 0);

	switch (key) {
	case XK_Escape:
	case XK_q:
	case XK_Q:
		return 0;
	case XK_r:
	case XK_R:
		reset_shape(app);
		break;
	case XK_Up:
		app->size = clamp_double(app->size + 0.05, 0.45, 1.80);
		break;
	case XK_Down:
		app->size = clamp_double(app->size - 0.05, 0.45, 1.80);
		break;
	case XK_Right:
		app->spread = clamp_double(app->spread + 0.05, 0.60, 1.45);
		break;
	case XK_Left:
		app->spread = clamp_double(app->spread - 0.05, 0.60, 1.45);
		break;
	default:
		return 1;
	}
	draw_scene(app);
	return 1;
}

int main(void)
{
	struct app app;
	XSetWindowAttributes attributes;
	XSizeHints hints;
	XEvent event;
	int running = 1;

	memset(&app, 0, sizeof(app));
	app.display = XOpenDisplay(NULL);
	if (!app.display) {
		fputs("xoppai: cannot open X display\n", stderr);
		return 1;
	}
	app.screen = DefaultScreen(app.display);
	app.width = 720;
	app.height = 480;
	reset_shape(&app);

	app.background = allocate_color(&app, "#07151c",
					 BlackPixel(app.display, app.screen));
	app.foreground = allocate_color(&app, "#e9f7ff",
					 WhitePixel(app.display, app.screen));
	app.accent = allocate_color(&app, "#35e4d1", app.foreground);
	app.shadow = allocate_color(&app, "#123f50", app.background);

	attributes.background_pixel = app.background;
	attributes.event_mask = ExposureMask | KeyPressMask | ButtonPressMask |
		ButtonReleaseMask | PointerMotionMask | StructureNotifyMask;
	app.window = XCreateWindow(app.display,
				   RootWindow(app.display, app.screen),
				   40, 40, (unsigned int)app.width,
				   (unsigned int)app.height, 0,
				   CopyFromParent, InputOutput, CopyFromParent,
				   CWBackPixel | CWEventMask, &attributes);
	XStoreName(app.display, app.window, "xoppai");
	XSetIconName(app.display, app.window, "xoppai");

	memset(&hints, 0, sizeof(hints));
	hints.flags = PMinSize;
	hints.min_width = 420;
	hints.min_height = 300;
	XSetWMNormalHints(app.display, app.window, &hints);

	app.wm_delete = XInternAtom(app.display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(app.display, app.window, &app.wm_delete, 1);
	app.gc = XCreateGC(app.display, app.window, 0, NULL);
	app.font = XLoadQueryFont(app.display, "fixed");
	if (app.font)
		XSetFont(app.display, app.gc, app.font->fid);
	recreate_buffer(&app);
	XMapWindow(app.display, app.window);

	while (running) {
		XNextEvent(app.display, &event);
		switch (event.type) {
		case Expose:
			if (event.xexpose.count == 0)
				draw_scene(&app);
			break;
		case ConfigureNotify:
			if (app.width != event.xconfigure.width ||
			    app.height != event.xconfigure.height) {
				app.width = event.xconfigure.width;
				app.height = event.xconfigure.height;
				recreate_buffer(&app);
				draw_scene(&app);
			}
			break;
		case ButtonPress:
			if (event.xbutton.button == Button1) {
				app.dragging = 1;
				app.drag_x = event.xbutton.x;
				app.drag_y = event.xbutton.y;
				app.drag_size = app.size;
				app.drag_spread = app.spread;
			} else if (event.xbutton.button == Button3) {
				reset_shape(&app);
				draw_scene(&app);
			}
			break;
		case ButtonRelease:
			if (event.xbutton.button == Button1)
				app.dragging = 0;
			break;
		case MotionNotify:
			if (app.dragging) {
				app.size = clamp_double(app.drag_size +
					(app.drag_y - event.xmotion.y) * 0.007,
					0.45, 1.80);
				app.spread = clamp_double(app.drag_spread +
					(event.xmotion.x - app.drag_x) * 0.004,
					0.60, 1.45);
				draw_scene(&app);
			}
			break;
		case KeyPress:
			running = handle_key(&app, &event.xkey);
			break;
		case ClientMessage:
			if ((Atom)event.xclient.data.l[0] == app.wm_delete)
				running = 0;
			break;
		default:
			break;
		}
	}

	if (app.font)
		XFreeFont(app.display, app.font);
	if (app.buffer)
		XFreePixmap(app.display, app.buffer);
	XFreeGC(app.display, app.gc);
	XDestroyWindow(app.display, app.window);
	XCloseDisplay(app.display);
	return 0;
}
