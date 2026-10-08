// not upstream: collision addon, Config/Collision.cfg reader (Orbiter-free: the file comes through a reader callback)
#include <charconv>
#include <cmath>
#include "CollCfg.h"

namespace {

std::string Trim (const std::string &s)
{
	size_t a = s.find_first_not_of (" \t\r\n"), b = s.find_last_not_of (" \t\r\n");
	return a == std::string::npos ? std::string () : s.substr (a, b - a + 1);
}

bool EqualNoCase (const std::string &a, const char *b)
{
	size_t i = 0;
	for (; i < a.size () && b[i]; i++) {
		char x = a[i], y = b[i];
		if (x >= 'a' && x <= 'z') x = (char)(x - 'a' + 'A');
		if (y >= 'a' && y <= 'z') y = (char)(y - 'a' + 'A');
		if (x != y) return false;
	}
	return i == a.size () && !b[i];
}

const char *SkipPlus (const char *b, const char *e) // from_chars takes no '+': one is skipped, but not before a '-'
{
	return b != e && *b == '+' && (b + 1 == e || b[1] != '-') ? b + 1 : b;
}

}

bool CollCfg::Parse (const std::string &s, bool &out)
{
	std::string t = Trim (s);
	if (EqualNoCase (t, "TRUE") || t == "1") { out = true; return true; }
	if (EqualNoCase (t, "FALSE") || t == "0") { out = false; return true; }
	return false;
}

bool CollCfg::Parse (const std::string &s, int &out)
{
	std::string t = Trim (s);
	int v = 0;
	const char *e = t.data () + t.size (), *b = SkipPlus (t.data (), e);
	auto r = std::from_chars (b, e, v);
	if (b == e || r.ec != std::errc () || r.ptr != e) return false;
	out = v;
	return true;
}

bool CollCfg::Parse (const std::string &s, double &out)
{
	std::string t = Trim (s);
	double v = 0;
	const char *e = t.data () + t.size (), *b = SkipPlus (t.data (), e);
	auto r = std::from_chars (b, e, v);
	if (b == e || r.ec != std::errc () || r.ptr != e || !std::isfinite (v)) return false;
	out = v;
	return true;
}

bool CollCfg::Parse (const std::string &s, std::string &out)
{
	out = Trim (s);
	return true;
}

bool CollCfg::Parse (const std::string &s, std::vector<std::string> &out)
{
	std::vector<std::string> w;
	size_t p = 0;
	while ((p = s.find_first_not_of (" \t\r\n", p)) != std::string::npos) {
		size_t q = s.find_first_of (" \t\r\n", p);
		w.push_back (s.substr (p, q == std::string::npos ? std::string::npos : q - p));
		p = q;
	}
	out.swap (w);
	return true;
}

CollCfgValues CollCfg::Read (const Reader &rd, std::vector<std::string> *bad)
{
	CollCfgValues v;
	CollCfgFields (v, [&] (const char *key, auto &field) {
		std::string s;
		if (!rd (key, s)) return;
		if (!Parse (s, field) && bad) bad->push_back (key);
	});
	return v;
}

std::vector<const char *> CollCfg::Keys ()
{
	std::vector<const char *> k;
	CollCfgValues v;
	CollCfgFields (v, [&] (const char *key, auto &) { k.push_back (key); });
	return k;
}
