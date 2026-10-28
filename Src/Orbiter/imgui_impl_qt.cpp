// not upstream: Dear ImGui platform backend for Qt windows (stands in for imgui_impl_win32)

#include "imgui_impl_qt.h"
#include "imgui_internal.h"
#include "WlWindowDrag.h"
#include <QClipboard>
#include <QCursor>
#include <QElapsedTimer>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QWheelEvent>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>
#include <qpa/qplatformwindow_p.h>
#include <cfloat>
#include <wayland-client.h>
#include <xcb/xcb.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>
#include <vulkan/vulkan_xcb.h>
#include <QVulkanInstance>

// detach-windows: X11 windows sit at ImGui's desktop positions; Wayland ones where the compositor put them, with ImGui's own positions

struct ImGui_ImplQt_Data {
	QWindow *Window = nullptr;                   // the render window (main viewport)
	QElapsedTimer Timer;
	qint64 Time = 0;
	ImGuiMouseCursor LastMouseCursor = ImGuiMouseCursor_COUNT;
	Qt::CursorShape CursorShape = Qt::ArrowCursor;
	QByteArray ClipboardText;
	QObject *Filter = nullptr;                   // the secondary windows' events, the screens' signals
	bool Wayland = false;
	bool WantUpdateMonitors = true;
	QWindow *MouseWindow = nullptr;              // the window the pointer is in (WM_MOUSELEAVE tracking)
	QWindow *PressWindow = nullptr;              // Wayland: the window and serial of the last press (the drag's)
	unsigned PressSerial = 0;
	int Carry = 0;                               // Wayland: 1 the compositor carries a window (WlWindowDrag), 2 dropped, 3 released, until ImGui's move ends
	ImGuiID CarryId = 0;                         // that window's viewport
	ImVec2 CarryGrab;                            // the grab point in it
	bool CarryOver = false;                      // the pointer is over the render window,
	ImVec2 CarryLocal;                           // there
	ImVec2 Hold;                                 // ImGui's mouse while the pointer is elsewhere
};

struct ImGui_ImplQt_ViewportData {
	QWindow *Window = nullptr;
	bool Owned = false;                          // made here (the render window is the application's)
	bool Popup = false;                          // an ImGui popup or tooltip: placed relative to its parent
	Qt::WindowFlags Flags;
	ImVec2 Pos;                                  // Wayland: the position ImGui last gave it
	ImVec2 Anchor;                               // Wayland: its position when a button went down in it
	bool Anchored = false;                       // a button is down in it: the mouse is mapped from Anchor
	ImVec2 PressPos;                             // the press there, in ImGui's coordinates
	QPointF PressLocal, Local;                   // the press and the last position, in the window
	bool SystemDrag = false;                     // Wayland: the compositor moves or resizes it for that button
};

class ImGui_ImplQt_Filter : public QObject {
public:
	bool eventFilter (QObject *o, QEvent *e) override;
};

static ImGui_ImplQt_Data *ImGui_ImplQt_GetBackendData ()
{
	return ImGui::GetCurrentContext() ? (ImGui_ImplQt_Data*)ImGui::GetIO().BackendPlatformUserData : nullptr;
}

static bool ImGui_ImplQt_Viewports ()
{
	return (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
}

static ImGuiViewport *ImGui_ImplQt_FindViewport (QWindow *w)
{
	for (ImGuiViewport *vp : ImGui::GetPlatformIO().Viewports)
		if (vp->PlatformHandle == (void*)w) return vp;
	return nullptr;
}

static ImGui_ImplQt_ViewportData *ImGui_ImplQt_Data_ (ImGuiViewport *vp)
{
	return vp ? (ImGui_ImplQt_ViewportData*)vp->PlatformUserData : nullptr;
}

static const char *ImGui_ImplQt_GetClipboardText (ImGuiContext*)
{
	ImGui_ImplQt_Data *bd = ImGui_ImplQt_GetBackendData();
	bd->ClipboardText = QGuiApplication::clipboard()->text().toUtf8();
	return bd->ClipboardText.constData();
}

static void ImGui_ImplQt_SetClipboardText (ImGuiContext*, const char *text)
{
	QGuiApplication::clipboard()->setText (QString::fromUtf8 (text));
}

// ImGui_ImplWin32_UpdateMonitors: the screens in Qt's desktop coordinates, the primary one first
static void ImGui_ImplQt_UpdateMonitors ()
{
	ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
	pio.Monitors.resize (0);
	QScreen *primary = QGuiApplication::primaryScreen();
	for (QScreen *s : QGuiApplication::screens()) {
		QRect g = s->geometry(), a = s->availableGeometry();
		ImGuiPlatformMonitor m;
		m.MainPos = ImVec2 ((float)g.x(), (float)g.y());
		m.MainSize = ImVec2 ((float)g.width(), (float)g.height());
		m.WorkPos = ImVec2 ((float)a.x(), (float)a.y());
		m.WorkSize = ImVec2 ((float)a.width(), (float)a.height());
		m.DpiScale = (float)s->devicePixelRatio();
		m.PlatformHandle = (void*)s;
		if (s == primary) pio.Monitors.push_front (m);
		else pio.Monitors.push_back (m);
	}
	ImGui_ImplQt_GetBackendData()->WantUpdateMonitors = false;
}

static void ImGui_ImplQt_WatchScreen (ImGui_ImplQt_Data *bd, QScreen *s)
{
	auto changed = [] { if (ImGui_ImplQt_Data *d = ImGui_ImplQt_GetBackendData()) d->WantUpdateMonitors = true; };
	QObject::connect (s, &QScreen::geometryChanged, bd->Filter, changed);
	QObject::connect (s, &QScreen::availableGeometryChanged, bd->Filter, changed);
}

// ImGui is moving the window of vp
static bool ImGui_ImplQt_Moving (ImGuiViewport *vp)
{
	ImGuiContext &g = *GImGui;
	ImGuiWindow *iw = ((ImGuiViewportP*)vp)->Window;
	return iw && g.MovingWindow && g.MovingWindow->RootWindowDockTree == iw;
}

// the mouse position ImGui gets for an event in w: the window's own, or with viewports the desktop's
static ImVec2 ImGui_ImplQt_MousePos (QWindow *w, ImGuiViewport *vp, const QPointF &local)
{
	if (!ImGui_ImplQt_Viewports()) return ImVec2 ((float)local.x(), (float)local.y());
	ImGui_ImplQt_Data *bd = ImGui_ImplQt_GetBackendData();
	if (!bd->Wayland) {
		QPointF g = w->mapToGlobal (local);
		return ImVec2 ((float)g.x(), (float)g.y());
	}
	ImGui_ImplQt_ViewportData *vd = ImGui_ImplQt_Data_ (vp);
	if (vd && vd->Anchored && (vd->SystemDrag || ImGui_ImplQt_Moving (vp))) return vd->PressPos; // only the compositor moves it
	ImVec2 o = (vd && vd->Anchored) ? vd->Anchor : vp->Pos; // a window ImGui resizes under a held button stays put here
	return ImVec2 (o.x + (float)local.x(), o.y + (float)local.y());
}

static QWindow *ImGui_ImplQt_ParentWindow (ImGuiViewport *vp)
{
	return vp->ParentViewport ? (QWindow*)vp->ParentViewport->PlatformHandle : nullptr;
}

// size, and position where the platform takes one: the desktop on X11, the parent window for a Wayland popup
static void ImGui_ImplQt_Place (ImGuiViewport *vp, ImGui_ImplQt_ViewportData *vd)
{
	ImGui_ImplQt_Data *bd = ImGui_ImplQt_GetBackendData();
	QSize size ((int)vp->Size.x, (int)vp->Size.y);
	if (!bd->Wayland) {
		vd->Window->setGeometry (QRect (QPoint ((int)vp->Pos.x, (int)vp->Pos.y), size));
		return;
	}
	if (vd->Popup && vp->ParentViewport) { // both positions are ImGui's own, so their difference is exact
		if (QWindow *pw = ImGui_ImplQt_ParentWindow (vp)) {
			QPoint d ((int)(vp->Pos.x - vp->ParentViewport->Pos.x), (int)(vp->Pos.y - vp->ParentViewport->Pos.y));
			vd->Window->setGeometry (QRect (pw->geometry().topLeft() + d, size));
			return;
		}
	}
	vd->Window->resize (size);
}

// Qt with xdg-session-management (6.12): a reopened window comes back where it was; older Qt has no such call
template<class T> static auto ImGui_ImplQt_RestoreId (T *w, const QString &id, int) -> decltype (w->setSessionRestoreId (id), void())
{
	w->setSessionRestoreId (id);
}
template<class T> static void ImGui_ImplQt_RestoreId (T*, const QString&, long) {}

// ImGui_ImplWin32_CreateWindow
static void ImGui_ImplQt_CreateWindow (ImGuiViewport *vp)
{
	ImGui_ImplQt_Data *bd = ImGui_ImplQt_GetBackendData();
	ImGui_ImplQt_ViewportData *vd = IM_NEW(ImGui_ImplQt_ViewportData)();
	vp->PlatformUserData = vd;
	ImGuiWindow *iw = ((ImGuiViewportP*)vp)->Window;
	vd->Popup = iw && (iw->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip));
	vd->Flags = ImGui_ImplQt_WindowFlags (vp->Flags, vd->Popup);
	vd->Pos = vp->Pos;
	vd->Owned = true;
	QWindow *w = vd->Window = new QWindow;
	vp->PlatformHandle = vp->PlatformHandleRaw = (void*)w;
	w->setSurfaceType (QSurface::VulkanSurface);
	w->setVulkanInstance (bd->Window->vulkanInstance());
	w->setFlags (vd->Flags);
	w->setTransientParent (ImGui_ImplQt_ParentWindow (vp));
	w->setProperty ("_q_showWithoutActivating", (vp->Flags & ImGuiViewportFlags_NoFocusOnAppearing) != 0);
	w->setCursor (QCursor (bd->CursorShape));
	ImGui_ImplQt_Place (vp, vd);
	w->installEventFilter (bd->Filter);
	w->create ();
	if (bd->Wayland && !vd->Popup && iw)
		if (auto *ww = w->nativeInterface<QNativeInterface::Private::QWaylandWindow>())
			ImGui_ImplQt_RestoreId (ww, QStringLiteral ("imgui:") + QString::fromUtf8 (iw->Name), 0);
	vp->PlatformRequestResize = false;
}

static void ImGui_ImplQt_DestroyWindow (ImGuiViewport *vp)
{
	ImGui_ImplQt_Data *bd = ImGui_ImplQt_GetBackendData();
	if (ImGui_ImplQt_ViewportData *vd = ImGui_ImplQt_Data_ (vp)) {
		if (vd->Owned && vd->Window) {
			if (bd->MouseWindow == vd->Window) bd->MouseWindow = nullptr;
			if (bd->PressWindow == vd->Window) bd->PressWindow = nullptr;
			vd->Window->removeEventFilter (bd->Filter);
			delete vd->Window; // its surface went with the renderer's window first
		}
		IM_DELETE(vd);
	}
	vp->PlatformUserData = vp->PlatformHandle = vp->PlatformHandleRaw = nullptr;
}

static void ImGui_ImplQt_ShowWindow (ImGuiViewport *vp)
{
	ImGui_ImplQt_Data *bd = ImGui_ImplQt_GetBackendData();
	ImGui_ImplQt_ViewportData *vd = ImGui_ImplQt_Data_ (vp);
	ImGuiContext &g = *GImGui;
	ImGuiWindow *iw = ((ImGuiViewportP*)vp)->Window;
	bool carried = false;
	if (bd->Wayland && !vd->Popup && iw && !bd->Carry && ImGui_ImplQt_Moving (vp) && g.IO.MouseDown[0] && bd->PressWindow) {
		// torn off the window it is dragged in: the compositor carries it with the pointer to its drop
		ImGuiViewport *mv = ImGui::GetMainViewport();
		ImVec2 grab (g.IO.MousePos.x - vp->Pos.x, g.IO.MousePos.y - vp->Pos.y);
		ImVec2 local (g.IO.MousePos.x - mv->Pos.x, g.IO.MousePos.y - mv->Pos.y);
		if ((carried = WlWindowDragStart (bd->PressWindow, bd->PressSerial, vd->Window, (int)grab.x, (int)grab.y))) {
			bd->Carry = 1, bd->CarryId = vp->ID, bd->CarryGrab = grab;
			bd->CarryOver = local.x >= 0 && local.y >= 0 && local.x < mv->Size.x && local.y < mv->Size.y;
			bd->CarryLocal = ImVec2 (ImClamp (local.x, 0.0f, mv->Size.x), ImClamp (local.y, 0.0f, mv->Size.y));
			ImVec2 park = ImGui_ImplQt_ParkedPos (bd->CarryLocal, mv->Size, vp->Size, grab);
			bd->Hold = bd->CarryOver ? g.IO.MousePos : ImVec2 (mv->Pos.x + park.x + grab.x, mv->Pos.y + park.y + grab.y);
		}
	}
	if (!carried) vd->Window->show ();
	if (bd->Wayland) vp->Flags |= ImGuiViewportFlags_IsMinimized; // no frame before the compositor's first configure
}

static ImVec2 ImGui_ImplQt_GetWindowPos (ImGuiViewport *vp)
{
	ImGui_ImplQt_Data *bd = ImGui_ImplQt_GetBackendData();
	ImGui_ImplQt_ViewportData *vd = ImGui_ImplQt_Data_ (vp);
	if (bd->Wayland) {
		if (vd->Owned) return vd->Pos;
		QScreen *s = vd->Window->screen(); // the render window: full screen or Style 1 at its screen's origin, elsewhere unknown
		QPoint o = s ? s->geometry().topLeft() : QPoint();
		return ImVec2 ((float)o.x(), (float)o.y());
	}
	QPoint p = vd->Window->mapToGlobal (QPoint (0, 0)); // ClientToScreen
	return ImVec2 ((float)p.x(), (float)p.y());
}

static void ImGui_ImplQt_SetWindowPos (ImGuiViewport *vp, ImVec2 pos)
{
	ImGui_ImplQt_Data *bd = ImGui_ImplQt_GetBackendData();
	ImGui_ImplQt_ViewportData *vd = ImGui_ImplQt_Data_ (vp);
	vd->Pos = pos;
	if (!bd->Wayland) vd->Window->setPosition ((int)pos.x, (int)pos.y);
	else if (vd->Popup) ImGui_ImplQt_Place (vp, vd);
}

static ImVec2 ImGui_ImplQt_GetWindowSize (ImGuiViewport *vp)
{
	QSize s = ImGui_ImplQt_Data_ (vp)->Window->size();
	return ImVec2 ((float)s.width(), (float)s.height());
}

static void ImGui_ImplQt_SetWindowSize (ImGuiViewport *vp, ImVec2 size)
{
	ImGui_ImplQt_Data_ (vp)->Window->resize ((int)size.x, (int)size.y);
}

static void ImGui_ImplQt_SetWindowFocus (ImGuiViewport *vp)
{
	ImGui_ImplQt_Data_ (vp)->Window->requestActivate ();
}

static bool ImGui_ImplQt_GetWindowFocus (ImGuiViewport *vp)
{
	return ImGui_ImplQt_Data_ (vp)->Window->isActive ();
}

// also before its first configure on Wayland (not exposed yet): nothing is presented to it then
static bool ImGui_ImplQt_GetWindowMinimized (ImGuiViewport *vp)
{
	QWindow *w = ImGui_ImplQt_Data_ (vp)->Window;
	return (w->windowStates() & Qt::WindowMinimized) || !w->isExposed();
}

static void ImGui_ImplQt_SetWindowTitle (ImGuiViewport *vp, const char *title)
{
	ImGui_ImplQt_Data_ (vp)->Window->setTitle (QString::fromUtf8 (title));
}

static void ImGui_ImplQt_SetWindowAlpha (ImGuiViewport *vp, float alpha)
{
	ImGui_ImplQt_Data_ (vp)->Window->setOpacity (alpha);
}

static float ImGui_ImplQt_GetWindowDpiScale (ImGuiViewport *vp)
{
	return (float)ImGui_ImplQt_Data_ (vp)->Window->devicePixelRatio ();
}

// Wayland: a drag of a window's own ImGui title bar or top/left edge, once past ImGui's drag threshold (clicks and
// double-clicks stay ImGui's), becomes the compositor's: a move carries the window (WlWindowDrag), so that ImGui knows
// when it is over the render window; a resize, or a move without that protocol, is the compositor's own, after which
// Qt ends the button
static void ImGui_ImplQt_CompositorDrag (ImGuiViewport *vp, ImGui_ImplQt_ViewportData *vd)
{
	ImGuiContext &g = *GImGui;
	ImGui_ImplQt_Data *bd = ImGui_ImplQt_GetBackendData();
	ImGuiWindow *iw = ((ImGuiViewportP*)vp)->Window;
	if (!iw || !vd->Anchored || vd->SystemDrag || bd->Carry || !g.IO.MouseDown[0]) return;
	QPointF d = vd->Local - vd->PressLocal;
	if (d.x() * d.x() + d.y() * d.y() < g.IO.MouseDragThreshold * g.IO.MouseDragThreshold) return;
	if (ImGui_ImplQt_Moving (vp)) {
		ImVec2 grab = g.ActiveIdClickOffset;
		if (bd->PressWindow == vd->Window && WlWindowDragStart (vd->Window, bd->PressSerial, vd->Window, (int)grab.x, (int)grab.y)) {
			bd->Carry = 1, bd->CarryId = vp->ID, bd->CarryGrab = grab;
			bd->CarryOver = false;
			bd->Hold = vd->PressPos; // where ImGui has it
		} else
			vd->SystemDrag = vd->Window->startSystemMove ();
		return;
	}
	int corner = -1, border = -1;
	for (int n = 0; n < 4; n++) {
		if (g.ActiveId == ImGui::GetWindowResizeCornerID (iw, n)) corner = n;
		if (g.ActiveId == ImGui::GetWindowResizeBorderID (iw, (ImGuiDir)n)) border = n;
	}
	if (Qt::Edges e = ImGui_ImplQt_ResizeEdges (corner, border))
		vd->SystemDrag = vd->Window->startSystemResize (e);
}

// ImGui_ImplWin32_UpdateWindow: parent and flags changed after creation
static void ImGui_ImplQt_UpdateWindow (ImGuiViewport *vp)
{
	ImGui_ImplQt_Data *bd = ImGui_ImplQt_GetBackendData();
	ImGui_ImplQt_ViewportData *vd = ImGui_ImplQt_Data_ (vp);
	if (!vd || !vd->Owned) return;
	QWindow *parent = ImGui_ImplQt_ParentWindow (vp);
	if (vd->Window->transientParent() != parent) vd->Window->setTransientParent (parent);
	Qt::WindowFlags f = ImGui_ImplQt_WindowFlags (vp->Flags, vd->Popup);
	if (f != vd->Flags) {
		vd->Flags = f;
		vd->Window->setFlags (f);
	}
	if (bd->Wayland && !vd->Popup) ImGui_ImplQt_CompositorDrag (vp, vd);
}

// imgui_impl_vulkan destroys the surface it gets, so it is made here for the window rather than taken from Qt
static int ImGui_ImplQt_CreateVkSurface (ImGuiViewport *vp, ImU64 vk_instance, const void *vk_allocator, ImU64 *out_vk_surface)
{
	ImGui_ImplQt_Data *bd = ImGui_ImplQt_GetBackendData();
	QWindow *w = ImGui_ImplQt_Data_ (vp)->Window;
	QVulkanInstance *qi = bd->Window->vulkanInstance();
	if (!qi) return VK_ERROR_INITIALIZATION_FAILED;
	VkInstance inst = (VkInstance)vk_instance;
	const VkAllocationCallbacks *alloc = (const VkAllocationCallbacks*)vk_allocator;
	VkSurfaceKHR *surf = (VkSurfaceKHR*)out_vk_surface;
	if (bd->Wayland) {
		auto create = (PFN_vkCreateWaylandSurfaceKHR)qi->getInstanceProcAddr ("vkCreateWaylandSurfaceKHR");
		auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
		wl_surface *s = (wl_surface*)QGuiApplication::platformNativeInterface()->nativeResourceForWindow ("surface", w);
		if (!create || !app || !s) return VK_ERROR_INITIALIZATION_FAILED;
		VkWaylandSurfaceCreateInfoKHR ci = { VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR };
		ci.display = app->display();
		ci.surface = s;
		return (int)create (inst, &ci, alloc, surf);
	}
	auto create = (PFN_vkCreateXcbSurfaceKHR)qi->getInstanceProcAddr ("vkCreateXcbSurfaceKHR");
	auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
	if (!create || !x11) return VK_ERROR_INITIALIZATION_FAILED;
	VkXcbSurfaceCreateInfoKHR ci = { VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR };
	ci.connection = x11->connection();
	ci.window = (xcb_window_t)w->winId();
	return (int)create (inst, &ci, alloc, surf);
}

bool ImGui_ImplQt_Init (QWindow *window)
{
	ImGuiIO &io = ImGui::GetIO();
	IMGUI_CHECKVERSION();
	IM_ASSERT(io.BackendPlatformUserData == nullptr && "Already initialized a platform backend!");

	ImGui_ImplQt_Data *bd = IM_NEW(ImGui_ImplQt_Data)();
	io.BackendPlatformUserData = (void*)bd;
	io.BackendPlatformName = "imgui_impl_qt";
	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
	io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;
	io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;    // detach-windows
	io.BackendFlags |= ImGuiBackendFlags_HasMouseHoveredViewport;
	io.BackendFlags |= ImGuiBackendFlags_HasParentViewport;

	bd->Window = window;
	bd->Timer.start();
	bd->Time = 0;
	bd->Wayland = QGuiApplication::platformName().startsWith ("wayland");
	bd->Filter = new ImGui_ImplQt_Filter;
	for (QScreen *s : QGuiApplication::screens()) ImGui_ImplQt_WatchScreen (bd, s);
	QObject::connect (qGuiApp, &QGuiApplication::screenAdded, bd->Filter, [](QScreen *s) {
		if (ImGui_ImplQt_Data *d = ImGui_ImplQt_GetBackendData()) { ImGui_ImplQt_WatchScreen (d, s); d->WantUpdateMonitors = true; } });
	QObject::connect (qGuiApp, &QGuiApplication::screenRemoved, bd->Filter, [] {
		if (ImGui_ImplQt_Data *d = ImGui_ImplQt_GetBackendData()) d->WantUpdateMonitors = true; });
	QObject::connect (qGuiApp, &QGuiApplication::primaryScreenChanged, bd->Filter, [] {
		if (ImGui_ImplQt_Data *d = ImGui_ImplQt_GetBackendData()) d->WantUpdateMonitors = true; });
	ImGui_ImplQt_UpdateMonitors ();

	ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
	pio.Platform_GetClipboardTextFn = ImGui_ImplQt_GetClipboardText;
	pio.Platform_SetClipboardTextFn = ImGui_ImplQt_SetClipboardText;
	pio.Platform_CreateWindow = ImGui_ImplQt_CreateWindow; // ImGui_ImplWin32_InitMultiViewportSupport
	pio.Platform_DestroyWindow = ImGui_ImplQt_DestroyWindow;
	pio.Platform_ShowWindow = ImGui_ImplQt_ShowWindow;
	pio.Platform_SetWindowPos = ImGui_ImplQt_SetWindowPos;
	pio.Platform_GetWindowPos = ImGui_ImplQt_GetWindowPos;
	pio.Platform_SetWindowSize = ImGui_ImplQt_SetWindowSize;
	pio.Platform_GetWindowSize = ImGui_ImplQt_GetWindowSize;
	pio.Platform_SetWindowFocus = ImGui_ImplQt_SetWindowFocus;
	pio.Platform_GetWindowFocus = ImGui_ImplQt_GetWindowFocus;
	pio.Platform_GetWindowMinimized = ImGui_ImplQt_GetWindowMinimized;
	pio.Platform_SetWindowTitle = ImGui_ImplQt_SetWindowTitle;
	pio.Platform_SetWindowAlpha = ImGui_ImplQt_SetWindowAlpha;
	pio.Platform_UpdateWindow = ImGui_ImplQt_UpdateWindow;
	pio.Platform_GetWindowDpiScale = ImGui_ImplQt_GetWindowDpiScale;
	pio.Platform_CreateVkSurface = ImGui_ImplQt_CreateVkSurface;

	ImGuiViewport *vp = ImGui::GetMainViewport();
	vp->PlatformHandle = vp->PlatformHandleRaw = (void*)window;
	ImGui_ImplQt_ViewportData *vd = IM_NEW(ImGui_ImplQt_ViewportData)();
	vd->Window = window;
	vd->Owned = false;
	vp->PlatformUserData = vd;
	return true;
}

void ImGui_ImplQt_Shutdown ()
{
	ImGui_ImplQt_Data *bd = ImGui_ImplQt_GetBackendData();
	IM_ASSERT(bd != nullptr && "No platform backend to shutdown, or already shutdown?");
	ImGuiIO &io = ImGui::GetIO();
	ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
	ImGui::DestroyPlatformWindows();
	pio.ClearPlatformHandlers();
	delete bd->Filter;
	io.BackendPlatformName = nullptr;
	io.BackendPlatformUserData = nullptr;
	io.BackendFlags &= ~(ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_HasSetMousePos | ImGuiBackendFlags_PlatformHasViewports |
		ImGuiBackendFlags_HasMouseHoveredViewport | ImGuiBackendFlags_HasParentViewport);
	IM_DELETE(bd);
}

static void ImGui_ImplQt_UpdateMouseCursor (ImGui_ImplQt_Data *bd)
{
	ImGuiIO &io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) return;
	ImGuiMouseCursor cur = ImGui::GetMouseCursor();
	if (io.MouseDrawCursor) cur = ImGuiMouseCursor_None;
	if (cur == bd->LastMouseCursor) return;
	bd->LastMouseCursor = cur;
	Qt::CursorShape shape;
	switch (cur) {
	case ImGuiMouseCursor_None:       shape = Qt::BlankCursor; break;
	case ImGuiMouseCursor_TextInput:  shape = Qt::IBeamCursor; break;
	case ImGuiMouseCursor_ResizeAll:  shape = Qt::SizeAllCursor; break;
	case ImGuiMouseCursor_ResizeNS:   shape = Qt::SizeVerCursor; break;
	case ImGuiMouseCursor_ResizeEW:   shape = Qt::SizeHorCursor; break;
	case ImGuiMouseCursor_ResizeNESW: shape = Qt::SizeBDiagCursor; break;
	case ImGuiMouseCursor_ResizeNWSE: shape = Qt::SizeFDiagCursor; break;
	case ImGuiMouseCursor_Hand:       shape = Qt::PointingHandCursor; break;
	case ImGuiMouseCursor_Wait:       shape = Qt::WaitCursor; break;
	case ImGuiMouseCursor_Progress:   shape = Qt::BusyCursor; break;
	case ImGuiMouseCursor_NotAllowed: shape = Qt::ForbiddenCursor; break;
	default:                          shape = Qt::ArrowCursor; break;
	}
	bd->CursorShape = shape;
	for (ImGuiViewport *vp : ImGui::GetPlatformIO().Viewports) // SetCursor is the whole application's on Windows
		if (QWindow *w = (QWindow*)vp->PlatformHandle) w->setCursor (QCursor (shape));
}

void ImGui_ImplQt_NewFrame ()
{
	ImGui_ImplQt_Data *bd = ImGui_ImplQt_GetBackendData();
	IM_ASSERT(bd != nullptr && "Context or backend not initialized? Did you call ImGui_ImplQt_Init()?");
	ImGuiIO &io = ImGui::GetIO();

	// display size in logical pixels; the renderer scales by the framebuffer ratio
	io.DisplaySize = ImVec2 ((float)bd->Window->width(), (float)bd->Window->height());
	float dpr = (float)bd->Window->devicePixelRatio();
	io.DisplayFramebufferScale = ImVec2 (dpr, dpr);
	if (bd->WantUpdateMonitors) ImGui_ImplQt_UpdateMonitors ();

	qint64 now = bd->Timer.nsecsElapsed();
	io.DeltaTime = (now > bd->Time ? (float)((now - bd->Time) * 1e-9) : 1.0f/60.0f);
	bd->Time = now;

	// Wayland: a window the compositor carries. ImGui moves it over the render window, where the drag says where the
	// pointer is; elsewhere ImGui keeps it parked outside the render window. The drop's release reaches no window.
	ImGuiViewport *mv = ImGui::GetMainViewport();
	if (bd->Carry == 1) {
		bool over;
		float x, y;
		bool ended = WlWindowDragPoll (bd->Window, over, x, y);
		ImGuiViewport *cv = ImGui::FindViewportByID (bd->CarryId);
		if (over) bd->CarryLocal = ImVec2 (x, y);
		else if (bd->CarryOver && cv) { // the pointer has left the render window
			ImVec2 park = ImGui_ImplQt_ParkedPos (bd->CarryLocal, mv->Size, cv->Size, bd->CarryGrab);
			bd->Hold = ImVec2 (mv->Pos.x + park.x + bd->CarryGrab.x, mv->Pos.y + park.y + bd->CarryGrab.y);
		}
		bd->CarryOver = over;
		ImVec2 p = over ? ImVec2 (mv->Pos.x + x, mv->Pos.y + y) : bd->Hold;
		io.AddMousePosEvent (p.x, p.y);
		if (ended) bd->Carry = 2;
	} else if (bd->Carry == 2) { // the release a frame after the drop's position, so ImGui's move takes that first
		io.AddMouseButtonEvent (ImGuiMouseButton_Left, false);
		bd->Carry = 3;
	} else if (bd->Carry == 3 && !GImGui->MovingWindow) { // ImGui has ended its move (merged, or not)
		bd->Carry = 0;
		if (ImGui_ImplQt_ViewportData *cd = ImGui_ImplQt_Data_ (ImGui::FindViewportByID (bd->CarryId)))
			cd->Anchored = cd->SystemDrag = false;
	}

	if (io.WantSetMousePos && !bd->Wayland) {
		if (ImGui_ImplQt_Viewports()) QCursor::setPos ((int)io.MousePos.x, (int)io.MousePos.y);
		else if (bd->Window->isActive()) QCursor::setPos (bd->Window->mapToGlobal (QPoint ((int)io.MousePos.x, (int)io.MousePos.y)));
	}

	// ImGui_ImplWin32_UpdateMouseData: the viewport the pointer is over, left out where it lets the mouse through;
	// while a window is carried, the render window or none (the carried one lets the mouse through, as ImGui wants)
	if (ImGui_ImplQt_Viewports()) {
		if (bd->Carry) io.AddMouseViewportEvent (bd->CarryOver ? mv->ID : 0);
		else {
			QWindow *hw = bd->Wayland ? bd->MouseWindow : QGuiApplication::topLevelAt (QCursor::pos());
			ImGuiViewport *hv = hw ? ImGui_ImplQt_FindViewport (hw) : nullptr;
			io.AddMouseViewportEvent ((hv && !(hv->Flags & ImGuiViewportFlags_NoInputs)) ? hv->ID : 0);
		}
	}

	ImGui_ImplQt_UpdateMouseCursor (bd);
}

static ImGuiKey ImGui_ImplQt_KeyToImGuiKey (int key, bool keypad)
{
	if (keypad) {
		switch (key) {
		case Qt::Key_0: return ImGuiKey_Keypad0;
		case Qt::Key_1: return ImGuiKey_Keypad1;
		case Qt::Key_2: return ImGuiKey_Keypad2;
		case Qt::Key_3: return ImGuiKey_Keypad3;
		case Qt::Key_4: return ImGuiKey_Keypad4;
		case Qt::Key_5: return ImGuiKey_Keypad5;
		case Qt::Key_6: return ImGuiKey_Keypad6;
		case Qt::Key_7: return ImGuiKey_Keypad7;
		case Qt::Key_8: return ImGuiKey_Keypad8;
		case Qt::Key_9: return ImGuiKey_Keypad9;
		case Qt::Key_Period: return ImGuiKey_KeypadDecimal;
		case Qt::Key_Slash: return ImGuiKey_KeypadDivide;
		case Qt::Key_Asterisk: return ImGuiKey_KeypadMultiply;
		case Qt::Key_Minus: return ImGuiKey_KeypadSubtract;
		case Qt::Key_Plus: return ImGuiKey_KeypadAdd;
		case Qt::Key_Enter: return ImGuiKey_KeypadEnter;
		case Qt::Key_Equal: return ImGuiKey_KeypadEqual;
		default: break;
		}
	}
	if (key >= Qt::Key_A && key <= Qt::Key_Z) return (ImGuiKey)(ImGuiKey_A + (key - Qt::Key_A));
	if (key >= Qt::Key_0 && key <= Qt::Key_9) return (ImGuiKey)(ImGuiKey_0 + (key - Qt::Key_0));
	if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return (ImGuiKey)(ImGuiKey_F1 + (key - Qt::Key_F1));
	switch (key) {
	case Qt::Key_Tab: case Qt::Key_Backtab: return ImGuiKey_Tab;
	case Qt::Key_Left: return ImGuiKey_LeftArrow;
	case Qt::Key_Right: return ImGuiKey_RightArrow;
	case Qt::Key_Up: return ImGuiKey_UpArrow;
	case Qt::Key_Down: return ImGuiKey_DownArrow;
	case Qt::Key_PageUp: return ImGuiKey_PageUp;
	case Qt::Key_PageDown: return ImGuiKey_PageDown;
	case Qt::Key_Home: return ImGuiKey_Home;
	case Qt::Key_End: return ImGuiKey_End;
	case Qt::Key_Insert: return ImGuiKey_Insert;
	case Qt::Key_Delete: return ImGuiKey_Delete;
	case Qt::Key_Backspace: return ImGuiKey_Backspace;
	case Qt::Key_Space: return ImGuiKey_Space;
	case Qt::Key_Return: return ImGuiKey_Enter;
	case Qt::Key_Enter: return ImGuiKey_KeypadEnter;
	case Qt::Key_Escape: return ImGuiKey_Escape;
	case Qt::Key_Apostrophe: return ImGuiKey_Apostrophe;
	case Qt::Key_Comma: return ImGuiKey_Comma;
	case Qt::Key_Minus: return ImGuiKey_Minus;
	case Qt::Key_Period: return ImGuiKey_Period;
	case Qt::Key_Slash: return ImGuiKey_Slash;
	case Qt::Key_Semicolon: return ImGuiKey_Semicolon;
	case Qt::Key_Equal: return ImGuiKey_Equal;
	case Qt::Key_BracketLeft: return ImGuiKey_LeftBracket;
	case Qt::Key_Backslash: return ImGuiKey_Backslash;
	case Qt::Key_BracketRight: return ImGuiKey_RightBracket;
	case Qt::Key_QuoteLeft: return ImGuiKey_GraveAccent;
	case Qt::Key_CapsLock: return ImGuiKey_CapsLock;
	case Qt::Key_ScrollLock: return ImGuiKey_ScrollLock;
	case Qt::Key_NumLock: return ImGuiKey_NumLock;
	case Qt::Key_Print: return ImGuiKey_PrintScreen;
	case Qt::Key_Pause: return ImGuiKey_Pause;
	case Qt::Key_Shift: return ImGuiKey_LeftShift;
	case Qt::Key_Control: return ImGuiKey_LeftCtrl;
	case Qt::Key_Alt: return ImGuiKey_LeftAlt;
	case Qt::Key_AltGr: return ImGuiKey_RightAlt;
	case Qt::Key_Meta: return ImGuiKey_LeftSuper;
	case Qt::Key_Menu: return ImGuiKey_Menu;
	default: return ImGuiKey_None;
	}
}

static void ImGui_ImplQt_UpdateKeyModifiers (Qt::KeyboardModifiers mod)
{
	ImGuiIO &io = ImGui::GetIO();
	io.AddKeyEvent (ImGuiMod_Ctrl, (mod & Qt::ControlModifier) != 0);
	io.AddKeyEvent (ImGuiMod_Shift, (mod & Qt::ShiftModifier) != 0);
	io.AddKeyEvent (ImGuiMod_Alt, (mod & Qt::AltModifier) != 0);
	io.AddKeyEvent (ImGuiMod_Super, (mod & Qt::MetaModifier) != 0);
}

static int ImGui_ImplQt_MouseButton (Qt::MouseButton b)
{
	switch (b) {
	case Qt::LeftButton:   return 0;
	case Qt::RightButton:  return 1;
	case Qt::MiddleButton: return 2;
	case Qt::BackButton:   return 3;
	case Qt::ForwardButton: return 4;
	default:               return -1;
	}
}

// any of the application's ImGui windows has the keyboard (WM_SETFOCUS/WM_KILLFOCUS of all of them)
static bool ImGui_ImplQt_AppFocused ()
{
	QWindow *f = QGuiApplication::focusWindow();
	return f && ImGui_ImplQt_FindViewport (f);
}

bool ImGui_ImplQt_EventHandler (QWindow *window, QEvent *event)
{
	if (!ImGui::GetCurrentContext()) return false;
	ImGui_ImplQt_Data *bd = ImGui_ImplQt_GetBackendData();
	ImGuiViewport *vp = bd ? ImGui_ImplQt_FindViewport (window) : nullptr;
	if (!vp) return false;
	ImGui_ImplQt_ViewportData *vd = ImGui_ImplQt_Data_ (vp);
	ImGuiIO &io = ImGui::GetIO();

	switch (event->type()) {
	case QEvent::Enter:
		bd->MouseWindow = window;
		return false;
	case QEvent::MouseMove: {
		if (bd->Carry) return false; // the drag has the pointer
		QMouseEvent *me = static_cast<QMouseEvent*> (event);
		vd->Local = me->position();
		ImVec2 p = ImGui_ImplQt_MousePos (window, vp, me->position());
		io.AddMouseSourceEvent (ImGuiMouseSource_Mouse);
		io.AddMousePosEvent (p.x, p.y);
		} return false;
	case QEvent::Leave:
		if (bd->MouseWindow == window || !bd->MouseWindow) {
			bd->MouseWindow = nullptr;
			if (!bd->Carry) io.AddMousePosEvent (-FLT_MAX, -FLT_MAX);
		}
		return false;
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonDblClick:
	case QEvent::MouseButtonRelease: {
		if (bd->Carry) return false;
		QMouseEvent *me = static_cast<QMouseEvent*> (event);
		bool down = (event->type() != QEvent::MouseButtonRelease);
		vd->Local = me->position();
		if (down && bd->Wayland) {
			if (auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>())
				bd->PressSerial = app->lastInputSerial(), bd->PressWindow = window;
			if (vd->Owned && !vd->Popup && !vd->Anchored) {
				vd->Anchor = vp->Pos, vd->Anchored = true;
				vd->PressLocal = me->position();
				vd->PressPos = ImVec2 (vd->Anchor.x + (float)me->position().x(), vd->Anchor.y + (float)me->position().y());
			}
		}
		int b = ImGui_ImplQt_MouseButton (me->button());
		ImVec2 p = ImGui_ImplQt_MousePos (window, vp, me->position());
		ImGui_ImplQt_UpdateKeyModifiers (me->modifiers());
		io.AddMouseSourceEvent (ImGuiMouseSource_Mouse);
		if (b >= 0) {
			io.AddMousePosEvent (p.x, p.y);
			io.AddMouseButtonEvent (b, down);
		} else if (!down && me->buttons() == Qt::NoButton) { // Qt ended the buttons itself (a compositor move or resize took the pointer)
			io.AddMousePosEvent (p.x, p.y);
			for (int i = 0; i < ImGuiMouseButton_COUNT; i++) io.AddMouseButtonEvent (i, false);
		}
		if (!down && vd->Anchored && !(me->buttons() & (Qt::LeftButton | Qt::RightButton | Qt::MiddleButton)))
			vd->Anchored = vd->SystemDrag = false;
		} return false;
	case QEvent::Wheel: {
		QWheelEvent *we = static_cast<QWheelEvent*> (event);
		QPoint d = we->angleDelta(); // 120 per notch, as WHEEL_DELTA
		io.AddMouseWheelEvent ((float)d.x() / 120.0f, (float)d.y() / 120.0f); // Qt's positive x is left, as ImGui's (WM_MOUSEHWHEEL's is right)
		} return false;
	case QEvent::KeyPress:
	case QEvent::KeyRelease: {
		QKeyEvent *ke = static_cast<QKeyEvent*> (event);
		bool down = (event->type() == QEvent::KeyPress);
		ImGui_ImplQt_UpdateKeyModifiers (ke->modifiers());
		ImGuiKey key = ImGui_ImplQt_KeyToImGuiKey (ke->key(), (ke->modifiers() & Qt::KeypadModifier) != 0);
		if (key != ImGuiKey_None) {
			io.AddKeyEvent (key, down);
			io.SetKeyEventNativeData (key, ke->key(), (int)ke->nativeScanCode());
		}
		if (down && !ke->text().isEmpty()) { // WM_CHAR
			QByteArray utf8 = ke->text().toUtf8();
			if ((unsigned char)utf8[0] >= 0x20 && utf8[0] != 0x7f)
				io.AddInputCharactersUTF8 (utf8.constData());
		}
		} return false;
	case QEvent::FocusIn:
	case QEvent::FocusOut:
		io.AddFocusEvent (ImGui_ImplQt_AppFocused ());
		return false;
	default:
		return false;
	}
}

// ImGui_ImplWin32_WndProcHandler_PlatformWindow: the secondary windows' events
bool ImGui_ImplQt_Filter::eventFilter (QObject *o, QEvent *e)
{
	QWindow *w = qobject_cast<QWindow*> (o);
	ImGui_ImplQt_Data *bd = ImGui_ImplQt_GetBackendData();
	ImGuiViewport *vp = (w && bd) ? ImGui_ImplQt_FindViewport (w) : nullptr;
	if (!vp) return false;
	switch (e->type()) {
	case QEvent::Close: // WM_CLOSE: ImGui closes it
		vp->PlatformRequestClose = true;
		e->ignore();
		return true;
	case QEvent::Move:
		if (!bd->Wayland) vp->PlatformRequestMove = true;
		return false;
	case QEvent::Resize:
		vp->PlatformRequestResize = true;
		return false;
	default:
		return ImGui_ImplQt_EventHandler (w, e);
	}
}
