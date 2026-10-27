// not upstream (input-bindings): unit tests for Src/Orbiter/InputDevices
#include <catch2/catch_test_macros.hpp>
#include "InputDevices.h"
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <linux/input.h>
#include <linux/uinput.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

static InputCaps Caps (const char *ev, const char *key, const char *abs, const char *rel, const char *prop)
{
	InputCaps c;
	REQUIRE(InputParseBits (ev, c.ev));
	REQUIRE(InputParseBits (key, c.key));
	REQUIRE(InputParseBits (abs, c.abs));
	REQUIRE(InputParseBits (rel, c.rel));
	REQUIRE(InputParseBits (prop, c.prop));
	return c;
}

// his Winwing Orion 2 base with the F-16 grip, as sysfs shows it
static const char *WW_EV = "10001b", *WW_KEY = "3ffffff 0 0 0 0 0 0 ffff00000000 0 0 0 0", *WW_ABS = "3007b";

TEST_CASE("sysfs capability bitmaps", "[inputdev]")
{
	InputCaps c = Caps (WW_EV, WW_KEY, WW_ABS, "0", "0");
	REQUIRE(c.ev[EV_KEY]);
	REQUIRE(c.ev[EV_ABS]);
	REQUIRE(c.key[BTN_JOYSTICK]);
	REQUIRE(c.key[0x12f]);
	REQUIRE(!c.key[0x130]);
	REQUIRE(c.key[BTN_TRIGGER_HAPPY1]);
	REQUIRE(c.key[0x2d9]);
	REQUIRE(!c.key[0x2da]);
	REQUIRE(c.key.count() == 42);
	REQUIRE(c.abs[ABS_X]);
	REQUIRE(c.abs[ABS_RZ]);
	REQUIRE(c.abs[ABS_THROTTLE]);
	REQUIRE(!c.abs[ABS_Z]);
	REQUIRE(c.abs[ABS_HAT0X]);
	REQUIRE(c.abs[ABS_HAT0Y]);
	std::bitset<ABS_CNT> b;
	REQUIRE(!InputParseBits ("12 zz", b));
	REQUIRE(!InputParseBits ("", b));
}

// his keyboard, mice and receiver nodes, as sysfs shows them
static const char *KBD_KEY = "3f00733fff 0 0 483ffff17aff32d bfd4444600000000 1 130ff38b17d007 ffff7bfad941dfff ffbeffdfffefffff fffffffffffffffe";
static const char *RCV_KEY = "3f00733fff 0 0 483ffff17aff32d bfd4444600000000 ffff0001 130ff38b17d000 677bfad9415fed 19ed68000004400 10000002";
static const char *CC_KEY  = "3f00733fff 0 0 483ffff17aff32d bfd4444600000000 1 130ff38b17d000 677bfad9415fed 19ed68000004400 10000002";

TEST_CASE("which nodes are controllers", "[inputdev]")
{
	REQUIRE(InputIsController (Caps (WW_EV, WW_KEY, WW_ABS, "0", "0")));               // his stick
	REQUIRE(InputIsLegacyJoystick (Caps (WW_EV, WW_KEY, WW_ABS, "0", "0")));
	REQUIRE(!InputIsController (Caps ("12001f", KBD_KEY, "100000000", "1040", "0")));    // keyboard
	REQUIRE(!InputIsController (Caps ("1f", RCV_KEY, "100000000", "1943", "0")));        // receiver: mouse with keys
	REQUIRE(!InputIsController (Caps ("1f", CC_KEY, "100000000", "1040", "0")));         // consumer control: media keys and BTN_0
	REQUIRE(!InputIsController (Caps ("9", "0", "10000000000", "0", "0")));              // a mouse node with ABS_MISC only
	REQUIRE(!InputIsController (Caps ("17", "1f0000 0 0 0 0", "0", "903", "0")));        // a mouse
	REQUIRE(!InputIsController (Caps ("b", "0", "3f", "0", "40")));                      // an accelerometer
	REQUIRE(!InputIsController (Caps ("b", "e520 10000 0 0 0 0", "2e0800000000003", "0", "5"))); // a touchpad
	REQUIRE(InputIsController (Caps ("9", "0", "23", "0", "0")));                        // rudder pedals: X, Y, RZ, no buttons
	REQUIRE(InputIsController (Caps ("3", "3ff 0 0 0 0", "0", "0", "0")));               // BTN_0..BTN_9 only (udev leaves it out)
	REQUIRE(InputIsController (Caps ("3", "ffff00000000 0 0 0 0", "0", "0", "0")));      // joystick buttons, no axes
	REQUIRE(InputIsController (Caps ("7", "1 0 0 0 0", "0", "80", "0")));                // a dial with a button
	InputCaps mt = Caps ("b", "ffff00000000 0 0 0 0", "3f", "0", "0");                   // extra axes spilled into ABS_MT codes
	for (int a = ABS_MISC; a < ABS_CNT; a++) mt.abs.set (a);
	REQUIRE(InputIsController (mt));
}

// ====================================================================================
// a fake sysfs and /dev/input in a temporary folder

struct Fixture {
	std::string root, sys, dev;
	Fixture ()
	{
		char tmpl[] = "/tmp/inputdev-XXXXXX";
		root = mkdtemp (tmpl);
		sys = root + "/sys", dev = root + "/dev";
		mkdir (sys.c_str(), 0755);
		mkdir (dev.c_str(), 0755);
	}
	~Fixture () { std::string cmd = "rm -rf '" + root + "'"; (void)!system (cmd.c_str()); }
	static void Put (const std::string &path, const std::string &text) { std::ofstream (path) << text << "\n"; }
	void Node (int n, const char *name, const char *vp, const char *uniq, const char *phys,
		const char *ev, const char *key, const char *abs, bool file = true, mode_t mode = 0644)
	{
		std::string d = sys + "/event" + std::to_string (n) + "/device/";
		mkdir ((sys + "/event" + std::to_string (n)).c_str(), 0755);
		mkdir (d.c_str(), 0755);
		mkdir ((d + "id").c_str(), 0755);
		mkdir ((d + "capabilities").c_str(), 0755);
		Put (d + "name", name);
		Put (d + "uniq", uniq);
		Put (d + "phys", phys);
		Put (d + "id/vendor", std::string (vp, 4));
		Put (d + "id/product", std::string (vp+5, 4));
		Put (d + "id/version", "0111");
		Put (d + "capabilities/ev", ev);
		Put (d + "capabilities/key", key);
		Put (d + "capabilities/abs", abs);
		Put (d + "capabilities/rel", "0");
		Put (d + "properties", "0");
		if (file) {
			std::string p = dev + "/event" + std::to_string (n);
			{ std::ofstream f (p); } // empty: the events are appended as input_event records
			chmod (p.c_str(), mode);
		}
	}
	void Events (int n, const std::vector<input_event> &ev)
	{
		std::ofstream f (dev + "/event" + std::to_string (n), std::ios::binary | std::ios::app);
		f.write ((const char*)ev.data(), ev.size()*sizeof(input_event));
	}
};

static input_event Ev (int type, int code, int value)
{
	input_event e;
	memset (&e, 0, sizeof e);
	e.type = type, e.code = code, e.value = value;
	return e;
}

TEST_CASE("devices found in sysfs, read from their nodes", "[inputdev]")
{
	Fixture fx;
	fx.Node (14, "Winwing WINWING Orion Joystick Base 2 + JGRIP-F16", "4098:bea8", "26CEF0B58F2048919206D052", "usb-0000:00:14.0-8/input0", WW_EV, WW_KEY, WW_ABS);
	fx.Node (4, "Logitech Wireless Keyboard PID:4075", "046d:4075", "", "usb-0000:00:14.0-1/input2", "12001f", KBD_KEY, "100000000");
	fx.Node (20, "Box", "1234:5678", "", "usb-0000:00:14.0-3/input0", "3", "ffff00000000 0 0 0 0", "0", false); // no node: gone already
	InputDevices devs (fx.sys, fx.dev);
	std::vector<InputDevEvent> ev;
	devs.Scan (&ev);
	REQUIRE(devs.Devices().size() == 1);
	InputDevice &d = *devs.Devices()[0];
	REQUIRE(ev.size() == 1);
	REQUIRE(ev[0].kind == InputDevEvent::ADDED);
	REQUIRE(d.state == InputDevice::OPEN);
	REQUIRE(d.vendor == 0x4098);
	REQUIRE(d.product == 0xbea8);
	REQUIRE(d.uniq == "26CEF0B58F2048919206D052");
	REQUIRE(d.Iface() == "input0");
	REQUIRE(d.Port() == "usb-0000:00:14.0-8");
	REQUIRE(d.EventNumber() == 14);
	REQUIRE(d.buttons.size() == 42);
	REQUIRE(d.buttons[0] == BTN_TRIGGER);
	REQUIRE(d.buttons[2] == BTN_THUMB2);   // B3: upstream's "button 3"
	REQUIRE(d.buttons[16] == BTN_TRIGGER_HAPPY1);
	REQUIRE(d.axes.size() == 6);           // X Y RX RY RZ THROTTLE, the hat apart
	REQUIRE(d.Axis (ABS_THROTTLE));
	REQUIRE(!d.Axis (ABS_HAT0X));
	REQUIRE(d.hatPresent[0]);
	REQUIRE(!d.hatPresent[1]);
	REQUIRE(devs.LegacyJoysticks().size() == 1);

	// events, as the node gives them
	fx.Events (14, { Ev (EV_KEY, BTN_TRIGGER, 1), Ev (EV_ABS, ABS_X, 1000), Ev (EV_ABS, ABS_HAT0Y, -1), Ev (EV_SYN, SYN_REPORT, 0),
		Ev (EV_KEY, BTN_TRIGGER, 2), Ev (EV_KEY, BTN_TRIGGER, 0), Ev (EV_KEY, BTN_TRIGGER_HAPPY1, 1), Ev (EV_KEY, BTN_TRIGGER_HAPPY1, 0),
		Ev (EV_ABS, ABS_HAT0Y, 0), Ev (EV_SYN, SYN_REPORT, 0) });
	ev.clear();
	devs.Poll (ev);
	REQUIRE(ev.size() == 7);
	REQUIRE((ev[0].kind == InputDevEvent::BUTTON && ev[0].index == 0 && ev[0].value == 1));
	REQUIRE((ev[1].kind == InputDevEvent::AXIS && ev[1].index == ABS_X && ev[1].value == 1000));
	REQUIRE((ev[2].kind == InputDevEvent::HAT && ev[2].index == 0 && ev[2].value == 0 && ev[2].value2 == -1));
	REQUIRE((ev[3].kind == InputDevEvent::BUTTON && ev[3].index == 0 && ev[3].value == 0)); // autorepeat (2) left out
	REQUIRE((ev[4].kind == InputDevEvent::BUTTON && ev[4].index == 16 && ev[4].value == 1)); // a press and release in one read: both
	REQUIRE((ev[5].kind == InputDevEvent::BUTTON && ev[5].index == 16 && ev[5].value == 0));
	REQUIRE((ev[6].kind == InputDevEvent::HAT && ev[6].value2 == 0));
}

TEST_CASE("events after SYN_DROPPED wait for the next report", "[inputdev]")
{
	Fixture fx;
	fx.Node (3, "Box", "1234:5678", "", "x/input0", "3", "ffff00000000 0 0 0 0", "0");
	InputDevices devs (fx.sys, fx.dev);
	devs.Scan ();
	REQUIRE(devs.Devices().size() == 1);
	InputDevice &d = *devs.Devices()[0];
	std::vector<input_event> e = { Ev (EV_SYN, SYN_DROPPED, 0), Ev (EV_KEY, BTN_TRIGGER, 1), Ev (EV_SYN, SYN_REPORT, 0), Ev (EV_KEY, BTN_THUMB, 1) };
	std::vector<InputDevEvent> out;
	devs.Feed (d, e.data(), e.size(), out);
	REQUIRE(out.size() == 1); // BTN_TRIGGER dropped (a file has no state to read back), BTN_THUMB after the report
	REQUIRE(out[0].index == 1);
}

TEST_CASE("a controller that can't be opened is listed", "[inputdev]")
{
	if (geteuid() == 0) return; // root opens anything
	Fixture fx;
	fx.Node (7, "Panel", "1234:0001", "", "x/input0", "3", "3ff 0 0 0 0", "0", true, 0);
	InputDevices devs (fx.sys, fx.dev);
	devs.Scan ();
	REQUIRE(devs.Devices().size() == 1);
	REQUIRE(devs.Devices()[0]->state == InputDevice::NOACCESS);
	REQUIRE(devs.Devices()[0]->err == EACCES);
	REQUIRE(devs.LegacyJoysticks().empty());
}

TEST_CASE("hot-plug in a watched folder", "[inputdev]")
{
	Fixture fx;
	InputDevices devs (fx.sys, fx.dev);
	devs.Scan ();
	REQUIRE(devs.Devices().empty());
	fx.Node (9, "Box", "1234:5678", "", "x/input0", "3", "ffff00000000 0 0 0 0", "0");
	std::vector<InputDevEvent> ev;
	devs.Poll (ev);
	REQUIRE(devs.Devices().size() == 1);
	REQUIRE(ev.size() == 1);
	REQUIRE(ev[0].kind == InputDevEvent::ADDED);
	unlink ((fx.dev + "/event9").c_str());
	ev.clear();
	devs.Poll (ev);
	REQUIRE(ev.size() == 1);
	REQUIRE(ev[0].kind == InputDevEvent::REMOVED);
	REQUIRE(devs.Devices().empty());
}

// ====================================================================================
// real virtual devices through /dev/uinput, where the user may write it (skipped elsewhere)

struct UBox {
	int fd = -1;
	UBox (const char *name, const char *phys, int nbtn)
	{
		fd = open ("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0) return;
		ioctl (fd, UI_SET_EVBIT, EV_KEY);
		for (int i = 0; i < nbtn; i++) ioctl (fd, UI_SET_KEYBIT, i < 16 ? BTN_JOYSTICK + i : BTN_TRIGGER_HAPPY1 + i - 16);
		ioctl (fd, UI_SET_PHYS, phys);
		uinput_setup us;
		memset (&us, 0, sizeof us);
		us.id.bustype = BUS_USB, us.id.vendor = 0x1209, us.id.product = 0x0001; // pid.codes test id
		strncpy (us.name, name, UINPUT_MAX_NAME_SIZE-1);
		if (ioctl (fd, UI_DEV_SETUP, &us) < 0 || ioctl (fd, UI_DEV_CREATE) < 0) { close (fd); fd = -1; }
	}
	~UBox () { if (fd >= 0) { ioctl (fd, UI_DEV_DESTROY); close (fd); } }
	void Key (int code, int v)
	{
		input_event e[2] = { Ev (EV_KEY, code, v), Ev (EV_SYN, SYN_REPORT, 0) };
		(void)!write (fd, e, sizeof e);
	}
};

static InputDevice *Find (InputDevices &devs, const char *name, const char *phys, std::vector<InputDevEvent> &ev, bool open = true)
{
	for (int t = 0; t < 300; t++) { // udev gives the node to the user a few ms after it appears
		devs.Poll (ev);
		for (auto &d : devs.Devices())
			if (d->name == name && d->phys == phys && (!open || d->state == InputDevice::OPEN)) return d.get();
		std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}
	return nullptr;
}

TEST_CASE("virtual controllers: found, read, unplugged and plugged again", "[inputdev][uinput]")
{
	if (access ("/dev/uinput", W_OK)) { WARN("/dev/uinput not writable: skipped"); return; }
	InputDevices devs;
	devs.Scan ();
	std::vector<InputDevEvent> ev;
	std::unique_ptr<UBox> box (new UBox ("Orbiter test box", "orbiter-test/input0", 60));
	REQUIRE(box->fd >= 0);
	InputDevice *d = Find (devs, "Orbiter test box", "orbiter-test/input0", ev);
	if (!d) { WARN("the virtual box can't be read (udev gives no access here): skipped"); return; }
	REQUIRE(d->buttons.size() == 60);
	REQUIRE(d->axes.empty());
	int id = d->id;
	ev.clear();
	box->Key (BTN_TRIGGER_HAPPY1 + 43, 1); // B60
	box->Key (BTN_TRIGGER_HAPPY1 + 43, 0);
	for (int t = 0; t < 100 && ev.size() < 2; t++) { devs.Poll (ev); std::this_thread::sleep_for (std::chrono::milliseconds (5)); }
	REQUIRE(ev.size() == 2);
	REQUIRE((ev[0].kind == InputDevEvent::BUTTON && ev[0].dev == id && ev[0].index == 59 && ev[0].value == 1));
	REQUIRE((ev[1].kind == InputDevEvent::BUTTON && ev[1].index == 59 && ev[1].value == 0));

	ev.clear();
	box.reset();
	bool removed = false;
	for (int t = 0; t < 200 && !removed; t++) {
		devs.Poll (ev);
		for (auto &e : ev) if (e.kind == InputDevEvent::REMOVED && e.dev == id) removed = true;
		std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}
	REQUIRE(removed);
	REQUIRE(!devs.Get (id));

	box.reset (new UBox ("Orbiter test box", "orbiter-test/input0", 60));
	ev.clear();
	InputDevice *d2 = Find (devs, "Orbiter test box", "orbiter-test/input0", ev);
	REQUIRE(d2);
	REQUIRE(d2->id != id);

	// a second, identical box: told apart by its interface
	UBox twin ("Orbiter test box", "orbiter-test/input1", 60);
	InputDevice *d3 = Find (devs, "Orbiter test box", "orbiter-test/input1", ev);
	REQUIRE(d3);
	REQUIRE(d3->Iface() == "input1");
	REQUIRE(Find (devs, "Orbiter test box", "orbiter-test/input0", ev)->Iface() == "input0");
}
