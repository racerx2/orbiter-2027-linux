// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport: group labels, material names and texture names from .msh text, read as the core's reader (Mesh.cpp)

#include "MshText.h"
#include <cstdio>
#include <cstring>
#include <strings.h>

static bool Line (std::istream &is, std::string &s)
{
	if (!std::getline (is, s)) return false;
	if (!s.empty () && s.back () == '\r') s.pop_back ();
	return true;
}

static std::string Word (const std::string &s, size_t from)
{
	size_t a = s.find_first_not_of (" \t", from);
	if (a == std::string::npos) return "";
	size_t b = s.find_first_of (" \t", a);
	return s.substr (a, b == std::string::npos ? std::string::npos : b - a);
}

bool GxReadMshText (std::istream &is, GxMshText &out)
{
	out = GxMshText ();
	std::string s;
	if (!Line (is, s) || s != "MSHX1") return false;
	int ngrp = 0;
	for (;;) {
		if (!Line (is, s)) return false;
		if (!strncasecmp (s.c_str (), "GROUPS", 6)) {
			if (sscanf (s.c_str () + 6, "%d", &ngrp) != 1) return false;
			break;
		}
	}
	out.ok = true;
	bool term = false;
	for (int g = 0; g < ngrp && !term; g++) {
		std::string label;
		int nvtx = 0, ntri = 0;
		bool loaded = false;
		for (;;) {
			if (!Line (is, s)) { term = true; break; }
			if (!strncasecmp (s.c_str (), "LABEL", 5)) label = Word (s, 5);
			else if (!strncasecmp (s.c_str (), "GEOM", 4)) {
				if (sscanf (s.c_str () + 4, "%d%d", &nvtx, &ntri) != 2) break;
				if (nvtx < 1 || nvtx > 1048576 || ntri < 1 || ntri > 1048576) break;
				loaded = true;
				for (int i = 0; i < nvtx + ntri; i++) if (!Line (is, s)) { loaded = false; term = true; break; }
				break;
			}
		}
		if (loaded) out.labels.push_back (label);
	}
	int n = 0;
	if (Line (is, s) && !strncmp (s.c_str (), "MATERIALS", 9) && sscanf (s.c_str () + 9, "%d", &n) == 1 && n >= 0 && n <= 65536) {
		for (int i = 0; i < n; i++) { if (!Line (is, s)) return true; out.materials.push_back (Word (s, 0)); }
		for (int i = 0; i < n; i++) for (int j = 0; j < 5; j++) if (!Line (is, s)) return true;
	} else return true;
	if (Line (is, s) && !strncmp (s.c_str (), "TEXTURES", 8) && sscanf (s.c_str () + 8, "%d", &n) == 1 && n > 0 && n <= 65536) {
		for (int i = 0; i < n; i++) {
			std::string w = "0", fl;
			if (Line (is, s)) {
				w = Word (s, 0);
				if (w.empty ()) w = "0";
				else fl = Word (s, s.find (w) + w.size ());
			}
			out.textures.push_back (w);
			out.dynamic.push_back (!fl.empty () && (fl[0] == 'D' || fl[0] == 'd'));
		}
	}
	return true;
}
