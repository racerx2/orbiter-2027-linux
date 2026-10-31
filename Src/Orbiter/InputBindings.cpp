// not upstream: keys, buttons, axes and switches bound to Orbiter's functions, keys and controls (input-bindings)

#include "InputBindings.h"
#include "Di7frame.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <linux/input.h>

// ====================================================================================
// channels

static const std::vector<InputChanInfo> &Chans ()
{
	static std::vector<InputChanInfo> c;
	if (c.empty()) {
		c.resize (ICH_COUNT);
		auto set = [](InputChanInfo &i, const std::string &n, bool abs, bool uni) { i.name = n, i.absolute = abs, i.unipolar = uni; };
		set (c[ICH_PITCH], "Pitch", false, false);
		set (c[ICH_YAW], "Yaw", false, false);
		set (c[ICH_BANK], "Bank", false, false);
		set (c[ICH_TRANSX], "TransX", false, false);
		set (c[ICH_TRANSY], "TransY", false, false);
		set (c[ICH_TRANSZ], "TransZ", false, false);
		set (c[ICH_BRAKEL], "BrakeL", false, true);
		set (c[ICH_BRAKER], "BrakeR", false, true);
		set (c[ICH_BRAKES], "Brakes", false, true);
		set (c[ICH_LOOKX], "LookX", false, false);
		set (c[ICH_LOOKY], "LookY", false, false);
		set (c[ICH_PANX], "PanX", false, false);
		set (c[ICH_PANY], "PanY", false, false);
		set (c[ICH_FOV], "FOV", false, false);
		set (c[ICH_DIST], "Distance", false, false);
		set (c[ICH_MAIN], "Main", true, true);
		set (c[ICH_MAINRETRO], "MainRetro", true, false);
		set (c[ICH_RETRO], "Retro", true, true);
		set (c[ICH_HOVER], "Hover", true, true);
		for (int i = 0; i < 8; i++) {
			set (c[ICH_MAIN1+i], "Main" + std::to_string (i+1), true, true);
			set (c[ICH_RETRO1+i], "Retro" + std::to_string (i+1), true, true);
			set (c[ICH_HOVER1+i], "Hover" + std::to_string (i+1), true, true);
		}
		set (c[ICH_ELEVTRIM], "ElevTrim", true, false);
		set (c[ICH_RUDDERTRIM], "RudderTrim", true, false);
		set (c[ICH_FLAPS], "Flaps", true, true);
	}
	return c;
}

const InputChanInfo &InputChannel (int ch)
{
	return Chans()[ch];
}

int InputChannelByName (const std::string &name)
{
	for (int i = 0; i < ICH_COUNT; i++) if (!strcasecmp (Chans()[i].name.c_str(), name.c_str())) return i;
	return -1;
}

// ====================================================================================
// names

static const struct { int dik; const char *name; } dikname[] = {
	{OAPI_KEY_ESCAPE,"ESC"}, {OAPI_KEY_1,"1"}, {OAPI_KEY_2,"2"}, {OAPI_KEY_3,"3"}, {OAPI_KEY_4,"4"}, {OAPI_KEY_5,"5"},
	{OAPI_KEY_6,"6"}, {OAPI_KEY_7,"7"}, {OAPI_KEY_8,"8"}, {OAPI_KEY_9,"9"}, {OAPI_KEY_0,"0"}, {OAPI_KEY_MINUS,"MINUS"},
	{OAPI_KEY_EQUALS,"EQUALS"}, {OAPI_KEY_BACK,"BACK"}, {OAPI_KEY_TAB,"TAB"}, {OAPI_KEY_Q,"Q"}, {OAPI_KEY_W,"W"},
	{OAPI_KEY_E,"E"}, {OAPI_KEY_R,"R"}, {OAPI_KEY_T,"T"}, {OAPI_KEY_Y,"Y"}, {OAPI_KEY_U,"U"}, {OAPI_KEY_I,"I"},
	{OAPI_KEY_O,"O"}, {OAPI_KEY_P,"P"}, {OAPI_KEY_LBRACKET,"LBRACKET"}, {OAPI_KEY_RBRACKET,"RBRACKET"},
	{OAPI_KEY_RETURN,"RETURN"}, {OAPI_KEY_LCONTROL,"LCONTROL"}, {OAPI_KEY_A,"A"}, {OAPI_KEY_S,"S"}, {OAPI_KEY_D,"D"},
	{OAPI_KEY_F,"F"}, {OAPI_KEY_G,"G"}, {OAPI_KEY_H,"H"}, {OAPI_KEY_J,"J"}, {OAPI_KEY_K,"K"}, {OAPI_KEY_L,"L"},
	{OAPI_KEY_SEMICOLON,"SEMICOLON"}, {OAPI_KEY_APOSTROPHE,"APOSTROPHE"}, {OAPI_KEY_GRAVE,"GRAVE"},
	{OAPI_KEY_LSHIFT,"LSHIFT"}, {OAPI_KEY_BACKSLASH,"BACKSLASH"}, {OAPI_KEY_Z,"Z"}, {OAPI_KEY_X,"X"}, {OAPI_KEY_C,"C"},
	{OAPI_KEY_V,"V"}, {OAPI_KEY_B,"B"}, {OAPI_KEY_N,"N"}, {OAPI_KEY_M,"M"}, {OAPI_KEY_COMMA,"COMMA"},
	{OAPI_KEY_PERIOD,"PERIOD"}, {OAPI_KEY_SLASH,"SLASH"}, {OAPI_KEY_RSHIFT,"RSHIFT"}, {OAPI_KEY_MULTIPLY,"MULTIPLY"},
	{OAPI_KEY_LALT,"LALT"}, {OAPI_KEY_SPACE,"SPACE"}, {OAPI_KEY_CAPITAL,"CAPITAL"}, {OAPI_KEY_F1,"F1"},
	{OAPI_KEY_F2,"F2"}, {OAPI_KEY_F3,"F3"}, {OAPI_KEY_F4,"F4"}, {OAPI_KEY_F5,"F5"}, {OAPI_KEY_F6,"F6"},
	{OAPI_KEY_F7,"F7"}, {OAPI_KEY_F8,"F8"}, {OAPI_KEY_F9,"F9"}, {OAPI_KEY_F10,"F10"}, {OAPI_KEY_NUMLOCK,"NUMLOCK"},
	{OAPI_KEY_SCROLL,"SCROLL"}, {OAPI_KEY_NUMPAD7,"NUMPAD7"}, {OAPI_KEY_NUMPAD8,"NUMPAD8"}, {OAPI_KEY_NUMPAD9,"NUMPAD9"},
	{OAPI_KEY_SUBTRACT,"SUBTRACT"}, {OAPI_KEY_NUMPAD4,"NUMPAD4"}, {OAPI_KEY_NUMPAD5,"NUMPAD5"}, {OAPI_KEY_NUMPAD6,"NUMPAD6"},
	{OAPI_KEY_ADD,"ADD"}, {OAPI_KEY_NUMPAD1,"NUMPAD1"}, {OAPI_KEY_NUMPAD2,"NUMPAD2"}, {OAPI_KEY_NUMPAD3,"NUMPAD3"},
	{OAPI_KEY_NUMPAD0,"NUMPAD0"}, {OAPI_KEY_DECIMAL,"DECIMAL"}, {OAPI_KEY_OEM_102,"OEM_102"}, {OAPI_KEY_F11,"F11"},
	{OAPI_KEY_F12,"F12"}, {0x64,"F13"}, {0x65,"F14"}, {0x66,"F15"}, {0x8D,"NUMPADEQUALS"}, {0xA0,"MUTE"},
	{0xAE,"VOLUMEDOWN"}, {0xB0,"VOLUMEUP"}, {0xB3,"NUMPADCOMMA"}, {OAPI_KEY_NUMPADENTER,"NUMPADENTER"},
	{OAPI_KEY_RCONTROL,"RCONTROL"}, {OAPI_KEY_DIVIDE,"DIVIDE"}, {OAPI_KEY_SYSRQ,"SYSRQ"}, {OAPI_KEY_RALT,"RALT"},
	{OAPI_KEY_PAUSE,"PAUSE"}, {OAPI_KEY_HOME,"HOME"}, {OAPI_KEY_UP,"UP"}, {OAPI_KEY_PRIOR,"PGUP"},
	{OAPI_KEY_LEFT,"LEFT"}, {OAPI_KEY_RIGHT,"RIGHT"}, {OAPI_KEY_END,"END"}, {OAPI_KEY_DOWN,"DOWN"},
	{OAPI_KEY_NEXT,"PGDOWN"}, {OAPI_KEY_INSERT,"INSERT"}, {OAPI_KEY_DELETE,"DELETE"}, {0xDB,"LWIN"}, {0xDC,"RWIN"},
	{0xDD,"APPS"}
};

std::string InputKeyName (int dik)
{
	for (auto &k : dikname) if (k.dik == dik) return k.name;
	char buf[16];
	snprintf (buf, sizeof buf, "0x%02X", dik & 0xFF);
	return buf;
}

int InputKeyByName (const std::string &name)
{
	for (auto &k : dikname) if (!strcasecmp (k.name, name.c_str())) return k.dik;
	if (!strcasecmp (name.c_str(), "PRIOR")) return OAPI_KEY_PRIOR;
	if (!strcasecmp (name.c_str(), "NEXT")) return OAPI_KEY_NEXT;
	if (!strcasecmp (name.c_str(), "ESCAPE")) return OAPI_KEY_ESCAPE;
	if (name.size() > 2 && name[0] == '0' && (name[1] == 'x' || name[1] == 'X')) {
		long v = strtol (name.c_str()+2, nullptr, 16);
		if (v > 0 && v < 256) return (int)v;
	}
	return 0;
}

std::string InputEvKeyName (int evcode)
{
	DWORD dik = KeyboardDevice::DIKCode (evcode);
	if (dik) return InputKeyName ((int)dik);
	if (evcode >= KEY_F16 && evcode <= KEY_F24) return "F" + std::to_string (16 + evcode - KEY_F16);
	if (evcode >= KEY_MACRO1 && evcode <= KEY_MACRO30) return "MACRO" + std::to_string (1 + evcode - KEY_MACRO1);
	return "K" + std::to_string (evcode);
}

int InputEvKeyByName (const std::string &name)
{
	int dik = InputKeyByName (name);
	if (dik) {
		for (int c = 1; c <= KEY_MAX; c++) if ((int)KeyboardDevice::DIKCode (c) == dik) return c;
		return 0;
	}
	if (name.size() >= 3 && toupper (name[0]) == 'F') {
		int n = atoi (name.c_str()+1);
		if (n >= 16 && n <= 24) return KEY_F16 + n - 16;
	}
	if (!strncasecmp (name.c_str(), "MACRO", 5)) {
		int n = atoi (name.c_str()+5);
		if (n >= 1 && n <= 30) return KEY_MACRO1 + n - 1;
	}
	if (name.size() > 1 && toupper (name[0]) == 'K' && isdigit ((unsigned char)name[1])) {
		int n = atoi (name.c_str()+1);
		if (n > 0 && n <= KEY_MAX) return n;
	}
	return 0;
}

static const struct { int code; const char *name; } absname[] = {
	{ABS_X,"X"}, {ABS_Y,"Y"}, {ABS_Z,"Z"}, {ABS_RX,"RX"}, {ABS_RY,"RY"}, {ABS_RZ,"RZ"}, {ABS_THROTTLE,"THROTTLE"},
	{ABS_RUDDER,"RUDDER"}, {ABS_WHEEL,"WHEEL"}, {ABS_GAS,"GAS"}, {ABS_BRAKE,"BRAKE"}, {ABS_PRESSURE,"PRESSURE"},
	{ABS_DISTANCE,"DISTANCE"}, {ABS_TILT_X,"TILT_X"}, {ABS_TILT_Y,"TILT_Y"}, {ABS_TOOL_WIDTH,"TOOL_WIDTH"},
	{ABS_VOLUME,"VOLUME"}, {ABS_MISC,"MISC"}
};

std::string InputAbsName (int code)
{
	for (auto &a : absname) if (a.code == code) return a.name;
	return "A" + std::to_string (code);
}

int InputAbsByName (const std::string &name)
{
	for (auto &a : absname) if (!strcasecmp (a.name, name.c_str())) return a.code;
	if (name.size() > 1 && toupper (name[0]) == 'A' && isdigit ((unsigned char)name[1])) {
		int n = atoi (name.c_str()+1);
		if (n >= 0 && n < ABS_CNT && !(n >= ABS_HAT0X && n <= ABS_HAT3Y)) return n;
	}
	return -1;
}

static const char *relname[] = { "X", "Y", "Z", "RX", "RY", "RZ", "HWHEEL", "DIAL", "WHEEL", "MISC" };

static std::string RelName (int code)
{
	return (code >= 0 && code < 10 ? relname[code] : "R" + std::to_string (code));
}

static int RelByName (const std::string &name)
{
	for (int i = 0; i < 10; i++) if (!strcasecmp (relname[i], name.c_str())) return i;
	if (name.size() > 1 && toupper (name[0]) == 'R' && isdigit ((unsigned char)name[1])) {
		int n = atoi (name.c_str()+1);
		if (n >= 0 && n < REL_CNT) return n;
	}
	return -1;
}

static const struct { uint16_t all, left, right; const char *name; } modgroup[] = {
	{KMOD_SHIFT, KMOD_LSHIFT, KMOD_RSHIFT, "SHIFT"},
	{KMOD_CTRL, KMOD_LCTRL, KMOD_RCTRL, "CTRL"},
	{KMOD_ALT, KMOD_LALT, KMOD_RALT, "ALT"}
};

std::string InputModsName (uint16_t mods)
{
	std::string s;
	for (auto &g : modgroup) {
		uint16_t m = mods & g.all;
		if (m == g.all) s += std::string (" ") + g.name;
		else if (m == g.left) s += std::string (" L") + g.name;
		else if (m == g.right) s += std::string (" R") + g.name;
	}
	return s;
}

static bool ModToken (const std::string &t, uint16_t &mods)
{
	for (auto &g : modgroup) {
		if (!strcasecmp (t.c_str(), g.name)) { mods |= g.all; return true; }
		if (t.size() == strlen (g.name)+1 && !strcasecmp (t.c_str()+1, g.name)) {
			if (toupper (t[0]) == 'L') { mods |= g.left; return true; }
			if (toupper (t[0]) == 'R') { mods |= g.right; return true; }
		}
	}
	return false;
}

bool InputModsMatch (uint16_t want, uint16_t have)
{
	for (auto &g : modgroup) {
		uint16_t w = want & g.all, h = have & g.all;
		if (!w) { if (h) return false; }
		else if (w == g.all) { if (!h) return false; }
		else if (!(h & w)) return false;
	}
	return true;
}

// ====================================================================================
// binding text

static std::vector<std::string> Tokens (const std::string &line)
{
	std::vector<std::string> t;
	size_t i = 0, n = line.size();
	while (i < n) {
		while (i < n && isspace ((unsigned char)line[i])) i++;
		if (i >= n || line[i] == ';') break;
		if (line[i] == '"') {
			size_t j = line.find ('"', i+1);
			if (j == std::string::npos) j = n;
			t.push_back ("\"" + line.substr (i+1, j-i-1));
			i = j+1;
		} else if (line[i] == '=') {
			t.push_back ("=");
			i++;
		} else {
			size_t j = i;
			while (j < n && !isspace ((unsigned char)line[j]) && line[j] != '=' && line[j] != ';') j++;
			t.push_back (line.substr (i, j-i));
			i = j;
		}
	}
	return t;
}

static bool Unquote (const std::string &t, std::string &s)
{
	if (t.empty() || t[0] != '"') return false;
	s = t.substr (1);
	return true;
}

static bool Num (const std::string &t, double &v)
{
	if (t.empty()) return false;
	char *end;
	v = strtod (t.c_str(), &end);
	return *end == '\0';
}

static bool Upper (const std::string &a, const char *b)
{
	return !strcasecmp (a.c_str(), b);
}

bool InputValidName (const std::string &s)
{
	if (s.empty() || s.size() > 32) return false;
	for (char c : s) if (!isalnum ((unsigned char)c) && c != '_' && c != '-') return false;
	return !Upper (s, "Key") && !Upper (s, "Mouse");
}
static bool ValidName (const std::string &s) { return InputValidName (s); }

static const char *hatdir[] = { "UP", "RIGHT", "DOWN", "LEFT" };

static bool ParseInput (const std::string &t, InputRef &r, std::string &err)
{
	size_t dot = t.find ('.');
	if (dot == std::string::npos || dot == 0 || dot+1 >= t.size()) { err = "input \"" + t + "\": device.element expected"; return false; }
	std::string dev = t.substr (0, dot), el = t.substr (dot+1);
	if (Upper (dev, "Key")) {
		r.src = INSRC_KEY, r.kind = INK_KEY;
		r.code = InputEvKeyByName (el);
		if (!r.code) { err = "unknown key \"" + el + "\""; return false; }
		return true;
	}
	if (Upper (dev, "Mouse")) {
		r.src = INSRC_MOUSE;
		static const char *mb[] = { "LEFT", "RIGHT", "MIDDLE", "BACK", "FORWARD" };
		static const char *wh[] = { "WHEELUP", "WHEELDOWN", "WHEELLEFT", "WHEELRIGHT" };
		for (int i = 0; i < 5; i++) if (Upper (el, mb[i])) { r.kind = INK_MBUTTON, r.code = i+1; return true; }
		for (int i = 0; i < 4; i++) if (Upper (el, wh[i])) { r.kind = INK_WHEEL, r.code = i; return true; }
		if (el.size() > 1 && toupper (el[0]) == 'B' && isdigit ((unsigned char)el[1])) {
			int n = atoi (el.c_str()+1);
			if (n >= 1 && n <= 27) { r.kind = INK_MBUTTON, r.code = n; return true; }
		}
		err = "unknown mouse input \"" + el + "\"";
		return false;
	}
	if (!ValidName (dev)) { err = "device name \"" + dev + "\": letters, digits, _ and - (not Key or Mouse)"; return false; }
	r.src = INSRC_DEV, r.dev = dev;
	if (el.size() > 1 && toupper (el[0]) == 'B' && isdigit ((unsigned char)el[1])) {
		int n = atoi (el.c_str()+1);
		if (n < 1 || n > KEY_CNT) { err = "button number " + el; return false; }
		r.kind = INK_BUTTON, r.code = n-1;
		return true;
	}
	if (!strncasecmp (el.c_str(), "HAT", 3) && el.size() > 5 && isdigit ((unsigned char)el[3]) && el[4] == '.') {
		r.kind = INK_HAT, r.code = el[3]-'0';
		if (r.code > 3) { err = "hat " + el; return false; }
		for (int i = 0; i < 4; i++) if (Upper (el.substr (5), hatdir[i])) { r.dir = i; return true; }
		err = "hat direction \"" + el.substr (5) + "\": UP, RIGHT, DOWN or LEFT";
		return false;
	}
	if (!strncasecmp (el.c_str(), "REL_", 4) && el.size() > 5 && (el.back() == '+' || el.back() == '-')) {
		r.kind = INK_REL, r.code = RelByName (el.substr (4, el.size()-5)), r.dir = (el.back() == '+' ? 1 : -1);
		if (r.code < 0) { err = "unknown relative axis \"" + el + "\""; return false; }
		return true;
	}
	r.kind = INK_AXIS, r.code = InputAbsByName (el);
	if (r.code < 0) { err = "unknown element \"" + el + "\""; return false; }
	return true;
}

static std::string FormatInput (const InputRef &r)
{
	switch (r.kind) {
	case INK_KEY: return "Key." + InputEvKeyName (r.code);
	case INK_MBUTTON: {
		static const char *mb[] = { "", "LEFT", "RIGHT", "MIDDLE", "BACK", "FORWARD" };
		return std::string ("Mouse.") + (r.code <= 5 ? std::string (mb[r.code]) : "B" + std::to_string (r.code));
	}
	case INK_WHEEL: {
		static const char *wh[] = { "WHEELUP", "WHEELDOWN", "WHEELLEFT", "WHEELRIGHT" };
		return std::string ("Mouse.") + wh[r.code & 3];
	}
	case INK_BUTTON: return r.dev + ".B" + std::to_string (r.code+1);
	case INK_HAT: return r.dev + ".HAT" + std::to_string (r.code) + "." + hatdir[r.dir & 3];
	case INK_REL: return r.dev + ".REL_" + RelName (r.code) + (r.dir > 0 ? "+" : "-");
	default: return r.dev + "." + InputAbsName (r.code);
	}
}

static std::string Fmt (double v)
{
	char buf[32];
	snprintf (buf, sizeof buf, "%.6g", v);
	return buf;
}

static bool HeldFunc (int f)
{
	return (f >= OAPI_LKEY_CockpitRotateLeft && f <= OAPI_LKEY_CockpitRotateDown) ||
		(f >= OAPI_LKEY_PanelShiftLeft && f <= OAPI_LKEY_PanelShiftDown) ||
		(f >= OAPI_LKEY_TrackRotateLeft && f <= OAPI_LKEY_DecHoverThrust) ||
		(f >= OAPI_LKEY_RCSPitchUp && f <= OAPI_LKEY_LPRCSBack) ||
		(f >= OAPI_LKEY_IncElevatorTrim && f <= OAPI_LKEY_WheelbrakeRight) ||
		f == OAPI_LKEY_IncFOV || f == OAPI_LKEY_DecFOV;
}

bool InputParseBinding (const std::string &text, Binding &b, std::string &err)
{
	b = Binding();
	std::vector<std::string> t = Tokens (text);
	size_t eq = std::find (t.begin(), t.end(), "=") - t.begin();
	if (t.empty() || eq == 0 || eq >= t.size()) { err = "BIND <input> [options] = <output> expected"; return false; }
	if (!ParseInput (t[0], b.in, err)) return false;
	bool isAxis = (b.in.kind == INK_AXIS);

	for (size_t i = 1; i < eq; i++) {
		const std::string &s = t[i];
		double v;
		if (s[0] == '@') {
			b.layer = atoi (s.c_str()+1);
			if (b.layer < 1 || b.layer > 8) { err = "layer " + s + ": @1 to @8"; return false; }
		} else if (ModToken (s, b.in.mods)) {
			if (b.in.src == INSRC_DEV) { err = s + ": modifiers are for Key and Mouse inputs (use a layer)"; return false; }
		} else if (Upper (s, "ZONE")) {
			double lo, hi;
			if (!isAxis || i+2 >= eq || !Num (t[i+1], lo) || !Num (t[i+2], hi) || lo < -1 || hi > 1 || lo >= hi) {
				err = "ZONE <from> <to> on an axis, -1 to 1"; return false;
			}
			b.in.zone = true, b.in.zlo = lo, b.in.zhi = hi;
			i += 2;
		} else if (Upper (s, "HOLD")) b.mode = IMODE_HOLD;
		else if (Upper (s, "PRESS")) b.mode = IMODE_PRESS;
		else if (Upper (s, "RELEASE")) b.mode = IMODE_RELEASE;
		else if (Upper (s, "BOTH")) b.mode = IMODE_BOTH;
		else if (Upper (s, "TOGGLE")) b.mode = IMODE_TOGGLE;
		else if (Upper (s, "REPEAT")) {
			b.mode = IMODE_REPEAT;
			if (i+1 < eq && Num (t[i+1], v)) {
				if (v <= 0 || v > 100) { err = "REPEAT rate: 0 to 100 per second"; return false; }
				b.rate = v, i++;
			}
		} else if (Upper (s, "PASS")) {
			if (b.in.src == INSRC_DEV) { err = "PASS is for Key and Mouse inputs"; return false; }
			b.pass = true;
		} else if (Upper (s, "VIEW")) {
			if (i+1 >= eq) { err = "VIEW COCKPIT or VIEW OUTSIDE"; return false; }
			if (Upper (t[i+1], "COCKPIT")) b.view = 1;
			else if (Upper (t[i+1], "OUTSIDE")) b.view = 2;
			else { err = "VIEW COCKPIT or VIEW OUTSIDE"; return false; }
			i++;
		} else { err = "unknown option \"" + s + "\" before ="; return false; }
	}
	bool wholeAxis = isAxis && !b.in.zone;
	if (wholeAxis && b.mode != IMODE_AUTO) { err = "button modes need a ZONE on an axis"; return false; }
	if (b.in.kind == INK_KEY) {
		int dik = (int)KeyboardDevice::DIKCode (b.in.code);
		if (dik == OAPI_KEY_LSHIFT || dik == OAPI_KEY_RSHIFT || dik == OAPI_KEY_LCONTROL || dik == OAPI_KEY_RCONTROL ||
			dik == OAPI_KEY_LALT || dik == OAPI_KEY_RALT) { err = "a modifier key is a modifier: bind it with another key"; return false; }
	}
	if (b.in.kind == INK_MBUTTON && b.in.code <= 2 && !b.in.mods && !b.layer) {
		err = "Mouse.LEFT and Mouse.RIGHT work the panels: give them a modifier or a layer"; return false;
	}

	size_t i = eq+1;
	if (i >= t.size()) { err = "output expected after ="; return false; }
	const std::string &o = t[i++];
	if (Upper (o, "NONE")) b.out = IOUT_NONE;
	else if (Upper (o, "FUNC")) {
		if (i >= t.size()) { err = "FUNC <function>"; return false; }
		b.out = IOUT_FUNC, b.func = Keymap::FuncByName (t[i].c_str());
		if (b.func < 0) { err = "unknown function \"" + t[i] + "\""; return false; }
		i++;
	} else if (Upper (o, "KEY")) {
		if (i >= t.size()) { err = "KEY <key> [modifiers]"; return false; }
		b.out = IOUT_KEY, b.key = InputKeyByName (t[i]);
		if (!b.key) { err = "unknown key \"" + t[i] + "\""; return false; }
		i++;
		while (i < t.size() && ModToken (t[i], b.kmods)) i++;
	} else if (Upper (o, "AXIS")) {
		if (i >= t.size()) { err = "AXIS <channel>"; return false; }
		b.out = IOUT_AXIS, b.chan = InputChannelByName (t[i]);
		if (b.chan < 0) { err = "unknown channel \"" + t[i] + "\""; return false; }
		i++;
		double v;
		if (i+1 < t.size() && Upper (t[i], "STEP") && Num (t[i+1], v)) b.amode = IAX_STEP, b.aval = v, i += 2;
		else if (i+1 < t.size() && Upper (t[i], "SET") && Num (t[i+1], v)) b.amode = IAX_SET, b.aval = v, i += 2;
		else if (i < t.size() && Num (t[i], v)) b.aval = v, i++;
	} else if (Upper (o, "MFD")) {
		if (i+1 >= t.size()) { err = "MFD <LEFT|RIGHT|1..12> <B1..B12|PWR|SEL|MNU>"; return false; }
		b.out = IOUT_MFD;
		if (Upper (t[i], "LEFT")) b.mfd = 0;
		else if (Upper (t[i], "RIGHT")) b.mfd = 1;
		else {
			b.mfd = atoi (t[i].c_str()) - 1;
			if (b.mfd < 0 || b.mfd >= MAXMFD) { err = "MFD LEFT, RIGHT or 1 to 12"; return false; }
		}
		const std::string &k = t[i+1];
		if (Upper (k, "PWR")) b.mfdbtn = IMFD_PWR;
		else if (Upper (k, "SEL")) b.mfdbtn = IMFD_SEL;
		else if (Upper (k, "MNU")) b.mfdbtn = IMFD_MNU;
		else if (k.size() > 1 && toupper (k[0]) == 'B' && atoi (k.c_str()+1) >= 1 && atoi (k.c_str()+1) <= 20) b.mfdbtn = atoi (k.c_str()+1) - 1;
		else { err = "MFD button B1..B20, PWR, SEL or MNU"; return false; }
		i += 2;
	} else if (Upper (o, "LAYER")) {
		if (i >= t.size() || atoi (t[i].c_str()) < 1 || atoi (t[i].c_str()) > 8) { err = "LAYER 1 to 8"; return false; }
		b.out = IOUT_LAYER, b.lnum = atoi (t[i].c_str());
		i++;
		if (i < t.size() && Upper (t[i], "LATCH")) b.latch = true, i++;
	} else { err = "unknown output \"" + o + "\": FUNC, KEY, AXIS, MFD, LAYER or NONE"; return false; }

	for (; i < t.size(); i++) {
		const std::string &s = t[i];
		double v;
		if (Upper (s, "INVERT")) b.invert = true;
		else if (Upper (s, "FULL")) b.half = false;
		else if (Upper (s, "HALF")) b.half = true;
		else if (Upper (s, "PICKUP")) b.pickup = true;
		else if ((Upper (s, "DZ") || Upper (s, "SAT") || Upper (s, "CURVE") || Upper (s, "SCALE")) && i+1 < t.size() && Num (t[i+1], v)) {
			if (Upper (s, "DZ")) b.dz = v;
			else if (Upper (s, "SAT")) b.sat = v;
			else if (Upper (s, "CURVE")) b.curve = v;
			else b.scale = v;
			i++;
		} else { err = "unknown option \"" + s + "\" after the output"; return false; }
	}
	if (b.dz < 0 || b.dz >= 1 || b.sat <= b.dz || b.sat > 1) { err = "DZ 0 to below 1, SAT above DZ up to 1"; return false; }
	if (b.curve <= 0 || b.curve > 10) { err = "CURVE above 0 up to 10"; return false; }

	if (b.out == IOUT_LAYER && b.layer) { err = "a LAYER binding is on the base layer"; return false; }
	if (wholeAxis && b.out != IOUT_AXIS && b.out != IOUT_NONE) { err = "an axis drives an AXIS channel (or use ZONE)"; return false; }
	if (wholeAxis && b.out == IOUT_AXIS && b.amode != IAX_VALUE) { err = "STEP and SET are for buttons"; return false; }
	if (b.out == IOUT_LAYER && wholeAxis) { err = "LAYER needs a button"; return false; }
	return true;
}

std::string InputFormatBinding (const Binding &b)
{
	std::string s = FormatInput (b.in);
	if (b.layer) s += " @" + std::to_string (b.layer);
	s += InputModsName (b.in.mods);
	if (b.in.zone) s += " ZONE " + Fmt (b.in.zlo) + " " + Fmt (b.in.zhi);
	static const char *mode[] = { "", " HOLD", " PRESS", " RELEASE", " BOTH", " TOGGLE", " REPEAT" };
	s += mode[b.mode];
	if (b.mode == IMODE_REPEAT && b.rate != 10.0) s += " " + Fmt (b.rate);
	if (b.view) s += (b.view == 1 ? " VIEW COCKPIT" : " VIEW OUTSIDE");
	if (b.pass) s += " PASS";
	s += " = ";
	switch (b.out) {
	case IOUT_NONE: s += "NONE"; break;
	case IOUT_FUNC: s += std::string ("FUNC ") + Keymap::FuncName (b.func); break;
	case IOUT_KEY: s += "KEY " + InputKeyName (b.key) + InputModsName (b.kmods); break;
	case IOUT_AXIS:
		s += "AXIS " + InputChannel (b.chan).name;
		if (b.amode == IAX_STEP) s += " STEP " + Fmt (b.aval);
		else if (b.amode == IAX_SET) s += " SET " + Fmt (b.aval);
		else if (!(b.in.kind == INK_AXIS && !b.in.zone)) s += (b.aval >= 0 ? " +" : " ") + Fmt (b.aval);
		break;
	case IOUT_MFD: {
		s += "MFD " + std::string (b.mfd == 0 ? "LEFT" : b.mfd == 1 ? "RIGHT" : std::to_string (b.mfd+1).c_str()) + " ";
		s += (b.mfdbtn == IMFD_PWR ? "PWR" : b.mfdbtn == IMFD_SEL ? "SEL" : b.mfdbtn == IMFD_MNU ? "MNU" : ("B" + std::to_string (b.mfdbtn+1)).c_str());
		break;
	}
	case IOUT_LAYER: s += "LAYER " + std::to_string (b.lnum) + (b.latch ? " LATCH" : ""); break;
	}
	if (b.invert) s += " INVERT";
	if (b.half) s += " HALF";
	if (b.dz) s += " DZ " + Fmt (b.dz);
	if (b.sat != 1.0) s += " SAT " + Fmt (b.sat);
	if (b.curve != 1.0) s += " CURVE " + Fmt (b.curve);
	if (b.scale != 1.0) s += " SCALE " + Fmt (b.scale);
	if (b.pickup) s += " PICKUP";
	return s;
}

bool InputSameInput (const Binding &a, const Binding &b)
{
	const InputRef &x = a.in, &y = b.in;
	return x.src == y.src && x.kind == y.kind && x.code == y.code && x.dir == y.dir && x.mods == y.mods &&
		!strcasecmp (x.dev.c_str(), y.dev.c_str()) && x.zone == y.zone &&
		(!x.zone || (x.zlo == y.zlo && x.zhi == y.zhi)) && a.layer == b.layer && a.view == b.view;
}

// ====================================================================================
// files

static std::string Trim (const std::string &s)
{
	size_t a = s.find_first_not_of (" \t\r\n"), e = s.find_last_not_of (" \t\r\n");
	return (a == std::string::npos ? std::string() : s.substr (a, e-a+1));
}

bool InputWriteFile (const std::string &path, const std::string &text)
{
	for (size_t p = path.find ('/', 1); p != std::string::npos; p = path.find ('/', p+1))
		mkdir (path.substr (0, p).c_str(), 0755);
	std::string tmp = path + ".tmp";
	FILE *f = fopen (tmp.c_str(), "w");
	if (!f) return false;
	bool ok = (fwrite (text.data(), 1, text.size(), f) == text.size());
	ok = (fflush (f) == 0) && ok;
	ok = (fclose (f) == 0) && ok;
	if (!ok || rename (tmp.c_str(), path.c_str()) != 0) {
		remove (tmp.c_str());
		return false;
	}
	return true;
}

static size_t CommentAt (const std::string &s) // the ';' that starts a line's comment, outside quotes
{
	bool q = false;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == '"') q = !q;
		else if (s[i] == ';' && !q) return i;
	}
	return std::string::npos;
}

bool InputReadProfile (const std::string &path, InputProfile &p)
{
	std::ifstream f (path);
	if (!f) return false;
	p.binds.clear(), p.errors.clear(), p.inherit.clear(), p.head.clear(), p.trail.clear();
	p.read = true;
	std::string line, pend; // lines kept as they are, for the next binding
	bool started = false;
	for (int n = 1; std::getline (f, line); n++) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		std::string s = Trim (line);
		std::vector<std::string> t = Tokens (s);
		std::string where = path + ":" + std::to_string (n) + ": ";
		if (!t.empty() && Upper (t[0], "BIND")) {
			Binding b;
			std::string err;
			if (InputParseBinding (s.substr (4), b, err)) {
				b.line = n;
				size_t c = CommentAt (s);
				if (c != std::string::npos) b.tail = s.substr (c);
				if (started) b.note = pend;
				else p.head = pend;
				pend.clear();
				started = true;
				p.binds.push_back (b);
				continue;
			}
			p.errors.push_back (where + err);
		} else if (!t.empty() && Upper (t[0], "INHERIT") && t.size() == 2) {
			p.inherit = t[1];
			if (!started) p.head = pend, pend.clear();
			started = true;
			continue;
		} else if (!t.empty()) p.errors.push_back (where + "BIND or INHERIT expected");
		pend += line + "\n";
	}
	if (started) p.trail = pend;
	else p.head = pend;
	return true;
}

bool InputWriteProfile (const std::string &path, const InputProfile &p, const std::string &header)
{
	std::string s = (p.read ? p.head : header);
	if (!p.inherit.empty()) s += "INHERIT " + p.inherit + "\n";
	for (auto &b : p.binds) s += b.note + "BIND " + InputFormatBinding (b) + (b.tail.empty() ? "" : " " + b.tail) + "\n";
	s += p.trail;
	return InputWriteFile (path, s);
}

std::vector<Binding> InputMerge (const std::vector<const InputProfile*> &chain)
{
	std::vector<Binding> r;
	for (const InputProfile *p : chain) {
		if (!p) continue;
		r.erase (std::remove_if (r.begin(), r.end(), [&](const Binding &x) {
			for (auto &b : p->binds) if (InputSameInput (x, b)) return true;
			return false; }), r.end());
		for (auto &b : p->binds) if (b.out != IOUT_NONE) r.push_back (b);
	}
	return r;
}

static std::string Quote (const std::string &s)
{
	return "\"" + s + "\"";
}

bool InputReadDevices (const std::string &path, DevicesFile &df)
{
	std::ifstream f (path);
	if (!f) return false;
	df.defs.clear(), df.errors.clear();
	std::string line;
	for (int n = 1; std::getline (f, line); n++) {
		std::string s = Trim (line);
		if (s.empty() || s[0] == ';') continue;
		std::vector<std::string> t = Tokens (s);
		std::string where = path + ":" + std::to_string (n) + ": ";
		if (t.size() >= 4 && Upper (t[0], "DEVICE") && t[2] == "=") {
			DeviceDef d;
			d.name = t[1];
			if (!ValidName (d.name)) { df.errors.push_back (where + "device name \"" + d.name + "\""); continue; }
			bool dup = false;
			for (auto &o : df.defs) if (!strcasecmp (o.name.c_str(), d.name.c_str())) dup = true;
			if (dup) { df.errors.push_back (where + "device " + d.name + " twice"); continue; }
			if (Upper (t[3], "INDEX") && t.size() == 5) {
				d.legacyIndex = atoi (t[4].c_str());
				if (d.legacyIndex < 1) { df.errors.push_back (where + "INDEX 1 or more"); continue; }
			} else {
				unsigned v, p;
				if (sscanf (t[3].c_str(), "%x:%x", &v, &p) != 2 || t.size() < 5 || !Unquote (t[4], d.product)) {
					df.errors.push_back (where + "DEVICE <name> = <vendor>:<product> \"<product name>\" expected"); continue;
				}
				d.vid = v, d.pid = p;
				bool bad = false;
				for (size_t i = 5; i < t.size(); i += 2) {
					std::string v2;
					if (i+1 >= t.size()) { bad = true; break; }
					v2 = (t[i+1][0] == '"' ? t[i+1].substr (1) : t[i+1]);
					if (Upper (t[i], "SERIAL")) d.serial = v2;
					else if (Upper (t[i], "IFACE")) d.iface = v2;
					else if (Upper (t[i], "PORT")) d.port = v2;
					else bad = true;
				}
				if (bad) { df.errors.push_back (where + "SERIAL, IFACE or PORT with a value expected"); continue; }
			}
			df.defs.push_back (d);
		} else if (t.size() == 6 && Upper (t[0], "CAL") && t[2] == "=") {
			size_t dot = t[1].find ('.');
			int code = (dot == std::string::npos ? -1 : InputAbsByName (t[1].substr (dot+1)));
			DeviceDef *d = nullptr;
			if (dot != std::string::npos)
				for (auto &o : df.defs) if (!strcasecmp (o.name.c_str(), t[1].substr (0, dot).c_str())) d = &o;
			DeviceCal c;
			c.code = code;
			c.min = atoi (t[3].c_str()), c.center = atoi (t[4].c_str()), c.max = atoi (t[5].c_str());
			if (!d || code < 0 || !(c.min < c.center && c.center < c.max)) {
				df.errors.push_back (where + "CAL <device>.<axis> = <min> <center> <max> after its DEVICE"); continue;
			}
			d->cal.push_back (c);
		} else df.errors.push_back (where + "DEVICE or CAL expected");
	}
	return true;
}

bool InputWriteDevices (const std::string &path, const DevicesFile &df)
{
	std::string s =
		"; Orbiter controllers, one line per device (Controls page or by hand)\n"
		"; DEVICE <name> = <vendor>:<product> \"<product name>\" [SERIAL s] [IFACE i] [PORT p]\n"
		"; CAL <name>.<axis> = <min> <center> <max>\n"
		"; Profiles (Global.cfg, Vessels/<class>.cfg) use these names.\n\n";
	for (auto &d : df.defs) {
		if (d.legacyIndex) s += "DEVICE " + d.name + " = INDEX " + std::to_string (d.legacyIndex) + "\n";
		else {
			char id[16];
			snprintf (id, sizeof id, "%04x:%04x", d.vid & 0xFFFF, d.pid & 0xFFFF);
			s += "DEVICE " + d.name + " = " + id + " " + Quote (d.product);
			if (!d.serial.empty()) s += " SERIAL " + Quote (d.serial);
			if (!d.iface.empty()) s += " IFACE " + Quote (d.iface);
			if (!d.port.empty()) s += " PORT " + Quote (d.port);
			s += "\n";
		}
		for (auto &c : d.cal)
			s += "CAL " + d.name + "." + InputAbsName (c.code) + " = " + std::to_string (c.min) + " " +
				std::to_string (c.center) + " " + std::to_string (c.max) + "\n";
	}
	return InputWriteFile (path, s);
}

std::map<int,int> InputMatchDevices (const std::vector<DeviceDef> &defs, const std::vector<const InputDevice*> &devs,
	const std::vector<const InputDevice*> &legacy)
{
	std::map<int,int> m;
	std::set<int> used;
	auto pick = [&](size_t di) {
		const DeviceDef &d = defs[di];
		std::vector<const InputDevice*> c;
		for (auto *x : devs)
			if (!used.count (x->id) && x->vendor == d.vid && x->product == d.pid && x->name == d.product &&
				(d.serial.empty() || x->uniq == d.serial)) c.push_back (x);
		auto prefer = [&](auto pred) {
			std::vector<const InputDevice*> f;
			for (auto *x : c) if (pred (x)) f.push_back (x);
			if (!f.empty()) c = f;
		};
		if (c.size() > 1 && !d.iface.empty()) prefer ([&](const InputDevice *x) { return x->Iface() == d.iface; });
		if (c.size() > 1 && !d.port.empty()) prefer ([&](const InputDevice *x) { return x->Port() == d.port; });
		if (!c.empty()) {
			m[c[0]->id] = (int)di;
			used.insert (c[0]->id);
		}
	};
	for (size_t i = 0; i < defs.size(); i++) if (!defs[i].legacyIndex && !defs[i].serial.empty()) pick (i);
	for (size_t i = 0; i < defs.size(); i++) if (!defs[i].legacyIndex && defs[i].serial.empty()) pick (i);
	for (size_t i = 0; i < defs.size(); i++) {
		int n = defs[i].legacyIndex;
		if (n >= 1 && n <= (int)legacy.size() && !used.count (legacy[n-1]->id)) {
			m[legacy[n-1]->id] = (int)i;
			used.insert (legacy[n-1]->id);
		}
	}
	return m;
}

DeviceDef InputDefFor (const InputDevice &x, const std::vector<DeviceDef> &defs)
{
	DeviceDef d;
	d.vid = x.vendor, d.pid = x.product, d.product = x.name;
	d.serial = x.uniq, d.iface = x.Iface(), d.port = x.Port();
	std::vector<std::string> w;
	std::string cur;
	for (char c : x.name + " ") {
		if (isalnum ((unsigned char)c)) cur += c;
		else if (!cur.empty()) {
			if (w.empty() || strcasecmp (w.back().c_str(), cur.c_str())) w.push_back (cur);
			cur.clear();
		}
	}
	static const struct { const char *word, *name; } role[] = { // what it is, by the first role in this order
		{"RUDDER", "Pedals"}, {"PEDAL", "Pedals"}, {"PEDALS", "Pedals"}, {"THROTTLE", "Throttle"}, {"YOKE", "Yoke"},
		{"JOYSTICK", "Stick"}, {"STICK", "Stick"}, {"WHEEL", "Wheel"}, {"PANEL", "Panel"}, {"BOX", "Box"},
		{"GAMEPAD", "Gamepad"}, {"CONTROLLER", "Gamepad"} };
	std::string base;
	for (auto &r : role) {
		for (auto &s : w) if (!strcasecmp (s.c_str(), r.word)) base = r.name;
		if (!base.empty()) break;
	}
	if (base.empty()) {
		if (w.size() > 1) w.erase (w.begin()); // the vendor
		for (auto &s : w) {
			std::string t = s;
			bool caps = true;
			for (char c : t) if (islower ((unsigned char)c)) caps = false;
			if (caps && t.size() > 3) for (size_t i = 1; i < t.size(); i++) t[i] = tolower ((unsigned char)t[i]);
			t[0] = toupper ((unsigned char)t[0]);
			if (!base.empty() && base.size() + t.size() > 28) break; // whole words
			base += t;
		}
		if (base.empty()) base = "Device";
		if (isdigit ((unsigned char)base[0])) base = "D" + base;
		if (base.size() > 28) base.resize (28);
	}
	d.name = base;
	for (int n = 2; ; n++) {
		bool clash = false;
		for (auto &o : defs) if (!strcasecmp (o.name.c_str(), d.name.c_str())) clash = true;
		if (!clash) break;
		d.name = base + std::to_string (n);
	}
	return d;
}

std::string InputAccessHint (const InputDevice &d)
{
	char id[96];
	snprintf (id, sizeof id, "ATTRS{idVendor}==\"%04x\", ATTRS{idProduct}==\"%04x\"", d.vendor & 0xFFFF, d.product & 0xFFFF);
	return d.path + ": " + strerror (d.err) + ". A udev rule gives it to you: SUBSYSTEM==\"input\", " + id +
		", TAG+=\"uaccess\" in /etc/udev/rules.d/71-orbiter-controllers.rules (GROUP=\"input\" where there is no logind),"
		" then: sudo udevadm control --reload && sudo udevadm trigger";
}

// ====================================================================================
// engine

bool InputEngine::IKey::operator< (const IKey &o) const
{
	if (src != o.src) return src < o.src;
	if (kind != o.kind) return kind < o.kind;
	if (dev != o.dev) return dev < o.dev;
	if (code != o.code) return code < o.code;
	return dir < o.dir;
}

InputEngine::InputEngine ()
{
	memset (keyCount_, 0, sizeof keyCount_);
	memset (modCount_, 0, sizeof modCount_);
	memset (funcCount_, 0, sizeof funcCount_);
	memset (layerCount_, 0, sizeof layerCount_);
	memset (layerLatch_, 0, sizeof layerLatch_);
	memset (tapPhase_, 0, sizeof tapPhase_);
	memset (tapMods_, 0, sizeof tapMods_);
	memset (frame_.key, 0, sizeof frame_.key);
	memset (frame_.func, 0, sizeof frame_.func);
	memset (frame_.chan, 0, sizeof frame_.chan);
	frameNo_ = 1;
	cockpit_ = true, suspended_ = false, muted_ = false;
}

bool InputEngine::HeldType (int func) const
{
	return HeldFunc (func);
}

bool InputEngine::Suspends (const Binding &b) const
{
	return b.out == IOUT_FUNC || b.out == IOUT_KEY || b.out == IOUT_MFD;
}

int InputEngine::DevIndex (const std::string &name)
{
	for (size_t i = 0; i < devNames_.size(); i++) if (!strcasecmp (devNames_[i].c_str(), name.c_str())) return (int)i;
	devNames_.push_back (name);
	return (int)devNames_.size()-1;
}

InputEngine::IKey InputEngine::KeyOf (const InputRef &r)
{
	IKey k;
	k.src = r.src, k.kind = r.kind, k.dev = (r.src == INSRC_DEV ? DevIndex (r.dev) : -1), k.code = r.code, k.dir = r.dir;
	if (r.kind == INK_AXIS) {
		k.dir = 0;
		if (r.zone) {
			int zi = -1;
			for (size_t i = 0; i < zones_.size(); i++)
				if (zones_[i].dev == k.dev && zones_[i].code == r.code && zones_[i].lo == r.zlo && zones_[i].hi == r.zhi) zi = (int)i;
			if (zi < 0) {
				zones_.push_back ({k.dev, r.code, r.zlo, r.zhi, false});
				zi = (int)zones_.size()-1;
			}
			k.dir = -1 - zi;
		}
	}
	return k;
}

void InputEngine::SetCalibration (const std::vector<DeviceDef> &defs)
{
	cal_.clear();
	for (auto &d : defs) {
		std::string n = d.name;
		for (auto &c : n) c = tolower ((unsigned char)c);
		cal_[n] = d.cal;
	}
	for (auto &a : axes_) SetAxis (a.first.first, a.first.second, a.second.raw, a.second.min, a.second.max);
}

void InputEngine::SetAxis (int dev, int code, int raw, int min, int max)
{
	AxisState &a = axes_[{dev, code}];
	a.raw = raw, a.min = min, a.max = max, a.valid = true;
	std::string n = devNames_[dev];
	for (auto &c : n) c = tolower ((unsigned char)c);
	double x = 0.0;
	auto it = cal_.find (n);
	const DeviceCal *cal = nullptr;
	if (it != cal_.end()) for (auto &c : it->second) if (c.code == code) cal = &c;
	if (cal) {
		if (raw >= cal->center) x = (double)(raw - cal->center) / (cal->max - cal->center);
		else x = (double)(raw - cal->center) / (cal->center - cal->min);
	} else {
		double c = 0.5*(min + max), h = 0.5*(max - min); // as DirectInput
		x = (h > 0 ? (raw - c)/h : 0.0);
	}
	a.x = std::max (-1.0, std::min (1.0, x));
}

void InputEngine::AttachDevice (const InputDevice &d, const std::string &name)
{
	int di = DevIndex (name);
	attached_[d.id] = di;
	for (auto &a : d.axes) {
		SetAxis (di, a.code, a.value, a.min, a.max);
		AxisState &s = axes_[{di, a.code}];
		s.raw0 = a.value, s.live = false; // unfitted axes and values from before the first report stay out
		for (auto &z : zones_) // already inside a zone: no press until it leaves and comes back
			if (z.dev == di && z.code == a.code) z.in = (axes_[{di, a.code}].x >= z.lo && axes_[{di, a.code}].x <= z.hi);
	}
	for (size_t b = 0; b < d.btn.size(); b++) // held already: waits for its release
		if (d.btn[b]) pressed_[{INSRC_DEV, INK_BUTTON, di, (int)b, 0}] = {{}, true, false};
	for (int h = 0; h < 4; h++) {
		if (!d.hatPresent[h]) continue;
		int x = d.hat[h][0], y = d.hat[h][1];
		bool on[4] = { y < 0, x > 0, y > 0, x < 0 };
		for (int k = 0; k < 4; k++) if (on[k]) pressed_[{INSRC_DEV, INK_HAT, di, h, k}] = {{}, true, false};
	}
}

void InputEngine::DetachDevice (int id)
{
	auto it = attached_.find (id);
	if (it == attached_.end()) return;
	int di = it->second;
	attached_.erase (it);
	std::vector<IKey> keys;
	for (auto &p : pressed_) if (p.first.src == INSRC_DEV && p.first.dev == di) keys.push_back (p.first);
	for (auto &k : keys) Release_ (k);
	for (auto a = axes_.begin(); a != axes_.end(); ) {
		if (a->first.first == di) a = axes_.erase (a);
		else ++a;
	}
}

void InputEngine::SetBindings (const std::vector<Binding> &binds, InputFrame *released)
{
	InputFrame tmp;
	ReleaseAll (released ? *released : tmp);
	binds_ = binds;
	st_.assign (binds_.size(), BindState());
	byInput_.clear();
	zones_.clear();
	for (size_t i = 0; i < binds_.size(); i++) {
		byInput_[KeyOf (binds_[i].in)].push_back ((int)i);
		BindState &s = st_[i];
		s.armed = !binds_[i].pickup;
	}
	for (auto &z : zones_) {
		auto a = axes_.find ({z.dev, z.code});
		z.in = (a != axes_.end() && a->second.valid && a->second.x >= z.lo && a->second.x <= z.hi);
	}
}

void InputEngine::ReleaseAll (InputFrame &rel)
{
	for (auto &p : pressed_) {
		if (!p.second.blocked)
			for (int bi : p.second.binds) {
				st_[bi].pressFrame = 0;
				st_[bi].repeating = false;
				Unhold (bi);
			}
		p.second.blocked = true;
	}
	for (size_t bi = 0; bi < st_.size(); bi++) {
		st_[bi].toggled = false;
		st_[bi].pendingRelease = false;
		st_[bi].pressFrame = 0;
		Unhold ((int)bi);
	}
	for (int k = 0; k < 256; k++) {
		if (tapPhase_[k] == 1 && !keyCount_[k]) pendEv_.push_back ({(uint32_t)k, false, tapMods_[k]});
		tapPhase_[k] = 0;
		tapQ_[k].clear();
	}
	memset (layerLatch_, 0, sizeof layerLatch_);
	memset (layerCount_, 0, sizeof layerCount_);
	memset (keyCount_, 0, sizeof keyCount_);
	memset (modCount_, 0, sizeof modCount_);
	memset (funcCount_, 0, sizeof funcCount_);
	mfdHeld_.clear();
	rel.ev = std::move (pendEv_);
	rel.mfd = std::move (pendMfd_);
	pendEv_.clear(), pendMfd_.clear(), pendSet_.clear();
	memset (frame_.key, 0, sizeof frame_.key);   // the frame the core reads until the next Step
	memset (frame_.func, 0, sizeof frame_.func);
	memset (frame_.chan, 0, sizeof frame_.chan);
}

void InputEngine::SetSuspended (bool s, InputFrame *released)
{
	if (s == suspended_) return;
	if (s) {
		for (size_t bi = 0; bi < binds_.size(); bi++)
			if (Suspends (binds_[bi])) {
				st_[bi].toggled = st_[bi].repeating = st_[bi].pendingRelease = false;
				st_[bi].pressFrame = 0;
				Unhold ((int)bi);
			}
		for (int k = 0; k < 256; k++) {
			if (tapPhase_[k] == 1 && !keyCount_[k]) pendEv_.push_back ({(uint32_t)k, false, tapMods_[k]});
			tapPhase_[k] = 0;
			tapQ_[k].clear();
		}
		std::vector<InputKeyEv> keep;
		for (auto &e : pendEv_) if (!e.down && !(e.key & IKEYFUNC_TAG)) keep.push_back (e); // only the releases go out
		if (released) {
			released->ev = keep;
			released->mfd.clear();
			for (auto &m : pendMfd_) if (m.btn >= 0 && m.event == PANEL_MOUSE_LBUP) released->mfd.push_back (m);
		}
		pendEv_.clear();
		pendMfd_.clear();
	}
	suspended_ = s;
}

void InputEngine::SetMuted (bool m, InputFrame *released)
{
	if (m && !muted_) {
		InputFrame tmp;
		ReleaseAll (released ? *released : tmp);
	}
	muted_ = m;
}

void InputEngine::ReleaseKeysMouse (InputFrame &rel)
{
	size_t e0 = pendEv_.size(), m0 = pendMfd_.size();
	for (auto it = pressed_.begin(); it != pressed_.end(); ) {
		if (it->first.src == INSRC_DEV) { ++it; continue; }
		if (!it->second.blocked)
			for (int bi : it->second.binds) {
				st_[bi].pressFrame = 0;
				st_[bi].repeating = false;
				if (!st_[bi].toggled) Unhold (bi);
			}
		it = pressed_.erase (it); // its release may never come: the keyboard device forgets it too
	}
	rel.ev.assign (pendEv_.begin() + e0, pendEv_.end());
	rel.mfd.assign (pendMfd_.begin() + m0, pendMfd_.end());
	pendEv_.resize (e0);
	pendMfd_.resize (m0);
}

int InputEngine::Layer () const
{
	for (int n = 8; n >= 1; n--) if (layerCount_[n] > 0 || layerLatch_[n]) return n;
	return 0;
}

std::vector<int> InputEngine::Resolve (const IKey &k, uint16_t mods, bool *found)
{
	std::vector<int> r, base, layers;
	auto it = byInput_.find (k);
	if (it != byInput_.end()) {
		int L = Layer();
		bool modsrc = (k.src != INSRC_DEV);
		for (int bi : it->second) {
			const Binding &b = binds_[bi];
			if (b.view && (b.view == 1) != cockpit_) continue;
			if (modsrc && !InputModsMatch (b.in.mods, mods)) continue;
			if (b.out == IOUT_LAYER) layers.push_back (bi);
			else if (b.layer == L) r.push_back (bi);
			else if (b.layer == 0) base.push_back (bi);
		}
		if (r.empty()) r = base;
		r.insert (r.end(), layers.begin(), layers.end());
	}
	if (found) *found = !r.empty();
	return r;
}

void InputEngine::Press_ (const IKey &k, uint16_t mods, bool taken)
{
	if (pressed_.count (k)) return;
	if (muted_) { pressed_[k] = {{}, true, false}; return; } // acts only after a release and a new press
	std::vector<int> bl = Resolve (k, mods, nullptr);
	pressed_[k] = {bl, false, taken};
	for (int bi : bl) Edge (bi, true);
}

void InputEngine::Release_ (const IKey &k)
{
	auto it = pressed_.find (k);
	if (it == pressed_.end()) return;
	Press p = it->second;
	pressed_.erase (it);
	if (!p.blocked) for (int bi : p.binds) Edge (bi, false);
}

void InputEngine::Pulse (const IKey &k, uint16_t mods)
{
	Press_ (k, mods, false);
	Release_ (k);
}

void InputEngine::Edge (int bi, bool press)
{
	const Binding &b = binds_[bi];
	BindState &s = st_[bi];
	if (suspended_ && Suspends (b)) return;
	if (b.out == IOUT_NONE) return;
	if (b.out == IOUT_LAYER) {
		if (b.latch || b.mode == IMODE_TOGGLE) { if (press) layerLatch_[b.lnum] = !layerLatch_[b.lnum]; }
		else if (press) Hold (bi);
		else Unhold (bi);
		return;
	}
	InputMode m = b.mode;
	if (m == IMODE_AUTO) m = (b.in.kind == INK_REL || b.in.kind == INK_WHEEL ? IMODE_PRESS : IMODE_HOLD); // steps are presses
	switch (m) {
	case IMODE_AUTO:
	case IMODE_HOLD:
		if (press) Hold (bi); else Unhold (bi);
		break;
	case IMODE_PRESS:
		if (press) Act (bi);
		break;
	case IMODE_RELEASE:
		if (!press) Act (bi);
		break;
	case IMODE_BOTH:
		Act (bi);
		break;
	case IMODE_TOGGLE:
		if (press) {
			s.toggled = !s.toggled;
			if (s.toggled) Hold (bi);
			else { s.pressFrame = 0; Unhold (bi); }
		}
		break;
	case IMODE_REPEAT:
		if (press) { Act (bi); s.repeating = true; s.repeatT = -0.4; }
		else s.repeating = false;
		break;
	}
}

void InputEngine::Act (int bi)
{
	const Binding &b = binds_[bi];
	switch (b.out) {
	case IOUT_FUNC:
		if (HeldType (b.func)) { Hold (bi); Unhold (bi); }
		else pendEv_.push_back ({IKEYFUNC_TAG | (uint32_t)b.func, true, 0});
		break;
	case IOUT_KEY:
		Tap (b.key, b.kmods);
		break;
	case IOUT_AXIS:
		if (InputChannel (b.chan).absolute) pendSet_.push_back ({b.chan, b.amode == IAX_VALUE ? IAX_SET : b.amode, b.aval});
		else { Hold (bi); Unhold (bi); }
		break;
	case IOUT_MFD:
		if (b.mfdbtn >= 0) { Hold (bi); Unhold (bi); }
		else pendMfd_.push_back ({b.mfd, b.mfdbtn, 0});
		break;
	default:
		break;
	}
}

void InputEngine::Hold (int bi)
{
	const Binding &b = binds_[bi];
	BindState &s = st_[bi];
	if (s.active) return;
	s.active = true;
	s.pressFrame = frameNo_;
	s.pendingRelease = false;
	switch (b.out) {
	case IOUT_FUNC:
		if (HeldType (b.func)) funcCount_[b.func]++;
		else pendEv_.push_back ({IKEYFUNC_TAG | (uint32_t)b.func, true, 0});
		break;
	case IOUT_KEY:
		KeyHold (b.key, b.kmods, 1);
		break;
	case IOUT_AXIS:
		if (InputChannel (b.chan).absolute) pendSet_.push_back ({b.chan, b.amode == IAX_VALUE ? IAX_SET : b.amode, b.aval});
		break;
	case IOUT_MFD:
		if (b.mfdbtn >= 0) {
			pendMfd_.push_back ({b.mfd, b.mfdbtn, PANEL_MOUSE_LBDOWN});
			mfdHeld_[{b.mfd, b.mfdbtn}]++;
		} else pendMfd_.push_back ({b.mfd, b.mfdbtn, 0});
		break;
	case IOUT_LAYER:
		layerCount_[b.lnum]++;
		break;
	default:
		break;
	}
}

void InputEngine::Unhold (int bi)
{
	const Binding &b = binds_[bi];
	BindState &s = st_[bi];
	if (!s.active) return;
	if (s.pressFrame == frameNo_) { s.pendingRelease = true; return; } // pressed this frame: let go after it
	s.active = false;
	s.pendingRelease = false;
	switch (b.out) {
	case IOUT_FUNC:
		if (HeldType (b.func) && funcCount_[b.func] > 0) funcCount_[b.func]--;
		break;
	case IOUT_KEY:
		KeyHold (b.key, b.kmods, -1);
		break;
	case IOUT_MFD:
		if (b.mfdbtn >= 0) {
			pendMfd_.push_back ({b.mfd, b.mfdbtn, PANEL_MOUSE_LBUP});
			auto it = mfdHeld_.find ({b.mfd, b.mfdbtn});
			if (it != mfdHeld_.end() && --it->second <= 0) mfdHeld_.erase (it);
		}
		break;
	case IOUT_LAYER:
		if (layerCount_[b.lnum] > 0) layerCount_[b.lnum]--;
		break;
	default:
		break;
	}
}

void InputEngine::ModHold (uint16_t mods, int delta)
{
	static const struct { uint16_t all, right; int ldik, rdik; } g[] = {
		{KMOD_SHIFT, KMOD_RSHIFT, OAPI_KEY_LSHIFT, OAPI_KEY_RSHIFT},
		{KMOD_CTRL, KMOD_RCTRL, OAPI_KEY_LCONTROL, OAPI_KEY_RCONTROL},
		{KMOD_ALT, KMOD_RALT, OAPI_KEY_LALT, OAPI_KEY_RALT} };
	for (auto &x : g) {
		uint16_t m = mods & x.all;
		if (!m) continue;
		int dik = (m == x.right ? x.rdik : x.ldik); // either side: the left key
		modCount_[dik] = std::max (0, modCount_[dik] + delta);
	}
}

void InputEngine::KeyHold (int key, uint16_t mods, int delta)
{
	key &= 0xFF;
	int before = keyCount_[key];
	keyCount_[key] = std::max (0, before + delta);
	ModHold (mods, delta);
	if (before == 0 && keyCount_[key] > 0 && tapPhase_[key] != 1) pendEv_.push_back ({(uint32_t)key, true, mods});
	if (before > 0 && keyCount_[key] == 0) pendEv_.push_back ({(uint32_t)key, false, mods});
}

void InputEngine::Tap (int key, uint16_t mods)
{
	tapQ_[key & 0xFF].push_back (mods);
}

void InputEngine::Zones (int dev, int code)
{
	auto a = axes_.find ({dev, code});
	if (a == axes_.end()) return;
	double x = a->second.x;
	for (size_t i = 0; i < zones_.size(); i++) {
		Zone &z = zones_[i];
		if (z.dev != dev || z.code != code) continue;
		const double hyst = 0.01;
		bool in = (z.in ? (x >= z.lo - hyst && x <= z.hi + hyst) : (x >= z.lo && x <= z.hi));
		if (in == z.in) continue;
		z.in = in;
		IKey k {INSRC_DEV, INK_AXIS, dev, code, -1 - (int)i};
		if (in) Press_ (k, 0, false);
		else Release_ (k);
	}
}

void InputEngine::DeviceEvent (const InputDevEvent &e)
{
	auto it = attached_.find (e.dev);
	if (it == attached_.end()) return;
	int di = it->second;
	switch (e.kind) {
	case InputDevEvent::BUTTON: {
		IKey k {INSRC_DEV, INK_BUTTON, di, e.index, 0};
		if (e.value) Press_ (k, 0, false);
		else Release_ (k);
		break;
	}
	case InputDevEvent::AXIS: {
		auto a = axes_.find ({di, e.index});
		if (a == axes_.end()) break;
		SetAxis (di, e.index, e.value, a->second.min, a->second.max);
		AxisState &s = a->second;
		if (!s.live && fabs ((double)e.value - s.raw0) >= std::max (1.0, 0.001*(s.max - s.min))) s.live = true;
		Zones (di, e.index);
		break;
	}
	case InputDevEvent::HAT: {
		int x = e.value, y = e.value2;
		bool on[4] = { y < 0, x > 0, y > 0, x < 0 };
		for (int d = 0; d < 4; d++) {
			IKey k {INSRC_DEV, INK_HAT, di, e.index, d};
			bool was = pressed_.count (k) > 0;
			if (on[d] && !was) Press_ (k, 0, false);
			else if (!on[d] && was) Release_ (k);
		}
		break;
	}
	case InputDevEvent::REL: {
		int n = std::min (abs (e.value), 64);
		IKey k {INSRC_DEV, INK_REL, di, e.index, e.value > 0 ? 1 : -1};
		for (int i = 0; i < n; i++) Pulse (k, 0);
		break;
	}
	default:
		break;
	}
}

bool InputEngine::KeyEvent (int evcode, bool down, uint16_t mods)
{
	IKey k {INSRC_KEY, INK_KEY, -1, evcode, 0};
	auto it = pressed_.find (k);
	if (down) {
		if (it != pressed_.end()) return it->second.taken; // autorepeat
		bool found;
		std::vector<int> bl = Resolve (k, mods, &found);
		if (!found) return false;
		bool taken = false;
		for (int bi : bl) if (!binds_[bi].pass) taken = true;
		pressed_[k] = {bl, false, taken};
		for (int bi : bl) Edge (bi, true);
		return taken;
	}
	if (it == pressed_.end()) return false;
	bool taken = it->second.taken;
	Release_ (k);
	return taken;
}

bool InputEngine::MouseButton (int button, bool down, uint16_t mods)
{
	IKey k {INSRC_MOUSE, INK_MBUTTON, -1, button, 0};
	auto it = pressed_.find (k);
	if (down) {
		if (it != pressed_.end()) return it->second.taken;
		bool found;
		std::vector<int> bl = Resolve (k, mods, &found);
		if (!found) return false;
		bool taken = false;
		for (int bi : bl) if (!binds_[bi].pass) taken = true;
		pressed_[k] = {bl, false, taken};
		for (int bi : bl) Edge (bi, true);
		return taken;
	}
	if (it == pressed_.end()) return false;
	bool taken = it->second.taken;
	Release_ (k);
	return taken;
}

bool InputEngine::MouseWheel (int dir, uint16_t mods)
{
	IKey k {INSRC_MOUSE, INK_WHEEL, -1, dir, 0};
	bool found;
	std::vector<int> bl = Resolve (k, mods, &found);
	if (!found) return false;
	bool taken = false;
	for (int bi : bl) if (!binds_[bi].pass) taken = true;
	Pulse (k, mods);
	return taken;
}

bool InputEngine::IsBoundKey (int evcode, uint16_t mods) const
{
	auto it = byInput_.find ({INSRC_KEY, INK_KEY, -1, evcode, 0});
	if (it == byInput_.end()) return false;
	for (int bi : it->second) if (!binds_[bi].pass && InputModsMatch (binds_[bi].in.mods, mods)) return true;
	return false;
}

bool InputEngine::IsTakenKey (int evcode) const
{
	auto it = pressed_.find ({INSRC_KEY, INK_KEY, -1, evcode, 0});
	return it != pressed_.end() && it->second.taken;
}

bool InputEngine::IsBoundWheel (uint16_t mods) const
{
	for (int d = 0; d < 4; d++) {
		auto it = byInput_.find ({INSRC_MOUSE, INK_WHEEL, -1, d, 0});
		if (it == byInput_.end()) continue;
		for (int bi : it->second) if (!binds_[bi].pass && InputModsMatch (binds_[bi].in.mods, mods)) return true;
	}
	return false;
}

double InputEngine::AxisValue (const std::string &dev, int code, bool *valid) const
{
	for (size_t i = 0; i < devNames_.size(); i++)
		if (!strcasecmp (devNames_[i].c_str(), dev.c_str())) {
			auto a = axes_.find ({(int)i, code});
			if (a != axes_.end()) {
				if (valid) *valid = a->second.valid;
				return a->second.x;
			}
		}
	if (valid) *valid = false;
	return 0.0;
}

double InputEngine::Process (const Binding &b, double x) const
{
	if (b.invert) x = -x;
	double dz = b.dz, sat = std::max (b.sat, b.dz + 1e-6);
	if (b.out == IOUT_AXIS && b.chan >= 0 && InputChannel (b.chan).unipolar) {
		double u = (b.half ? std::max (x, 0.0) : 0.5*(x + 1.0));
		u = (u <= dz ? 0.0 : u >= sat ? 1.0 : (u - dz)/(sat - dz));
		if (b.curve != 1.0) u = pow (u, b.curve);
		return u * b.scale;
	}
	double a = fabs (x);
	a = (a <= dz ? 0.0 : a >= sat ? 1.0 : (a - dz)/(sat - dz)); // DirectInput's dead zone and saturation
	if (b.curve != 1.0) a = pow (a, b.curve);
	return (x < 0 ? -a : a) * b.scale;
}

const InputFrame &InputEngine::Step (double dt)
{
	InputFrame &f = frame_;
	f.ev = std::move (pendEv_);
	f.set = std::move (pendSet_);
	f.mfd = std::move (pendMfd_);
	pendEv_.clear(), pendSet_.clear(), pendMfd_.clear();

	// repeats
	for (size_t bi = 0; bi < binds_.size(); bi++) {
		BindState &s = st_[bi];
		if (!s.repeating || (suspended_ && Suspends (binds_[bi]))) continue;
		s.repeatT += dt;
		double period = 1.0 / binds_[bi].rate;
		for (int n = 0; s.repeatT >= period && n < 100; n++) {
			s.repeatT -= period;
			Act ((int)bi);
		}
	}
	f.ev.insert (f.ev.end(), pendEv_.begin(), pendEv_.end());
	f.set.insert (f.set.end(), pendSet_.begin(), pendSet_.end());
	f.mfd.insert (f.mfd.end(), pendMfd_.begin(), pendMfd_.end());
	pendEv_.clear(), pendSet_.clear(), pendMfd_.clear();

	// taps: one frame down, one frame free
	for (int k = 0; k < 256; k++) {
		if (tapPhase_[k] == 1) {
			if (!keyCount_[k]) f.ev.push_back ({(uint32_t)k, false, tapMods_[k]});
			tapPhase_[k] = 2;
		} else if (tapPhase_[k] == 2) {
			tapPhase_[k] = 0;
		} else if (!tapQ_[k].empty()) {
			tapMods_[k] = tapQ_[k].front();
			tapQ_[k].pop_front();
			if (!keyCount_[k]) f.ev.push_back ({(uint32_t)k, true, tapMods_[k]});
			tapPhase_[k] = 1;
		}
	}

	// held keys and functions
	memset (f.key, 0, sizeof f.key);
	for (int k = 0; k < 256; k++) if (keyCount_[k] > 0 || modCount_[k] > 0) f.key[k] = 0x80;
	for (int k = 0; k < 256; k++)
		if (tapPhase_[k] == 1) {
			f.key[k] = 0x80;
			uint16_t m = tapMods_[k];
			if (m & KMOD_SHIFT) f.key[(m & KMOD_SHIFT) == KMOD_RSHIFT ? OAPI_KEY_RSHIFT : OAPI_KEY_LSHIFT] = 0x80;
			if (m & KMOD_CTRL)  f.key[(m & KMOD_CTRL) == KMOD_RCTRL ? OAPI_KEY_RCONTROL : OAPI_KEY_LCONTROL] = 0x80;
			if (m & KMOD_ALT)   f.key[(m & KMOD_ALT) == KMOD_RALT ? OAPI_KEY_RALT : OAPI_KEY_LALT] = 0x80;
		}
	for (int i = 0; i < LKEY_COUNT; i++) f.func[i] = (funcCount_[i] > 0);

	// channels
	memset (f.chan, 0, sizeof f.chan);
	for (size_t bi = 0; bi < binds_.size(); bi++) {
		const Binding &b = binds_[bi];
		if (st_[bi].active && b.out == IOUT_AXIS && !InputChannel (b.chan).absolute && !(b.in.kind == INK_AXIS && !b.in.zone))
			f.chan[b.chan] += b.aval;
	}
	std::vector<bool> nowActive (binds_.size(), false);
	for (auto &e : byInput_) {
		const IKey &k = e.first;
		if (k.kind != INK_AXIS || k.dir != 0) continue;
		auto a = axes_.find ({k.dev, k.code});
		if (a == axes_.end() || !a->second.valid || !a->second.live) continue;
		for (int bi : Resolve (k, 0, nullptr)) {
			const Binding &b = binds_[bi];
			if (b.out != IOUT_AXIS) continue;
			double v = Process (b, a->second.x);
			if (!InputChannel (b.chan).absolute) {
				f.chan[b.chan] += v;
				continue;
			}
			BindState &s = st_[bi];
			nowActive[bi] = true;
			if (!s.wasActive) { s.haveLast = false; s.haveV0 = false; s.armed = !b.pickup; }
			if (!s.armed) {
				if (!s.haveV0) s.v0 = v, s.haveV0 = true;
				else if (fabs (v - s.v0) >= 0.01) s.armed = true;
			}
			if (s.armed && (!s.haveLast || fabs (v - s.last) >= 2e-3 || ((v == 0.0 || fabs (v) == 1.0) && v != s.last))) {
				f.set.push_back ({b.chan, IAX_SET, v});
				s.last = v, s.haveLast = true;
			}
		}
	}
	for (size_t bi = 0; bi < binds_.size(); bi++) st_[bi].wasActive = nowActive[bi];
	for (int c = 0; c < ICH_NCONT; c++)
		f.chan[c] = std::max (InputChannel (c).unipolar ? 0.0 : -1.0, std::min (1.0, f.chan[c]));

	// MFD buttons held: pressed every frame after the one they went down in
	for (auto &m : mfdHeld_) {
		bool down = false;
		for (auto &e : f.mfd) if (e.mfd == m.first.first && e.btn == m.first.second && e.event == PANEL_MOUSE_LBDOWN) down = true;
		if (!down) f.mfd.push_back ({m.first.first, m.first.second, PANEL_MOUSE_LBPRESSED});
	}

	frameNo_++;
	for (size_t bi = 0; bi < binds_.size(); bi++)
		if (st_[bi].pendingRelease) Unhold ((int)bi);
	return f;
}

// ====================================================================================
// InputSystem

InputSystem::InputSystem (const std::string &_dir, InputDevices *devices)
	: dir (_dir), devs (devices), devMtime (0), checkT (0)
{
}

std::string InputClassPath (const std::string &vesselClass)
{
	std::string c = vesselClass;
	std::replace (c.begin(), c.end(), '\\', '/');
	return c;
}

std::string InputResolveCase (const std::string &path)
{
	struct stat st;
	if (path.empty() || !stat (path.c_str(), &st)) return path;
	std::string done = (path[0] == '/' ? "/" : "");
	size_t i = (path[0] == '/' ? 1 : 0);
	while (i <= path.size()) {
		size_t e = path.find ('/', i);
		if (e == std::string::npos) e = path.size();
		std::string part = path.substr (i, e - i), here = done + part;
		if (!part.empty() && stat (here.c_str(), &st))
			if (DIR *d = opendir (done.empty() ? "." : done.c_str())) {
				while (dirent *de = readdir (d))
					if (!strcasecmp (de->d_name, part.c_str())) { here = done + de->d_name; break; }
				closedir (d);
			}
		done = here + (e < path.size() ? "/" : "");
		i = e + 1;
	}
	return done;
}

long InputSystem::Mtime (const std::string &path) const
{
	struct stat s;
	return (stat (InputResolveCase (path).c_str(), &s) ? 0 : (long)s.st_mtim.tv_sec * 1000 + s.st_mtim.tv_nsec / 1000000);
}

std::vector<std::string> InputLegacyLines (const InputLegacyCfg &cfg, const std::string &S)
{
	std::string dz = " DZ " + Fmt (cfg.deadzone * 1e-4);
	std::vector<std::string> lines = {
		S + ".X = AXIS Bank" + dz, S + ".X @1 = AXIS Yaw" + dz,
		S + ".Y = AXIS Pitch" + dz, S + ".Y = AXIS TransY" + dz,
		S + ".RZ = AXIS Yaw" + dz, S + ".RZ = AXIS TransX" + dz,
		S + ".B3 = LAYER 1",
		S + ".HAT0.UP = AXIS LookY +1", S + ".HAT0.DOWN = AXIS LookY -1",
		S + ".HAT0.RIGHT = AXIS LookX +1", S + ".HAT0.LEFT = AXIS LookX -1",
		S + ".HAT0.UP @1 = AXIS PanY +1", S + ".HAT0.DOWN @1 = AXIS PanY -1",
		S + ".HAT0.RIGHT @1 = AXIS PanX +1", S + ".HAT0.LEFT @1 = AXIS PanX -1" };
	static const char *thax[] = { "", "Z", "THROTTLE", "RUDDER" };
	if (cfg.throttleAxis >= 1 && cfg.throttleAxis <= 3) {
		double s = cfg.throttleSat * 1e-4;
		lines.push_back (S + "." + thax[cfg.throttleAxis] + " = AXIS Main INVERT DZ " + Fmt (0.5*(1.0-s)) +
			" SAT " + Fmt (0.5*(1.0+s)) + (cfg.throttleIgnore ? " PICKUP" : ""));
	}
	return lines;
}

static const char *legacyHeader =
	"; Orbiter global input profile: bindings for every vessel (Vessels/<class>.cfg on top)\n"
	"; Made from Orbiter.cfg's joystick settings (JoystickIndex, deadzone, throttle axis), as the old joystick worked.\n"
	"; BIND <input> [@layer] [mods] [ZONE a b] [mode] = <output> [options]; see the User Manual, Controllers.\n\n";

// not upstream (input-bindings): JoystickIndex, or for 0 the first stick or yoke connected, else the first that isn't a throttle, pedals, panel or box
static unsigned JoyIndex (unsigned joyIndex, const InputDevices *devs)
{
	if (joyIndex) return joyIndex;
	std::vector<const InputDevice*> lj = devs->LegacyJoysticks();
	for (int pass = 0; pass < 2; pass++)
		for (size_t i = 0; i < lj.size(); i++) {
			std::string r = InputDefFor (*lj[i], {}).name;
			bool stick = (r == "Stick" || r == "Yoke"), other = (r != "Throttle" && r != "Pedals" && r != "Panel" && r != "Box");
			if (pass ? other : stick) return (unsigned)i + 1;
		}
	return 0;
}

void InputSystem::Migrate (const InputLegacyCfg &cfg)
{
	legacy = cfg; // for Global's reset (controls-editor)
	std::string gpath = dir + "/Global.cfg", dpath = dir + "/Devices.cfg";
	unsigned idx = JoyIndex (cfg.joyIndex, devs);
	if (Mtime (gpath)) { // not upstream (input-bindings): one made empty is made again once a joystick is there
		std::ifstream f (InputResolveCase (gpath), std::ios::binary);
		std::stringstream ss;
		ss << f.rdbuf();
		if (!idx || ss.str() != legacyHeader) return;
	}
	DevicesFile df;
	InputReadDevices (dpath, df);
	InputProfile g;
	g.name = "Global";
	if (idx > 0) {
		std::vector<const InputDevice*> lj = devs->LegacyJoysticks();
		DeviceDef d;
		if (idx <= lj.size()) d = InputDefFor (*lj[idx-1], df.defs);
		else d.legacyIndex = (int)idx;
		d.name = "Stick";
		for (int n = 2; ; n++) {
			bool clash = false;
			for (auto &o : df.defs) if (!strcasecmp (o.name.c_str(), d.name.c_str())) clash = true;
			if (!clash) break;
			d.name = "Stick" + std::to_string (n);
		}
		df.defs.push_back (d);
		for (auto &l : InputLegacyLines (cfg, d.name)) {
			Binding b;
			std::string err;
			if (InputParseBinding (l, b, err)) g.binds.push_back (b);
			else msgs.push_back ("Input migration: " + l + ": " + err);
		}
	}
	InputWriteDevices (dpath, df);
	InputWriteProfile (gpath, g, legacyHeader);
	if (idx && !cfg.joyIndex) msgs.push_back ("Input: Config/Input binds the first joystick connected, " + devs->LegacyJoysticks()[idx-1]->name + ", as " + df.defs.back().name);
	else msgs.push_back ("Input: Config/Input made from Orbiter.cfg's joystick settings");
}

std::string InputSystem::DefaultGlobal (InputProfile &g) const
{
	g = InputProfile();
	g.name = "Global";
	g.head = legacyHeader, g.read = true;
	unsigned idx = JoyIndex (legacy.joyIndex, devs); // not upstream (input-bindings): the controller Migrate takes
	if (!idx) return "";
	std::string dev;
	std::vector<const InputDevice*> lj = devs->LegacyJoysticks();
	if (idx <= lj.size()) dev = NameOf (lj[idx-1]->id);
	for (auto &d : devfile.defs) if (dev.empty() && d.legacyIndex == (int)idx) dev = d.name;
	if (dev.empty() && !legacy.joyIndex)
		return "The first joystick connected (" + lj[idx-1]->name + ") has no name in Devices.cfg: no controller bindings for all vessels.";
	if (dev.empty())
		return "Orbiter.cfg's joystick (JoystickIndex = " + std::to_string (legacy.joyIndex) +
			") isn't connected and has no name in Devices.cfg: no controller bindings for all vessels.";
	for (auto &l : InputLegacyLines (legacy, dev)) {
		Binding b;
		std::string err;
		if (InputParseBinding (l, b, err)) g.binds.push_back (b);
	}
	return "";
}

void InputSystem::Load ()
{
	DevicesFile df;
	std::string dpath = dir + "/Devices.cfg";
	if (InputReadDevices (dpath, df)) {
		devfile = df;
		for (auto &e : df.errors) msgs.push_back ("Input: " + e);
	}
	devMtime = Mtime (dpath);
	profiles.clear();
	profMtime.clear();
	for (auto &cl : claimed) engine.DetachDevice (cl.first); // names may have changed
	claimed.clear();
	engine.SetCalibration (devfile.defs);
	Claim();
}

const InputProfile *InputSystem::Profile (const std::string &name)
{
	std::string path = dir + "/" + name + ".cfg";
	long mt = Mtime (path);
	auto it = profiles.find (name);
	if (it != profiles.end() && profMtime[name] == mt) return (mt ? &it->second : nullptr);
	profMtime[name] = mt;
	InputProfile p;
	p.name = name;
	if (mt && InputReadProfile (InputResolveCase (path), p)) for (auto &e : p.errors) msgs.push_back ("Input: " + e);
	profiles[name] = p;
	return (mt ? &profiles[name] : nullptr);
}

void InputSystem::Rebind (InputFrame *released)
{
	std::vector<const InputProfile*> chain;
	if (!vclass.empty()) {
		std::set<std::string> seen;
		std::string own = "Vessels/" + vclass, n = own;
		while (!n.empty()) {
			if (seen.count (n)) { msgs.push_back ("Input: INHERIT goes round in a circle at " + n); break; }
			if (seen.size() >= 8) { msgs.push_back ("Input: INHERIT more than 8 deep at " + n); break; }
			seen.insert (n);
			const InputProfile *p = Profile (n);
			if (!p) {
				if (n != own) msgs.push_back ("Input: INHERIT " + n + ": no " + dir + "/" + n + ".cfg");
				break;
			}
			chain.insert (chain.begin(), p);
			n = p->inherit;
		}
	}
	chain.insert (chain.begin(), Profile ("Global"));
	engine.SetBindings (InputMerge (chain), released);
}

void InputSystem::SetProfile (const std::string &vesselClass, InputFrame *released)
{
	vclass = InputClassPath (vesselClass);
	Rebind (released);
}

void InputSystem::Claim ()
{
	std::vector<const InputDevice*> open;
	for (auto &d : devs->Devices())
		if (d->state == InputDevice::OPEN) open.push_back (d.get());
	std::sort (open.begin(), open.end(), [](const InputDevice *a, const InputDevice *b) { return a->EventNumber() < b->EventNumber(); });
	std::map<int,int> m = InputMatchDevices (devfile.defs, open, devs->LegacyJoysticks());
	bool dirty = false;
	for (auto *d : open) {
		auto it = m.find (d->id);
		if (it == m.end()) {
			devfile.defs.push_back (InputDefFor (*d, devfile.defs));
			m[d->id] = (int)devfile.defs.size()-1;
			msgs.push_back ("Input: new controller \"" + d->name + "\" is " + devfile.defs.back().name);
			dirty = true;
		} else if (devfile.defs[it->second].legacyIndex) {
			DeviceDef n = InputDefFor (*d, devfile.defs);
			n.name = devfile.defs[it->second].name;
			n.cal = devfile.defs[it->second].cal;
			devfile.defs[it->second] = n;
			dirty = true;
		}
	}
	for (auto *d : open) {
		int di = m[d->id];
		auto c = claimed.find (d->id);
		if (c == claimed.end() || c->second != di) {
			if (c != claimed.end()) engine.DetachDevice (d->id);
			engine.AttachDevice (*d, devfile.defs[di].name);
			claimed[d->id] = di;
		}
	}
	if (dirty) {
		if (InputWriteDevices (dir + "/Devices.cfg", devfile)) devMtime = Mtime (dir + "/Devices.cfg");
		else msgs.push_back ("Input: could not write " + dir + "/Devices.cfg");
	}
}

std::string InputSystem::NameOf (int devId) const
{
	auto it = claimed.find (devId);
	return (it == claimed.end() || it->second >= (int)devfile.defs.size() ? std::string() : devfile.defs[it->second].name);
}

void InputSystem::Poll (InputFrame *released)
{
	std::vector<InputDevEvent> ev;
	devs->Poll (ev);
	for (auto &e : ev) {
		if (e.kind == InputDevEvent::ADDED) {
			Claim();
			if (InputDevice *d = devs->Get (e.dev))
				if (d->state == InputDevice::OPEN) msgs.push_back ("Input: " + d->name + " (" + NameOf (e.dev) + ") connected");
		} else if (e.kind == InputDevEvent::REMOVED) {
			msgs.push_back ("Input: " + NameOf (e.dev) + " disconnected");
			engine.DetachDevice (e.dev);
			claimed.erase (e.dev);
		} else {
			if (capture && !captured) CaptureEvent (e);
			engine.DeviceEvent (e);
		}
	}
	double t = std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count();
	std::map<std::string, double> na;
	for (auto &d : devs->Devices()) { // still no access 2 s after it appeared (udev gives a new node to the user a moment later)
		if (d->state != InputDevice::NOACCESS) continue;
		if (noAccess.count (d->sysname)) na[d->sysname] = noAccess[d->sysname];
	}
	noAccess.swap (na); // gone or open now: forgotten
	for (auto &d : devs->Devices()) {
		if (d->state != InputDevice::NOACCESS) continue;
		auto it = noAccess.find (d->sysname);
		if (it == noAccess.end()) noAccess[d->sysname] = t;
		else if (it->second >= 0 && t - it->second >= 2.0) {
			msgs.push_back ("Input: no access to \"" + d->name + "\", " + InputAccessHint (*d));
			it->second = -1; // told
		}
	}
	if (t - checkT >= 1.0) { // files edited by hand while running
		checkT = t;
		bool changed = (Mtime (dir + "/Devices.cfg") != devMtime);
		for (auto &p : profMtime) if (Mtime (dir + "/" + p.first + ".cfg") != p.second) changed = true;
		if (Mtime (dir + "/Global.cfg") && !profMtime.count ("Global")) changed = true;
		if (changed) {
			Load();
			Rebind (released);
			msgs.push_back ("Input: Config/Input read again");
		}
	}
}

std::vector<std::string> InputSystem::Messages ()
{
	std::vector<std::string> m;
	m.swap (msgs);
	return m;
}

static double Now ()
{
	return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count();
}

void InputSystem::Touch ()
{
	touchT = Now();
}

bool InputSystem::Shown () const
{
	return Now() - touchT < 0.5;
}

void InputSystem::Capture (bool on)
{
	if (on) Touch();
	capture = on, captured = false, got = InputRef();
	capStart.clear();
	if (on)
		for (auto &d : devs->Devices())
			if (d->state == InputDevice::OPEN)
				for (auto &a : d->axes) capStart[{d->id, a.code}] = a.value;
}

bool InputSystem::Captured (InputRef &r) const
{
	if (!captured) return false;
	r = got;
	return true;
}

bool InputCaptureOf (const InputDevEvent &e, const InputDevice &d, const std::string &name, int start, InputRef &r)
{
	r = InputRef();
	r.src = INSRC_DEV, r.dev = name;
	switch (e.kind) {
	case InputDevEvent::BUTTON:
		r.kind = INK_BUTTON, r.code = e.index;
		return e.value != 0;
	case InputDevEvent::HAT: { // the engine's directions (DeviceEvent)
		int x = e.value, y = e.value2;
		r.kind = INK_HAT, r.code = e.index;
		if (y < 0) r.dir = 0;
		else if (x > 0) r.dir = 1;
		else if (y > 0) r.dir = 2;
		else if (x < 0) r.dir = 3;
		else return false;
		return true;
	}
	case InputDevEvent::REL:
		r.kind = INK_REL, r.code = e.index, r.dir = (e.value > 0 ? 1 : -1);
		return e.value != 0;
	case InputDevEvent::AXIS: { // 40 % of its range from where it was: a stick pushed from its centre to an end, a lever from an end
		const InputAxis *a = d.Axis (e.index);
		r.kind = INK_AXIS, r.code = e.index;
		return a && a->max > a->min && fabs ((double)e.value - start) >= 0.4 * (double)(a->max - a->min);
	}
	default:
		return false;
	}
}

void InputSystem::CaptureEvent (const InputDevEvent &e)
{
	std::string name = NameOf (e.dev);
	InputDevice *d = devs->Get (e.dev);
	if (name.empty() || !d) return;
	auto s = capStart.find ({e.dev, e.index});
	if (e.kind == InputDevEvent::AXIS && s == capStart.end()) return; // plugged in since the capture started
	InputRef r;
	if (InputCaptureOf (e, *d, name, s == capStart.end() ? 0 : s->second, r)) got = r, captured = true;
}

void InputSystem::CaptureKey (int evcode, uint16_t mods)
{
	if (!capture || captured) return;
	got = InputRef();
	got.src = INSRC_KEY, got.kind = INK_KEY, got.code = evcode, got.mods = mods;
	captured = true;
}

void InputSystem::CaptureMouse (int button, uint16_t mods)
{
	if (!capture || captured) return;
	got = InputRef();
	got.src = INSRC_MOUSE, got.kind = INK_MBUTTON, got.code = button, got.mods = mods;
	captured = true;
}

void InputSystem::CaptureWheel (int dir, uint16_t mods)
{
	if (!capture || captured) return;
	got = InputRef();
	got.src = INSRC_MOUSE, got.kind = INK_WHEEL, got.code = dir, got.mods = mods;
	captured = true;
}
