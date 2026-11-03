// not upstream (head-track): TrackIR head tracking in Orbiter: the tracker (Src/HeadTrack), Config/TrackIR.cfg, the camera control
// (as upstream's TrackIR plugin registered it), the bindings' HEADTRACK presses and the Controllers pages' section

#ifndef __HEADTRACK_H
#define __HEADTRACK_H

#include "HeadTracker.h"
#include <memory>
#include <string>
#include <vector>

class InputSystem;
class QProcess;

// the settings as both Controllers pages show them: a tab's rows, and the axes' columns
struct HtField {
	enum Kind { CHECK, NUMBER, CHOICE, HEADING };
	const char *tab;           // "Model", "Axes", "Views", "Device"
	Kind kind;
	const char *label, *tip;
	double lo, hi, step;       // NUMBER, in the unit the label names
	int decimals;
	const char *choices;       // CHOICE: '\0' between the items, "\0\0" at the end (ImGui::Combo's form)
	double (*get) (const ht::Settings &);
	void (*set) (ht::Settings &, double);
	bool (*shown) (const ht::Settings &); // nullptr: always
};
const std::vector<HtField> &HtFields ();
struct HtAxisColumn {
	const char *label, *tip;
	bool check;
	double lo, hi, step;
	int decimals;
	double (*get) (const ht::AxisDef &);
	void (*set) (ht::AxisDef &, double);
};
const std::vector<HtAxisColumn> &HtAxisColumns ();

class HeadTrack {
public:
	HeadTrack (const std::string &configDir); // Config, resolved
	~HeadTrack ();

	const ht::Settings &Settings () const { return set; }
	bool SetSettings (const ht::Settings &s, std::string &err); // written to Config/TrackIR.cfg, taken by the tracker and the control
	void Preview (const ht::Settings &s); // taken by the tracker and the control, not written (a page's edit until OK)
	std::string FirmwareDir () const { return cfgDir + "/HeadTrack"; }
	bool FindFirmware (const std::string &folder, std::string &msg); // the user's TrackIR software folder: what it found, or why not

	void Acquire ();          // a Controllers page shows it: the tracker runs while one is shown, or a session with it on
	void Release ();
	void Touch ();            // the in-sim page, each frame it shows: held as Acquire until it stops for half a second (Poll)
	void SessionStart ();     // the camera control registered, the tracker started (head tracking on)
	void SessionEnd ();
	void Action (int iht);    // a binding's IHT_CENTER or IHT_TOGGLE
	void Center () { tracker.Center (); }
	void Toggle ();
	ht::TrackStatus Status () const { return tracker.Status (); }
	bool Poll (InputSystem *inp); // the main thread, each frame and from the pages: the default keys the first time a TrackIR is found (true: Global changed)
	std::string StatusText () const; // one line: the device and what it is doing
	static std::string AccessHint (); // the udev rule that gives the TrackIR to the user
	void GiveAccess ();       // the rule written through pkexec (the desktop asks for the password); the tracker opens it at its next look
	bool GivingAccess () const { return access != nullptr; }
	static std::string KeysText (InputSystem *inp); // Global's HEADTRACK bindings, one line
	std::string FirmwareText () const; // the firmware files in Config/HeadTrack

	ht::HeadTracker tracker;

private:
	class Control;
	void Run ();              // started or stopped as the users and the setting want
	void Register ();
	void Unregister ();       // only if the camera still has it
	bool Load ();

	std::string cfgDir;
	ht::Settings set;
	std::unique_ptr<Control> ctrl;
	bool registered = false, session = false;
	int users = 0;
	bool touchHeld = false;
	double touched = 0;       // steady clock, s
	QProcess *access = nullptr;
	std::string accessMsg;    // how GiveAccess ended
	ht::TrackState logged = ht::HT_STOPPED;
};

#endif // !__HEADTRACK_H
