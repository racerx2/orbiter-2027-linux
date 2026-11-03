// not upstream (head-track): Config/TrackIR.cfg: upstream's TrackIR plugin keys (CMODE .. TRKPRM, read and written as it did) and the tracker's own

#include "HtSettings.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace ht {

namespace {

const double PI = 3.14159265358979323846, RAD = PI / 180.0;

std::string Trim (const std::string &s)
{
	size_t a = s.find_first_not_of (" \t\r\n"), b = s.find_last_not_of (" \t\r\n");
	return a == std::string::npos ? std::string () : s.substr (a, b - a + 1);
}

std::string Upper (std::string s)
{
	for (char &c : s) c = (char)std::toupper ((unsigned char)c);
	return s;
}

std::string Fmt (const char *f, double a)
{
	char b[64];
	std::snprintf (b, sizeof b, f, a);
	return b;
}

int AxisByKey (const std::string &k) // AXIS_PITCH .. AXIS_Z
{
	for (int i = 0; i < AX_COUNT; i++) if (k == "AXIS_" + Upper (AxisName (i))) return i;
	return -1;
}

} // namespace

const char *PresetName (int p)
{
	static const char *n[MP_COUNT] = {"TrackClip PRO", "TrackClip", "Custom clip", "Custom cap"};
	return p >= 0 && p < MP_COUNT ? n[p] : "";
}

const char *PresetKey (int p)
{
	static const char *n[MP_COUNT] = {"TRACKCLIP_PRO", "TRACKCLIP", "CLIP", "CAP"};
	return p >= 0 && p < MP_COUNT ? n[p] : "";
}

Settings::Settings ()
{
	for (int i = 0; i < AX_COUNT; i++) axes[i] = DefaultAxis (i);
}

Model Settings::MakeModel () const
{
	switch (preset) { // linuxtrack's NaturalPoint models
	case MP_TRACKCLIP: return CapModel (67, 54, 96, 160, 50);
	case MP_CUSTOM_CLIP: return ClipModel (clip[0], clip[1], clip[2], clip[3], clip[4], clip[5], clip[6]);
	case MP_CUSTOM_CAP: return CapModel (cap[0], cap[1], cap[2], cap[3], cap[4]);
	default: return ClipModel (40, 110, 30, 50, -100, -100, 50);
	}
}

bool Settings::IrOn () const
{
	switch (preset) {
	case MP_TRACKCLIP_PRO: return false; // its own LEDs
	case MP_TRACKCLIP: return true;      // reflectors
	default: return !active;
	}
}

bool Settings::Read (const std::string &text)
{
	*this = Settings ();
	std::istringstream in (text);
	std::string line;
	while (std::getline (in, line)) {
		size_t eq = line.find ('=');
		std::string key = eq == std::string::npos ? std::string () : Upper (Trim (line.substr (0, eq)));
		std::string val = eq == std::string::npos ? std::string () : Trim (line.substr (eq + 1));
		std::istringstream v (val);
		int i, j;
		double t, s;
		if (key == "ENABLE" && (v >> i)) enable = i != 0;
		else if (key == "KEYS" && (v >> i)) keysAdded = i != 0;
		else if (key == "MODEL") {
			std::string m = Upper (val);
			for (int p = 0; p < MP_COUNT; p++) if (m == PresetKey (p)) preset = p;
		}
		else if (key == "ACTIVE" && (v >> i)) active = i != 0;
		else if (key == "CLIP") { double c[7]; if (v >> c[0] >> c[1] >> c[2] >> c[3] >> c[4] >> c[5] >> c[6]) std::copy (c, c + 7, clip); }
		else if (key == "CAP") { double c[5]; if (v >> c[0] >> c[1] >> c[2] >> c[3] >> c[4]) std::copy (c, c + 5, cap); }
		else if (key == "ORIENT" && (v >> i)) orientation = i & 15;
		else if (key == "ALIGN" && (v >> i)) align = i != 0;
		else if (key == "THRESHOLD" && (v >> i)) tir.threshold = std::clamp (i, 30, 253);
		else if (key == "IRLED" && (v >> i)) tir.irBrightness = std::clamp (i, 5, 7);
		else if (key == "STATUSLED" && (v >> i >> j)) tir.statusLed = i != 0, tir.statusBrightness = std::clamp (j, 0, 3);
		else if (key == "BLOB" && (v >> i >> j)) tir.minBlob = std::max (1, i), tir.maxBlob = std::max (tir.minBlob, j);
		else if (int a = AxisByKey (key); a >= 0) {
			int en, inv;
			float dz, lc, rc, f, ll, rl, fl;
			if (v >> en >> inv >> dz >> lc >> rc >> f >> ll >> rl >> fl) {
				AxisDef &d = axes[a];
				d.enabled = en != 0, d.inverted = inv != 0;
				d.deadzone = std::clamp (dz, 0.0f, 0.99f);
				d.lcurv = std::clamp (lc, 0.0f, 1.0f), d.rcurv = std::clamp (rc, 0.0f, 1.0f);
				d.factor = std::clamp (f, 0.0f, 100.0f);
				d.llimit = std::clamp (ll, 0.0f, 1000.0f), d.rlimit = std::clamp (rl, 0.0f, 1000.0f);
				d.filter = std::clamp (fl, 0.0f, 10.0f);
			}
		}
		// upstream's TrackIR plugin (ReadData), with its limits
		else if (key == "CMODE" && (v >> i)) cmode = (unsigned)i & 0x1F;
		else if (key == "VCROT" && (v >> i >> t)) vcRot = i != 0, vcRotRange = std::max (1.0 * RAD, std::min (PI, t));
		else if (key == "VCPOS" && (v >> i >> t)) vcPos = i != 0, vcPosRange = std::max (0.0, std::min (2.0, t));
		else if (key == "VCROLL" && (v >> i)) vcRoll = i != 0;
		else if (key == "FREEZE" && (v >> i >> t)) freeze = i != 0, freezeT = std::max (0.0, t);
		else if (key == "TRKROT" && (v >> i >> j)) trkRot = i != 0, trkByPos = j != 0;
		else if (key == "TRKZOOM" && (v >> i)) trkZoom = i != 0;
		else if (key == "TRKPRM" && (v >> t >> s)) trkDead = t, trkSpeed = s;
		else if (!Trim (line).empty ()) other += line + "\n";
	}
	return true;
}

std::string Settings::Write () const
{
	std::ostringstream o;
	o << "ENABLE = " << (enable ? 1 : 0) << "\n";
	o << "KEYS = " << (keysAdded ? 1 : 0) << "\n";
	o << "MODEL = " << PresetKey (preset) << "\n";
	o << "ACTIVE = " << (active ? 1 : 0) << "\n";
	o << "CLIP =";
	for (double c : clip) o << " " << Fmt ("%g", c);
	o << "\nCAP =";
	for (double c : cap) o << " " << Fmt ("%g", c);
	o << "\nORIENT = " << orientation << "\n";
	o << "ALIGN = " << (align ? 1 : 0) << "\n";
	o << "THRESHOLD = " << tir.threshold << "\n";
	o << "IRLED = " << tir.irBrightness << "\n";
	o << "STATUSLED = " << (tir.statusLed ? 1 : 0) << " " << tir.statusBrightness << "\n";
	o << "BLOB = " << tir.minBlob << " " << tir.maxBlob << "\n";
	for (int a = 0; a < AX_COUNT; a++) {
		const AxisDef &d = axes[a];
		o << "AXIS_" << Upper (AxisName (a)) << " = " << (d.enabled ? 1 : 0) << " " << (d.inverted ? 1 : 0)
		  << Fmt (" %g", d.deadzone) << Fmt (" %g", d.lcurv) << Fmt (" %g", d.rcurv) << Fmt (" %g", d.factor)
		  << Fmt (" %g", d.llimit) << Fmt (" %g", d.rlimit) << Fmt (" %g", d.filter) << "\n";
	}
	// upstream's keys, as its clbkWriteConfig wrote them
	o << "CMODE = " << cmode << "\n";
	o << "VCROT = " << (vcRot ? 1 : 0) << Fmt (" %f", vcRotRange) << "\n";
	o << "VCPOS = " << (vcPos ? 1 : 0) << Fmt (" %f", vcPosRange) << "\n";
	o << "VCROLL = " << (vcRoll ? 1 : 0) << "\n";
	o << "FREEZE = " << (freeze ? 1 : 0) << Fmt (" %f", freezeT) << "\n";
	o << "TRKROT = " << (trkRot ? 1 : 0) << " " << (trkByPos ? 1 : 0) << "\n";
	o << "TRKZOOM = " << (trkZoom ? 1 : 0) << "\n";
	o << "TRKPRM = " << Fmt ("%0.2f", trkDead) << Fmt (" %0.2f", trkSpeed) << "\n";
	o << other;
	return o.str ();
}

} // namespace ht
