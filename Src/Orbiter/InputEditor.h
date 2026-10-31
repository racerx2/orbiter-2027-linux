// not upstream (controls-editor): editing Config/Input from the Controls pages (Launchpad and in the sim)

#ifndef __INPUTEDITOR_H
#define __INPUTEDITOR_H

#include "InputBindings.h"
#include <QObject>
#include <set>
#include <string>
#include <vector>

class Keymap;

// one profile at a time; every change is written at once (the caller then has Config/Input read again)
class InputEditor {
public:
	InputEditor (InputSystem *sys, const std::string &vesselDir); // vesselDir: Config/Vessels
	std::vector<std::string> Classes () const;    // vessel classes: those with a profile first, then Config/Vessels
	bool Open (const std::string &profile);       // "Global" or "Vessels/<class>"; false: unreadable file
	const std::string &Name () const { return name; }
	const InputProfile &Profile () const { return prof; }
	std::vector<Binding> Inherited () const;      // what Global and INHERIT give under this profile, not replaced in it
	bool Put (int index, const Binding &b, std::string &err); // index -1 adds
	bool Remove (int index, std::string &err);
	std::string Conflict (const Binding &b, int index, const Keymap *km) const; // a keymap.cfg key the binding takes; "" if none
	std::string AlsoDoes (const Binding &b, int index) const; // the control's other bindings in this profile; "" if none
	bool Rename (const std::string &from, const std::string &to, std::string &err); // the profiles, then Devices.cfg
	bool WriteCalibration (const std::string &dev, const std::vector<DeviceCal> &cal, std::string &err); // the device's CAL lines
	bool SetZones (double dz, double sat, int &changed, std::string &err); // every controller axis bound to a channel, every profile

	// back to the defaults; each old file is kept next to it as .bak
	bool Reset (std::string &note, std::string &err); // this profile: Global as Orbiter first makes it, a vessel class's own bindings gone
	std::vector<std::string> VesselProfiles () const; // "Vessels/<class>" of each file
	bool ResetVessels (std::string &err);             // every vessel class's own bindings gone
	static bool ResetKeymap (Keymap &km, const std::string &path, std::string &err); // keymap.cfg (path resolved): Orbiter's keys

	static std::string VesselDir (const InputSystem *sys); // Config/Vessels, next to Config/Input
	static const InputDevice *Device (InputSystem *sys, const std::string &name); // connected, by its Devices.cfg name
	static bool Check (Binding &b, std::string &err); // as the file reader takes it
	static std::string InputText (const Binding &b);  // before the "=" of its BIND line
	static std::string OutputText (const Binding &b); // after it

private:
	std::string Path (const std::string &profile) const;
	std::vector<std::string> ProfileFiles () const; // Global and every Vessels/ profile on disk
	bool Write (std::string &err);
	bool Clear (const std::string &profile, std::string &err); // a vessel class's file gone (emptied if another INHERITs it)

	InputSystem *sys;
	std::string vdir, name;
	InputProfile prof;
};

// a device's axes through their whole travel: lowest and highest raw seen, and where each rests at the end
class InputCalibrator {
public:
	struct Axis {
		int code;
		int min, max;       // the device's range
		int lo, hi, rest;   // seen
		bool centring;      // rests near the middle of its travel (a stick, rudder); otherwise a lever or toe brake
	};
	void Start (const InputDevice &d);
	void Update (const InputDevice &d);
	void Finish (const InputDevice &d);  // the rest values now; centring guessed from them
	bool Moved (const Axis &a) const;    // through at least 10 % of its range
	std::vector<DeviceCal> Result () const; // the moved axes; centre: the rest of a centring axis, else the middle of the travel
	std::vector<Axis> axes;
};

// press to bind: keyboard keys, mouse buttons and wheel steps for the capture, before any window sees them.
// Plain left and right clicks pass, so the page's own buttons work.
class InputCaptureFilter : public QObject {
public:
	enum Mode { INPUT, KEY };            // INPUT: InputSystem::CaptureKey/Mouse/Wheel; KEY: the key a KEY output sends
	InputCaptureFilter (InputSystem *sys, Mode mode);
	~InputCaptureFilter ();
	bool KeyGot (int &dik, uint16_t &mods) const; // KEY mode
	bool eventFilter (QObject *o, QEvent *e) override;

private:
	InputSystem *sys;
	Mode mode;
	bool got = false;
	int dik = 0;
	uint16_t kmods = 0;
	std::set<int> keysTaken, buttonsTaken; // their releases are taken too
};

#endif // !__INPUTEDITOR_H
