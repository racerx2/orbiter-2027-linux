// not upstream (head-track): TrackIR head tracking in Orbiter (HeadTrack.h); the camera control is upstream's TrackIR plugin's, fed by the tracker

#include "HeadTrack.h"
#include "CamAPI.h"
#include "Camera.h"
#include "HtFirmware.h"
#include "InputBindings.h"
#include "InputEditor.h"
#include "Keymap.h"
#include "Log.h"
#include "Orbiter.h"
#include <QProcess>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

extern Orbiter *g_pOrbiter;
extern Camera *g_camera;

// upstream's TrackIR control: the pose the tracker made, in the camera's terms
class HeadTrack::Control : public ExternalCameraControl {
public:
	explicit Control (HeadTrack *o) : ExternalCameraControl (CAMDATA_POS | CAMDATA_DIR, CAMMODE_VC), owner (o) {}
	void Apply (const ht::Settings &s)
	{
		SetCameraMode (s.cmode);
		SetDataMode (CAMDATA_POS | CAMDATA_YAW | CAMDATA_PITCH | (s.vcRoll ? CAMDATA_ROLL : 0));
		vcmode.trackrotation = s.vcRot;
		vcmode.trackposition = s.vcPos;
		vcmode.rotationrange = s.vcRotRange;
		vcmode.positionrange = s.vcPosRange;
		vcmode.freezeonmouse = s.freeze;
		vcmode.freeze_t = s.freezeT;
		trkmode.trackrotation = s.trkRot;
		trkmode.rotationdata = s.trkByPos ? TrackMode::BYPOSITION : TrackMode::BYROTATION;
		trkmode.trackzoom = s.trkZoom;
		trkmode.deadzone = s.trkDead;
		trkmode.speed = s.trkSpeed;
	}
	bool clbkPoll (CamData *data) override
	{
		if (!owner->tracker.On ()) {
			if (!centreOnce) return false;
			centreOnce = false; // switched off: the view back to its default once
			*data = CamData ();
			return true;
		}
		ht::RawPose p;
		uint64_t seq;
		if (!owner->tracker.Pose (p, seq)) return false;
		auto now = std::chrono::steady_clock::now ();
		if (seq != last) last = seq, fresh = now;
		else if (now - fresh > std::chrono::milliseconds (100)) return false; // lost: the view stays, the track view stops turning
		data->yaw = p.yaw * RAD;      // both: positive left
		data->pitch = p.pitch * RAD;  // up
		data->roll = p.roll * RAD;    // the head tilted left
		data->x = p.tx * 1e-3;        // m: right, up, forward (the tracker's z is towards the user)
		data->y = p.ty * 1e-3;
		data->z = -p.tz * 1e-3;
		return true;
	}
	bool centreOnce = false;
private:
	HeadTrack *owner;
	uint64_t last = 0;
	std::chrono::steady_clock::time_point fresh;
};

HeadTrack::HeadTrack (const std::string &configDir) : cfgDir (configDir)
{
	ctrl = std::make_unique<Control> (this);
	Load ();
	ctrl->Apply (set);
}

HeadTrack::~HeadTrack ()
{
	if (access) access->disconnect (), access->kill (), access->waitForFinished (1000), delete access;
	Unregister ();
	tracker.Stop ();
}

void HeadTrack::Register ()
{
	if (registered) return;
	registered = oapiRegisterExternalCameraControl (ctrl.get ());
	if (!registered) LOGOUT ("Head tracking: another module has the camera's external control; the TrackIR doesn't move the view");
}

void HeadTrack::Unregister ()
{
	if (registered && g_camera && g_camera->ExternalControl () == ctrl.get ()) oapiUnregisterExternalCameraControl (); // a module may have taken it since
	registered = false;
}

bool HeadTrack::Load ()
{
	std::ifstream f (cfgDir + "/TrackIR.cfg");
	if (!f) return false; // the defaults
	std::stringstream ss;
	ss << f.rdbuf ();
	return set.Read (ss.str ());
}

void HeadTrack::Preview (const ht::Settings &s)
{
	ht::Settings p = s;
	p.enable = set.enable, p.keysAdded = set.keysAdded; // these two only as SetSettings writes them
	set = p;
	ctrl->Apply (set);
	if (tracker.Running ()) tracker.Update (set);
}

bool HeadTrack::SetSettings (const ht::Settings &s, std::string &err)
{
	bool wasOn = set.enable;
	set = s;
	ctrl->Apply (set);
	if (tracker.Running ()) tracker.Update (set);
	std::string path = cfgDir + "/TrackIR.cfg", tmp = path + ".part";
	{
		std::ofstream o (tmp, std::ios::trunc);
		o << set.Write ();
		if (!o.flush ()) { err = "can't write " + path; return false; }
	}
	if (std::rename (tmp.c_str (), path.c_str ())) { err = "can't write " + path; return false; }
	if (wasOn != set.enable) {
		if (session && set.enable) Register ();
		if (!set.enable) Unregister ();
		Run ();
	}
	return true;
}

bool HeadTrack::FindFirmware (const std::string &folder, std::string &msg)
{
	std::string err;
	auto found = ht::FindFirmware (folder, FirmwareDir (), err);
	if (found.empty ()) {
		msg = err.empty () ? "No TrackIR firmware in " + folder + ". Pick the TrackIR software's folder (from a Windows install), or a linuxtrack tir_firmware folder." : err;
		return false;
	}
	msg = "Found:";
	for (auto &f : found) msg += " " + f.name + " (in " + f.from + ")";
	LOGOUT ("Head tracking: firmware %s", msg.c_str ());
	if (tracker.Running ()) tracker.Update (set); // a device waiting for its firmware takes it at its next look
	return true;
}

void HeadTrack::Run ()
{
	bool want = users > 0 || (session && set.enable);
	if (want && !tracker.Running ()) tracker.Start (set, FirmwareDir ());
	else if (!want && tracker.Running ()) tracker.Stop ();
}

void HeadTrack::Acquire ()
{
	users++;
	Run ();
}

void HeadTrack::Release ()
{
	if (users > 0) users--;
	Run ();
}

static double Now ()
{
	return std::chrono::duration<double> (std::chrono::steady_clock::now ().time_since_epoch ()).count ();
}

void HeadTrack::Touch ()
{
	touched = Now ();
	if (!touchHeld) touchHeld = true, Acquire ();
}

void HeadTrack::SessionStart ()
{
	session = true;
	tracker.SetOn (true);
	ctrl->centreOnce = false;
	if (set.enable) Register ();
	Run ();
}

void HeadTrack::SessionEnd ()
{
	session = false;
	Unregister ();
	Run ();
}

void HeadTrack::Toggle ()
{
	bool on = !tracker.On ();
	tracker.SetOn (on);
	ctrl->centreOnce = !on; // the centre kept: back on, the view follows the head again
}

void HeadTrack::Action (int iht)
{
	if (iht == IHT_TOGGLE) Toggle ();
	else Center ();
}

bool HeadTrack::Poll (InputSystem *inp)
{
	if (touchHeld && Now () - touched > 0.5) touchHeld = false, Release (); // the in-sim page closed
	ht::TrackStatus st = tracker.Status ();
	if (st.state != logged) { // a line in Orbiter.log as it changes
		logged = st.state;
		if (st.state != ht::HT_STOPPED)
			LOGOUT ("Head tracking: %s%s%s", ht::StateName (st.state), st.model != ht::TIR_NONE ? (std::string (", ") + ht::TirModelName (st.model)).c_str () : "",
				st.message.empty () ? "" : (": " + st.message).c_str ());
	}
	if (set.keysAdded || st.model == ht::TIR_NONE || !inp) return false;
	// the first TrackIR found: F12 centres, CTRL F12 turns it on and off, where Global and keymap.cfg leave those keys free
	InputEditor ed (inp, InputEditor::VesselDir (inp));
	bool open = ed.Open ("Global");
	if (!open) LOGOUT ("Head tracking: Config/Input/Global.cfg can't be read; no keys added");
	std::string added;
	for (int i = 0; i < 2 && open; i++) {
		Binding b;
		b.in.src = INSRC_KEY, b.in.kind = INK_KEY;
		b.in.code = InputEvKeyByName ("F12");
		b.in.mods = i ? KMOD_CTRL : 0;
		b.out = IOUT_HEADTRACK, b.ht = i ? IHT_TOGGLE : IHT_CENTER;
		bool used = false;
		for (const Binding &o : ed.Profile ().binds) if (InputSameInput (b, o)) used = true;
		if (!used && ed.Conflict (b, -1, &g_pOrbiter->keymap).empty ()) {
			std::string err;
			if (ed.Put (-1, b, err)) added += i ? ", CTRL F12 turns it on and off" : ", F12 centres the view";
			else LOGOUT ("Head tracking: %s", err.c_str ());
		}
	}
	ht::Settings s = set;
	s.keysAdded = true;
	std::string err;
	if (!SetSettings (s, err)) LOGOUT ("Head tracking: %s", err.c_str ());
	if (added.empty ()) return false;
	LOGOUT ("Head tracking: keys bound in Global%s", added.c_str ());
	g_pOrbiter->OnOptionChanged (OPTCAT_JOYSTICK, OPTITEM_JOYSTICK_DEVICE); // Config/Input read again
	return true;
}

std::string HeadTrack::StatusText () const
{
	ht::TrackStatus st = tracker.Status ();
	std::string m = st.model != ht::TIR_NONE ? ht::TirModelName (st.model) : "TrackIR";
	switch (st.state) {
	case ht::HT_STOPPED: return "Not running";
	case ht::HT_SEARCHING: return "No TrackIR plugged in";
	case ht::HT_NO_ACCESS:
		if (access) return m + ": waiting for the password";
		return m + ": Linux doesn't let Orbiter use it yet. Give access fixes that (it asks for your password)." + (accessMsg.empty () ? "" : " " + accessMsg);
	case ht::HT_BUSY: return m + ": another program is using it. Close that program.";
	case ht::HT_NO_FIRMWARE: return m + ": it needs NaturalPoint's firmware. Head tracking..., Device, Find firmware.";
	case ht::HT_STARTING: return m + ": starting";
	case ht::HT_TRACKING: {
		char b[64];
		snprintf (b, sizeof b, ": tracking, %d point%s", (int)st.blobs.size (), st.blobs.size () == 1 ? "" : "s");
		return m + b + (st.havePose ? "" : " (it needs 3)") + (st.on ? "" : ", switched off");
	}
	default: return m + ": failed" + (st.message.empty () ? "" : " (" + st.message + ")");
	}
}

void HeadTrack::GiveAccess ()
{
	if (access) return;
	std::error_code ec;
	bool logind = std::filesystem::exists ("/run/systemd/seats", ec); // uaccess needs logind; without it the input group
	std::string rule = std::string ("SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"131d\", ") + (logind ? "TAG+=\"uaccess\"" : "MODE=\"0660\", GROUP=\"input\"");
	std::string sh = "printf '%s\\n' '" + rule + "' > /etc/udev/rules.d/71-orbiter-trackir.rules && udevadm control --reload && "
		"udevadm trigger --action=change --subsystem-match=usb --attr-match=idVendor=131d";
	accessMsg.clear ();
	access = new QProcess;
	QObject::connect (access, &QProcess::finished, access, [this](int code, QProcess::ExitStatus how) {
		if (how != QProcess::NormalExit) accessMsg = "(The password prompt ended.)";
		else if (code == 0) accessMsg = "(Access given: it opens in a moment. If not, plug it in again.)";
		else if (code == 126 || code == 127) accessMsg = "(Not given: the password was cancelled or refused.)";
		else accessMsg = "(Not given: " + access->readAllStandardError ().trimmed ().toStdString () + ")";
		LOGOUT ("Head tracking: give access: %s", accessMsg.c_str ());
		access->deleteLater ();
		access = nullptr;
	});
	QObject::connect (access, &QProcess::errorOccurred, access, [this](QProcess::ProcessError e) {
		if (e != QProcess::FailedToStart) return;
		accessMsg = "(pkexec isn't installed. As root: " + AccessHint () + ")";
		access->deleteLater ();
		access = nullptr;
	});
	access->start ("pkexec", {"/bin/sh", "-c", QString::fromStdString (sh)});
}

static std::string PlainInput (const Binding &b) // "Key.F12 CTRL" as "Ctrl+F12"
{
	std::string t = InputEditor::InputText (b);
	if (b.in.src != INSRC_KEY || t.compare (0, 4, "Key.")) return t;
	std::istringstream in (t.substr (4));
	std::string key, mod, s;
	in >> key;
	while (in >> mod) {
		for (size_t i = 1; i < mod.size (); i++) mod[i] = (char)tolower ((unsigned char)mod[i]);
		s += mod + "+";
	}
	return s + key;
}

std::string HeadTrack::KeysText (InputSystem *inp)
{
	InputProfile g;
	std::string s;
	if (inp && InputReadProfile (inp->Dir () + "/Global.cfg", g))
		for (const Binding &b : g.binds) {
			if (b.out != IOUT_HEADTRACK) continue;
			s += (s.empty () ? "" : ", ") + PlainInput (b) + (b.ht == IHT_TOGGLE ? " turns it on and off" : " centres the view");
		}
	return s.empty () ? "none (Bindings, Add..., Head tracking)" : s;
}

std::string HeadTrack::FirmwareText () const
{
	std::string s;
	for (ht::TirModel m : {ht::TIR_4, ht::TIR_5, ht::TIR_5V2}) {
		const char *f = ht::TirFirmwareFile (m);
		std::error_code ec;
		bool have = f && std::filesystem::exists (FirmwareDir () + "/" + f, ec);
		s += std::string (s.empty () ? "" : ", ") + ht::TirModelName (m) + (have ? ": found" : ": not found");
	}
	return s + ". The TrackIR 5 revision 3 needs none.";
}

std::string HeadTrack::AccessHint ()
{
	return "A udev rule gives it to you: SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"131d\", TAG+=\"uaccess\" in "
		"/etc/udev/rules.d/71-orbiter-trackir.rules (GROUP=\"input\" where there is no logind), then: "
		"sudo udevadm control --reload && sudo udevadm trigger, and plug the TrackIR in again";
}

// ====================================================================================
// the pages' rows

namespace {

bool IsClip (const ht::Settings &s) { return s.preset == ht::MP_CUSTOM_CLIP; }
bool IsCap (const ht::Settings &s) { return s.preset == ht::MP_CUSTOM_CAP; }
bool IsCustom (const ht::Settings &s) { return IsClip (s) || IsCap (s); }
const int turns[4] = {0, ht::ORIENT_FLIP_X | ht::ORIENT_FLIP_Y, ht::ORIENT_XCHG_XY | ht::ORIENT_FLIP_X, ht::ORIENT_XCHG_XY | ht::ORIENT_FLIP_Y};

#define F_CHECK(tab, label, tip, field) {tab, HtField::CHECK, label, tip, 0, 1, 1, 0, nullptr, \
	[](const ht::Settings &s) -> double { return s.field ? 1 : 0; }, [](ht::Settings &s, double v) { s.field = v != 0; }, nullptr}
#define F_BIT(tab, label, tip, bit) {tab, HtField::CHECK, label, tip, 0, 1, 1, 0, nullptr, \
	[](const ht::Settings &s) -> double { return (s.cmode & bit) ? 1 : 0; }, [](ht::Settings &s, double v) { s.cmode = v ? s.cmode | bit : s.cmode & ~bit; }, nullptr}
#define F_NUM(tab, label, tip, lo, hi, step, dec, field, shown) {tab, HtField::NUMBER, label, tip, lo, hi, step, dec, nullptr, \
	[](const ht::Settings &s) -> double { return s.field; }, [](ht::Settings &s, double v) { s.field = v; }, shown}
#define F_HEAD(tab, label) {tab, HtField::HEADING, label, "", 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr}

} // namespace

const std::vector<HtField> &HtFields ()
{
	static const std::vector<HtField> f = {
		{"Model", HtField::CHOICE, "Model", "What you wear: the TrackClip PRO (its own LEDs, on the side of a headset), the TrackClip (reflectors, on a cap's peak), "
			"or three points of your own", 0, 0, 0, 0, "TrackClip PRO\0TrackClip\0Custom clip\0Custom cap\0\0",
			[](const ht::Settings &s) -> double { return s.preset; }, [](ht::Settings &s, double v) { s.preset = (int)v; }, nullptr},
		{"Model", HtField::CHECK, "Its LEDs light themselves (the TrackIR's lamps off)", "On for LEDs, off for reflectors", 0, 1, 1, 0, nullptr,
			[](const ht::Settings &s) -> double { return s.active ? 1 : 0; }, [](ht::Settings &s, double v) { s.active = v != 0; }, IsCustom},
		F_NUM ("Model", "Y1 (mm)", "The clip's dimensions as linuxtrack's model page measures them", 0, 500, 1, 0, clip[0], IsClip),
		F_NUM ("Model", "Y2 (mm)", "The clip's dimensions as linuxtrack's model page measures them", 0, 500, 1, 0, clip[1], IsClip),
		F_NUM ("Model", "Z1 (mm)", "The clip's dimensions as linuxtrack's model page measures them", -500, 500, 1, 0, clip[2], IsClip),
		F_NUM ("Model", "Z2 (mm)", "The clip's dimensions as linuxtrack's model page measures them", -500, 500, 1, 0, clip[3], IsClip),
		F_NUM ("Model", "Head centre X (mm)", "From the top point to the middle of your head: + is right, as the TrackIR sees you", -500, 500, 1, 0, clip[4], IsClip),
		F_NUM ("Model", "Head centre Y (mm)", "From the top point to the middle of your head: + is up", -500, 500, 1, 0, clip[5], IsClip),
		F_NUM ("Model", "Head centre Z (mm)", "From the top point to the middle of your head: + is away from the TrackIR", -500, 500, 1, 0, clip[6], IsClip),
		F_NUM ("Model", "Width of the two side points (mm)", "", 1, 500, 1, 0, cap[0], IsCap),
		F_NUM ("Model", "Height of the top point (mm)", "Above the line between the side points", 0, 500, 1, 0, cap[1], IsCap),
		F_NUM ("Model", "Side points in front of the top one (mm)", "", -500, 500, 1, 0, cap[2], IsCap),
		F_NUM ("Model", "Head centre below the top point (mm)", "", -500, 500, 1, 0, cap[3], IsCap),
		F_NUM ("Model", "Head centre behind the top point (mm)", "", -500, 500, 1, 0, cap[4], IsCap),
		{"Model", HtField::CHOICE, "The TrackIR", "How it sits on the monitor. On its side: try both ways; the right one turns the view with your head",
			0, 0, 0, 0, "Upright\0Upside down\0On its side, one way\0On its side, the other way\0\0",
			[](const ht::Settings &s) -> double { for (int i = 0; i < 4; i++) if ((s.orientation & 7) == turns[i]) return i; return 0; },
			[](ht::Settings &s, double v) { s.orientation = (s.orientation & ht::ORIENT_FROM_BEHIND) | turns[(int)v & 3]; }, nullptr},
		{"Model", HtField::CHECK, "It looks at the back of the head", "The TrackIR behind you", 0, 1, 1, 0, nullptr,
			[](const ht::Settings &s) -> double { return (s.orientation & ht::ORIENT_FROM_BEHIND) ? 1 : 0; },
			[](ht::Settings &s, double v) { s.orientation = v ? s.orientation | ht::ORIENT_FROM_BEHIND : s.orientation & ~ht::ORIENT_FROM_BEHIND; }, nullptr},

		F_HEAD ("Views", "The views it moves"),
		F_BIT ("Views", "Virtual cockpit", "", 0x01),
		F_BIT ("Views", "2D panels", "", 0x02),
		F_BIT ("Views", "Generic cockpit", "", 0x04),
		F_BIT ("Views", "Track view: it orbits and zooms", "Outside: the head's turns or moves orbit the vessel, leaning zooms", 0x08),
		F_BIT ("Views", "Ground observer: it looks around", "", 0x10),
		F_HEAD ("Views", "Virtual cockpit"),
		F_CHECK ("Views", "Turn the view", "", vcRot),
		{"Views", HtField::NUMBER, "Turn range (degrees)", "180: the view turns as the axes give it; less turns it less", 1, 180, 5, 0, nullptr,
			[](const ht::Settings &s) -> double { return s.vcRotRange * DEG; }, [](ht::Settings &s, double v) { s.vcRotRange = v / DEG; }, nullptr},
		F_CHECK ("Views", "Move the eye", "Lean and move: the eye moves in the cockpit", vcPos),
		F_NUM ("Views", "Move range (m)", "As far as the eye goes from its place", 0, 2, 0.05, 2, vcPosRange, nullptr),
		F_CHECK ("Views", "Tilt the view", "The head's roll", vcRoll),
		F_CHECK ("Views", "The mouse pauses it", "After a mouse look the tracker waits, then the view comes back to the head", freeze),
		F_NUM ("Views", "Pause (s)", "", 0, 10, 0.5, 1, freezeT, nullptr),
		F_HEAD ("Views", "Track view"),
		F_CHECK ("Views", "Orbit", "", trkRot),
		{"Views", HtField::CHOICE, "Orbit by", "", 0, 0, 0, 0, "Turning the head\0Moving the head\0\0",
			[](const ht::Settings &s) -> double { return s.trkByPos ? 1 : 0; }, [](ht::Settings &s, double v) { s.trkByPos = v != 0; }, nullptr},
		F_CHECK ("Views", "Zoom by leaning", "", trkZoom),
		F_NUM ("Views", "Dead zone", "How far the head goes before the view moves", 0, 1, 0.05, 2, trkDead, nullptr),
		F_NUM ("Views", "Speed", "", 0.1, 10, 0.1, 1, trkSpeed, nullptr),

		{"Device", HtField::NUMBER, "Threshold", "How bright a pixel must be to count: raise it if lamps or reflections show as points", 30, 253, 1, 0, nullptr,
			[](const ht::Settings &s) -> double { return s.tir.threshold; }, [](ht::Settings &s, double v) { s.tir.threshold = (int)v; }, nullptr},
		{"Device", HtField::NUMBER, "Lamp brightness", "The TrackIR's infrared lamps, for reflectors", 5, 7, 1, 0, nullptr,
			[](const ht::Settings &s) -> double { return s.tir.irBrightness; }, [](ht::Settings &s, double v) { s.tir.irBrightness = (int)v; }, nullptr},
		{"Device", HtField::CHECK, "Status light", "The green light while it tracks", 0, 1, 1, 0, nullptr,
			[](const ht::Settings &s) -> double { return s.tir.statusLed ? 1 : 0; }, [](ht::Settings &s, double v) { s.tir.statusLed = v != 0; }, nullptr},
		{"Device", HtField::NUMBER, "Status light brightness", "", 0, 3, 1, 0, nullptr,
			[](const ht::Settings &s) -> double { return s.tir.statusBrightness; }, [](ht::Settings &s, double v) { s.tir.statusBrightness = (int)v; }, nullptr},
		{"Device", HtField::NUMBER, "Smallest point (pixels)", "Smaller ones are ignored", 1, 1000, 1, 0, nullptr,
			[](const ht::Settings &s) -> double { return s.tir.minBlob; }, [](ht::Settings &s, double v) { s.tir.minBlob = (int)v; s.tir.maxBlob = std::max (s.tir.maxBlob, s.tir.minBlob); }, nullptr},
		{"Device", HtField::NUMBER, "Largest point (pixels)", "Bigger ones (a lamp, a window) are ignored", 1, 20000, 10, 0, nullptr,
			[](const ht::Settings &s) -> double { return s.tir.maxBlob; }, [](ht::Settings &s, double v) { s.tir.maxBlob = (int)v; s.tir.minBlob = std::min (s.tir.minBlob, s.tir.maxBlob); }, nullptr},
	};
	return f;
}

#define A_COL(label, tip, check, lo, hi, step, dec, field) {label, tip, check, lo, hi, step, dec, \
	[](const ht::AxisDef &a) -> double { return a.field; }, [](ht::AxisDef &a, double v) { a.field = (decltype (a.field))v; }}

const std::vector<HtAxisColumn> &HtAxisColumns ()
{
	static const std::vector<HtAxisColumn> c = {
		A_COL ("On", "", true, 0, 1, 1, 0, enabled),
		A_COL ("Invert", "", true, 0, 1, 1, 0, inverted),
		A_COL ("Speed", "How far the view goes for the head's move", false, 0, 100, 0.1, 1, factor),
		A_COL ("Dead zone", "The part of the travel round the centre that does nothing", false, 0, 0.99, 0.01, 2, deadzone),
		A_COL ("Curve -", "0.5 straight; less gentle near the centre, more sharp (left, up, back)", false, 0, 1, 0.05, 2, lcurv),
		A_COL ("Curve +", "0.5 straight; less gentle near the centre, more sharp (right, down, forward)", false, 0, 1, 0.05, 2, rcurv),
		A_COL ("Limit -", "The furthest the view goes (degrees, mm)", false, 0, 1000, 5, 0, llimit),
		A_COL ("Limit +", "The furthest the view goes (degrees, mm)", false, 0, 1000, 5, 0, rlimit),
		A_COL ("Smoothing", "0 none; more is steadier and slower", false, 0, 10, 0.05, 2, filter),
	};
	return c;
}
