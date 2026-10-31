// WineBridge helper: a module's text for Orbiter, which shows UTF-8, and Orbiter's text for a module. Released Orbiter was ANSI: a
// module's text is in Wine's ANSI code page, or UTF-8 (a name Orbiter gave it, or a module built after upstream moved to UTF-8)
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "wb_helper.h"

namespace {
	bool Ascii (const char *s, uint32_t n)
	{
		for (uint32_t i = 0; i < n; i++) if ((unsigned char)s[i] >= 0x80) return false;
		return true;
	}

	// n bytes of code page cp to code page to: malloc'ed, *len its bytes (no terminating zero counted); 0 on failure
	char *Convert (UINT cp, UINT to, const char *s, uint32_t n, uint32_t *len)
	{
		int wn = MultiByteToWideChar (cp, 0, s, (int)n, 0, 0);
		WCHAR *w = wn > 0 ? (WCHAR *)malloc (wn * sizeof (WCHAR)) : 0;
		if (!w) return 0;
		MultiByteToWideChar (cp, 0, s, (int)n, w, wn);
		int un = WideCharToMultiByte (to, 0, w, wn, 0, 0, 0, 0);
		char *out = un > 0 ? (char *)malloc (un + 1) : 0;
		if (out) {
			WideCharToMultiByte (to, 0, w, wn, out, un, 0, 0);
			out[un] = 0;
			*len = (uint32_t)un;
		}
		free (w);
		return out;
	}
}

wb::Utf8::Utf8 (const char *str, int len) : s (str), n (0), own (0)
{
	if (!str) return;
	n = len < 0 ? (uint32_t)strlen (str) : (uint32_t)len;
	if (!n || Ascii (str, n) || MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, str, (int)n, 0, 0) > 0) return;
	uint32_t k;
	if ((own = Convert (CP_ACP, CP_UTF8, str, n, &k))) s = own, n = k;
}

wb::Utf8::Utf8 (const wchar_t *w, uint32_t wn) : s (""), n (0), own (0)
{
	int un = wn ? WideCharToMultiByte (CP_UTF8, 0, w, (int)wn, 0, 0, 0, 0) : 0;
	if (un <= 0 || !(own = (char *)malloc (un + 1))) return;
	WideCharToMultiByte (CP_UTF8, 0, w, (int)wn, own, un, 0, 0);
	own[un] = 0;
	s = own, n = (uint32_t)un;
}

void wb::TextStr (WbWriter &w, const char *s)
{
	if (!s) { w.str (0); return; }
	Utf8 t (s);
	w.u32 (t.n); w.raw (t.s, t.n);
}

uint32_t wb::Utf8Cut (const char *s, uint32_t n, uint32_t room)
{
	if (n <= room) return n;
	uint32_t k = room;
	while (k > 0 && ((unsigned char)s[k] & 0xC0) == 0x80) k--; // s[k] starts the character that doesn't fit
	return k;
}

char *wb::ToModule (const char *utf8, uint32_t n, const void *code)
{
	uint32_t k;
	char *out = 0;
	if (utf8 && n && !Ascii (utf8, n) && CallerApi (code) < API_UTF8) out = Convert (CP_UTF8, CP_ACP, utf8, n, &k);
	if (!out) {
		out = (char *)malloc (n + 1);
		if (n) memcpy (out, utf8, n);
		out[n] = 0;
	}
	return out;
}
