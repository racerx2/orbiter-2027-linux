// not upstream (controls-editor): editing Config/Input from the Controls pages (InputEditor.h)

#include "InputEditor.h"
#include "Di7frame.h"
#include "Keymap.h"
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

static const char *globalHeader =
	"; Orbiter global input profile: bindings for every vessel (Vessels/<class>.cfg on top)\n"
	"; BIND <input> [@layer] [mods] [ZONE a b] [mode] = <output> [options]; see the User Manual, Controllers.\n\n";

// *.cfg under root, as names relative to it without the extension
static void CfgNames (const std::string &root, std::vector<std::string> &out)
{
	std::error_code ec;
	if (!fs::is_directory (root, ec)) return;
	for (auto it = fs::recursive_directory_iterator (root, fs::directory_options::skip_permission_denied, ec);
		!ec && it != fs::recursive_directory_iterator(); it.increment (ec)) {
		if (!it->is_regular_file (ec) || strcasecmp (it->path().extension().c_str(), ".cfg")) continue;
		fs::path rel = fs::relative (it->path(), root, ec);
		if (ec) continue;
		rel.replace_extension ();
		out.push_back (rel.generic_string());
	}
}

static bool Less (const std::string &a, const std::string &b) { return strcasecmp (a.c_str(), b.c_str()) < 0; }

InputEditor::InputEditor (InputSystem *_sys, const std::string &vesselDir)
	: sys (_sys), vdir (vesselDir)
{
}

std::vector<std::string> InputEditor::Classes () const
{
	std::vector<std::string> own, all, r;
	CfgNames (sys->Dir() + "/Vessels", own);
	CfgNames (vdir, all);
	std::sort (own.begin(), own.end(), Less);
	std::sort (all.begin(), all.end(), Less);
	r = own;
	for (auto &c : all)
		if (std::none_of (own.begin(), own.end(), [&](const std::string &o) { return !strcasecmp (o.c_str(), c.c_str()); }))
			r.push_back (c);
	return r;
}

std::string InputEditor::Path (const std::string &profile) const
{
	return InputResolveCase (sys->Dir() + "/" + profile + ".cfg"); // the engine finds a class's file whatever its letter case
}

std::vector<std::string> InputEditor::ProfileFiles () const
{
	std::vector<std::string> v;
	CfgNames (sys->Dir() + "/Vessels", v);
	for (auto &s : v) s = "Vessels/" + s;
	v.insert (v.begin(), "Global");
	return v;
}

bool InputEditor::Open (const std::string &profile)
{
	name = profile;
	prof = InputProfile();
	prof.name = profile;
	std::error_code ec;
	if (!fs::exists (Path (profile), ec)) return true;
	return InputReadProfile (Path (profile), prof);
}

std::vector<Binding> InputEditor::Inherited () const
{
	std::vector<InputProfile> chain; // nearest first
	if (strcasecmp (name.c_str(), "Global")) {
		std::set<std::string> seen = { name };
		for (std::string n = prof.inherit; !n.empty() && !seen.count (n) && seen.size() <= 8; ) {
			seen.insert (n);
			InputProfile p;
			if (!InputReadProfile (Path (n), p)) break;
			chain.push_back (p);
			n = p.inherit;
		}
		InputProfile g;
		if (InputReadProfile (Path ("Global"), g)) chain.push_back (g);
	}
	std::vector<const InputProfile*> base; // base first
	for (auto it = chain.rbegin(); it != chain.rend(); ++it) base.push_back (&*it);
	std::vector<Binding> r = InputMerge (base);
	r.erase (std::remove_if (r.begin(), r.end(), [&](const Binding &x) {
		for (auto &b : prof.binds) if (InputSameInput (x, b)) return true;
		return false; }), r.end());
	return r;
}

std::string InputEditor::VesselDir (const InputSystem *sys)
{
	const std::string &d = sys->Dir();
	size_t s = d.rfind ('/');
	return (s == std::string::npos ? std::string ("Vessels") : d.substr (0, s) + "/Vessels");
}

const InputDevice *InputEditor::Device (InputSystem *sys, const std::string &name)
{
	for (auto &d : sys->Devices().Devices())
		if (d->state == InputDevice::OPEN && !strcasecmp (sys->NameOf (d->id).c_str(), name.c_str())) return d.get();
	return nullptr;
}

bool InputEditor::Check (Binding &b, std::string &err)
{
	Binding n;
	if (!InputParseBinding (InputFormatBinding (b), n, err)) return false;
	n.note = b.note, n.tail = b.tail, n.line = b.line;
	b = n;
	return true;
}

std::string InputEditor::InputText (const Binding &b)
{
	std::string s = InputFormatBinding (b);
	size_t e = s.find (" = ");
	return (e == std::string::npos ? s : s.substr (0, e));
}

std::string InputEditor::OutputText (const Binding &b)
{
	std::string s = InputFormatBinding (b);
	size_t e = s.find (" = ");
	return (e == std::string::npos ? std::string() : s.substr (e+3));
}

static std::string Header (const std::string &profile)
{
	if (!strcasecmp (profile.c_str(), "Global")) return globalHeader;
	return "; Orbiter input profile for vessel class " + profile.substr (profile.find ('/')+1) + ", on top of Global.cfg\n"
		"; BIND <input> [@layer] [mods] [ZONE a b] [mode] = <output> [options]; see the User Manual, Controllers.\n\n";
}

bool InputEditor::Write (std::string &err)
{
	std::string header = Header (name);
	if (!prof.read) prof.head = header, prof.read = true; // a new file: it keeps this header from now on
	if (InputWriteProfile (Path (name), prof, header)) return true;
	err = "could not write " + Path (name);
	return false;
}

bool InputEditor::Put (int index, const Binding &b, std::string &err)
{
	Binding n = b;
	if (index >= 0 && index < (int)prof.binds.size()) n.note = prof.binds[index].note, n.tail = prof.binds[index].tail;
	if (!Check (n, err)) return false;
	InputProfile keep = prof;
	if (index >= 0 && index < (int)prof.binds.size()) prof.binds[index] = n;
	else prof.binds.push_back (n);
	if (Write (err)) return true;
	prof = keep;
	return false;
}

bool InputEditor::Remove (int index, std::string &err)
{
	if (index < 0 || index >= (int)prof.binds.size()) { err = "no such binding"; return false; }
	InputProfile keep = prof;
	std::string note = prof.binds[index].note; // the lines above it stay in the file
	prof.binds.erase (prof.binds.begin() + index);
	if (index < (int)prof.binds.size()) prof.binds[index].note = note + prof.binds[index].note;
	else prof.trail = note + prof.trail;
	if (Write (err)) return true;
	prof = keep;
	return false;
}

std::string InputEditor::AlsoDoes (const Binding &b, int index) const
{
	std::string s;
	for (int i = 0; i < (int)prof.binds.size(); i++)
		if (i != index && InputSameInput (b, prof.binds[i])) s += (s.empty() ? "" : ", ") + OutputText (prof.binds[i]);
	return s.empty() ? s : "This control also does " + s + " here (all of them act).";
}

std::string InputEditor::Conflict (const Binding &b, int, const Keymap *km) const
{
	if (b.in.kind == INK_KEY && km) {
		int dik = (int)KeyboardDevice::DIKCode (b.in.code);
		for (int f = 0; f < LKEY_COUNT; f++) {
			WORD k = km->Key (f);
			if (dik && (k & 0x00FF) == dik && (k & 0xFF00) == b.in.mods && !b.pass)
				return std::string ("keymap.cfg has this key for ") + Keymap::FuncName (f) + ": the binding takes it.";
		}
	}
	return "";
}

bool InputEditor::Rename (const std::string &from, const std::string &to, std::string &err)
{
	if (!InputValidName (to)) { err = "a name has letters, digits, _ and - (up to 32), and isn't Key or Mouse"; return false; }
	std::string dpath = sys->Dir() + "/Devices.cfg";
	DevicesFile df;
	if (!InputReadDevices (dpath, df)) { err = "could not read " + dpath; return false; }
	DeviceDef *def = nullptr;
	for (auto &d : df.defs) {
		if (!strcasecmp (d.name.c_str(), from.c_str())) def = &d;
		else if (!strcasecmp (d.name.c_str(), to.c_str())) { err = to + " is the name of another controller"; return false; }
	}
	if (!def) { err = "no controller " + from + " in " + dpath; return false; }
	for (auto &pn : ProfileFiles()) { // the profiles first: a failed write leaves the old name working
		InputProfile p;
		if (!InputReadProfile (Path (pn), p)) continue;
		bool changed = false;
		for (auto &b : p.binds)
			if (b.in.src == INSRC_DEV && !strcasecmp (b.in.dev.c_str(), from.c_str())) b.in.dev = to, changed = true;
		if (changed && !InputWriteProfile (Path (pn), p, "")) { err = "could not write " + Path (pn); return false; }
	}
	def->name = to;
	if (!InputWriteDevices (dpath, df)) { err = "could not write " + dpath; return false; }
	Open (name);
	return true;
}

bool InputEditor::WriteCalibration (const std::string &dev, const std::vector<DeviceCal> &cal, std::string &err)
{
	std::string dpath = sys->Dir() + "/Devices.cfg";
	DevicesFile df;
	if (!InputReadDevices (dpath, df)) { err = "could not read " + dpath; return false; }
	for (auto &d : df.defs)
		if (!strcasecmp (d.name.c_str(), dev.c_str())) {
			d.cal = cal;
			if (InputWriteDevices (dpath, df)) return true;
			err = "could not write " + dpath;
			return false;
		}
	err = "no controller " + dev + " in " + dpath;
	return false;
}

static bool KeepBak (const std::string &path, std::string &err) // the old file next to it as .bak
{
	std::error_code ec;
	if (!fs::exists (path, ec)) return true;
	fs::copy_file (path, path + ".bak", fs::copy_options::overwrite_existing, ec);
	if (!ec) return true;
	err = "could not copy " + path + " to " + path + ".bak";
	return false;
}

std::vector<std::string> InputEditor::VesselProfiles () const
{
	std::vector<std::string> v = ProfileFiles();
	v.erase (v.begin()); // Global
	std::sort (v.begin(), v.end(), Less);
	return v;
}

bool InputEditor::Clear (const std::string &profile, std::string &err)
{
	std::string path = Path (profile);
	std::error_code ec;
	if (!fs::exists (path, ec)) return true;
	if (!KeepBak (path, err)) return false;
	bool used = false;
	for (auto &pn : ProfileFiles()) {
		InputProfile p;
		if (strcasecmp (pn.c_str(), profile.c_str()) && InputReadProfile (Path (pn), p) && !strcasecmp (p.inherit.c_str(), profile.c_str())) used = true;
	}
	if (used) { // INHERIT there keeps working
		InputProfile e;
		e.name = profile;
		if (InputWriteProfile (path, e, Header (profile))) return true;
		err = "could not write " + path;
		return false;
	}
	if (fs::remove (path, ec)) return true;
	err = "could not remove " + path;
	return false;
}

bool InputEditor::Reset (std::string &note, std::string &err)
{
	note.clear ();
	bool ok;
	if (!strcasecmp (name.c_str(), "Global")) {
		InputProfile g;
		note = sys->DefaultGlobal (g);
		ok = KeepBak (Path (name), err);
		if (ok && !(ok = InputWriteProfile (Path (name), g, ""))) err = "could not write " + Path (name);
	} else ok = Clear (name, err);
	Open (name);
	return ok;
}

bool InputEditor::ResetVessels (std::string &err)
{
	bool ok = true;
	for (auto &pn : VesselProfiles()) { // all of them go, so no INHERIT among them is left
		std::string path = Path (pn);
		std::error_code ec;
		if (!KeepBak (path, err)) { ok = false; break; }
		if (!fs::remove (path, ec)) { err = "could not remove " + path; ok = false; break; }
	}
	Open (name);
	return ok;
}

bool InputEditor::ResetKeymap (Keymap &km, const std::string &path, std::string &err)
{
	std::string tmp = path + ".tmp";
	std::error_code ec;
	Keymap def; // Orbiter's keys
	if (!def.Write (tmp.c_str())) { fs::remove (tmp, ec); err = "could not write " + tmp; return false; }
	if (!KeepBak (path, err)) { fs::remove (tmp, ec); return false; }
	fs::rename (tmp, path, ec);
	if (ec) { fs::remove (tmp, ec); err = "could not replace " + path; return false; }
	km.SetDefault ();
	return true;
}

// ====================================================================================

void InputCalibrator::Start (const InputDevice &d)
{
	axes.clear();
	for (auto &a : d.axes) axes.push_back ({a.code, a.min, a.max, a.value, a.value, a.value, false});
}

void InputCalibrator::Update (const InputDevice &d)
{
	for (auto &x : axes)
		if (const InputAxis *a = d.Axis (x.code)) x.lo = std::min (x.lo, a->value), x.hi = std::max (x.hi, a->value);
}

void InputCalibrator::Finish (const InputDevice &d)
{
	Update (d);
	for (auto &x : axes) {
		if (const InputAxis *a = d.Axis (x.code)) x.rest = a->value;
		x.centring = Moved (x) && fabs (x.rest - 0.5 * (x.lo + x.hi)) <= 0.2 * (x.hi - x.lo);
	}
}

bool InputCalibrator::Moved (const Axis &a) const
{
	return a.max > a.min && a.hi - a.lo >= 2 && (a.hi - a.lo) >= 0.1 * (a.max - a.min);
}

std::vector<DeviceCal> InputCalibrator::Result () const
{
	std::vector<DeviceCal> r;
	for (auto &x : axes) {
		if (!Moved (x)) continue;
		int c = (x.centring ? x.rest : (x.lo + x.hi) / 2);
		c = std::max (x.lo + 1, std::min (x.hi - 1, c));
		r.push_back ({x.code, x.lo, c, x.hi});
	}
	return r;
}

// ====================================================================================

InputCaptureFilter::InputCaptureFilter (InputSystem *_sys, Mode _mode)
	: sys (_sys), mode (_mode)
{
	if (mode == INPUT) sys->Capture (true);
	QCoreApplication::instance()->installEventFilter (this);
}

InputCaptureFilter::~InputCaptureFilter ()
{
	QCoreApplication::instance()->removeEventFilter (this);
	if (mode == INPUT) sys->Capture (false);
}

bool InputCaptureFilter::KeyGot (int &k, uint16_t &m) const
{
	if (!got) return false;
	k = dik, m = kmods;
	return true;
}

static uint16_t QtMods (Qt::KeyboardModifiers m)
{
	uint16_t r = 0;
	if (m & Qt::ShiftModifier) r |= KMOD_SHIFT;
	if (m & Qt::ControlModifier) r |= KMOD_CTRL;
	if (m & Qt::AltModifier) r |= KMOD_ALT;
	return r;
}

bool InputCaptureFilter::eventFilter (QObject *, QEvent *e)
{
	if (!sys->Shown()) return false; // the page went away without ending the capture
	switch (e->type()) {
	case QEvent::KeyPress: {
		QKeyEvent *k = static_cast<QKeyEvent*> (e);
		int ev = (int)k->nativeScanCode() - 8;
		if (keysTaken.count (ev)) return true; // its auto-repeat
		int q = k->key();
		if (q == Qt::Key_Shift || q == Qt::Key_Control || q == Qt::Key_Alt || q == Qt::Key_Meta || q == Qt::Key_AltGr || ev <= 0)
			return false; // modifiers stay modifiers
		uint16_t m = QtMods (k->modifiers());
		if (mode == INPUT) sys->CaptureKey (ev, m);
		else if (!got) {
			if (int d = (int)KeyboardDevice::DIKCode (ev)) got = true, dik = d, kmods = m;
		}
		keysTaken.insert (ev);
		return true;
	}
	case QEvent::KeyRelease: {
		int ev = (int)static_cast<QKeyEvent*> (e)->nativeScanCode() - 8;
		if (!keysTaken.count (ev)) return false;
		if (!static_cast<QKeyEvent*> (e)->isAutoRepeat()) keysTaken.erase (ev);
		return true;
	}
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonDblClick: {
		if (mode != INPUT) return false;
		QMouseEvent *me = static_cast<QMouseEvent*> (e);
		int n = 1;
		for (unsigned v = (unsigned)me->button(); v > 1; v >>= 1) n++; // Orbiter's numbers: 1 left, 2 right, 3 middle, 4 back, 5 forward
		uint16_t m = QtMods (me->modifiers());
		if (me->button() == Qt::NoButton || (n <= 2 && !m)) return false; // a plain click works the page
		sys->CaptureMouse (n, m);
		buttonsTaken.insert (n);
		return true;
	}
	case QEvent::MouseButtonRelease: {
		QMouseEvent *me = static_cast<QMouseEvent*> (e);
		int n = 1;
		for (unsigned v = (unsigned)me->button(); v > 1; v >>= 1) n++;
		if (!buttonsTaken.count (n)) return false;
		buttonsTaken.erase (n);
		return true;
	}
	case QEvent::Wheel: {
		if (mode != INPUT) return false;
		QWheelEvent *we = static_cast<QWheelEvent*> (e);
		QPoint d = we->angleDelta();
		uint16_t m = QtMods (we->modifiers());
		if (d.y()) sys->CaptureWheel (d.y() > 0 ? 0 : 1, m);
		else if (d.x()) sys->CaptureWheel (d.x() > 0 ? 2 : 3, m);
		return true;
	}
	default:
		return false;
	}
}
