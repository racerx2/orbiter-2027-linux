// not upstream (input-bindings): unit tests for Src/Orbiter/InputBindings
#include <catch2/catch_test_macros.hpp>
#include "InputBindings.h"
#include "Config.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <linux/input.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

// Keymap.cpp reads keymap.cfg through these; the tests use only its function names
bool GetItemString (std::istream &is, const char *label, char *val, size_t n) { return false; }
std::string oapiResolvePath (const char *path) { return path; }

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

static std::vector<Binding> Binds (std::initializer_list<const char*> lines)
{
	std::vector<Binding> v;
	for (auto l : lines) v.push_back (Parse (l));
	return v;
}

static InputDevice Dev (int id, int nbtn, std::vector<int> axes, int amin = 0, int amax = 65535, bool hat = false)
{
	InputDevice d;
	d.id = id, d.state = InputDevice::OPEN, d.fd = -1, d.err = 0, d.dropped = false;
	d.vendor = d.product = d.version = 0;
	for (int i = 0; i < nbtn; i++) d.buttons.push_back (i < 16 ? BTN_JOYSTICK + i : BTN_TRIGGER_HAPPY1 + i - 16);
	d.btn.assign (nbtn, 0);
	for (int a : axes) d.axes.push_back ({a, amin, amax, (amin + amax)/2});
	for (int h = 0; h < 4; h++) {
		d.hatPresent[h] = (hat && h == 0);
		d.hat[h][0] = d.hat[h][1] = d.hatCenter[h][0] = d.hatCenter[h][1] = 0;
	}
	return d;
}

static void Btn (InputEngine &e, int id, int b, int v) { e.DeviceEvent ({InputDevEvent::BUTTON, id, b, v, 0}); }
static void Axis (InputEngine &e, int id, int code, int raw) { e.DeviceEvent ({InputDevEvent::AXIS, id, code, raw, 0}); }
static void Hat (InputEngine &e, int id, int x, int y) { e.DeviceEvent ({InputDevEvent::HAT, id, 0, x, y}); }

static int Downs (const InputFrame &f, int key)
{
	int n = 0;
	for (auto &e : f.ev) if (e.key == (uint32_t)key && e.down) n++;
	return n;
}

static int Ups (const InputFrame &f, int key)
{
	int n = 0;
	for (auto &e : f.ev) if (e.key == (uint32_t)key && !e.down) n++;
	return n;
}

// the old DInput joystick scaling (JoystickDevice::Scale): DirectInput range, dead zone and saturation
static long DIScale (int raw, int amin, int amax, long lMin, long lMax, unsigned dz, unsigned sat)
{
	double c = 0.5*(amin + amax), h = 0.5*(amax - amin);
	double t = (h > 0 ? (raw - c)/h : 0.0), at = fabs (t);
	double dzf = dz*1e-4, satf = std::max (sat*1e-4, dzf+1e-6);
	at = (at <= dzf ? 0.0 : at >= satf ? 1.0 : (at-dzf)/(satf-dzf));
	t = (t < 0 ? -at : at);
	return (long)floor (lMin + 0.5*(t+1.0)*(lMax - lMin) + 0.5);
}

TEST_CASE("binding lines read and written", "[bindings]")
{
	const char *lines[] = {
		"Stick.X = AXIS Bank DZ 0.25",
		"Stick.X @1 = AXIS Yaw DZ 0.25",
		"Throttle.Z = AXIS Main INVERT DZ 0.025 SAT 0.975 PICKUP",
		"Stick.HAT0.UP = AXIS LookY +1",
		"Stick.HAT0.LEFT @1 = AXIS PanX -1",
		"Box.B12 BOTH = KEY G",
		"Box.B13 = FUNC Pause",
		"Box.B14 = KEY G CTRL",
		"Box.B15 TOGGLE = KEY NUMPAD2 LCTRL",
		"Key.F13 = KEY G",
		"Key.F20 CTRL = FUNC IncSimSpeed",
		"Key.G ALT PASS = KEY G",
		"Mouse.BACK CTRL = MFD LEFT SEL",
		"Mouse.WHEELUP SHIFT = FUNC IncFOV",
		"Mouse.LEFT @2 = MFD 3 B5",
		"Throttle.RX ZONE 0.9 1 = KEY B",
		"Stick.B3 = LAYER 1",
		"Stick.B4 = LAYER 2 LATCH",
		"Box.REL_DIAL+ = AXIS ElevTrim STEP 0.02",
		"Box.B20 REPEAT 5 = FUNC IncSimSpeed",
		"Box.B21 VIEW COCKPIT = MFD RIGHT PWR",
		"Pedals.RZ = AXIS Yaw CURVE 1.5",
		"Pedals.Z = AXIS BrakeL HALF",
		"Box.B22 = AXIS Main1 SET 0.5",
		"Box.B23 = NONE",
	};
	for (const char *l : lines) {
		Binding b = Parse (l);
		REQUIRE(InputFormatBinding (b) == l);
	}
	Binding b = Parse ("Stick.A41 = AXIS Pitch");
	REQUIRE((b.in.kind == INK_AXIS && b.in.code == 41));
	b = Parse ("Box.B13 = FUNC pause"); // names any case
	REQUIRE(b.func == OAPI_LKEY_Pause);
	b = Parse ("Key.NUMPAD2 = KEY PRIOR"); // keymap.cfg's names and the API's
	REQUIRE(b.key == OAPI_KEY_PRIOR);
	REQUIRE(b.in.code == KEY_KP2);
	b = Parse ("Key.F24 = KEY F13");
	REQUIRE(b.in.code == KEY_F24);
	REQUIRE(b.key == 0x64);

	const char *bad[] = {
		"Stick.X = KEY G",               // an axis drives a channel
		"Stick.X HOLD = AXIS Bank",      // modes need a zone
		"Stick.B3 @1 = LAYER 1",         // layers on the base layer
		"Stick.B1 CTRL = KEY G",         // modifiers are for keys and the mouse
		"Mouse.LEFT = FUNC Pause",       // the panels' button
		"Key.LSHIFT = KEY G",            // a modifier is a modifier
		"Key.G = KEY NOSUCHKEY",
		"Box.B1 = FUNC NoSuchFunction",
		"Box.B1 = AXIS NoSuchChannel",
		"Box.HAT0.SIDEWAYS = KEY G",
		"Key.X = AXIS Bank DZ 1.5",
		"Mouse.Stick = KEY G",
		"Key.Stick.B1 = KEY G",
		"Box.B1 = MFD 13 B1",
	};
	for (const char *l : bad) {
		Binding x;
		std::string err;
		INFO(l);
		REQUIRE(!InputParseBinding (l, x, err));
		REQUIRE(!err.empty());
	}
}

TEST_CASE("the migrated stick gives the old joystick's values", "[bindings]")
{
	InputEngine e;
	e.SetBindings (Binds ({ "Stick.X = AXIS Bank DZ 0.25", "Stick.Y = AXIS Pitch DZ 0.25", "Stick.RZ = AXIS Yaw DZ 0.25" }));
	InputDevice d = Dev (1, 4, {ABS_X, ABS_Y, ABS_RZ});
	e.AttachDevice (d, "Stick");
	for (int raw = 0; raw <= 65535; raw += 7) {
		Axis (e, 1, ABS_X, raw);
		Axis (e, 1, ABS_Y, 65535 - raw);
		const InputFrame &f = e.Step (0.02);
		long up = DIScale (raw, 0, 65535, -1000, 1000, 2500, 10000);
		INFO(raw);
		REQUIRE((long)floor (-1000.0 + 0.5*(f.chan[ICH_BANK]+1.0)*2000.0 + 0.5) == up); // as UserJoyInput_OnRunning scales it
		REQUIRE((long)floor (-1000.0 + 0.5*(f.chan[ICH_PITCH]+1.0)*2000.0 + 0.5) == DIScale (65535 - raw, 0, 65535, -1000, 1000, 2500, 10000));
		REQUIRE(f.chan[ICH_YAW] == 0.0); // RZ at its center
	}
	// the throttle: range -1000..0, saturation 9500, then >> 3 and * -0.008 (UserJoyInput_OnRunning)
	Binding th = Parse ("Stick.Z = AXIS Main INVERT DZ 0.025 SAT 0.975");
	for (int raw = 0; raw <= 1023; raw++) {
		long L = DIScale (raw, 0, 1023, -1000, 0, 0, 9500);
		double old = std::min (1.0, -0.008 * (L >> 3));
		double x = (raw - 511.5)/511.5;
		INFO(raw);
		REQUIRE(fabs (e.Process (th, x) - old) <= 0.008 + 1e-12); // within upstream's 1/125 step
	}
}

TEST_CASE("layers, fallthrough and press-release pairs", "[bindings]")
{
	InputEngine e;
	e.SetBindings (Binds ({ "Stick.X = AXIS Bank", "Stick.X @1 = AXIS Yaw", "Stick.Y = AXIS Pitch", "Stick.B3 = LAYER 1",
		"Stick.B5 = KEY A", "Stick.B5 @1 = KEY B", "Stick.HAT0.UP = AXIS LookY +1", "Stick.HAT0.UP @1 = AXIS PanY +1" }));
	InputDevice d = Dev (1, 8, {ABS_X, ABS_Y}, 0, 65535, true);
	e.AttachDevice (d, "Stick");
	Axis (e, 1, ABS_X, 65535);
	Axis (e, 1, ABS_Y, 0);
	const InputFrame *f = &e.Step (0.02);
	REQUIRE(f->chan[ICH_BANK] == 1.0);
	REQUIRE(f->chan[ICH_PITCH] == -1.0);
	REQUIRE(f->chan[ICH_YAW] == 0.0);

	Btn (e, 1, 4, 1); // B5 on the base layer: A
	f = &e.Step (0.02);
	REQUIRE(Downs (*f, OAPI_KEY_A) == 1);
	REQUIRE(f->key[OAPI_KEY_A]);

	Btn (e, 1, 2, 1); // B3: layer 1; X turns to yaw, Y stays pitch
	Hat (e, 1, 0, -1);
	f = &e.Step (0.02);
	REQUIRE(e.Layer() == 1);
	REQUIRE(f->chan[ICH_BANK] == 0.0);
	REQUIRE(f->chan[ICH_YAW] == 1.0);
	REQUIRE(f->chan[ICH_PITCH] == -1.0);
	REQUIRE(f->chan[ICH_PANY] == 1.0);
	REQUIRE(f->chan[ICH_LOOKY] == 0.0);

	Btn (e, 1, 4, 0); // B5 released on layer 1: A goes up, B was never pressed
	f = &e.Step (0.02);
	REQUIRE(Ups (*f, OAPI_KEY_A) == 1);
	REQUIRE(Downs (*f, OAPI_KEY_B) + Ups (*f, OAPI_KEY_B) == 0);
	REQUIRE(f->chan[ICH_PANY] == 1.0); // the hat held from layer 1 keeps its binding

	Btn (e, 1, 4, 1); // B5 on layer 1: B
	f = &e.Step (0.02);
	REQUIRE(Downs (*f, OAPI_KEY_B) == 1);
	Btn (e, 1, 2, 0);
	Btn (e, 1, 4, 0);
	Hat (e, 1, 0, 0);
	f = &e.Step (0.02);
	REQUIRE(Ups (*f, OAPI_KEY_B) == 1);
	REQUIRE(e.Layer() == 0);
	REQUIRE(f->chan[ICH_BANK] == 1.0);
	REQUIRE(f->chan[ICH_PANY] == 0.0);
}

TEST_CASE("button modes and one-frame presses", "[bindings]")
{
	InputEngine e;
	e.SetBindings (Binds ({ "Box.B1 = KEY G", "Box.B2 PRESS = KEY H", "Box.B3 BOTH = KEY J", "Box.B4 TOGGLE = KEY K",
		"Box.B5 REPEAT 10 = KEY L", "Box.B6 RELEASE = KEY M" }));
	InputDevice d = Dev (2, 8, {});
	e.AttachDevice (d, "Box");

	// HOLD pressed and released between two frames: down for one frame
	Btn (e, 2, 0, 1); Btn (e, 2, 0, 0);
	const InputFrame *f = &e.Step (0.02);
	REQUIRE(Downs (*f, OAPI_KEY_G) == 1);
	REQUIRE(f->key[OAPI_KEY_G]);
	f = &e.Step (0.02);
	REQUIRE(Ups (*f, OAPI_KEY_G) == 1);
	REQUIRE(!f->key[OAPI_KEY_G]);

	// three quick presses (an encoder): down, up, free, down, up, free, down, up
	for (int i = 0; i < 3; i++) { Btn (e, 2, 1, 1); Btn (e, 2, 1, 0); }
	std::string seq;
	for (int i = 0; i < 9; i++) {
		f = &e.Step (0.02);
		seq += (Downs (*f, OAPI_KEY_H) ? "D" : Ups (*f, OAPI_KEY_H) ? "U" : ".");
		if (Downs (*f, OAPI_KEY_H)) REQUIRE(f->key[OAPI_KEY_H]);
	}
	REQUIRE(seq == "DU.DU.DU.");

	// BOTH: a toggle switch flips a toggle key either way
	Btn (e, 2, 2, 1);
	f = &e.Step (0.02);
	REQUIRE(Downs (*f, OAPI_KEY_J) == 1);
	e.Step (0.02); e.Step (0.02);
	Btn (e, 2, 2, 0);
	f = &e.Step (0.02);
	REQUIRE(Downs (*f, OAPI_KEY_J) == 1);
	e.Step (0.02); e.Step (0.02);

	// TOGGLE: first press holds, second lets go
	Btn (e, 2, 3, 1); Btn (e, 2, 3, 0);
	f = &e.Step (0.02);
	REQUIRE(Downs (*f, OAPI_KEY_K) == 1);
	f = &e.Step (0.02);
	REQUIRE(f->key[OAPI_KEY_K]);
	Btn (e, 2, 3, 1); Btn (e, 2, 3, 0);
	f = &e.Step (0.02);
	REQUIRE(Ups (*f, OAPI_KEY_K) == 1);

	// REPEAT 10/s: a press, then after 0.4 s one every 0.1 s
	Btn (e, 2, 4, 1);
	int presses = 0;
	for (int i = 0; i < 100; i++) presses += Downs (e.Step (0.01), OAPI_KEY_L); // 1 s
	Btn (e, 2, 4, 0);
	for (int i = 0; i < 10; i++) presses += Downs (e.Step (0.01), OAPI_KEY_L);
	REQUIRE(presses >= 6);
	REQUIRE(presses <= 7);

	// RELEASE
	Btn (e, 2, 5, 1);
	REQUIRE(Downs (e.Step (0.02), OAPI_KEY_M) == 0);
	Btn (e, 2, 5, 0);
	REQUIRE(Downs (e.Step (0.02), OAPI_KEY_M) == 1);
}

TEST_CASE("key combinations carry their modifiers", "[bindings]")
{
	InputEngine e;
	e.SetBindings (Binds ({ "Box.B1 = KEY NUMPAD2 CTRL", "Box.B2 PRESS = KEY D RSHIFT", "Box.B3 = KEY NUMPAD2" }));
	InputDevice d = Dev (3, 4, {});
	e.AttachDevice (d, "Box");
	Btn (e, 3, 0, 1);
	const InputFrame *f = &e.Step (0.02);
	REQUIRE(f->ev.size() == 1);
	REQUIRE((f->ev[0].key == OAPI_KEY_NUMPAD2 && f->ev[0].down && f->ev[0].mods == KMOD_CTRL));
	REQUIRE(f->key[OAPI_KEY_NUMPAD2]);
	REQUIRE(f->key[OAPI_KEY_LCONTROL]); // either side: the left key
	Btn (e, 3, 2, 1);                    // the same key held twice: one down
	f = &e.Step (0.02);
	REQUIRE(Downs (*f, OAPI_KEY_NUMPAD2) == 0);
	Btn (e, 3, 0, 0);
	f = &e.Step (0.02);
	REQUIRE(Ups (*f, OAPI_KEY_NUMPAD2) == 0); // still held by B3
	REQUIRE(f->key[OAPI_KEY_NUMPAD2]);
	REQUIRE(!f->key[OAPI_KEY_LCONTROL]);
	Btn (e, 3, 2, 0);
	REQUIRE(Ups (e.Step (0.02), OAPI_KEY_NUMPAD2) == 1);
	Btn (e, 3, 1, 1);
	f = &e.Step (0.02);
	REQUIRE((f->ev.size() == 1 && f->ev[0].mods == KMOD_RSHIFT));
	REQUIRE(f->key[OAPI_KEY_RSHIFT]);
	REQUIRE(!f->key[OAPI_KEY_LSHIFT]);
}

TEST_CASE("functions: held ones in the state, the others as tagged events", "[bindings]")
{
	InputEngine e;
	e.SetBindings (Binds ({ "Box.B1 = FUNC RCSPitchUp", "Box.B2 = FUNC Pause", "Box.B3 PRESS = FUNC IncFOV" }));
	InputDevice d = Dev (4, 4, {});
	e.AttachDevice (d, "Box");
	Btn (e, 4, 0, 1);
	Btn (e, 4, 1, 1);
	Btn (e, 4, 2, 1);
	const InputFrame *f = &e.Step (0.02);
	REQUIRE(f->func[OAPI_LKEY_RCSPitchUp]);
	REQUIRE(f->func[OAPI_LKEY_IncFOV]);
	REQUIRE(f->ev.size() == 1);
	REQUIRE((f->ev[0].key == (IKEYFUNC_TAG | OAPI_LKEY_Pause) && f->ev[0].down));
	f = &e.Step (0.02);
	REQUIRE(f->func[OAPI_LKEY_RCSPitchUp]);
	REQUIRE(!f->func[OAPI_LKEY_IncFOV]); // PRESS on a held function: one frame
	REQUIRE(f->ev.empty());              // Pause once
	Btn (e, 4, 0, 0);
	REQUIRE(!e.Step (0.02).func[OAPI_LKEY_RCSPitchUp]);

	Keymap km;
	char ks[256];
	memset (ks, 0, 256);
	unsigned char held[LKEY_COUNT] = {0};
	km.SetHeld (held);
	REQUIRE(!km.IsLogicalKey (ks, OAPI_LKEY_RCSPitchUp));
	held[OAPI_LKEY_RCSPitchUp] = 1;
	REQUIRE(km.IsLogicalKey (ks, OAPI_LKEY_RCSPitchUp));
	DWORD key = IKEYFUNC_TAG | OAPI_LKEY_Pause;
	REQUIRE(!km.IsLogicalKey (key, ks, OAPI_LKEY_Quicksave));
	REQUIRE(km.IsLogicalKey (key, ks, OAPI_LKEY_Pause));
	REQUIRE(key == 0);
	key = OAPI_KEY_P;
	ks[OAPI_KEY_LCONTROL] = (char)0x80;
	REQUIRE(km.IsLogicalKey (key, ks, OAPI_LKEY_Pause)); // the keyboard as before
	REQUIRE(Keymap::FuncByName ("KillMainRetroThrust") == OAPI_LKEY_KillMainRetro);
	REQUIRE(std::string (Keymap::FuncName (OAPI_LKEY_DlgOptions)) == "DlgOptions");
}

TEST_CASE("keyboard and mouse inputs are taken", "[bindings]")
{
	InputEngine e;
	e.SetBindings (Binds ({ "Key.F13 = KEY G", "Key.F14 CTRL = FUNC Pause", "Key.G ALT PASS = KEY H", "Mouse.BACK = MFD LEFT B1",
		"Mouse.WHEELUP CTRL = FUNC IncSimSpeed" }));
	REQUIRE(e.KeyEvent (KEY_F13, true, 0));
	REQUIRE(e.KeyEvent (KEY_F13, true, 0)); // autorepeat stays taken
	REQUIRE(e.IsTakenKey (KEY_F13));
	const InputFrame *f = &e.Step (0.02);
	REQUIRE(Downs (*f, OAPI_KEY_G) == 1);
	REQUIRE(e.KeyEvent (KEY_F13, false, KMOD_LCTRL)); // released with another modifier: still its release
	REQUIRE(Ups (e.Step (0.02), OAPI_KEY_G) == 1);

	REQUIRE(!e.KeyEvent (KEY_F14, true, 0));          // no CTRL: not bound
	REQUIRE(!e.KeyEvent (KEY_F14, false, 0));
	REQUIRE(e.IsBoundKey (KEY_F14, KMOD_RCTRL));
	REQUIRE(!e.IsBoundKey (KEY_F14, KMOD_RCTRL | KMOD_LSHIFT));
	REQUIRE(e.KeyEvent (KEY_F14, true, KMOD_RCTRL));
	REQUIRE(e.Step (0.02).ev.size() == 1);
	REQUIRE(e.KeyEvent (KEY_F14, false, KMOD_RCTRL));

	REQUIRE(!e.KeyEvent (KEY_G, true, KMOD_LALT));    // PASS: acts, and stays a G
	REQUIRE(Downs (e.Step (0.02), OAPI_KEY_H) == 1);
	REQUIRE(!e.KeyEvent (KEY_G, false, KMOD_LALT));
	REQUIRE(!e.IsBoundKey (KEY_G, KMOD_LALT));

	REQUIRE(e.MouseButton (4, true, 0));
	f = &e.Step (0.02);
	REQUIRE(f->mfd.size() == 1);
	REQUIRE((f->mfd[0].mfd == 0 && f->mfd[0].btn == 0 && f->mfd[0].event == PANEL_MOUSE_LBDOWN));
	f = &e.Step (0.02);
	REQUIRE((f->mfd.size() == 1 && f->mfd[0].event == PANEL_MOUSE_LBPRESSED));
	REQUIRE(e.MouseButton (4, false, 0));
	f = &e.Step (0.02);
	REQUIRE((f->mfd.size() == 1 && f->mfd[0].event == PANEL_MOUSE_LBUP));
	REQUIRE(!e.MouseButton (3, true, 0));

	REQUIRE(!e.IsBoundWheel (0));
	REQUIRE(e.IsBoundWheel (KMOD_LCTRL));
	REQUIRE(e.MouseWheel (0, KMOD_LCTRL));
	REQUIRE(e.MouseWheel (0, KMOD_LCTRL));
	REQUIRE(e.Step (0.02).ev.size() == 2); // two notches, two presses
}

TEST_CASE("axis zones and absolute channels", "[bindings]")
{
	InputEngine e;
	e.SetBindings (Binds ({ "Thr.RX ZONE 0.9 1 = KEY B", "Thr.Z = AXIS Main PICKUP", "Thr.RY = AXIS Hover" }));
	InputDevice d = Dev (5, 0, {ABS_RX, ABS_Z, ABS_RY}, -1000, 1000);
	d.axes[0].value = 950; // already in the zone at start: no press
	d.axes[1].value = 0;
	e.AttachDevice (d, "Thr");
	const InputFrame *f = &e.Step (0.02);
	REQUIRE(Downs (*f, OAPI_KEY_B) == 0);
	REQUIRE(f->set.empty()); // nothing has moved yet
	Axis (e, 5, ABS_RY, 2); // Hover at its first change, Main waits for a move
	f = &e.Step (0.02);
	REQUIRE(f->set.size() == 1);
	REQUIRE((f->set[0].chan == ICH_HOVER && fabs (f->set[0].v - 0.501) < 1e-12));
	REQUIRE(e.Step (0.02).set.empty());

	Axis (e, 5, ABS_RX, 800);
	Axis (e, 5, ABS_RX, 910);
	REQUIRE(Downs (e.Step (0.02), OAPI_KEY_B) == 1);
	e.Step (0.02);
	Axis (e, 5, ABS_RX, 895); // inside the 1 % hysteresis
	REQUIRE(e.Step (0.02).ev.empty());
	Axis (e, 5, ABS_RX, 880);
	REQUIRE(Ups (e.Step (0.02), OAPI_KEY_B) == 1);

	Axis (e, 5, ABS_Z, 10); // under 0.01 (half a % on 0..1): still waiting
	REQUIRE(e.Step (0.02).set.empty());
	Axis (e, 5, ABS_Z, 40);
	f = &e.Step (0.02);
	REQUIRE((f->set.size() == 1 && f->set[0].chan == ICH_MAIN && fabs (f->set[0].v - 0.52) < 1e-12));
	Axis (e, 5, ABS_Z, 42); // 1e-3: the keys keep their setting
	REQUIRE(e.Step (0.02).set.empty());
	Axis (e, 5, ABS_Z, 1000);
	f = &e.Step (0.02);
	REQUIRE((f->set.size() == 1 && f->set[0].v == 1.0));
}

TEST_CASE("an axis acts once it has moved since its device was found", "[bindings]")
{
	const std::vector<Binding> binds = Binds ({ "Pedals.RZ = AXIS Yaw", "Stick.RZ = AXIS Yaw", "Stick.RZ = AXIS TransX",
		"Stick.THROTTLE = AXIS Main", "Thr.Z = AXIS Main PICKUP" });
	InputEngine e;
	e.SetBindings (binds);
	InputDevice ped = Dev (20, 0, {ABS_RZ}, 0, 511), stick = Dev (21, 4, {ABS_RZ, ABS_THROTTLE}, 0, 4095), thr = Dev (22, 0, {ABS_Z}, 0, 1023);
	ped.axes[0].value = 0;   // the kernel's value before the first report
	stick.axes[0].value = 0; // not fitted: never changes
	stick.axes[1].value = 0;
	thr.axes[0].value = 0;
	e.AttachDevice (ped, "Pedals");
	e.AttachDevice (stick, "Stick");
	e.AttachDevice (thr, "Thr");
	for (int i = 0; i < 50; i++) {
		const InputFrame &f = e.Step (0.02);
		REQUIRE(f.chan[ICH_YAW] == 0.0);
		REQUIRE(f.chan[ICH_TRANSX] == 0.0);
		REQUIRE(f.set.empty());
	}
	Axis (e, 21, ABS_RZ, 0); // a resync with the same value
	e.SetBindings (binds); // a vessel switch
	REQUIRE(e.Step (0.02).chan[ICH_YAW] == 0.0);

	Axis (e, 20, ABS_RZ, 256); // the pedals' first report: centred
	const InputFrame *f = &e.Step (0.02);
	REQUIRE(fabs (f->chan[ICH_YAW] - 0.5/255.5) < 1e-12);
	Axis (e, 20, ABS_RZ, 511);
	REQUIRE(e.Step (0.02).chan[ICH_YAW] == 1.0);

	Axis (e, 22, ABS_Z, 700); // the lever's first report: live, PICKUP still waits for a real move
	REQUIRE(e.Step (0.02).set.empty());
	Axis (e, 22, ABS_Z, 712);
	f = &e.Step (0.02);
	REQUIRE((f->set.size() == 1 && f->set[0].chan == ICH_MAIN && fabs (f->set[0].v - 0.5*(1.0 + 200.5/511.5)) < 1e-12));

	e.DetachDevice (20); // plugged in again: starts over
	ped.axes[0].value = 511;
	e.AttachDevice (ped, "Pedals");
	REQUIRE(e.Step (0.02).chan[ICH_YAW] == 0.0);
}

TEST_CASE("profiles: global, vessel and inherited", "[bindings]")
{
	InputProfile g, xr, xr2;
	g.binds = Binds ({ "Stick.B1 = KEY G", "Stick.B2 = FUNC Pause", "Stick.X = AXIS Bank" });
	xr.binds = Binds ({ "Stick.B1 = KEY H", "Stick.B3 = KEY J" });
	xr2.binds = Binds ({ "Stick.B2 = NONE", "Stick.B1 @1 = KEY K" });
	std::vector<Binding> m = InputMerge ({ &g, &xr, &xr2 });
	REQUIRE(m.size() == 4);
	int g1 = 0, h1 = 0, k1 = 0, p = 0;
	for (auto &b : m) {
		if (b.out == IOUT_KEY && b.key == OAPI_KEY_G) g1++;
		if (b.out == IOUT_KEY && b.key == OAPI_KEY_H) h1++;
		if (b.out == IOUT_KEY && b.key == OAPI_KEY_K) k1++;
		if (b.out == IOUT_FUNC) p++;
	}
	REQUIRE((g1 == 0 && h1 == 1 && k1 == 1 && p == 0));
}

TEST_CASE("everything let go: profile switch, device loss, text box", "[bindings]")
{
	InputEngine e;
	e.SetBindings (Binds ({ "Box.B1 = KEY G", "Box.B2 = FUNC RCSPitchUp", "Box.B3 = MFD LEFT B2", "Stick.X = AXIS Bank" }));
	InputDevice box = Dev (6, 4, {}), stick = Dev (7, 0, {ABS_X});
	e.AttachDevice (box, "Box");
	e.AttachDevice (stick, "Stick");
	Btn (e, 6, 0, 1); Btn (e, 6, 1, 1); Btn (e, 6, 2, 1);
	Axis (e, 7, ABS_X, 65535);
	e.Step (0.02);

	InputFrame rel;
	e.SetBindings (Binds ({ "Box.B1 = KEY G", "Stick.X = AXIS Bank" }), &rel);
	REQUIRE(Ups (rel, OAPI_KEY_G) == 1);
	REQUIRE((rel.mfd.size() == 1 && rel.mfd[0].event == PANEL_MOUSE_LBUP));
	REQUIRE(!e.Frame().func[OAPI_LKEY_RCSPitchUp]);
	const InputFrame *f = &e.Step (0.02);
	REQUIRE(f->ev.empty());          // B1 still held: waits for its release
	REQUIRE(f->chan[ICH_BANK] == 1.0);
	Btn (e, 6, 0, 0);
	REQUIRE(e.Step (0.02).ev.empty());
	Btn (e, 6, 0, 1);
	REQUIRE(Downs (e.Step (0.02), OAPI_KEY_G) == 1);

	e.DetachDevice (6); // unplugged while held
	REQUIRE(Ups (e.Step (0.02), OAPI_KEY_G) == 1);
	e.DetachDevice (7);
	REQUIRE(e.Step (0.02).chan[ICH_BANK] == 0.0);

	e.AttachDevice (box, "Box");
	e.AttachDevice (stick, "Stick");
	Axis (e, 7, ABS_X, 65535);
	Btn (e, 6, 0, 1);
	e.Step (0.02);
	e.SetSuspended (true, &rel); // a text box opens
	REQUIRE(Ups (rel, OAPI_KEY_G) == 1);
	Btn (e, 6, 0, 0); Btn (e, 6, 0, 1);
	f = &e.Step (0.02);
	REQUIRE(f->ev.empty());
	REQUIRE(f->chan[ICH_BANK] == 1.0); // analog keeps working
	e.SetSuspended (false);
	Btn (e, 6, 0, 0); Btn (e, 6, 0, 1);
	REQUIRE(Downs (e.Step (0.02), OAPI_KEY_G) == 1);

	e.SetMuted (true, &rel); // the window lost its focus
	REQUIRE(Ups (rel, OAPI_KEY_G) == 1);
	Btn (e, 6, 0, 0); Btn (e, 6, 0, 1);
	e.SetMuted (false);
	REQUIRE(e.Step (0.02).ev.empty()); // pressed while muted: acts only when pressed again
}

TEST_CASE("Devices.cfg and the identity of a device", "[bindings]")
{
	char tmpl[] = "/tmp/inputbind-XXXXXX";
	std::string dir = mkdtemp (tmpl);
	DevicesFile df;
	DeviceDef a;
	a.name = "Box1", a.vid = 0x1209, a.pid = 1, a.product = "Orbiter test box", a.iface = "input1", a.port = "usb-1";
	a.cal.push_back ({ABS_X, 10, 500, 1000});
	DeviceDef b = a;
	b.name = "Box2", b.iface = "input0";
	b.cal.clear();
	DeviceDef c;
	c.name = "Stick", c.legacyIndex = 1;
	df.defs = { a, b, c };
	REQUIRE(InputWriteDevices (dir + "/Devices.cfg", df));
	DevicesFile rd;
	REQUIRE(InputReadDevices (dir + "/Devices.cfg", rd));
	REQUIRE(rd.errors.empty());
	REQUIRE(rd.defs.size() == 3);
	REQUIRE(rd.defs[0].iface == "input1");
	REQUIRE((rd.defs[0].cal.size() == 1 && rd.defs[0].cal[0].center == 500));
	REQUIRE(rd.defs[2].legacyIndex == 1);

	InputDevice d0 = Dev (10, 4, {}), d1 = Dev (11, 4, {}), w = Dev (12, 4, {ABS_X});
	for (InputDevice *d : {&d0, &d1}) d->vendor = 0x1209, d->product = 1, d->name = "Orbiter test box";
	d0.phys = "usb-1/input0", d1.phys = "usb-1/input1";
	w.vendor = 0x4098, w.product = 0xbea8, w.name = "Winwing WINWING Orion Joystick Base 2 + JGRIP-F16", w.uniq = "26CE", w.phys = "usb-0000:00:14.0-8/input0";
	std::map<int,int> m = InputMatchDevices (rd.defs, {&d0, &d1, &w}, {&w});
	REQUIRE(m[11] == 0); // Box1 is the one on input1
	REQUIRE(m[10] == 1);
	REQUIRE(m[12] == 2); // the old list's first stick
	DeviceDef n = InputDefFor (w, rd.defs);
	REQUIRE(n.name == "Stick2"); // Stick is taken
	REQUIRE(n.serial == "26CE");
	REQUIRE(n.iface == "input0");
	auto named = [&](const char *product, const std::vector<DeviceDef> &defs) {
		InputDevice x = Dev (13, 4, {});
		x.name = product;
		return InputDefFor (x, defs).name;
	};
	REQUIRE(named ("Winwing WINWING Orion Throttle Base II + F15EX HANDLE L + F15EX HANDLE R", rd.defs) == "Throttle");
	REQUIRE(named ("Saitek Saitek Pro Flight Rudder Pedals", rd.defs) == "Pedals");
	REQUIRE(named ("Saitek Pro Flight Switch Panel", rd.defs) == "Panel");
	REQUIRE(named ("Xbox Wireless Controller", rd.defs) == "Gamepad");
	DeviceDef thr;
	thr.name = "Throttle";
	REQUIRE(named ("Thrustmaster TWCS Throttle", {thr}) == "Throttle2");
	REQUIRE(named ("Saitek X52 Flight Control System", rd.defs) == "X52FlightControlSystem");
	REQUIRE(named ("VKB-Sim Gladiator EVO R", rd.defs) == "SimGladiatorEVOR");
	REQUIRE(named ("Acme Super Long Name Device With Many Words In It", rd.defs) == "SuperLongNameDeviceWithMany"); // whole words, 28 at most
	std::string cmd = "rm -rf '" + dir + "'";
	(void)!system (cmd.c_str());
}

TEST_CASE("Global.cfg made from Orbiter.cfg's joystick settings", "[bindings]")
{
	char tmpl[] = "/tmp/inputbind-XXXXXX";
	std::string dir = mkdtemp (tmpl), sys = dir + "/sys", dev = dir + "/dev";
	mkdir (sys.c_str(), 0755);
	mkdir (dev.c_str(), 0755);
	InputDevices devs (sys, dev); // no controllers: the stick comes later
	InputSystem is (dir + "/Input", &devs);
	is.Migrate ({1, 2500, 2, 9500, true});
	std::ifstream gf (dir + "/Input/Global.cfg");
	std::stringstream gs;
	gs << gf.rdbuf();
	std::string g = gs.str();
	REQUIRE(g.find ("BIND Stick.X = AXIS Bank DZ 0.25\n") != std::string::npos);
	REQUIRE(g.find ("BIND Stick.X @1 = AXIS Yaw DZ 0.25\n") != std::string::npos);
	REQUIRE(g.find ("BIND Stick.Y = AXIS TransY DZ 0.25\n") != std::string::npos);
	REQUIRE(g.find ("BIND Stick.RZ = AXIS TransX DZ 0.25\n") != std::string::npos);
	REQUIRE(g.find ("BIND Stick.B3 = LAYER 1\n") != std::string::npos);
	REQUIRE(g.find ("BIND Stick.THROTTLE = AXIS Main INVERT DZ 0.025 SAT 0.975 PICKUP\n") != std::string::npos);
	REQUIRE(g.find ("BIND Stick.HAT0.LEFT @1 = AXIS PanX -1\n") != std::string::npos);
	DevicesFile df;
	REQUIRE(InputReadDevices (dir + "/Input/Devices.cfg", df));
	REQUIRE((df.defs.size() == 1 && df.defs[0].name == "Stick" && df.defs[0].legacyIndex == 1));
	is.Migrate ({0, 0, 0, 0, false}); // only once
	is.Load ();
	is.SetProfile ("DeltaGlider", nullptr); // no profile of its own: Global only, nothing to say
	std::vector<std::string> msg = is.Messages();
	REQUIRE(msg.size() == 1);
	REQUIRE(msg[0].find ("made from Orbiter.cfg") != std::string::npos);
	mkdir ((dir + "/Input/Vessels").c_str(), 0755);
	std::ofstream (dir + "/Input/Vessels/A.cfg") << "INHERIT Vessels/B\nBIND Stick.B1 = KEY A\n";
	std::ofstream (dir + "/Input/Vessels/B.cfg") << "INHERIT Vessels/A\nBIND Stick.B2 = KEY B\n";
	std::ofstream (dir + "/Input/Vessels/C.cfg") << "INHERIT Vessels/Nothing\n";
	is.SetProfile ("A", nullptr);
	msg = is.Messages();
	REQUIRE((msg.size() == 1 && msg[0].find ("circle at Vessels/A") != std::string::npos));
	is.SetProfile ("C", nullptr);
	msg = is.Messages();
	REQUIRE((msg.size() == 1 && msg[0].find ("INHERIT Vessels/Nothing") != std::string::npos));
	std::string cmd = "rm -rf '" + dir + "'";
	(void)!system (cmd.c_str());
}

// a joystick-class controller (X Y Z RZ, a hat, 12 buttons) in a fake sysfs, its node an empty file
static void StickNode (const std::string &sys, const std::string &dev, int n, const char *name = "Generic USB Joystick", const char *vid = "0079", const char *pid = "0006")
{
	std::string d = sys + "/event" + std::to_string (n) + "/device/";
	for (std::string p : {sys + "/event" + std::to_string (n), d, d + "id", d + "capabilities"}) mkdir (p.c_str(), 0755);
	auto put = [](const std::string &path, const std::string &text) { std::ofstream (path) << text << "\n"; };
	put (d + "name", name);
	put (d + "uniq", "");
	put (d + "phys", "usb-0000:00:14.0-" + std::to_string (n) + "/input0");
	put (d + "id/vendor", vid);
	put (d + "id/product", pid);
	put (d + "id/version", "0110");
	put (d + "capabilities/ev", "1b");
	put (d + "capabilities/key", "fff00000000 0 0 0 0");
	put (d + "capabilities/abs", "30027");
	put (d + "capabilities/rel", "0");
	put (d + "properties", "0");
	std::ofstream (dev + "/event" + std::to_string (n));
}

TEST_CASE("Global.cfg binds the first joystick when Orbiter.cfg names none", "[bindings]")
{
	char tmpl[] = "/tmp/inputbind-XXXXXX";
	std::string dir = mkdtemp (tmpl), sys = dir + "/sys", dev = dir + "/dev";
	mkdir (sys.c_str(), 0755);
	mkdir (dev.c_str(), 0755);
	auto read = [&](const char *f) { std::ifstream in (dir + "/Input/" + f); std::stringstream s; s << in.rdbuf(); return s.str(); };
	{
		InputDevices devs (sys, dev); // nothing connected: Global.cfg made empty
		InputSystem is (dir + "/Input", &devs);
		is.Migrate ({0, 1000, 0, 0, false});
		std::string empty = read ("Global.cfg");
		REQUIRE((!empty.empty() && empty.find ("\nBIND ") == std::string::npos));
		InputSystem again (dir + "/Input", &devs);
		again.Migrate ({0, 1000, 0, 0, false}); // still none: left as it is, no note
		REQUIRE(read ("Global.cfg") == empty);
		REQUIRE(again.Messages().empty());
	}
	StickNode (sys, dev, 5, "Winwing WINWING Orion Throttle Base II + F15EX HANDLE L + F15EX HANDLE R", "4098", "bd64");
	{
		InputDevices devs (sys, dev); // a throttle alone: no stick to bind
		devs.Scan ();
		InputSystem is (dir + "/Input", &devs);
		std::string empty = read ("Global.cfg");
		is.Migrate ({0, 1000, 0, 0, false});
		REQUIRE(read ("Global.cfg") == empty);
		REQUIRE(is.Messages().empty());
	}
	StickNode (sys, dev, 7); // the stick after the throttle: the stick is taken
	InputDevices devs (sys, dev);
	devs.Scan ();
	InputSystem is (dir + "/Input", &devs);
	is.Migrate ({0, 1000, 0, 0, false}); // the stick is there: made again with it
	std::string g = read ("Global.cfg");
	REQUIRE(g.find ("BIND Stick.X = AXIS Bank DZ 0.1\n") != std::string::npos);
	REQUIRE(g.find ("BIND Stick.HAT0.LEFT @1 = AXIS PanX -1\n") != std::string::npos);
	DevicesFile df;
	REQUIRE(InputReadDevices (dir + "/Input/Devices.cfg", df));
	REQUIRE((df.defs.size() == 1 && df.defs[0].name == "Stick" && df.defs[0].vid == 0x0079 && df.defs[0].pid == 0x0006));
	std::vector<std::string> msg = is.Messages();
	REQUIRE((msg.size() == 1 && msg[0].find ("the first joystick connected, Generic USB Joystick, as Stick") != std::string::npos));
	is.Load ();
	InputProfile reset;
	REQUIRE(is.DefaultGlobal (reset).empty()); // the editor's reset takes it too
	REQUIRE(reset.binds.size() == 15);
	is.Migrate ({0, 1000, 0, 0, false}); // bound now: kept
	REQUIRE(read ("Global.cfg") == g);
	std::ofstream (dir + "/Input/Global.cfg") << "; mine, nothing bound\n";
	is.Migrate ({0, 1000, 0, 0, false}); // edited: kept
	REQUIRE(read ("Global.cfg") == "; mine, nothing bound\n");
	std::string cmd = "rm -rf '" + dir + "'";
	(void)!system (cmd.c_str());
}
