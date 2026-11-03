// not upstream (head-track): the head's pose from three points, ported from linuxtrack's pose.c and tracking.c (MIT, LICENSE-linuxtrack)

#ifndef __HTPOSE_H
#define __HTPOSE_H

#include "HtAxis.h"
#include "HtBlobs.h"
#include <vector>

namespace ht {

// mm, camera frame: +x right, +y up (facing the camera), +z out of the camera; p0 the top point, p1 the left one
struct Model {
	enum Type { CAP, CLIP } type = CLIP;
	double p0[3] = {0, 0, 0}, p1[3] = {0, 0, 0}, p2[3] = {0, 0, 0};
	double hc[3] = {0, 0, 0}; // the head's centre, from p0
};
Model CapModel (double x, double y, double z, double hy, double hz);                       // a cap or a clip on its peak
Model ClipModel (double y1, double y2, double z1, double z2, double hx, double hy, double hz); // a clip on the side of the head

struct RawPose { double pitch = 0, yaw = 0, roll = 0, tx = 0, ty = 0, tz = 0; }; // degrees, mm, from the centre

// camera turn (linuxtrack's Camera-orientation bits)
enum { ORIENT_FLIP_X = 1, ORIENT_FLIP_Y = 2, ORIENT_XCHG_XY = 4, ORIENT_FROM_BEHIND = 8 };

class PoseSolver {
public:
	bool Init (const Model &m, double focal = 660.0); // false: the dimensions make no base
	void RemoveCameraRotation (std::vector<Blob> &blobs, int orientation) const;
	void SortBlobs (std::vector<Blob> &blobs) const;  // the model's order: top first (and left second for a cap)
	bool Process (const std::vector<Blob> &blobs, bool centering, RawPose &pose); // three sorted blobs; centering: this is the centre

private:
	void IterPose (const std::vector<Blob> &blobs, double points[3][3]);
	bool GetCbase (const double p0[3], const double p1[3], const double p2[3], const double c[3], double cbase[3]);
	bool Center (const double rp0[3], const double rp1[3], const double rp2[3], const double cntr_base[3], double center[3], double tr[3][3]);
	bool GetPose (const double rp0[3], const double rp1[3], const double rp2[3], const double cntr_base[3],
		const double center[3], const double tr[3][3], double angles[3], double trans[3]);

	Model::Type type = Model::CLIP;
	double focal = 660.0;
	double mp0[3] = {}, mp1[3] = {}, mp2[3] = {}; // the model's points from the head's centre
	double c_base[3] = {}, tr_center[3] = {}, tr_rot[3][3] = {};
};

// the axes' curves, filters and the translations turned with the head: what the view gets (degrees, mm)
class PosePost {
public:
	void Reset ();
	bool Apply (const AxisDef axes[AX_COUNT], const RawPose &raw, RawPose &out, bool align = true);
private:
	float fa[3] = {0, 0, 0}, ft[3] = {0, 0, 0};
};

} // namespace ht

#endif // !__HTPOSE_H
