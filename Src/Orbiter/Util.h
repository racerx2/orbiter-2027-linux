// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#ifndef __UTIL_H
#define __UTIL_H

// windows.h left out: OrbiterAPI.h brings the Win32-named integer types
#include "Vecmat.h"
#include "OrbiterAPI.h"
#include "Orbiter.h"
#include <filesystem> // not upstream: SortedEntries
#include <vector>     // not upstream: SortedEntries

extern Orbiter *g_pOrbiter;

LONGLONG NameToId (const char *name);
// converts a file name into an identifier. Note that this is not guaranteed to be unique.

// should go into the graphics client
inline DWORD GetSurfColour (DWORD r, DWORD g, DWORD b)
{
	DWORD bpp = g_pOrbiter->ViewBPP();
	if (bpp >= 24) return (r << 16) + (g << 8) + b;
	else           return (((r*319)/2559) << 11) + (((g*639)/2559) << 5) + ((b*319)/2559);
}

DWORDLONG Str2Crc (const char *str);
// converts str into an integer identifier (not guaranteed unique!)

// create the directories 
bool MakePath (const char *fname);

// case-insensitive comparison of std::strings
bool iequal(const std::string& s1, const std::string& s2);

// conversion between Vector and VECTOR3 structures

inline Vector MakeVector (const VECTOR3 &v)
{ return Vector(v.x, v.y, v.z); }

inline VECTOR3 MakeVECTOR3 (const Vector &v)
{ return _V(v.x, v.y, v.z); }

inline VECTOR4 MakeVECTOR4 (const Vector &v)
{ return _V(v.x, v.y, v.z, 1.0); }

inline void CopyVector (const VECTOR3 &v3, Vector &v)
{ v.x = v3.x, v.y = v3.y, v.z = v3.z; }

inline void CopyVector (const Vector &v, VECTOR3 &v3)
{ v3.x = v.x, v3.y = v.y, v3.z = v.z; }

inline void EulerAngles (const Matrix &R, Vector &e)
{
	e.x = atan2 (R.m23, R.m33);
	e.y = -asin (R.m13);
	e.z = atan2 (R.m12, R.m11);
}

inline void EulerAngles (const Matrix &R, VECTOR3 &e)
{
	e.x = atan2 (R.m23, R.m33);
	e.y = -asin (R.m13);
	e.z = atan2 (R.m12, R.m11);
}

double rand1();
// uniformly distributed random number, range [0,1]

RECT GetClientPos (QWidget *hWnd, QWidget *hChild);
void SetClientPos (QWidget *hWnd, QWidget *hChild, RECT &r);

// GetCursorPos + ScreenToClient: cursor in the window's device pixels (screen device pixels if hWnd is NULL)
POINT CursorPos (const QWindow *hWnd);

// Floating point output stream formatter
struct FltFormatter
{
	int precision;
	double value;
	FltFormatter (int precision, double value);
	friend std::ostream& operator<< (std::ostream& os, const FltFormatter& v);
};

struct FltFormat
{
	int precision;
	FltFormat (int precision = 6);
	FltFormatter operator() (double value) const;
};

// Convert a CSS color string to a DWORD (in 0xbbggrr format)
DWORD GetCSSColor(const char *col);

// not upstream: GetProcAddress counterpart; dlsym also searches a module's dependencies, only the module's own symbol counts
void *ModuleProc (void *hModule, const char *name);

// not upstream: LoadLibrary counterpart; counts the opens per handle, a module reopened after its last free gets InitModule again (ModuleAttach)
void *ModuleLoad (const char *path);

// not upstream: FreeLibrary counterpart; the last free detaches, as dlclose may keep a module (unique symbols) to exit
void ModuleFree (void *hModule);

// not upstream: GetModuleFileName counterpart; the path the module was loaded from
const char *ModuleFileName (void *hModule);

// not upstream: a folder's entries in FindFirstFile's NTFS order; directory_iterator lists them unsorted on Linux
std::vector<std::filesystem::directory_entry> SortedEntries (std::filesystem::directory_iterator it);

// not upstream: an add-on's drop-in parts of a config file: <cfg without .cfg>.d/*.cfg, resolved, in NTFS order (design dropin-addons)
std::vector<std::string> ConfigFragments (const char *cfgpath);

#endif //!__UTIL_H
