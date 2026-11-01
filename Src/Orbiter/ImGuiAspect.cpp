// not upstream: ImGui windows that keep their shape (ImGuiAspect.h)

#include "OrbiterAPI.h"
#include "ImGuiAspect.h"
#include "imgui_internal.h"
#include "imgui_extras.h"
#include "imgui_impl_qt.h"
#include <cfloat>

ImGuiAspectSide ImGuiAspectSideOf (ImGuiWindow *w)
{
	ImGuiContext &g = *GImGui;
	if (!w) return ImGuiAspectSide::None;
	if (g.ActiveId)
		for (int n = 0; n < 4; n++) {
			if (g.ActiveId == ImGui::GetWindowResizeCornerID (w, n)) return ImGuiAspectSide::Corner;
			if (g.ActiveId == ImGui::GetWindowResizeBorderID (w, (ImGuiDir)n))
				return (n == ImGuiDir_Left || n == ImGuiDir_Right) ? ImGuiAspectSide::Width : ImGuiAspectSide::Height;
		}
	if (w->ViewportOwned && w->Viewport) {
		Qt::Edges e = ImGui_ImplQt_SystemResizeEdges (w->Viewport);
		bool x = e & (Qt::LeftEdge | Qt::RightEdge), y = e & (Qt::TopEdge | Qt::BottomEdge);
		if (x && y) return ImGuiAspectSide::Corner;
		if (x) return ImGuiAspectSide::Width;
		if (y) return ImGuiAspectSide::Height;
	}
	return ImGuiAspectSide::None;
}

namespace {

struct AspectPrm { float aspect, extra, minWidth; };
AspectPrm nextAspect; // the next window's, read in its Begin

// ImGui calls it inside the window's Begin, where its current window is the one being sized
void AspectCallback (ImGuiSizeCallbackData *d)
{
	const AspectPrm &p = *(const AspectPrm *)d->UserData;
	d->DesiredSize = ImGuiAspectSize (d->DesiredSize, ImGuiAspectSideOf (GImGui->CurrentWindow), p.aspect, p.extra, p.minWidth);
}

}

namespace ImGui {
	DLLEXPORT void SetNextWindowAspect (float aspect, float extra, float min_width)
	{
		nextAspect = { aspect, extra, min_width };
		SetNextWindowSizeConstraints (ImVec2 (min_width, 0.0f), ImVec2 (FLT_MAX, FLT_MAX), aspect > 0.0f ? AspectCallback : nullptr, &nextAspect);
	}
}
