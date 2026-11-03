// not upstream (head-track): the head tracker's settings, as Config/TrackIR.cfg keeps them (upstream's camera keys and the tracker's own)

#ifndef __HTSETTINGS_H
#define __HTSETTINGS_H

#include "HtAxis.h"
#include "HtPose.h"
#include "HtTirDevice.h"
#include <string>

namespace ht {

enum ModelPreset { MP_TRACKCLIP_PRO, MP_TRACKCLIP, MP_CUSTOM_CLIP, MP_CUSTOM_CAP, MP_COUNT };
const char *PresetName (int p);      // "TrackClip PRO"
const char *PresetKey (int p);       // "TRACKCLIP_PRO"

struct Settings {
	bool enable = true;
	bool keysAdded = false;            // F12 and CTRL F12 offered once
	int preset = MP_TRACKCLIP_PRO;
	bool active = true;                // custom models: LEDs of their own (the illuminators off)
	double clip[7] = {40, 110, 30, 50, -100, -100, 50}; // Y1 Y2 Z1 Z2, head X Y Z
	double cap[5] = {67, 54, 96, 160, 50};              // X Y Z, head Y Z
	int orientation = 0;               // ORIENT_* bits
	bool align = false;                // moves in the head's directions; Orbiter's camera turns them with the view itself
	TirParams tir;
	AxisDef axes[AX_COUNT];
	// the camera (upstream's keys): CAMMODE_* bits, VC rotation range [rad], position range [m], freeze, track view
	unsigned cmode = 0x01 | 0x04;      // VC and generic cockpit
	bool vcRot = true, vcPos = true, vcRoll = true, freeze = false;
	double vcRotRange = 3.14159265358979323846, vcPosRange = 0.5, freezeT = 2.0;
	bool trkRot = true, trkByPos = false, trkZoom = true;
	double trkDead = 0.2, trkSpeed = 2.0;

	Settings ();
	Model MakeModel () const;          // the preset's, or the custom dimensions
	bool IrOn () const;                // the illuminators: off for active LEDs
	bool Read (const std::string &text);   // a file's lines (KEY = value); unknown keys kept as they were
	std::string Write () const;
private:
	std::string other;                 // lines it doesn't know, written back
};

} // namespace ht

#endif // !__HTSETTINGS_H
