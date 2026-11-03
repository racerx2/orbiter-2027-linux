// not upstream: a C string as a Lua string literal, for commands built from names (a script's or file's): nothing in it can end the string
#pragma once
#include <cstdio>
#include <string>

inline std::string LuaQuoted (const char *s)
{
	std::string q (1, '"');
	for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
		if (*p == '"' || *p == '\\') { q += '\\'; q += (char)*p; }
		else if (*p < 32 || *p == 127) { char e[5]; snprintf (e, sizeof e, "\\%03u", *p); q += e; }
		else q += (char)*p;
	}
	return q + '"';
}
