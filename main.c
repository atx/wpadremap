
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <xkbcommon/xkbcommon.h>
#include <wayland-client.h>

#include <libinput.h>
#include <libudev.h>

#include "virtual-keyboard-unstable-v1-client-protocol.h"

struct wpadremap {
	struct libinput *li;
	struct udev *udev;

	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_seat *seat;
	struct zwp_virtual_keyboard_manager_v1 *manager;
	struct zwp_virtual_keyboard_v1 *keyboard;
	struct xkb_context *xkb;
	struct xkb_keymap *keymap;

	uint32_t button_bitmap;
};


enum modifiers {
	MOD_NONE = 0,
	MOD_SHIFT = 1,
	MOD_CAPSLOCK = 2,
	MOD_CTRL = 4,
	MOD_ALT = 8,
	MOD_LOGO = 64,
	MOD_ALTGR = 128
};


void fail(const char *format, ...)
{
	va_list vas;
	va_start(vas, format);
	vfprintf(stderr, format, vas);
	va_end(vas);
	fprintf(stderr, "\n");
	exit(EXIT_FAILURE);
}


void log_print(const char *format, ...)
{
	va_list vas;
	va_start(vas, format);
	vfprintf(stderr, format, vas);
	va_end(vas);
	fprintf(stderr, "\n");
}


static int pad_button_to_keycode(unsigned int button)
{
	if (button >= 1 && button <= 9) {
		return button + 1;
	}
	if (button == 0) {
		return 11;
	}
	return -1;
}


static int open_restricted(const char *path, int flags, void *user_data)
{
	int fd = open(path, flags);
	return fd < 0 ? -errno : fd;
}


static void close_restricted(int fd, void *user_data)
{
	close(fd);
}


static const struct libinput_interface interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};


static void handle_libinput_tablet_pad_button(struct wpadremap *wpr, struct libinput_event *ev)
{
	struct libinput_event_tablet_pad *evp = libinput_event_get_tablet_pad_event(ev);
	unsigned int pad_button = libinput_event_tablet_pad_get_button_number(evp);
	enum libinput_button_state state = libinput_event_tablet_pad_get_button_state(evp);
	int modifiers = MOD_ALT;
	bool is_pressed = state == LIBINPUT_BUTTON_STATE_PRESSED;

	int keycode = pad_button_to_keycode(pad_button);
	if (keycode < 0) {
		log_print(
			"Unmapped pad button %d! Add it to pad_button_to_keycode!",
			pad_button
		);
		return;
	}

	log_print(
		"Pad button %d mapped to keycode %d %s",
		pad_button, keycode, is_pressed ? "pressed" : "released"
	);

	zwp_virtual_keyboard_v1_modifiers(
		wpr->keyboard, is_pressed ? modifiers : 0, 0, 0, 0
	);
	zwp_virtual_keyboard_v1_key(
		wpr->keyboard, 0, keycode,
		is_pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED
	);
}


static void handle_libinput_events(struct wpadremap *wpr)
{
	struct libinput_event *ev;

	libinput_dispatch(wpr->li);

	while ((ev = libinput_get_event(wpr->li))) {
		switch (libinput_event_get_type(ev)) {
		case LIBINPUT_EVENT_NONE:
			fail("Got LIBINPUT_EVENT_NONE");
			break;
		case LIBINPUT_EVENT_DEVICE_ADDED:
			// TODO: Log if we found a tablet pad
			break;
		case LIBINPUT_EVENT_DEVICE_REMOVED:
			// TODO
			break;
		case LIBINPUT_EVENT_TABLET_PAD_BUTTON:
			handle_libinput_tablet_pad_button(wpr, ev);
			break;
		default:
			// Meh
			break;
		};
	}
}


static int init_libinput(struct wpadremap *wpr)
{
	wpr->udev = udev_new();
	wpr->li = libinput_udev_create_context(&interface, wpr, wpr->udev);
	if (!wpr->li) {
		fprintf(stderr, "Failed to initialize libinput\n");
		exit(-1);
	}

	// TODO: Use wayland-provided seat
	if (libinput_udev_assign_seat(wpr->li, "seat0")) {
		fprintf(stderr, "Failed to set seat\n");
		libinput_unref(wpr->li);
		exit(-1);
	}

	return 0;
}


static void handle_wl_event(void *data, struct wl_registry *registry,
							uint32_t name, const char *interface,
							uint32_t version)
{
	struct wpadremap *wpr = data;
	if (!strcmp(interface, wl_seat_interface.name)) {
		wpr->seat = wl_registry_bind(
			registry, name, &wl_seat_interface, version <= 7 ? version : 7
		);
	} else if (!strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name)) {
		wpr->manager = wl_registry_bind(
			registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1
		);
	}
}

static void handle_wl_event_remove(void *data, struct wl_registry *registry,
								   uint32_t name)
{
}


static const struct wl_registry_listener registry_listener = {
	.global = handle_wl_event,
	.global_remove = handle_wl_event_remove,
};


static int init_virtual_keyboard(struct wpadremap *wpr)
{
	struct xkb_rule_names rules = {
		.rules = NULL,
		.model = NULL,
		.layout = "us",
		.variant = NULL,
		.options = NULL
	};
	wpr->keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
		wpr->manager, wpr->seat
	);

	wpr->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (wpr->xkb == NULL) {
		fail("Unable to create the XKB context");
	}
	wpr->keymap = xkb_keymap_new_from_names(wpr->xkb, &rules, 0);
	if (wpr->keymap == NULL) {
		fail("Unable to create a keymap");
	}

	char *keymap_string = xkb_keymap_get_as_string(wpr->keymap, XKB_KEYMAP_FORMAT_TEXT_V1);

	size_t keymap_size = strlen(keymap_string) + 1;
	char filename[] = "/tmp/wpadremap-XXXXXX";
	int fd = mkstemp(filename);
	if (fd < 0) {
		fail("Failed to create the temporary keymap file");
	}
	unlink(filename);
	FILE *f = fdopen(fd, "w");
	// TODO: Just use write here...
	fwrite(keymap_string, 1, keymap_size, f);
	fflush(f);

	zwp_virtual_keyboard_v1_keymap(
		wpr->keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fileno(f), keymap_size
	);

	wl_display_roundtrip(wpr->display);

	fclose(f);

	return 0;
}


static int init_wayland(struct wpadremap *wpr)
{
	wpr->display = wl_display_connect(NULL);
	if (wpr->display == NULL) {
		fail("Wayland connection failed");
	}
	wpr->registry = wl_display_get_registry(wpr->display);
	wl_registry_add_listener(wpr->registry, &registry_listener, wpr);
	wl_display_dispatch(wpr->display);
	wl_display_roundtrip(wpr->display);
	if (wpr->manager == NULL) {
		fail("Unable to create the virtual keyboard");
	}
	return init_virtual_keyboard(wpr);
}


static void event_loop(struct wpadremap *wpr)
{
	struct pollfd fds[2];

	fds[0].fd = wl_display_get_fd(wpr->display);
	fds[0].events = POLLIN;
	fds[0].revents = 0;
	fds[1].fd = libinput_get_fd(wpr->li);
	fds[1].events = POLLIN;
	fds[1].revents = 0;

	fprintf(stderr, "Entering the event loop\n");

	while (true) {
		while (wl_display_prepare_read(wpr->display) != 0) {
			wl_display_dispatch_pending(wpr->display);
			wl_display_flush(wpr->display);
		}
		poll(fds, 2, -1);
		handle_libinput_events(wpr);
		wl_display_flush(wpr->display);
		wl_display_read_events(wpr->display);
		wl_display_dispatch_pending(wpr->display);
	}
}


int main(int argc, char **argv)
{
	struct wpadremap wpr;
	memset(&wpr, 0, sizeof(wpr));

	init_wayland(&wpr);
	init_libinput(&wpr);

	event_loop(&wpr);
}
