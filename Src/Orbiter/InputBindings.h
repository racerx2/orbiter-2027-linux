// not upstream: keys, buttons, axes and switches bound to Orbiter's functions, keys and controls (input-bindings)

#ifndef __INPUTBINDINGS_H
#define __INPUTBINDINGS_H

#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "InputDevices.h"
#include "Keymap.h"

// ====================================================================================
// channels: analog controls

enum InputChan {
	ICH_PITCH, ICH_YAW, ICH_BANK, ICH_TRANSX, ICH_TRANSY, ICH_TRANSZ,
	ICH_BRAKEL, ICH_BRAKER, ICH_BRAKES,
	ICH_LOOKX, ICH_LOOKY, ICH_PANX, ICH_PANY, ICH_FOV, ICH_DIST,
	ICH_NCONT,                                     // continuous above, absolute below
	ICH_MAIN = ICH_NCONT, ICH_MAINRETRO, ICH_RETRO, ICH_HOVER,
	ICH_MAIN1, ICH_RETRO1 = ICH_MAIN1+8, ICH_HOVER1 = ICH_RETRO1+8,
	ICH_ELEVTRIM = ICH_HOVER1+8, ICH_RUDDERTRIM, ICH_FLAPS,
	ICH_COUNT
};

struct InputChanInfo {
	std::string name;
	bool absolute;  // set when the input moves, not every frame
	bool unipolar;  // 0..1
};
const InputChanInfo &InputChannel (int ch);
int InputChannelByName (const std::string &name);

// ====================================================================================
// names

std::string InputKeyName (int dik);                   // OAPI_KEY_ name without the prefix (keymap.cfg's PGUP/PGDOWN)
int InputKeyByName (const std::string &name);         // DIK code, 0 if none
std::string InputEvKeyName (int evcode);              // a keyboard key by evdev code
int InputEvKeyByName (const std::string &name);       // evdev code, 0 if none
std::string InputAbsName (int code);
int InputAbsByName (const std::string &name);
std::string InputModsName (uint16_t mods);            // " CTRL SHIFT" style, leading space
bool InputModsMatch (uint16_t want, uint16_t have);   // keymap.cfg's rule: CTRL either side, LCTRL left only, none means up

// ====================================================================================
// bindings

enum InputSrc : uint8_t { INSRC_DEV, INSRC_KEY, INSRC_MOUSE };
enum InputKind : uint8_t { INK_BUTTON, INK_AXIS, INK_HAT, INK_REL, INK_KEY, INK_MBUTTON, INK_WHEEL };

struct InputRef {
	InputSrc src = INSRC_DEV;
	InputKind kind = INK_BUTTON;
	std::string dev;          // INSRC_DEV: the device's name in Devices.cfg
	int code = 0;             // button index, ABS code, hat 0..3, REL code, evdev key, mouse button 1.., wheel 0 up 1 down 2 left 3 right
	int dir = 0;              // hat: 0 up 1 right 2 down 3 left; REL: +1 or -1
	uint16_t mods = 0;        // keyboard and mouse: KMOD_*
	bool zone = false;        // axis range as a button
	double zlo = 0, zhi = 0;
};

enum InputOut : uint8_t { IOUT_NONE, IOUT_FUNC, IOUT_KEY, IOUT_AXIS, IOUT_MFD, IOUT_LAYER };
enum InputMode : uint8_t { IMODE_AUTO, IMODE_HOLD, IMODE_PRESS, IMODE_RELEASE, IMODE_BOTH, IMODE_TOGGLE, IMODE_REPEAT };
enum InputAxMode : uint8_t { IAX_VALUE, IAX_STEP, IAX_SET };
enum { IMFD_PWR = -1, IMFD_SEL = -2, IMFD_MNU = -3 };

struct Binding {
	InputRef in;
	int layer = 0;            // 0 base, 1..8
	int view = 0;             // 0 any, 1 cockpit, 2 outside
	InputMode mode = IMODE_AUTO;
	double rate = 10.0;       // REPEAT: presses per second
	bool pass = false;        // a keyboard or mouse input also acts as itself
	InputOut out = IOUT_NONE;
	int func = -1;            // OAPI_LKEY_*
	int key = 0;              // DIK
	uint16_t kmods = 0;
	int chan = -1;
	InputAxMode amode = IAX_VALUE;
	double aval = 1.0;
	int mfd = 0, mfdbtn = 0;  // mfdbtn: 0.. or IMFD_*
	int lnum = 0;             // LAYER
	bool latch = false;
	bool invert = false, half = false, pickup = false;
	double dz = 0.0, sat = 1.0, curve = 1.0, scale = 1.0;
	int line = 0;
};

bool InputParseBinding (const std::string &text, Binding &b, std::string &err); // the part after "BIND"
std::string InputFormatBinding (const Binding &b);                              // the part after "BIND"
bool InputSameInput (const Binding &a, const Binding &b);                       // same input, layer and view

struct InputProfile {
	std::string name;         // "Global", "Vessels/DeltaGlider"
	std::string inherit;
	std::vector<Binding> binds;
	std::vector<std::string> errors;
};
bool InputReadProfile (const std::string &path, InputProfile &p);
bool InputWriteProfile (const std::string &path, const InputProfile &p, const std::string &header);
std::vector<Binding> InputMerge (const std::vector<const InputProfile*> &chain); // base first; later ones replace per input and layer

struct DeviceCal { int code; int min, center, max; };
struct DeviceDef {
	std::string name;
	unsigned vid = 0, pid = 0;
	std::string product, serial, iface, port;
	int legacyIndex = 0;      // DEVICE x = INDEX n: the n-th device of the old JoystickIndex list
	std::vector<DeviceCal> cal;
};
struct DevicesFile {
	std::vector<DeviceDef> defs;
	std::vector<std::string> errors;
};
bool InputReadDevices (const std::string &path, DevicesFile &f);
bool InputWriteDevices (const std::string &path, const DevicesFile &f);
bool InputWriteFile (const std::string &path, const std::string &text); // a temporary file, then rename

// which Devices.cfg entry each device is: identity, then serial, interface and port; the old list for INDEX entries
std::map<int,int> InputMatchDevices (const std::vector<DeviceDef> &defs, const std::vector<const InputDevice*> &devs,
	const std::vector<const InputDevice*> &legacy);
DeviceDef InputDefFor (const InputDevice &d, const std::vector<DeviceDef> &defs); // a new entry with an unused name
std::string InputAccessHint (const InputDevice &d);  // a device found but not readable: the udev rule that gives it to the user

// ====================================================================================
// engine



struct InputKeyEv { uint32_t key; bool down; uint16_t mods; };
struct InputMfdEv { int mfd; int btn; int event; };   // btn >= 0: PANEL_MOUSE_*; IMFD_*: on press
struct InputChanSet { int chan; InputAxMode mode; double v; };

struct InputFrame {
	unsigned char key[256];          // held by bindings, with their modifiers
	unsigned char func[LKEY_COUNT];  // held functions
	double chan[ICH_NCONT];
	std::vector<InputKeyEv> ev;
	std::vector<InputChanSet> set;
	std::vector<InputMfdEv> mfd;
};

class InputEngine {
public:
	InputEngine ();

	void SetBindings (const std::vector<Binding> &binds, InputFrame *released = nullptr);
	void SetCalibration (const std::vector<DeviceDef> &defs);
	void AttachDevice (const InputDevice &d, const std::string &name);
	void DetachDevice (int id);
	void ReleaseAll (InputFrame &released);      // key-ups and MFD ups to deliver now; held inputs wait for release
	void SetView (bool cockpit) { cockpit_ = cockpit; }
	void SetSuspended (bool s, InputFrame *released = nullptr); // text box or selection menu: keys, functions and MFD held back
	void SetMuted (bool m, InputFrame *released = nullptr);     // the window has no focus: inputs kept current, nothing acts
	void ReleaseKeysMouse (InputFrame &released);              // the keyboard and mouse went elsewhere

	void DeviceEvent (const InputDevEvent &e);
	bool KeyEvent (int evcode, bool down, uint16_t mods); // true: taken
	bool MouseButton (int button, bool down, uint16_t mods);
	bool MouseWheel (int dir, uint16_t mods);
	bool IsBoundKey (int evcode, uint16_t mods) const;
	bool IsTakenKey (int evcode) const;
	bool IsBoundWheel (uint16_t mods) const;
	const InputFrame &Frame () const { return frame_; }

	const InputFrame &Step (double dt);
	int Layer () const;
	double AxisValue (const std::string &dev, int code, bool *valid = nullptr) const;
	double Process (const Binding &b, double x) const;   // an axis value through a binding's options

private:
	struct IKey {
		uint8_t src, kind; int dev; int code; int dir;
		bool operator< (const IKey &o) const;
	};
	struct Press { std::vector<int> binds; bool blocked; bool taken; };
	struct BindState {
		bool active = false, toggled = false, pendingRelease = false, wasActive = false, repeating = false, armed = true;
		uint64_t pressFrame = 0;
		double repeatT = 0, last = 0, v0 = 0;
		bool haveLast = false, haveV0 = false;
	};
	struct Zone { int dev, code; double lo, hi; bool in; };
	struct AxisState { double x; bool valid; int raw, min, max; int raw0; bool live; }; // live: moved since its device was found

	int DevIndex (const std::string &name);
	IKey KeyOf (const InputRef &r);
	std::vector<int> Resolve (const IKey &k, uint16_t mods, bool *found);
	void Press_ (const IKey &k, uint16_t mods, bool taken);
	void Release_ (const IKey &k);
	void Pulse (const IKey &k, uint16_t mods);
	void Act (int bi);
	void Hold (int bi);
	void Unhold (int bi);
	void Edge (int bi, bool press);
	void Tap (int key, uint16_t mods);
	void KeyHold (int key, uint16_t mods, int delta);
	void ModHold (uint16_t mods, int delta);
	bool HeldType (int func) const;
	bool Suspends (const Binding &b) const;
	void Zones (int dev, int code);
	void SetAxis (int dev, int code, int raw, int min, int max);

	std::vector<Binding> binds_;
	std::vector<BindState> st_;
	std::map<IKey, std::vector<int>> byInput_;
	std::vector<std::string> devNames_;
	std::map<int,int> attached_;                   // device id -> name index
	std::map<std::pair<int,int>, AxisState> axes_; // (name index, ABS code)
	std::map<std::string, std::vector<DeviceCal>> cal_;
	std::vector<Zone> zones_;
	std::map<IKey, Press> pressed_;
	int keyCount_[256], modCount_[256], funcCount_[LKEY_COUNT];
	int layerCount_[9];
	bool layerLatch_[9];
	std::deque<uint16_t> tapQ_[256];
	int tapPhase_[256];
	uint16_t tapMods_[256];
	std::vector<InputKeyEv> pendEv_;
	std::vector<InputChanSet> pendSet_;
	std::vector<InputMfdEv> pendMfd_;
	std::map<std::pair<int,int>, int> mfdHeld_;
	InputFrame frame_;
	uint64_t frameNo_;
	bool cockpit_, suspended_, muted_;
};

// ====================================================================================
// the files under Config/Input, the devices and the engine together

struct InputLegacyCfg {        // Orbiter.cfg's joystick lines (CFG_JOYSTICKPRM)
	unsigned joyIndex, deadzone, throttleAxis, throttleSat;
	bool throttleIgnore;
};

class InputSystem {
public:
	InputSystem (const std::string &dir, InputDevices *devices); // dir: Config/Input
	void Migrate (const InputLegacyCfg &cfg);  // once: Global.cfg from the old joystick settings
	void Load ();                              // Devices.cfg and the profiles again
	void SetProfile (const std::string &vesselClass, InputFrame *released);
	void Poll (InputFrame *released);          // devices, hot-plug, files changed on disk
	InputEngine &Engine () { return engine; }
	const InputEngine &Engine () const { return engine; }
	InputDevices &Devices () { return *devs; }
	const DevicesFile &DeviceDefs () const { return devfile; }
	std::string NameOf (int devId) const;      // the Devices.cfg name of a device, "" if none
	std::vector<std::string> Messages ();      // file errors and device news since the last call
	const std::string &Dir () const { return dir; }

private:
	void Claim ();                             // match devices to Devices.cfg, new entries for new devices
	const InputProfile *Profile (const std::string &name);
	void Rebind (InputFrame *released);
	long Mtime (const std::string &path) const;

	std::string dir;
	InputDevices *devs;
	DevicesFile devfile;
	long devMtime;
	std::map<std::string, InputProfile> profiles;
	std::map<std::string, long> profMtime;
	std::string vclass;
	std::map<int,int> claimed;                 // device id -> defs index
	InputEngine engine;
	std::vector<std::string> msgs;
	std::map<std::string, double> noAccess;   // node -> first seen without access (s), -1 once told
	double checkT;
};

#endif // !__INPUTBINDINGS_H
