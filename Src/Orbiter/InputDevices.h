// not upstream: game controllers from evdev, every device at once, found in sysfs, hot-plugged (input-bindings)

#ifndef __INPUTDEVICES_H
#define __INPUTDEVICES_H

#include <bitset>
#include <memory>
#include <string>
#include <vector>

// the kernel's EV_CNT, KEY_CNT, ABS_CNT, REL_CNT and INPUT_PROP_CNT (checked in InputDevices.cpp): its KEY_ macros stay out of Orbiter's headers
enum { INPUTDEV_EV_CNT = 0x20, INPUTDEV_KEY_CNT = 0x300, INPUTDEV_ABS_CNT = 0x40, INPUTDEV_REL_CNT = 0x10, INPUTDEV_PROP_CNT = 0x20 };

struct InputCaps {
	std::bitset<INPUTDEV_EV_CNT> ev;
	std::bitset<INPUTDEV_KEY_CNT> key;
	std::bitset<INPUTDEV_ABS_CNT> abs;
	std::bitset<INPUTDEV_REL_CNT> rel;
	std::bitset<INPUTDEV_PROP_CNT> prop;
};

// a sysfs capability bitmap: hex words, most significant first
bool InputParseBits (const std::string &text, size_t nbits, std::vector<bool> &bits);
template<size_t N> bool InputParseBits (const std::string &text, std::bitset<N> &bits)
{
	std::vector<bool> v;
	if (!InputParseBits (text, N, v)) return false;
	bits.reset();
	for (size_t i = 0; i < N; i++) if (v[i]) bits.set (i);
	return true;
}

bool InputIsJoystick (const InputCaps &c);        // udev's input_id joystick test
bool InputIsController (const InputCaps &c);      // joysticks, plus button boxes and dials udev leaves out
bool InputIsLegacyJoystick (const InputCaps &c);  // the test of the single-joystick list (JoystickIndex)

struct InputAxis {
	int code;
	int min, max;
	int value;
};

struct InputDevice {
	enum State { OPEN, NOACCESS, GONE };
	int id;                      // never reused while the program runs
	std::string sysname;         // event14
	std::string path;            // /dev/input/event14
	std::string name, uniq, phys;
	unsigned vendor, product, version;
	InputCaps caps;
	State state;
	int err;                     // errno of a failed open
	std::vector<int> buttons;    // EV_KEY codes >= BTN_MISC in code order: B1 = buttons[0]
	std::vector<InputAxis> axes; // ABS codes in code order, hats left out
	bool hatPresent[4];
	int hat[4][2];               // -1, 0, +1
	int hatCenter[4][2];
	std::vector<int> rels;       // REL codes
	std::vector<unsigned char> btn;
	int fd;
	bool dropped;                // SYN_DROPPED seen: skip to the next SYN_REPORT, then read the state

	int EventNumber () const;
	std::string Iface () const;  // "input0" from phys
	std::string Port () const;   // phys without "/inputN"
	int ButtonIndex (int code) const;
	const InputAxis *Axis (int code) const;
};

struct InputDevEvent {
	enum Kind { ADDED, REMOVED, BUTTON, AXIS, HAT, REL };
	Kind kind;
	int dev;    // InputDevice::id
	int index;  // BUTTON: button index; AXIS: ABS code; HAT: hat 0..3; REL: REL code
	int value;  // BUTTON: 0/1; AXIS: raw; HAT: x; REL: steps
	int value2; // HAT: y
};

class InputDevices {
public:
	InputDevices (const std::string &sysroot = "/sys/class/input", const std::string &devroot = "/dev/input");
	~InputDevices ();

	void Scan (std::vector<InputDevEvent> *out = nullptr); // every node now; ADDED for new ones
	void Poll (std::vector<InputDevEvent> &out);           // hot-plug, then every open device's events
	void Feed (InputDevice &d, const void *events, size_t n, std::vector<InputDevEvent> &out); // input_event records

	const std::vector<std::unique_ptr<InputDevice>> &Devices () const { return devs; }
	InputDevice *Get (int id);
	std::vector<const InputDevice*> LegacyJoysticks () const; // the old JoystickIndex list, in its order

private:
	bool ReadSysfs (const std::string &sysname, InputDevice &d) const;
	void ScanNode (const std::string &sysname, std::vector<InputDevEvent> *out);
	bool Open (InputDevice &d);
	void Close (InputDevice &d);
	void Read (InputDevice &d, std::vector<InputDevEvent> &out);
	void Resync (InputDevice &d, std::vector<InputDevEvent> &out);
	void Remove (InputDevice &d, std::vector<InputDevEvent> &out);
	void HotPlug (std::vector<InputDevEvent> &out);

	std::string sysroot, devroot;
	std::vector<std::unique_ptr<InputDevice>> devs;
	int nextId;
	int inotifyFd;
};

#endif // !__INPUTDEVICES_H
