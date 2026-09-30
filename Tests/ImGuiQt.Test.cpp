// not upstream: Dear ImGui's Qt platform backend (imgui_impl_qt.cpp), offscreen
#include <catch2/catch_test_macros.hpp>
#include <QGuiApplication>
#include <QWheelEvent>
#include <QWindow>
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_qt.h"

ImGuiContext *GImGui = nullptr; // stub: Orbiter's exe defines the context pointer (DlgMgr.cpp) that imconfig.h declares

TEST_CASE("wheel events keep their direction (Qt's and ImGui's positive x is left)", "[imgui]")
{
	static int argc = 1;
	static char name[] = "ImGuiQt.Test", *argv[] = {name, nullptr};
	if (qEnvironmentVariableIsEmpty ("QT_QPA_PLATFORM")) qputenv ("QT_QPA_PLATFORM", "offscreen");
	QGuiApplication app (argc, argv);
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
