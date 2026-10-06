// custom: launcher skins; parsers for skin.cfg, Launcher.cfg and scenario facts

#include "LauncherFacts.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <strings.h>

namespace fs = std::filesystem;

namespace custom {

static const size_t MAX_CFG_BYTES = 1 << 20;
static const size_t MAX_SCN_BYTES = 4 << 20;
static const size_t MAX_LINE = 64 * 1024;
static const size_t MAX_STORED_SHIPS = 4096;
static const int MAX_SKIN_FILES = 20000;

static std::string Trim (const std::string &s)
{
	size_t a = s.find_first_not_of (" \t\r\n");
	if (a == std::string::npos) return std::string ();
	size_t b = s.find_last_not_of (" \t\r\n");
	return s.substr (a, b - a + 1);
}

static std::string Lower (std::string s)
{
	for (char &c : s)
		if (c >= 'A' && c <= 'Z') c = char (c - 'A' + 'a');
	return s;
}

// strncasecmp prefix test, as State.cpp, Psys.cpp and Vesselstatus.cpp match their keywords
static bool Prefix (const std::string &s, const char *p)
{
	size_t n = strlen (p);
	return s.size () >= n && !strncasecmp (s.c_str (), p, n);
}

static bool Equal (const std::string &s, const char *p)
{
	return !strcasecmp (s.c_str (), p);
}

static bool ToDouble (const std::string &s, double &v)
{
	const char *c = s.c_str ();
	char *end = nullptr;
	double d = strtod (c, &end);
	if (end == c || !std::isfinite (d)) return false;
	v = d;
	return true;
}

static int ToInt (const std::string &s, int def, int lo, int hi)
{
	const char *c = s.c_str ();
	char *end = nullptr;
	long v = strtol (c, &end, 10);
	if (end == c) return def;
	return (int)std::clamp (v, (long)lo, (long)hi);
}

static bool OneLine (const std::string &s)
{
	return s.find_first_of ("\r\n") == std::string::npos;
}

// one line of at most MAX_LINE characters (a longer line comes back empty); false at the end or past maxTotal
static bool ReadLine (std::istream &is, std::string &line, size_t &total, size_t maxTotal)
{
	line.clear ();
	std::streambuf *sb = is.rdbuf ();
	if (!sb || total >= maxTotal) return false;
	bool any = false, toolong = false;
	for (int c; (c = sb->sbumpc ()) != std::char_traits<char>::eof (); ) {
		any = true;
		++total;
		if (c == '\n') break;
		if (!toolong) {
			if (line.size () >= MAX_LINE) toolong = true, line.clear ();
			else line.push_back ((char)c);
		}
		if (total >= maxTotal) break;
	}
	if (toolong) line.clear ();
	if (!line.empty () && line.back () == '\r') line.pop_back ();
	return any;
}

std::vector<CfgEntry> ReadCfg (std::istream &is)
{
	std::vector<CfgEntry> out;
	std::string line;
	size_t total = 0;
	bool first = true;
	while (ReadLine (is, line, total, MAX_CFG_BYTES)) {
		if (first && line.compare (0, 3, "\xEF\xBB\xBF") == 0) line.erase (0, 3);
		first = false;
		std::string t = Trim (line);
		if (t.empty () || t[0] == ';' || t[0] == '#') continue;
		size_t eq = t.find ('=');
		if (eq == std::string::npos) continue;
		CfgEntry e;
		e.key = Lower (Trim (t.substr (0, eq)));
		e.value = Trim (t.substr (eq + 1));
		if (!e.key.empty ()) out.push_back (e);
	}
	return out;
}

// ---------------------------------------------------------------------------------------------------------
// skin.cfg

static bool Inside (const fs::path &dir, const fs::path &file)
{
	std::string d = dir.string (), f = file.string ();
	if (d.empty () || d.back () != '/') d += '/';
	return f.size () > d.size () && f.compare (0, d.size (), d) == 0;
}

static std::string CheckFile (const fs::path &dir, const std::string &rel, const char *key)
{
	fs::path p (rel);
	bool dotdot = false;
	for (const auto &part : p)
		if (part == "..") dotdot = true;
	if (rel.empty () || p.is_absolute () || dotdot)
		return std::string (key) + " must be a relative path inside the skin folder: " + rel;
	std::error_code ec;
	fs::path f = fs::canonical (dir / p, ec);
	if (ec || !fs::is_regular_file (f, ec)) return std::string (key) + " file not found: " + rel;
	if (!Inside (dir, f)) return std::string (key) + " file is outside the skin folder: " + rel;
	return std::string ();
}

// a folder inside the skin folder (custom: launcher layouts)
static std::string CheckDir (const fs::path &dir, const std::string &rel, const char *key)
{
	fs::path p (rel);
	bool dotdot = false;
	for (const auto &part : p)
		if (part == "..") dotdot = true;
	if (rel.empty () || p.is_absolute () || dotdot)
		return std::string (key) + " must be a relative path inside the skin folder: " + rel;
	std::error_code ec;
	fs::path f = fs::canonical (dir / p, ec);
	if (ec || !fs::is_directory (f, ec)) return std::string (key) + " folder not found: " + rel;
	if (!Inside (dir, f)) return std::string (key) + " folder is outside the skin folder: " + rel;
	return std::string ();
}

// a QML skin may not link out of its folder or load native code through a qmldir "plugin" line
static std::string CheckQmlTree (const fs::path &dir)
{
	std::error_code ec;
	int n = 0;
	fs::recursive_directory_iterator it (dir, fs::directory_options::skip_permission_denied, ec), end;
	for (; !ec && it != end; it.increment (ec)) {
		if (++n > MAX_SKIN_FILES) return "the skin folder has too many files";
		const fs::path &p = it->path ();
		std::error_code ec2;
		if (it->is_symlink (ec2)) {
			fs::path target = fs::canonical (p, ec2);
			if (ec2 || !Inside (dir, target)) return "the skin folder links outside itself: " + p.string ();
		}
		if (p.filename () != "qmldir") continue;
		std::ifstream f (p);
		std::string line;
		size_t total = 0;
		while (ReadLine (f, line, total, MAX_CFG_BYTES)) {
			std::istringstream ws (Trim (line));
			std::string w1, w2;
			ws >> w1 >> w2;
			if (w1 == "plugin" || (w1 == "optional" && w2 == "plugin"))
				return "a qmldir file loads native code (plugin line): " + p.string ();
		}
	}
	if (ec) return "the skin folder can't be read";
	return std::string ();
}

SkinManifest ReadSkin (const std::string &dirPath, int supportedApi)
{
	SkinManifest m;
	fs::path given = fs::path (dirPath).lexically_normal ();
	if (given.filename ().empty ()) given = given.parent_path ();
	m.id = given.filename ().string ();
	m.name = m.id;
	std::error_code ec;
	fs::path dir = fs::canonical (given, ec);
	if (ec || !fs::is_directory (dir, ec)) {
		m.reason = "skin folder not found";
		return m;
	}
	m.dir = dir.string ();
	std::ifstream is (dir / "skin.cfg");
	if (!is) {
		m.reason = "skin.cfg not found";
		return m;
	}
	m.api = 0;
	bool haveApi = false;
	for (const auto &e : ReadCfg (is)) {
		if (e.key == "name") { if (!e.value.empty ()) m.name = e.value; }
		else if (e.key == "author") m.author = e.value;
		else if (e.key == "version") m.version = e.value;
		else if (e.key == "description") m.description = e.value;
		else if (e.key == "api") m.api = ToInt (e.value, 0, 0, 1000000), haveApi = true;
		else if (e.key == "qml") m.qml = e.value;
		else if (e.key == "qss") m.qss = e.value;
		else if (e.key == "ui") m.ui = e.value;
		else if (e.key == "forms") m.forms = e.value;
		else if (e.key == "minwidth") m.minWidth = ToInt (e.value, 0, 0, 16384);
		else if (e.key == "minheight") m.minHeight = ToInt (e.value, 0, 0, 16384);
		else if (e.key == "width") m.width = ToInt (e.value, 0, 0, 16384);
		else if (e.key == "height") m.height = ToInt (e.value, 0, 0, 16384);
	}
	if (!haveApi) m.api = 1;
	if (m.api < 1) {
		m.reason = "Api must be a whole number, 1 or more";
		return m;
	}
	if (m.api > supportedApi) {
		m.reason = "needs a newer Orbiter (launcher API " + std::to_string (m.api) + ")";
		return m;
	}
	if (m.qml.empty () && m.qss.empty () && m.ui.empty () && m.forms.empty ()) {
		m.reason = "skin.cfg names no Qml file, Forms file, Qss file or Ui folder";
		return m;
	}
	if (!m.qml.empty () && !m.forms.empty ()) {
		m.reason = "skin.cfg names both a Qml file and a Forms file; a skin has one launcher";
		return m;
	}
	std::string r;
	if (!m.qml.empty () && !(r = CheckFile (dir, m.qml, "Qml")).empty ()) { m.reason = r; return m; }
	if (!m.qss.empty () && !(r = CheckFile (dir, m.qss, "Qss")).empty ()) { m.reason = r; return m; }
	if (!m.ui.empty () && !(r = CheckDir (dir, m.ui, "Ui")).empty ()) { m.reason = r; return m; }
	if (!m.forms.empty () && !(r = CheckFile (dir, m.forms, "Forms")).empty ()) { m.reason = r; return m; }
	if ((!m.qml.empty () || !m.ui.empty () || !m.forms.empty ()) && !(r = CheckQmlTree (dir)).empty ()) { m.reason = r; return m; }
	m.ok = true;
	return m;
}

// ---------------------------------------------------------------------------------------------------------
// Launcher.cfg

LauncherCfg ReadLauncherCfg (std::istream &is)
{
	LauncherCfg c;
	auto add = [](std::vector<std::string> &v, const std::string &s, size_t cap) {
		if (!s.empty () && v.size () < cap && std::find (v.begin (), v.end (), s) == v.end ()) v.push_back (s);
	};
	for (const auto &e : ReadCfg (is)) {
		if (e.key == "skin") c.skin = e.value;
		else if (e.key == "recent") add (c.recent, e.value, MAX_RECENT);
		else if (e.key == "favourite") add (c.favourites, e.value, MAX_FAVOURITES);
		else if (e.key == "layoutrun") c.layoutRun = e.value;
	}
	return c;
}

void WriteLauncherCfg (std::ostream &os, const LauncherCfg &cfg)
{
	os << "; Launchpad skin settings (custom build), written by Orbiter\n";
	os << "Skin = " << (OneLine (cfg.skin) ? cfg.skin : std::string ()) << "\n";
	for (const auto &s : cfg.recent)
		if (OneLine (s) && !s.empty ()) os << "Recent = " << s << "\n";
	for (const auto &s : cfg.favourites)
		if (OneLine (s) && !s.empty ()) os << "Favourite = " << s << "\n";
	if (!cfg.layoutRun.empty () && OneLine (cfg.layoutRun)) os << "LayoutRun = " << cfg.layoutRun << "\n";
}

bool LoadLauncherCfg (const std::string &path, LauncherCfg &cfg)
{
	cfg = LauncherCfg ();
	std::ifstream is (path);
	if (!is) return false;
	cfg = ReadLauncherCfg (is);
	return true;
}

bool SaveLauncherCfg (const std::string &path, const LauncherCfg &cfg)
{
	std::string tmp = path + ".tmp";
	{
		std::ofstream os (tmp, std::ios::out | std::ios::trunc);
		if (!os) return false;
		WriteLauncherCfg (os, cfg);
		os.flush ();
		if (!os) {
			os.close ();
			std::remove (tmp.c_str ());
			return false;
		}
	}
	if (std::rename (tmp.c_str (), path.c_str ()) != 0) {
		std::remove (tmp.c_str ());
		return false;
	}
	return true;
}

void AddRecent (LauncherCfg &cfg, const std::string &scn)
{
	if (scn.empty () || !OneLine (scn)) return;
	auto &v = cfg.recent;
	v.erase (std::remove (v.begin (), v.end (), scn), v.end ());
	v.insert (v.begin (), scn);
	if (v.size () > MAX_RECENT) v.resize (MAX_RECENT);
}

bool ToggleFavourite (LauncherCfg &cfg, const std::string &scn)
{
	auto &v = cfg.favourites;
	auto it = std::find (v.begin (), v.end (), scn);
	if (it != v.end ()) {
		v.erase (it);
		return false;
	}
	if (scn.empty () || !OneLine (scn) || v.size () >= MAX_FAVOURITES) return false;
	v.push_back (scn);
	return true;
}

// ---------------------------------------------------------------------------------------------------------
// scenario facts

static void ParseDate (const std::string &r, ScenarioFacts &f)
{
	double v;
	if (Prefix (r, "MJD")) { if (ToDouble (r.substr (3), v)) f.dateKind = 'M', f.dateValue = v; }
	else if (Prefix (r, "JD")) { if (ToDouble (r.substr (2), v)) f.dateKind = 'J', f.dateValue = v; }
	else if (Prefix (r, "JE")) { if (ToDouble (r.substr (2), v)) f.dateKind = 'E', f.dateValue = v; }
}

ScenarioFacts ReadScenario (std::istream &is)
{
	struct Ship {
		std::string name, cls, status, body, base;
		int pad = 0;
	};
	enum State { TOP, ENV, FOCUS, SHIPS, VESSEL };

	ScenarioFacts f;
	std::vector<Ship> ships;
	Ship *cur = nullptr;
	State st = TOP;
	bool seenEnv = false, seenFocus = false, stop = false;
	std::string line;
	size_t total = 0;

	while (!stop && ReadLine (is, line, total, MAX_SCN_BYTES)) {
		std::string t = Trim (line);
		switch (st) {
		case TOP: // FindLine: a case-insensitive prefix of the raw line
			if (Prefix (line, "BEGIN_ENVIRONMENT")) st = ENV;
			else if (Prefix (line, "BEGIN_FOCUS")) st = FOCUS;
			else if (Prefix (line, "BEGIN_SHIPS")) st = SHIPS;
			break;
		case ENV:
			if (Equal (t, "END_ENVIRONMENT")) st = TOP, seenEnv = true;
			else if (Prefix (t, "Date")) ParseDate (Trim (t.substr (4)), f);
			else if (Prefix (t, "System")) f.system = Trim (t.substr (6));
			break;
		case FOCUS:
			if (Equal (t, "END_FOCUS")) st = TOP, seenFocus = true;
			else if (Prefix (t, "Ship")) f.focus = Trim (t.substr (4));
			break;
		case SHIPS:
			if (Equal (t, "END_SHIPS")) {
				st = TOP;
				stop = seenEnv && seenFocus;
			} else if (!t.empty ()) {
				size_t c = t.find (':');
				Ship s;
				s.name = Trim (c == std::string::npos ? t : t.substr (0, c));
				s.cls = (c == std::string::npos ? std::string () : Trim (t.substr (c + 1)));
				if (s.cls.empty ()) s.cls = s.name; // Psys.cpp: no class, the class is the name
				f.vesselCount++;
				if (ships.size () < MAX_STORED_SHIPS) {
					ships.push_back (s);
					cur = &ships.back ();
				} else cur = nullptr;
				st = VESSEL;
			}
			break;
		case VESSEL:
			if (Equal (t, "END")) {
				st = SHIPS;
				cur = nullptr;
			} else if (cur && Prefix (t, "STATUS")) {
				std::string r = Trim (t.substr (6));
				if (Prefix (r, "LANDED")) cur->status = "Landed", cur->body = Trim (r.substr (6));
				else if (Prefix (r, "ORBITING")) cur->status = "Orbiting", cur->body = Trim (r.substr (8));
			} else if (cur && Prefix (t, "BASE")) {
				std::string r = Trim (t.substr (4));
				size_t c = r.find (':');
				cur->base = Trim (c == std::string::npos ? r : r.substr (0, c));
				cur->pad = (c == std::string::npos ? 0 : ToInt (Trim (r.substr (c + 1)), 0, 0, 9999));
			}
			break;
		}
	}

	for (size_t i = 0; i < ships.size () && i < MAX_LISTED_VESSELS; i++)
		f.vessels.push_back ({ships[i].name, ships[i].cls});
	for (const auto &s : ships)
		if (!f.focus.empty () && !strcasecmp (s.name.c_str (), f.focus.c_str ())) {
			f.focusClass = s.cls;
			f.focusStatus = s.status;
			f.focusBody = s.body;
			f.focusBase = s.base;
			f.focusPad = s.pad;
			break;
		}
	return f;
}

bool ReadBlock (std::istream &is, const char *block, std::string &text, size_t maxBytes)
{
	text.clear ();
	is.clear ();
	is.seekg (0);
	const std::string begin = std::string ("BEGIN_") + block, end = std::string ("END_") + block;
	std::string line;
	size_t total = 0;
	bool in = false;
	while (ReadLine (is, line, total, MAX_SCN_BYTES)) {
		if (!in) {
			in = Prefix (line, begin.c_str ()); // FindLine
			continue;
		}
		if (Prefix (line, end.c_str ())) break;
		if (line.empty ()) text += '\n';
		else text += line + ' ';
		if (text.size () >= maxBytes) {
			text.resize (maxBytes);
			break;
		}
	}
	is.clear ();
	is.seekg (0);
	return in;
}

}
