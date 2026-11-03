// not upstream (head-track): an axis's response curve and filter, ported from linuxtrack's axis.c and spline.c (MIT, LICENSE-linuxtrack)

#ifndef __HTAXIS_H
#define __HTAXIS_H

namespace ht {

enum Axis { AX_PITCH, AX_YAW, AX_ROLL, AX_TX, AX_TY, AX_TZ, AX_COUNT };
const char *AxisName (int axis); // "Pitch" .. "Z", for the settings and the page

struct AxisDef {
	bool enabled = true;
	bool inverted = false;
	float deadzone = 0.0f;       // 0..1 of the travel
	float lcurv = 0.5f, rcurv = 0.5f; // 0..1, 0.5 straight
	float factor = 5.0f;         // sensitivity
	float llimit = 80.0f, rlimit = 80.0f; // degrees or mm
	float filter = 0.2f;         // 0 none
};
AxisDef DefaultAxis (int axis);  // linuxtrack's defaults for the axis

float ValOnAxis (const AxisDef &a, float x);                  // the head's move through the curve
float FilterAxis (const AxisDef &a, int axis, float x, float *y_minus_1); // smoothed, y_minus_1 kept by the caller

} // namespace ht

#endif // !__HTAXIS_H
