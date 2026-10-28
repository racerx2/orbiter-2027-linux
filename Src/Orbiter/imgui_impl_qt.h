// not upstream: Dear ImGui platform backend for Qt windows (stands in for imgui_impl_win32)
// Feeds display size, time, mouse, keyboard, focus, cursor shape, clipboard and monitors, and makes the secondary
// viewports: an ImGui window dragged out of the render window gets a window of its own (detach-windows).

#pragma once
#include "imgui.h"
#include <QtCore/qnamespace.h>

class QWindow;
class QEvent;

bool ImGui_ImplQt_Init (QWindow *window);
void ImGui_ImplQt_Shutdown ();
void ImGui_ImplQt_NewFrame ();

// ImGui_ImplWin32_WndProcHandler counterpart: call with every event of the render window (the secondary windows'
// events come through the backend's own filter); returns true if the backend consumed the event
// (the caller checks io.WantCapture* itself)
bool ImGui_ImplQt_EventHandler (QWindow *window, QEvent *event);

// window flags of a secondary viewport (ImGui_ImplWin32_GetWin32StyleFromViewportFlags); popup: an ImGui popup or tooltip
inline Qt::WindowFlags ImGui_ImplQt_WindowFlags (ImGuiViewportFlags flags, bool popup)
{
	Qt::WindowFlags f = popup ? Qt::ToolTip : ((flags & ImGuiViewportFlags_NoTaskBarIcon) ? Qt::Tool : Qt::Window);
	if (flags & ImGuiViewportFlags_NoDecoration)   f |= Qt::FramelessWindowHint;
	if (flags & ImGuiViewportFlags_TopMost)        f |= Qt::WindowStaysOnTopHint;
	if (flags & ImGuiViewportFlags_NoFocusOnClick) f |= Qt::WindowDoesNotAcceptFocus;
	if (flags & ImGuiViewportFlags_NoInputs)       f |= Qt::WindowTransparentForInput;
	return f;
}

// Wayland: the edges of a compositor resize for an ImGui resize corner (0 bottom-right, 1 bottom-left, 2 top-left,
// 3 top-right) or border (ImGuiDir); none where ImGui's own resize keeps the top-left corner in place
inline Qt::Edges ImGui_ImplQt_ResizeEdges (int corner, int border)
{
	switch (corner) {
	case 1: return Qt::LeftEdge | Qt::BottomEdge;
	case 2: return Qt::LeftEdge | Qt::TopEdge;
	case 3: return Qt::RightEdge | Qt::TopEdge;
	default: break;
	}
	if (border == ImGuiDir_Left) return Qt::LeftEdge;
	if (border == ImGuiDir_Up) return Qt::TopEdge;
	return Qt::Edges();
}

// Wayland: where a window the compositor carried out of the render window is kept in ImGui's coordinates (relative to
// the render window, of size main): fully outside it, next to the edge nearest p (the pointer's last position in it),
// its grab point in line with p
inline ImVec2 ImGui_ImplQt_ParkedPos (ImVec2 p, ImVec2 main, ImVec2 size, ImVec2 grab)
{
	float l = p.x, r = main.x - p.x, t = p.y, b = main.y - p.y;
	if (l <= r && l <= t && l <= b) return ImVec2 (-size.x - 1.0f, p.y - grab.y);
	if (r <= t && r <= b)           return ImVec2 (main.x + 1.0f, p.y - grab.y);
	if (t <= b)                     return ImVec2 (p.x - grab.x, -size.y - 1.0f);
	return ImVec2 (p.x - grab.x, main.y + 1.0f);
}
