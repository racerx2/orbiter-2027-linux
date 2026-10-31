// not upstream (controls-editor): the Controls pages' model (Src/Orbiter/InputEditor) and the files it writes
#include <catch2/catch_test_macros.hpp>
#include "InputEditor.h"
#include "Di7frame.h"
#include "Keymap.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <linux/input.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

// Keymap.cpp reads keymap.cfg through these; the tests use its defaults
bool GetItemString (std::istream &is, const char *label, char *val, size_t n) { return false; }
std::string oapiResolvePath (const char *path) { return path; }

struct TempDir { // removed with everything in it at the end of the test
	std::string path;
	TempDir () { char t[] = "/tmp/orbiter-inputeditor-XXXXXX"; if (mkdtemp (t)) path = t; }
	~TempDir () { std::error_code ec; if (!path.empty()) std::filesystem::remove_all (path, ec); }
};

static void Put (const std::string &path, const std::string &text)
{
	for (size_t p = path.find ('/', 1); p != std::string::npos; p = path.find ('/', p+1)) mkdir (path.substr (0, p).c_str(), 0755);
	std::ofstream f (path);
	f << text;
}

static std::string Get (const std::string &path)
{
	std::ifstream f (path);
	std::stringstream s;
	s << f.rdbuf();
	return s.str();
}

static Binding Parse (const std::string &line)
{
	Binding b;
	std::string err;
	INFO(line);
	bool ok = InputParseBinding (line, b, err);
	INFO(err);
	REQUIRE(ok);
	return b;
}

static InputDevice Dev (std::vector<int> axes, int amin, int amax)
{
	InputDevice d;
	d.id = 1, d.state = InputDevice::OPEN, d.fd = -1, d.err = 0, d.dropped = false;
	d.vendor = d.product = d.version = 0;
	for (int a : axes) d.axes.push_back ({a, amin, amax, (amin + amax)/2});
	for (int h = 0; h < 4; h++) {
		d.hatPresent[h] = false;
		d.hat[h][0] = d.hat[h][1] = d.hatCenter[h][0] = d.hatCenter[h][1] = 0;
	}
	return d;
}

static void SetAxis (InputDevice &d, int code, int v)
{
	for (auto &a : d.axes) if (a.code == code) a.value = v;
}

TEST_CASE("a hand-edited profile is written back with every line", "[editor]")
{
	TempDir td;
	REQUIRE(!td.path.empty());
	std::string dir = td.path, path = dir + "/Global.cfg";
	std::string in =
		"; my profile\n"
		"; second line\n"
		"\n"
		"INHERIT Vessels/Base\n"
		"; the gear\n"
		"BIND Box.B1 BOTH = KEY G     ; gear switch\n"
		"garbage line here\n"
		"BIND Box.B2 = FUNC NMPrograde\n"
		"\n"
		"; end\n";
	Put (path, in);
	InputProfile p;
	REQUIRE(InputReadProfile (path, p));
	REQUIRE(p.binds.size() == 2);
	REQUIRE(p.errors.size() == 1);
	REQUIRE(p.inherit == "Vessels/Base");
	REQUIRE(InputWriteProfile (path, p, "; a new header\n"));
	REQUIRE(Get (path) ==
		"; my profile\n"
		"; second line\n"
		"\n"
		"INHERIT Vessels/Base\n"
		"; the gear\n"
		"BIND Box.B1 BOTH = KEY G ; gear switch\n"
		"garbage line here\n"
		"BIND Box.B2 = FUNC NMPrograde\n"
		"\n"
		"; end\n");
	InputProfile q; // a new profile gets the header
	q.binds.push_back (Parse ("Box.B3 = FUNC Pause"));
	REQUIRE(InputWriteProfile (dir + "/New.cfg", q, "; a new header\n"));
	REQUIRE(Get (dir + "/New.cfg") == "; a new header\nBIND Box.B3 = FUNC Pause\n");
}

TEST_CASE("press to bind takes the first control used", "[editor]")
{
	InputDevice d = Dev ({ABS_X, ABS_Z}, 0, 1023);
	InputRef r;
	REQUIRE(InputCaptureOf ({InputDevEvent::BUTTON, 1, 4, 1, 0}, d, "Box", 0, r));
	REQUIRE((r.src == INSRC_DEV && r.kind == INK_BUTTON && r.code == 4 && r.dev == "Box"));
	REQUIRE(!InputCaptureOf ({InputDevEvent::BUTTON, 1, 4, 0, 0}, d, "Box", 0, r)); // its release
	REQUIRE(InputCaptureOf ({InputDevEvent::HAT, 1, 0, 0, -1}, d, "Stick", 0, r));
	REQUIRE((r.kind == INK_HAT && r.dir == 0));
	REQUIRE(InputCaptureOf ({InputDevEvent::HAT, 1, 0, 1, 0}, d, "Stick", 0, r));
	REQUIRE(r.dir == 1);
	REQUIRE(InputCaptureOf ({InputDevEvent::HAT, 1, 0, -1, 0}, d, "Stick", 0, r));
	REQUIRE(r.dir == 3);
	REQUIRE(!InputCaptureOf ({InputDevEvent::HAT, 1, 0, 0, 0}, d, "Stick", 0, r)); // back to centre
	REQUIRE(InputCaptureOf ({InputDevEvent::REL, 1, REL_DIAL, -2, 0}, d, "Box", 0, r));
	REQUIRE((r.kind == INK_REL && r.code == REL_DIAL && r.dir == -1));
	REQUIRE(!InputCaptureOf ({InputDevEvent::AXIS, 1, ABS_Z, 400, 0}, d, "Throttle", 0, r)); // a lever at its end, moved 39 %
	REQUIRE(InputCaptureOf ({InputDevEvent::AXIS, 1, ABS_Z, 410, 0}, d, "Throttle", 0, r));   // 40 % of its range
	REQUIRE((r.kind == INK_AXIS && r.code == ABS_Z && r.dev == "Throttle"));
	REQUIRE(!InputCaptureOf ({InputDevEvent::AXIS, 1, ABS_X, 700, 0}, d, "Stick", 512, r));  // a stick nudged from its centre
	REQUIRE(InputCaptureOf ({InputDevEvent::AXIS, 1, ABS_X, 1023, 0}, d, "Stick", 512, r)); // pushed to an end
	REQUIRE(InputCaptureOf ({InputDevEvent::AXIS, 1, ABS_X, 0, 0}, d, "Stick", 512, r));
	REQUIRE(!InputCaptureOf ({InputDevEvent::AXIS, 1, ABS_RZ, 1023, 0}, d, "Stick", 0, r));   // no such axis
}

TEST_CASE("the editor writes the profiles at once", "[editor]")
{
	TempDir td;
	REQUIRE(!td.path.empty());
	std::string dir = td.path;
	Put (dir + "/Vessels/DeltaGlider.cfg", "ClassName = DeltaGlider\n");
	Put (dir + "/Vessels/Sub/Thing.cfg", "ClassName = Sub/Thing\n");
	Put (dir + "/Input/Vessels/ShuttleA.cfg", "BIND Stick.B2 = FUNC Pause\n");
	Put (dir + "/Input/Global.cfg", "; global\nBIND Stick.B1 = FUNC Pause\nBIND Stick.X = AXIS Bank\n");
	InputDevices devs (dir + "/nosys", dir + "/nodev");
	InputSystem sys (dir + "/Input", &devs);
	InputEditor ed (&sys, InputEditor::VesselDir (&sys));
	REQUIRE(InputEditor::VesselDir (&sys) == dir + "/Vessels");
	REQUIRE(ed.Classes() == std::vector<std::string> ({"ShuttleA", "DeltaGlider", "Sub/Thing"}));

	REQUIRE(ed.Open ("Vessels/DeltaGlider"));
	REQUIRE(ed.Profile().binds.empty());
	REQUIRE(ed.Inherited().size() == 2);
	std::string err;
	REQUIRE(ed.Put (-1, Parse ("Stick.B1 = FUNC Undock"), err));
	REQUIRE(Get (dir + "/Input/Vessels/DeltaGlider.cfg") ==
		"; Orbiter input profile for vessel class DeltaGlider, on top of Global.cfg\n"
		"; BIND <input> [@layer] [mods] [ZONE a b] [mode] = <output> [options]; see the User Manual, Controllers.\n\n"
		"BIND Stick.B1 = FUNC Undock\n");
	REQUIRE(ed.Inherited().size() == 1); // Global's Stick.B1 is replaced here
	REQUIRE(ed.Inherited()[0].in.kind == INK_AXIS);

	Binding bad = Parse ("Stick.B3 = FUNC Pause");
	bad.in.kind = INK_AXIS, bad.in.code = ABS_Y; // a whole axis can't press a function
	REQUIRE(!ed.Put (-1, bad, err));
	REQUIRE(!err.empty());
	REQUIRE(ed.Profile().binds.size() == 1);

	REQUIRE(ed.Put (0, Parse ("Stick.B1 @2 = FUNC Undock"), err));
	REQUIRE(ed.Profile().binds[0].layer == 2);
	REQUIRE(ed.Inherited().size() == 2);
	REQUIRE(ed.Remove (0, err));
	REQUIRE(Get (dir + "/Input/Vessels/DeltaGlider.cfg") ==
		"; Orbiter input profile for vessel class DeltaGlider, on top of Global.cfg\n"
		"; BIND <input> [@layer] [mods] [ZONE a b] [mode] = <output> [options]; see the User Manual, Controllers.\n\n");

	REQUIRE(ed.Open ("Global")); // comments stay
	REQUIRE(ed.Put (-1, Parse ("Key.F13 = KEY G"), err));
	REQUIRE(Get (dir + "/Input/Global.cfg") == "; global\nBIND Stick.B1 = FUNC Pause\nBIND Stick.X = AXIS Bank\nBIND Key.F13 = KEY G\n");
	REQUIRE(ed.Inherited().empty());
}

TEST_CASE("a vessel class's profile is found whatever its letter case", "[editor]")
{
	TempDir td;
	REQUIRE(!td.path.empty());
	std::string dir = td.path;
	Put (dir + "/Input/vessels/deltaglider.cfg", "BIND Stick.B1 = FUNC Pause\n"); // made from Config/Vessels/Deltaglider.cfg's name
	REQUIRE(InputResolveCase (dir + "/Input/Vessels/DeltaGlider.cfg") == dir + "/Input/vessels/deltaglider.cfg");
	REQUIRE(InputResolveCase (dir + "/Input/Vessels/Other.cfg") == dir + "/Input/vessels/Other.cfg"); // a new file in the folder that is there
	REQUIRE(InputClassPath ("XR\\XR2Ravenstar") == "XR/XR2Ravenstar");
	InputDevices devs (dir + "/nosys", dir + "/nodev");
	InputSystem sys (dir + "/Input", &devs);
	InputEditor ed (&sys, InputEditor::VesselDir (&sys));
	REQUIRE(ed.Open ("Vessels/DeltaGlider")); // the class name the scenario gives
	REQUIRE(ed.Profile().binds.size() == 1);
	std::string err;
	REQUIRE(ed.Put (-1, Parse ("Stick.B2 = FUNC Undock"), err));
	REQUIRE(Get (dir + "/Input/vessels/deltaglider.cfg") == "BIND Stick.B1 = FUNC Pause\nBIND Stick.B2 = FUNC Undock\n");
}

TEST_CASE("renaming a controller renames its bindings", "[editor]")
{
	TempDir td;
	REQUIRE(!td.path.empty());
	std::string dir = td.path;
	Put (dir + "/Input/Devices.cfg", "DEVICE Stick = 4098:bea8 \"Orion\"\nCAL Stick.X = 0 32768 65535\nDEVICE Pedals = 06a3:0763 \"Pedals\"\n");
	Put (dir + "/Input/Global.cfg", "BIND Stick.B1 = FUNC Pause ; pause\nBIND Pedals.RZ = AXIS Yaw\n");
	Put (dir + "/Input/Vessels/DeltaGlider.cfg", "; dg\nBIND Stick.X = AXIS Bank\n");
	InputDevices devs (dir + "/nosys", dir + "/nodev");
	InputSystem sys (dir + "/Input", &devs);
	InputEditor ed (&sys, InputEditor::VesselDir (&sys));
	ed.Open ("Global");
	std::string err;
	REQUIRE(!ed.Rename ("Stick", "Pedals", err)); // taken
	REQUIRE(!ed.Rename ("Stick", "Key", err));    // not a controller's name
	REQUIRE(!ed.Rename ("Stick", "my stick", err));
	REQUIRE(!ed.Rename ("Yoke", "Wheel", err));   // no such controller
	REQUIRE(ed.Rename ("Stick", "Joy", err));
	REQUIRE(Get (dir + "/Input/Global.cfg") == "BIND Joy.B1 = FUNC Pause ; pause\nBIND Pedals.RZ = AXIS Yaw\n");
	REQUIRE(Get (dir + "/Input/Vessels/DeltaGlider.cfg") == "; dg\nBIND Joy.X = AXIS Bank\n");
	DevicesFile df;
	REQUIRE(InputReadDevices (dir + "/Input/Devices.cfg", df));
	REQUIRE(df.defs.size() == 2);
	REQUIRE(df.defs[0].name == "Joy");
	REQUIRE(df.defs[0].cal.size() == 1);
	REQUIRE(ed.Profile().binds[0].in.dev == "Joy"); // the open profile too

	REQUIRE(ed.WriteCalibration ("Pedals", {{ABS_RZ, 47, 212, 368}}, err));
	REQUIRE(InputReadDevices (dir + "/Input/Devices.cfg", df));
	REQUIRE(df.defs[1].cal.size() == 1);
	REQUIRE((df.defs[1].cal[0].code == ABS_RZ && df.defs[1].cal[0].min == 47 && df.defs[1].cal[0].center == 212 && df.defs[1].cal[0].max == 368));
	REQUIRE(ed.WriteCalibration ("Pedals", {}, err)); // back to the raw range
	REQUIRE(InputReadDevices (dir + "/Input/Devices.cfg", df));
	REQUIRE(df.defs[1].cal.empty());
}

TEST_CASE("the old Joystick tab's dead zone and throttle saturation on every axis binding", "[editor]")
{
	TempDir td;
	REQUIRE(!td.path.empty());
	std::string dir = td.path;
	Put (dir + "/Input/Devices.cfg", "DEVICE Stick = 0079:0006 \"Stick\"\n");
	Put (dir + "/Input/Global.cfg", "BIND Stick.X = AXIS Bank DZ 0.25 SAT 0.9\nBIND Stick.Z = AXIS Main INVERT DZ 0.025 SAT 0.975 PICKUP\n"
		"BIND Stick.Y ZONE 0.8 1 = FUNC Pause\nBIND Stick.HAT0.UP = AXIS LookY +1\n");
	Put (dir + "/Input/Vessels/DeltaGlider.cfg", "BIND Stick.RZ = AXIS Yaw\n");
	InputDevices devs (dir + "/nosys", dir + "/nodev");
	InputSystem sys (dir + "/Input", &devs);
	InputEditor ed (&sys, InputEditor::VesselDir (&sys));
	ed.Open ("Global");
	std::string err;
	int n = 0;
	REQUIRE(ed.SetZones (0.1, 0.9, n, err));
	REQUIRE(n == 3);
	REQUIRE(Get (dir + "/Input/Global.cfg") == "BIND Stick.X = AXIS Bank DZ 0.1 SAT 0.9\nBIND Stick.Z = AXIS Main INVERT DZ 0.05 SAT 0.95 PICKUP\n"
		"BIND Stick.Y ZONE 0.8 1 = FUNC Pause\nBIND Stick.HAT0.UP = AXIS LookY +1\n");
	REQUIRE(Get (dir + "/Input/Vessels/DeltaGlider.cfg") == "BIND Stick.RZ = AXIS Yaw DZ 0.1\n");
	REQUIRE(ed.SetZones (0.1, 0.9, n, err)); // the same again: nothing written
	REQUIRE(n == 0);
}

TEST_CASE("calibration: a stick, a lever and an axis not moved", "[editor]")
{
	InputDevice d = Dev ({ABS_X, ABS_Z, ABS_RZ}, 0, 65535);
	SetAxis (d, ABS_Z, 0);
	InputCalibrator c;
	c.Start (d);
	for (int v : {32768, 1500, 64000, 30000}) SetAxis (d, ABS_X, v), c.Update (d);
	for (int v : {0, 30000, 63000, 2000}) SetAxis (d, ABS_Z, v), c.Update (d);
	SetAxis (d, ABS_RZ, 33000), c.Update (d); // 0.7 % of its range: not moved
	SetAxis (d, ABS_X, 33100), SetAxis (d, ABS_Z, 0); // let go: the stick centres, the lever stays at its end
	c.Finish (d);
	REQUIRE(c.axes.size() == 3);
	REQUIRE(c.axes[0].centring);
	REQUIRE(!c.axes[1].centring);
	REQUIRE(!c.Moved (c.axes[2]));
	std::vector<DeviceCal> r = c.Result();
	REQUIRE(r.size() == 2);
	REQUIRE((r[0].code == ABS_X && r[0].min == 1500 && r[0].center == 33100 && r[0].max == 64000));
	REQUIRE((r[1].code == ABS_Z && r[1].min == 0 && r[1].center == 31500 && r[1].max == 63000));
	c.axes[1].centring = true; // the user says it centres
	REQUIRE(c.Result()[1].center == 1); // at its end: kept inside the travel
}

TEST_CASE("notes: a control's other bindings, a key keymap.cfg uses", "[editor]")
{
	TempDir td;
	REQUIRE(!td.path.empty());
	std::string dir = td.path;
	Put (dir + "/Input/Global.cfg", "BIND Stick.B1 = FUNC Pause\nBIND Stick.B1 = FUNC Undock\nBIND Stick.B2 = FUNC Undock\n");
	InputDevices devs (dir + "/nosys", dir + "/nodev");
	InputSystem sys (dir + "/Input", &devs);
	InputEditor ed (&sys, InputEditor::VesselDir (&sys));
	ed.Open ("Global");
	Keymap km;
	REQUIRE(ed.AlsoDoes (ed.Profile().binds[0], 0).find ("FUNC Undock") != std::string::npos); // several on one control all act
	REQUIRE(ed.Conflict (ed.Profile().binds[0], 0, &km).empty());
	REQUIRE(ed.AlsoDoes (ed.Profile().binds[2], 2).empty());
	int f = -1, ev = 0;
	for (int i = 0; i < LKEY_COUNT && !ev; i++) { // a function whose default key has no modifier
		WORD k = km.Key (i);
		if (!k || (k & 0xFF00)) continue;
		for (int e = 1; e < 255 && !ev; e++) if ((int)KeyboardDevice::DIKCode (e) == (k & 0xFF)) f = i, ev = e;
	}
	REQUIRE(ev);
	Binding b = Parse ("Key.F13 = KEY G");
	b.in.code = ev;
	std::string c = ed.Conflict (b, -1, &km);
	INFO(c);
	REQUIRE(c.find (Keymap::FuncName (f)) != std::string::npos);
	b.pass = true; // the key still does its own work
	REQUIRE(ed.Conflict (b, -1, &km).empty());
	b.pass = false, b.in.mods = KMOD_CTRL;
	REQUIRE(ed.Conflict (b, -1, &km).empty());
}

TEST_CASE("reset to defaults: Global, a vessel class, all of them, the keyboard keys", "[editor]")
{
	TempDir td;
	REQUIRE(!td.path.empty());
	std::string dir = td.path, in = dir + "/Input";
	Put (in + "/Devices.cfg", "DEVICE Joy = INDEX 2\nDEVICE Pedals = 06a3:0763 \"Pedals\"\n"); // the old JoystickIndex 2, renamed
	Put (in + "/Global.cfg", "; mine\nBIND Key.F13 = KEY G\n");
	Put (in + "/Vessels/DeltaGlider.cfg", "BIND Joy.B1 = FUNC Undock\n");
	Put (in + "/Vessels/ShuttleA.cfg", "INHERIT Vessels/Base\nBIND Joy.B2 = FUNC Undock\n");
	Put (in + "/Vessels/Base.cfg", "BIND Joy.B3 = FUNC Undock\n");
	InputDevices devs (dir + "/nosys", dir + "/nodev");
	InputSystem sys (in, &devs);
	sys.Migrate ({2, 1000, 1, 9500, true}); // Global.cfg is there: only kept
	sys.Load ();
	REQUIRE(Get (in + "/Global.cfg") == "; mine\nBIND Key.F13 = KEY G\n");
	InputEditor ed (&sys, InputEditor::VesselDir (&sys));
	std::string note, err;

	ed.Open ("Global");
	REQUIRE(ed.Reset (note, err));
	INFO(note);
	REQUIRE(note.empty());
	REQUIRE(Get (in + "/Global.cfg.bak") == "; mine\nBIND Key.F13 = KEY G\n");
	std::string g = Get (in + "/Global.cfg");
	REQUIRE(g.find ("Made from Orbiter.cfg's joystick settings") != std::string::npos);
	REQUIRE(g.find ("BIND Joy.X = AXIS Bank DZ 0.1\n") != std::string::npos);
	REQUIRE(g.find ("BIND Joy.Z = AXIS Main INVERT DZ 0.025 SAT 0.975 PICKUP\n") != std::string::npos);
	REQUIRE(g.find ("Key.F13") == std::string::npos);
	REQUIRE(ed.Profile().binds.size() == 16);

	sys.Migrate ({0, 1000, 0, 0, false}); // no joystick in Orbiter.cfg
	REQUIRE(ed.Reset (note, err));
	REQUIRE(ed.Profile().binds.empty());
	sys.Migrate ({3, 1000, 0, 0, false}); // one Devices.cfg doesn't know
	REQUIRE(ed.Reset (note, err));
	REQUIRE(note.find ("JoystickIndex = 3") != std::string::npos);
	REQUIRE(ed.Profile().binds.empty());

	REQUIRE(ed.VesselProfiles() == std::vector<std::string> ({"Vessels/Base", "Vessels/DeltaGlider", "Vessels/ShuttleA"}));
	ed.Open ("Vessels/DeltaGlider");
	REQUIRE(ed.Reset (note, err)); // gone: Global applies
	REQUIRE(!std::filesystem::exists (in + "/Vessels/DeltaGlider.cfg"));
	REQUIRE(Get (in + "/Vessels/DeltaGlider.cfg.bak") == "BIND Joy.B1 = FUNC Undock\n");
	REQUIRE(ed.Profile().binds.empty());
	ed.Open ("Vessels/Base");
	REQUIRE(ed.Reset (note, err)); // ShuttleA INHERITs it: emptied, not removed
	REQUIRE(Get (in + "/Vessels/Base.cfg").find ("BIND Joy") == std::string::npos);
	REQUIRE(Get (in + "/Vessels/Base.cfg").find ("vessel class Base") != std::string::npos);
	REQUIRE(ed.Profile().binds.empty());
	REQUIRE(ed.VesselProfiles().size() == 2);

	REQUIRE(ed.ResetVessels (err));
	REQUIRE(ed.VesselProfiles().empty());
	REQUIRE(Get (in + "/Vessels/ShuttleA.cfg.bak") == "INHERIT Vessels/Base\nBIND Joy.B2 = FUNC Undock\n");
	REQUIRE(ed.Classes().empty()); // a .bak isn't a profile

	std::string kpath = dir + "/keymap.cfg", ref = dir + "/ref.cfg";
	Put (kpath, "Pause = P\n");
	Keymap km, def;
	REQUIRE(def.Write (ref.c_str()));
	REQUIRE(InputEditor::ResetKeymap (km, kpath, err));
	REQUIRE(Get (kpath) == Get (ref));
	REQUIRE(Get (kpath + ".bak") == "Pause = P\n");
	REQUIRE(!std::filesystem::exists (kpath + ".tmp"));
	REQUIRE(!InputEditor::ResetKeymap (km, dir + "/nofolder/keymap.cfg", err));
	REQUIRE(!err.empty());
}
