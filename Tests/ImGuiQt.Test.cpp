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
TEST_CASE("Wayland: the compositor resizes the edges that move a window's top-left corner (detach-windows)", "[imgui]")
{
	REQUIRE(ImGui_ImplQt_ResizeEdges (0, -1) == Qt::Edges());
	REQUIRE(ImGui_ImplQt_ResizeEdges (1, -1) == (Qt::LeftEdge | Qt::BottomEdge));
	REQUIRE(ImGui_ImplQt_ResizeEdges (2, -1) == (Qt::LeftEdge | Qt::TopEdge));
	REQUIRE(ImGui_ImplQt_ResizeEdges (3, -1) == (Qt::RightEdge | Qt::TopEdge));
	REQUIRE(ImGui_ImplQt_ResizeEdges (-1, ImGuiDir_Left) == Qt::LeftEdge);
	REQUIRE(ImGui_ImplQt_ResizeEdges (-1, ImGuiDir_Up) == Qt::TopEdge);
	REQUIRE(ImGui_ImplQt_ResizeEdges (-1, ImGuiDir_Right) == Qt::Edges());
	REQUIRE(ImGui_ImplQt_ResizeEdges (-1, ImGuiDir_Down) == Qt::Edges());

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
