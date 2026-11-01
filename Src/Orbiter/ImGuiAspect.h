// not upstream: ImGui windows that keep their shape (ImGui::SetNextWindowAspect, imgui_extras.h)

#pragma once
#include "imgui.h"
#include <algorithm>
#include <cmath>

struct ImGuiWindow;

// the side of a window being dragged: a width edge, a height edge, a corner, or none known
enum class ImGuiAspectSide { None, Width, Height, Corner };

// the size in shape for the size wanted: width = (height - extra) * aspect, at least minWidth wide, whole pixels.
// A size already in shape to the pixel is kept; a corner or no known side takes the nearest point of the shape line
inline ImVec2 ImGuiAspectSize (ImVec2 want, ImGuiAspectSide side, float aspect, float extra, float minWidth)
{
	auto height = [&] (float w) { return w / aspect + extra; };
	if (want.x >= minWidth && std::fabs (want.y - height (want.x)) <= 1.0f) return want;
	float w;
	switch (side) {
	case ImGuiAspectSide::Width:  w = want.x; break;
	case ImGuiAspectSide::Height: w = (want.y - extra) * aspect; break;
	default:                      w = aspect * (aspect * want.x + want.y - extra) / (aspect * aspect + 1.0f); break;
	}
	w = std::round (std::max (w, minWidth));
	return ImVec2 (w, std::round (height (w)));
}

// the side of w being dragged: ImGui's active resize grip of it, else the window manager's resize of its own window
ImGuiAspectSide ImGuiAspectSideOf (ImGuiWindow *w);
