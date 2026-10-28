// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// =======================================================================
// key mapping interface
// =======================================================================

#ifndef __KEYMAP_H
#define __KEYMAP_H

// windows.h left out: Orbitersdk.h brings the WORD/DWORD types
#include "Orbitersdk.h"

// key modifier list
#define KMOD_LSHIFT 0x0100
#define KMOD_RSHIFT 0x0200
#define KMOD_SHIFT  (KMOD_LSHIFT|KMOD_RSHIFT)
#define KMOD_LCTRL  0x0400
#define KMOD_RCTRL  0x0800
#define KMOD_CTRL   (KMOD_LCTRL|KMOD_RCTRL)
#define KMOD_LALT   0x1000
#define KMOD_RALT   0x2000
#define KMOD_ALT    (KMOD_LALT|KMOD_RALT)

#define IKEYFUNC_TAG 0x10000u // not upstream: a binding's function press in the buffered key stream, IKEYFUNC_TAG | OAPI_LKEY_* (input-bindings)

class Keymap {
public:
	Keymap ();

	void SetDefault ();
	// set orbiter-default key mapping

	bool Read (const char *fname);
	// parse keymap table from file

	bool Write (const char *fname);
	// write keymap table to file (not upstream: false if it wasn't written, controls-editor)

	bool IsLogicalKey (char *kstate, int lfunc) const;
	// return true if logical function lfunc is selected by key state kstate

	bool IsLogicalKey (DWORD &key, char *kstate, int lfunc, bool clearkey = true) const;
	// return true if logical function lfunc is selected by key and kstate
	// (kstate only used for modifier keys)
	// if clearkey == true then key is set to 0 on return, if a match is found

	// not upstream (input-bindings): the functions' names, and the functions bindings hold
	static int FuncByName (const char *name);
	static const char *FuncName (int lfunc);
	void SetHeld (const unsigned char *h) { held = h; }
	WORD Key (int lfunc) const { return func[lfunc]; }  // not upstream (controls-editor): the function's key and modifiers
	void KeyText (int lfunc, char *cbuf) const;          // as keymap.cfg writes it, "" if none

private:
	bool IsMatchingModifier (char *kstate, WORD key) const;

	bool ScanStr (char *cbuf, WORD &key) const;
	char *PrintStr (char *cbuf, WORD &key) const;

	WORD func[LKEY_COUNT];
	// list of logical function keys
	// LOBYTE: key id; HIBYTE: modifier (see modifier list)

	const unsigned char *held = nullptr; // not upstream: functions held by bindings this frame
};

#endif // !__KEYMAP_H
