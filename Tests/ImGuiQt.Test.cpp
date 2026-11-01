// not upstream: Dear ImGui's Qt platform backend (imgui_impl_qt.cpp), offscreen
#include <catch2/catch_test_macros.hpp>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWindow>
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_qt.h"
#include "WlWindowDrag.h"
#include "OrbiterAPI.h"
#include "imgui_extras.h"
#include "ImGuiAspect.h"
#include <cmath>

ImGuiContext *GImGui = nullptr; // stub: Orbiter's exe defines the context pointer (DlgMgr.cpp) that imconfig.h declares
bool WlWindowDragStart (QWindow*, unsigned, QWindow*, int, int) { return false; } // stub: offscreen is not Wayland
bool WlWindowDragPoll (QWindow*, bool &over, float&, float&) { over = false; return false; }

static QGuiApplication &App ()
{
	static int argc = 1;
	static char name[] = "ImGuiQt.Test", *argv[] = {name, nullptr};
	if (qEnvironmentVariableIsEmpty ("QT_QPA_PLATFORM")) qputenv ("QT_QPA_PLATFORM", "offscreen");
	static QGuiApplication app (argc, argv);
	return app;
}

TEST_CASE("wheel events keep their direction (Qt's and ImGui's positive x is left)", "[imgui]")
{
	App ();
	QWindow w;
	w.resize (200, 100);
	ImGui::CreateContext();
	REQUIRE(ImGui_ImplQt_Init (&w));
	ImGuiContext &g = *ImGui::GetCurrentContext();
	auto wheel = [&](QPoint angle) {
		QWheelEvent e (QPointF (10, 10), QPointF (10, 10), QPoint (), angle, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
		ImGui_ImplQt_EventHandler (&w, &e);
		REQUIRE(!g.InputEventsQueue.empty());
		const ImGuiInputEvent &q = g.InputEventsQueue.back();
		REQUIRE(q.Type == ImGuiInputEventType_MouseWheel);
		return ImVec2 (q.MouseWheel.WheelX, q.MouseWheel.WheelY);
	};
	ImVec2 left = wheel (QPoint (120, 0)); // X11 button 6 (scroll left) comes as angleDelta (120, 0)
	REQUIRE(left.x == 1.0f);
	REQUIRE(left.y == 0.0f);
	ImVec2 up = wheel (QPoint (0, 120));
	REQUIRE(up.x == 0.0f);
	REQUIRE(up.y == 1.0f);
	ImGui_ImplQt_Shutdown();
	ImGui::DestroyContext();
}

TEST_CASE("secondary viewports get the window flags imgui_impl_win32 gives them (detach-windows)", "[imgui]")
{
	REQUIRE(ImGui_ImplQt_WindowFlags (ImGuiViewportFlags_NoDecoration, false) == (Qt::Window | Qt::FramelessWindowHint));
	REQUIRE(ImGui_ImplQt_WindowFlags (ImGuiViewportFlags_NoDecoration | ImGuiViewportFlags_NoTaskBarIcon, false) == (Qt::Tool | Qt::FramelessWindowHint));
	REQUIRE(ImGui_ImplQt_WindowFlags (ImGuiViewportFlags_NoDecoration | ImGuiViewportFlags_NoTaskBarIcon | ImGuiViewportFlags_NoFocusOnClick |
		ImGuiViewportFlags_TopMost, true) == (Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus | Qt::WindowStaysOnTopHint));
	REQUIRE((ImGui_ImplQt_WindowFlags (ImGuiViewportFlags_NoInputs, false) & Qt::WindowTransparentForInput));
	REQUIRE(!(ImGui_ImplQt_WindowFlags (0, false) & Qt::FramelessWindowHint));
}

// the corner and border numbers the Wayland compositor resize relies on, read from ImGui itself: a press on each
// grip or border of a window makes it ImGui's active item
TEST_CASE("Wayland: the compositor resizes a detached window by any edge or corner (gpsnav-window)", "[imgui]")
{
	REQUIRE(ImGui_ImplQt_ResizeEdges (0, -1) == (Qt::RightEdge | Qt::BottomEdge));
	REQUIRE(ImGui_ImplQt_ResizeEdges (1, -1) == (Qt::LeftEdge | Qt::BottomEdge));
	REQUIRE(ImGui_ImplQt_ResizeEdges (2, -1) == (Qt::LeftEdge | Qt::TopEdge));
	REQUIRE(ImGui_ImplQt_ResizeEdges (3, -1) == (Qt::RightEdge | Qt::TopEdge));
	REQUIRE(ImGui_ImplQt_ResizeEdges (-1, ImGuiDir_Left) == Qt::LeftEdge);
	REQUIRE(ImGui_ImplQt_ResizeEdges (-1, ImGuiDir_Up) == Qt::TopEdge);
	REQUIRE(ImGui_ImplQt_ResizeEdges (-1, ImGuiDir_Right) == Qt::RightEdge);
	REQUIRE(ImGui_ImplQt_ResizeEdges (-1, ImGuiDir_Down) == Qt::BottomEdge);
	REQUIRE(ImGui_ImplQt_ResizeEdges (-1, -1) == Qt::Edges());

	App ();
	QWindow w;
	w.resize (800, 600);
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.Fonts->AddFontDefault();
	io.Fonts->Build();
	REQUIRE(ImGui_ImplQt_Init (&w));
	ImGuiContext &g = *ImGui::GetCurrentContext();
	ImGuiWindow *win = nullptr;
	auto frame = [&]() {
		ImGui_ImplQt_NewFrame();
		ImGui::NewFrame();
		ImGui::SetNextWindowPos (ImVec2 (200, 150));
		ImGui::SetNextWindowSize (ImVec2 (300, 200));
		ImGui::Begin ("Detach test");
		win = ImGui::GetCurrentWindow();
		ImGui::End();
		ImGui::Render();
	};
	auto press = [&](float x, float y) {
		io.AddMousePosEvent (x, y);
		frame ();
		io.AddMouseButtonEvent (0, true);
		frame ();
		ImGuiID id = g.ActiveId;
		io.AddMouseButtonEvent (0, false);
		frame ();
		io.AddMousePosEvent (-FLT_MAX, -FLT_MAX);
		frame ();
		return id;
	};
	frame ();
	REQUIRE(win);
	REQUIRE(press (200 + 3, 350 - 3) == ImGui::GetWindowResizeCornerID (win, 1)); // bottom-left grip
	REQUIRE(press (500 - 3, 350 - 3) == ImGui::GetWindowResizeCornerID (win, 0)); // bottom-right grip
	REQUIRE(press (200 - 2, 250) == ImGui::GetWindowResizeBorderID (win, ImGuiDir_Left));
	REQUIRE(press (350, 150 - 2) == ImGui::GetWindowResizeBorderID (win, ImGuiDir_Up));
	ImGui_ImplQt_Shutdown();
	ImGui::DestroyContext();
}

// Qt's releaseButtons after startSystemMove/startSystemResize: a release whose button and buttons are both NoButton
TEST_CASE("a release with no button ends ImGui's buttons (detach-windows)", "[imgui]")
{
	App ();
	QWindow w;
	w.resize (200, 100);
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.Fonts->AddFontDefault();
	io.Fonts->Build();
	REQUIRE(ImGui_ImplQt_Init (&w));
	auto frame = [&]() {
		ImGui_ImplQt_NewFrame();
		ImGui::NewFrame();
		ImGui::Render();
	};
	QMouseEvent left (QEvent::MouseButtonPress, QPointF (20, 20), QPointF (20, 20), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QMouseEvent right (QEvent::MouseButtonPress, QPointF (20, 20), QPointF (20, 20), Qt::RightButton, Qt::LeftButton | Qt::RightButton, Qt::NoModifier);
	ImGui_ImplQt_EventHandler (&w, &left);
	ImGui_ImplQt_EventHandler (&w, &right);
	frame ();
	frame ();
	REQUIRE(io.MouseDown[0]);
	REQUIRE(io.MouseDown[1]);
	QMouseEvent ended (QEvent::MouseButtonRelease, QPointF (20, 20), QPointF (20, 20), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
	ImGui_ImplQt_EventHandler (&w, &ended);
	frame ();
	frame ();
	for (int i = 0; i < ImGuiMouseButton_COUNT; i++) REQUIRE(!io.MouseDown[i]);
	ImGui_ImplQt_EventHandler (&w, &left); // a release of an unknown button while Qt still has the left one down ends nothing
	frame ();
	QMouseEvent other (QEvent::MouseButtonRelease, QPointF (20, 20), QPointF (20, 20), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
	ImGui_ImplQt_EventHandler (&w, &other);
	frame ();
	REQUIRE(io.MouseDown[0]);
	ImGui_ImplQt_Shutdown();
	ImGui::DestroyContext();
}

// Wayland: a window the compositor carried out of the render window (800x600) is kept fully outside it, next to the
// edge the pointer left by, with its grab point (30, 10) in line with the pointer's last position
TEST_CASE("Wayland: a carried window is parked outside the render window by the edge it left by (detach-windows)", "[imgui]")
{
	ImVec2 main (800, 600), size (300, 200), grab (30, 10);
	auto park = [&](float x, float y) { return ImGui_ImplQt_ParkedPos (ImVec2 (x, y), main, size, grab); };
	ImVec2 l = park (2, 300), r = park (797, 300), t = park (400, 1), b = park (400, 598);
	REQUIRE((l.x == -301 && l.y == 290));
	REQUIRE((r.x == 801 && r.y == 290));
	REQUIRE((t.x == 370 && t.y == -201));
	REQUIRE((b.x == 370 && b.y == 601));
	for (ImVec2 p : { l, r, t, b }) // no part of it inside: ImGui can neither merge it nor see it over the render window
		REQUIRE((p.x + size.x < 0 || p.x > main.x || p.y + size.y < 0 || p.y > main.y));
	ImVec2 c = park (5, 3); // near a corner: the nearer edge, the top
	REQUIRE((c.x == -25 && c.y == -201));
}

// a 1.45 window with a 20 px title bar, at least 360 wide
TEST_CASE("a window's shape from the size wanted and the side dragged (gpsnav-window)", "[imgui]")
{
	const float A = 1.45f, E = 20.0f, M = 360.0f;
	auto sz = [&](float x, float y, ImGuiAspectSide s) { return ImGuiAspectSize (ImVec2 (x, y), s, A, E, M); };
	auto same = [](ImVec2 a, float x, float y) { return a.x == x && a.y == y; };
	auto inShape = [&](ImVec2 a) { return a.x >= M && std::fabs (a.y - (a.x / A + E)) <= 1.0f && a.x == std::round (a.x) && a.y == std::round (a.y); };
	for (ImGuiAspectSide s : { ImGuiAspectSide::None, ImGuiAspectSide::Width, ImGuiAspectSide::Height, ImGuiAspectSide::Corner })
		REQUIRE(same (sz (580, 420, s), 580, 420)); // in shape: kept
	REQUIRE(same (sz (725, 300, ImGuiAspectSide::Width), 725, 520));
	REQUIRE(same (sz (600, 540, ImGuiAspectSide::Height), 754, 540));
	ImVec2 c = sz (725, 300, ImGuiAspectSide::Corner); // between the two, nearest the point wanted
	REQUIRE(inShape (c));
	REQUIRE((c.x > 580 && c.x < 725));
	auto dist = [](ImVec2 a, float x, float y) { return std::hypot (a.x - x, a.y - y); };
	for (float w = c.x - 20; w <= c.x + 20; w += 1) REQUIRE(dist (c, 725, 300) <= dist (ImVec2 (w, w / A + E), 725, 300) + 1.0f);
	REQUIRE(same (sz (725, 300, ImGuiAspectSide::None), c.x, c.y));
	REQUIRE(same (sz (100, 50, ImGuiAspectSide::Width), 360, 268)); // the minimum width
	REQUIRE(same (sz (900, 120, ImGuiAspectSide::Height), 360, 268));
	for (ImGuiAspectSide s : { ImGuiAspectSide::None, ImGuiAspectSide::Width, ImGuiAspectSide::Height, ImGuiAspectSide::Corner })
		for (float x = 300; x < 1500; x += 37)
			for (float y = 200; y < 1100; y += 41) {
				ImVec2 r = sz (x, y, s);
				REQUIRE(inShape (r));
				for (ImGuiAspectSide t : { ImGuiAspectSide::None, ImGuiAspectSide::Width, ImGuiAspectSide::Height, ImGuiAspectSide::Corner })
					REQUIRE(same (sz (r.x, r.y, t), r.x, r.y)); // its own result is kept, whatever side comes next
			}
}

// ImGui's own resize of a window made with SetNextWindowAspect: the side follows the grip held, and a corner held still
// gives the same size every frame (GPSNav's old rule flipped between two)
TEST_CASE("a window that keeps its shape gives one size per pointer position, by any grip (gpsnav-window)", "[imgui]")
{
	App ();
	QWindow w;
	w.resize (800, 600);
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.Fonts->AddFontDefault();
	io.Fonts->Build();
	REQUIRE(ImGui_ImplQt_Init (&w));
	ImGuiContext &g = *ImGui::GetCurrentContext();
	const float A = 1.45f;
	ImGuiWindow *win = nullptr;
	float t = 0.0f;
	auto frame = [&]() {
		ImGui_ImplQt_NewFrame();
		ImGui::NewFrame();
		t = ImGui::GetFrameHeight ();
		ImGui::SetNextWindowPos (ImVec2 (100, 100), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize (ImVec2 (290, 200 + t), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowAspect (A, t, 100.0f);
		ImGui::Begin ("Aspect test");
		win = ImGui::GetCurrentWindow();
		ImGui::End();
		ImGui::Render();
	};
	auto inShape = [&]() { return std::fabs (win->SizeFull.y - (win->SizeFull.x / A + t)) <= 1.0f; };
	auto drag = [&](float x0, float y0, float x1, float y1) {
		io.AddMousePosEvent (x0, y0);
		frame ();
		io.AddMouseButtonEvent (0, true);
		frame ();
		ImGuiID id = g.ActiveId;
		io.AddMousePosEvent (x1, y1);
		frame ();
		return id;
	};
	auto release = [&]() {
		io.AddMouseButtonEvent (0, false);
		frame ();
		io.AddMousePosEvent (-FLT_MAX, -FLT_MAX);
		frame ();
	};
	frame ();
	frame ();
	REQUIRE(inShape ());
	ImVec2 p = win->Pos, s = win->SizeFull;

	REQUIRE(drag (p.x + s.x - 3, p.y + s.y - 3, p.x + s.x + 60, p.y + s.y + 5) == ImGui::GetWindowResizeCornerID (win, 0));
	ImVec2 held = win->SizeFull;
	REQUIRE(inShape ());
	REQUIRE(held.x > s.x);
	for (int i = 0; i < 10; i++) {
		frame ();
		REQUIRE((win->SizeFull.x == held.x && win->SizeFull.y == held.y));
		REQUIRE((win->Pos.x == p.x && win->Pos.y == p.y)); // the bottom-right grip keeps the top-left corner
	}
	release ();
	REQUIRE((win->SizeFull.x == held.x && win->SizeFull.y == held.y));

	s = win->SizeFull, p = win->Pos;
	REQUIRE(drag (p.x + s.x + 2, p.y + s.y / 2, p.x + s.x + 42, p.y + s.y / 2) == ImGui::GetWindowResizeBorderID (win, ImGuiDir_Right));
	REQUIRE(win->SizeFull.x == s.x + 40);
	REQUIRE(inShape ());
	release ();

	s = win->SizeFull, p = win->Pos;
	REQUIRE(drag (p.x + s.x / 2, p.y + s.y + 2, p.x + s.x / 2, p.y + s.y + 32) == ImGui::GetWindowResizeBorderID (win, ImGuiDir_Down));
	REQUIRE(win->SizeFull.y == s.y + 30);
	REQUIRE(inShape ());
	release ();

	ImGui::SetWindowSize ("Aspect test", ImVec2 (500, 500)); // no grip: the nearest size in shape, then kept
	frame ();
	REQUIRE(inShape ());
	ImVec2 n = win->SizeFull;
	for (int i = 0; i < 3; i++) {
		frame ();
		REQUIRE((win->SizeFull.x == n.x && win->SizeFull.y == n.y));
	}
	ImGui_ImplQt_Shutdown();
	ImGui::DestroyContext();
}

// X11 (here offscreen, which isn't Wayland): with viewports the mouse is the event's root position. Qt keeps the geometry
// last asked for, so mapping the local position from it is wrong while a move ImGui asked for hasn't reached the server
TEST_CASE("X11: the mouse is the event's own desktop position, not one mapped from the window's geometry (gpsnav-window)", "[imgui]")
{
	App ();
	QWindow w;
	w.setGeometry (100, 100, 400, 300);
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	REQUIRE(ImGui_ImplQt_Init (&w));
	ImGuiContext &g = *ImGui::GetCurrentContext();
	w.setPosition (700, 500); // asked for; the events below came from where the window was
	QMouseEvent move (QEvent::MouseMove, QPointF (10, 20), QPointF (110, 120), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
	ImGui_ImplQt_EventHandler (&w, &move);
	REQUIRE(!g.InputEventsQueue.empty());
	const ImGuiInputEvent &q = g.InputEventsQueue.back();
	REQUIRE(q.Type == ImGuiInputEventType_MousePos);
	REQUIRE((q.MousePos.PosX == 110.0f && q.MousePos.PosY == 120.0f));
	ImGui_ImplQt_Shutdown();
	ImGui::DestroyContext();
}

// X11: monitors as his three (1920x1080, 2560x1440, 1920x1080 side by side, the first the primary with a 40 px panel)
TEST_CASE("X11: a window whose title bar isn't wholly on the monitors goes back to the primary's middle (gpsnav-window)", "[imgui]")
{
	ImGuiPlatformMonitor m[3];
	m[0].MainPos = ImVec2 (0, 0), m[0].MainSize = ImVec2 (1920, 1080), m[0].WorkPos = ImVec2 (0, 0), m[0].WorkSize = ImVec2 (1920, 1040);
	m[1].MainPos = ImVec2 (1920, 0), m[1].MainSize = ImVec2 (2560, 1440);
	m[2].MainPos = ImVec2 (4480, 0), m[2].MainSize = ImVec2 (1920, 1080);
	REQUIRE(ImGui_ImplQt_TitleOnMonitors (1800, 2100, 100, m, 3));    // across the first two
	REQUIRE(ImGui_ImplQt_TitleOnMonitors (4000, 4700, 900, m, 3));    // across the second and third
	REQUIRE(!ImGui_ImplQt_TitleOnMonitors (1800, 2100, 1200, m, 3));  // below the first one's bottom
	REQUIRE(!ImGui_ImplQt_TitleOnMonitors (6300, 6500, 100, m, 3));   // past the last one's right edge
	REQUIRE(!ImGui_ImplQt_TitleOnMonitors (6500, 7000, 100, m, 3));   // on a monitor gone
	REQUIRE(!ImGui_ImplQt_TitleOnMonitors (-10, 300, 100, m, 3));     // left of the first
	REQUIRE(!ImGui_ImplQt_TitleOnMonitors (2000, 2600, 100, m, 1));   // only the first one left
	ImVec2 p = ImGui_ImplQt_BackOnPrimary (ImVec2 (1045, 741), m[0]);
	REQUIRE((p.x == 437.5f && p.y == 149.5f));
	ImVec2 big = ImGui_ImplQt_BackOnPrimary (ImVec2 (2000, 1200), m[0]); // larger than the work area: its top-left
	REQUIRE((big.x == 0.0f && big.y == 0.0f));
}
