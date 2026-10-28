// not upstream (detach-windows): an ImGui window carried by the Wayland compositor with the pointer

#include "WlWindowDrag.h"
#include <QGuiApplication>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>
#include <cstring>
#include <unistd.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-toplevel-drag-v1-client-protocol.h"

namespace {

wl_display *display = nullptr;
wl_event_queue *queue = nullptr;                 // own queue, dispatched here rather than by Qt
wl_data_device_manager *ddm = nullptr;
uint32_t ddmVersion = 0;
xdg_toplevel_drag_manager_v1 *manager = nullptr;
wl_data_device *device = nullptr;                // own device on Qt's seat, for the drag only
wl_data_source *source = nullptr;
xdg_toplevel_drag_v1 *drag = nullptr;
bool ended = false;
bool tried = false;
wl_surface *focus = nullptr;                     // our surface the pointer is over during the drag
double focusX = 0, focusY = 0;
bool leaving = false;                            // a leave in this dispatch: the pointer's, or the drop's if the end follows

void Global (void*, wl_registry *reg, uint32_t name, const char *iface, uint32_t version)
{
	if (!strcmp (iface, wl_data_device_manager_interface.name)) {
		ddmVersion = version < 3 ? version : 3;
		ddm = (wl_data_device_manager*)wl_registry_bind (reg, name, &wl_data_device_manager_interface, ddmVersion);
	} else if (!strcmp (iface, xdg_toplevel_drag_manager_v1_interface.name))
		manager = (xdg_toplevel_drag_manager_v1*)wl_registry_bind (reg, name, &xdg_toplevel_drag_manager_v1_interface, 1);
}
void GlobalRemove (void*, wl_registry*, uint32_t) {}
const wl_registry_listener registryListener = { Global, GlobalRemove };

// the drag is over: its objects go, the toplevel drag first (it may only go once the drag has ended)
void Finish ()
{
	if (drag) { xdg_toplevel_drag_v1_destroy (drag); drag = nullptr; }
	if (source) { wl_data_source_destroy (source); source = nullptr; }
	if (device) {
		if (wl_data_device_get_version (device) >= WL_DATA_DEVICE_RELEASE_SINCE_VERSION) wl_data_device_release (device);
		else wl_data_device_destroy (device);
		device = nullptr;
	}
	wl_display_flush (display);
	ended = true;
}

void Target (void*, wl_data_source*, const char*) {}
void Send (void*, wl_data_source*, const char*, int32_t fd) { close (fd); }
void Cancelled (void*, wl_data_source*) { Finish (); }    // also after a drop nobody took, which is every drop here
void DropPerformed (void*, wl_data_source*) {}
void Finished (void*, wl_data_source*) { Finish (); }
void Action (void*, wl_data_source*, uint32_t) {}
const wl_data_source_listener sourceListener = { Target, Send, Cancelled, DropPerformed, Finished, Action };

void Offer (void*, wl_data_device*, wl_data_offer *o) { wl_data_offer_destroy (o); } // offers to this device aren't read
void Enter (void*, wl_data_device*, uint32_t, wl_surface *s, wl_fixed_t x, wl_fixed_t y, wl_data_offer*)
{
	focus = s, focusX = wl_fixed_to_double (x), focusY = wl_fixed_to_double (y), leaving = false;
}
void Leave (void*, wl_data_device*) { leaving = true; }
void Motion (void*, wl_data_device*, uint32_t, wl_fixed_t x, wl_fixed_t y)
{
	focusX = wl_fixed_to_double (x), focusY = wl_fixed_to_double (y);
}
void Drop (void*, wl_data_device*) {}
void Selection (void*, wl_data_device*, wl_data_offer*) {}
const wl_data_device_listener deviceListener = { Offer, Enter, Leave, Motion, Drop, Selection };

bool Init ()
{
	if (tried) return (manager && ddm);
	tried = true;
	if (!QGuiApplication::platformName().startsWith ("wayland")) return false;
	auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
	if (!app || !app->display() || !app->seat()) return false;
	display = app->display();
	queue = wl_display_create_queue (display);
	wl_display *dpy = (wl_display*)wl_proxy_create_wrapper (display);
	wl_proxy_set_queue ((wl_proxy*)dpy, queue);
	wl_registry *reg = wl_display_get_registry (dpy);
	wl_proxy_wrapper_destroy (dpy);
	wl_registry_add_listener (reg, &registryListener, nullptr);
	wl_display_roundtrip_queue (display, queue);
	wl_registry_destroy (reg);
	return (manager && ddm);
}

} // namespace

bool WlWindowDragStart (QWindow *origin, unsigned serial, QWindow *window, int dx, int dy)
{
	if (source || !Init ()) return false;
	QPlatformNativeInterface *ni = QGuiApplication::platformNativeInterface();
	wl_surface *from = (wl_surface*)ni->nativeResourceForWindow ("surface", origin);
	bool shown = window->isVisible ();
	xdg_toplevel *top = shown ? (xdg_toplevel*)ni->nativeResourceForWindow ("xdg_toplevel", window) : nullptr;
	if (!from || (shown && !top)) return false;
	auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
	wl_seat *seat = (wl_seat*)wl_proxy_create_wrapper (app->seat());
	wl_proxy_set_queue ((wl_proxy*)seat, queue);
	device = wl_data_device_manager_get_data_device (ddm, seat);
	wl_proxy_wrapper_destroy (seat);
	wl_data_device_add_listener (device, &deviceListener, nullptr);
	source = wl_data_device_manager_create_data_source (ddm); // offers no MIME type: no other program can take the drop
	wl_data_source_add_listener (source, &sourceListener, nullptr);
	if (ddmVersion >= 3) wl_data_source_set_actions (source, WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE);
	drag = xdg_toplevel_drag_manager_v1_get_xdg_toplevel_drag (manager, source);
	if (top) xdg_toplevel_drag_v1_attach (drag, top, dx, dy); // an existing window: before the drag starts
	wl_data_device_start_drag (device, source, from, nullptr, serial);
	ended = false;
	focus = nullptr, leaving = false;
	if (!shown) {
		window->show (); // the toplevel is made here; it is mapped by its first frame
		if ((top = (xdg_toplevel*)ni->nativeResourceForWindow ("xdg_toplevel", window)))
			xdg_toplevel_drag_v1_attach (drag, top, dx, dy);
	}
	wl_display_flush (display);
	return true;
}

bool WlWindowDragPoll (QWindow *main, bool &over, float &x, float &y)
{
	over = false;
	if (!queue) return false;
	wl_display_dispatch_queue_pending (display, queue);
	if (leaving && !ended) focus = nullptr, leaving = false; // the pointer left; the drop's leave comes with the end
	wl_surface *ms = (wl_surface*)QGuiApplication::platformNativeInterface()->nativeResourceForWindow ("surface", main);
	over = focus && focus == ms;
	x = (float)focusX, y = (float)focusY;
	if (!ended) return false;
	ended = false;
	focus = nullptr, leaving = false;
	return true;
}
