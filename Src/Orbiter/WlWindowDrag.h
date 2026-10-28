// not upstream (detach-windows): an ImGui window carried by the Wayland compositor with the pointer (xdg-toplevel-drag-v1)
// until the button is released, as ImGui itself moves it on X11 and Windows: torn off the render window, or moved

#pragma once

class QWindow;

// origin: the window the press was on; serial: that press; dx, dy: the grab point in window. A window already shown is
// attached before the drag starts; a new one is shown here and attached before its first frame.
// false: not offered (not Wayland, or no xdg-toplevel-drag), and the caller shows or moves window itself.
bool WlWindowDragStart (QWindow *origin, unsigned serial, QWindow *window, int dx, int dy);

// once per frame during a drag: over is true while the pointer is over main's window, at x, y in it.
// true once, when the drag is over (button released, or drag refused or cancelled); over then tells where the drop was.
bool WlWindowDragPoll (QWindow *main, bool &over, float &x, float &y);
