// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// ====================================================================================
// File: Di7frame.cpp
// Desc: Class to manage the DirectInput environment objects
// ====================================================================================

#include "Di7frame.h"
#include "Log.h"
#include <string.h>
#include <stdio.h>
#include <chrono>
#include <algorithm>
#include <linux/input.h>

// evdev key codes that differ from the set-1 scan codes DirectInput uses (codes 1-88 are identical)
static const struct { int ev; DWORD dik; } evdik[] = {
	{KEY_KPENTER, 0x9C}, {KEY_RIGHTCTRL, 0x9D}, {KEY_KPSLASH, 0xB5}, {KEY_SYSRQ, 0xB7},
	{KEY_RIGHTALT, 0xB8}, {KEY_PAUSE, 0xC5}, {KEY_HOME, 0xC7}, {KEY_UP, 0xC8}, {KEY_PAGEUP, 0xC9},
	{KEY_LEFT, 0xCB}, {KEY_RIGHT, 0xCD}, {KEY_END, 0xCF}, {KEY_DOWN, 0xD0}, {KEY_PAGEDOWN, 0xD1},
	{KEY_INSERT, 0xD2}, {KEY_DELETE, 0xD3}, {KEY_LEFTMETA, 0xDB}, {KEY_RIGHTMETA, 0xDC},
	{KEY_COMPOSE, 0xDD}, {KEY_KPEQUAL, 0x8D}, {KEY_F13, 0x64}, {KEY_F14, 0x65}, {KEY_F15, 0x66},
	{KEY_KPCOMMA, 0xB3}, {KEY_MUTE, 0xA0}, {KEY_VOLUMEDOWN, 0xAE}, {KEY_VOLUMEUP, 0xB0}
};

static DWORD MsTime ()
{
	return (DWORD)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ====================================================================================
// KeyboardDevice

KeyboardDevice::KeyboardDevice (DWORD _bufsize)
{
	acquired = false;
	memset (kstate, 0, 256);
	bufsize = _bufsize;
	seq = 0;
}

DWORD KeyboardDevice::DIKCode (int evcode)
{
	if (evcode > 0 && evcode <= KEY_F12) return (DWORD)evcode;
	for (auto &m : evdik) if (m.ev == evcode) return m.dik;
	return 0;
}

void KeyboardDevice::KeyEvent (int evcode, bool down)
{
	if (!acquired) return;
	DWORD dik = DIKCode (evcode);
	if (!dik || dik > 255) return;
	if (((kstate[dik] & 0x80) != 0) == down) return; // auto-repeat: DirectInput reports transitions only
	kstate[dik] = (down ? (char)0x80 : 0);
	if (buf.size() >= bufsize) return;                // buffer overflow drops events, like DIPROP_BUFFERSIZE
	buf.push_back ({dik, down ? 0x80u : 0u, MsTime(), seq++});
}

int KeyboardDevice::Acquire ()
{
	acquired = true;
	return DI_OK;
}

void KeyboardDevice::Unacquire ()
{
	acquired = false;
	memset (kstate, 0, 256);
	buf.clear();
}

int KeyboardDevice::GetDeviceState (DWORD size, void *state)
{
	if (!acquired) return DIERR_NOTACQUIRED;
	memcpy (state, kstate, std::min (size, (DWORD)256));
	return DI_OK;
}

int KeyboardDevice::GetDeviceData (DWORD size, KeyData *dod, DWORD *n, DWORD flags)
{
	if (!acquired) { *n = 0; return DIERR_NOTACQUIRED; }
	DWORD k = std::min (*n, (DWORD)buf.size());
	for (DWORD i = 0; i < k; i++) dod[i] = buf[i];
	buf.erase (buf.begin(), buf.begin()+k);
	*n = k;
	return DI_OK;
}

// JoystickDevice left out: InputDevices reads every controller (input-bindings)

//-----------------------------------------------------------------------------
// Name: CDIFramework7()
// Desc: Constructor
//-----------------------------------------------------------------------------
CDIFramework7::CDIFramework7 ()
{
	m_pdidKbdDevice   = NULL;
}

//-----------------------------------------------------------------------------
// Name: ~CDIFramework7()
// Desc: Destructor
//-----------------------------------------------------------------------------
CDIFramework7::~CDIFramework7 ()
{
	Destroy ();
}

//-----------------------------------------------------------------------------
// Name: Create()
// Desc: Initialises the DirectInput objects
//-----------------------------------------------------------------------------
int CDIFramework7::Create (void *hInst)
{
	// DirectInput8Create left out: evdev has no input system object

	// joystick enumeration left out: controllers are InputDevices (input-bindings)
	return DI_OK;
}

//-----------------------------------------------------------------------------
// Name: Destroy()
// Desc: Deletes devices and DI object
//-----------------------------------------------------------------------------
void CDIFramework7::Destroy ()
{
	DestroyDevices();
}

//-----------------------------------------------------------------------------
// Name: CreateKbdDevice()
// Desc: Creates a DirectInput device for the keyboard
//-----------------------------------------------------------------------------
int CDIFramework7::CreateKbdDevice (QWindow *hWnd)
{
	// cooperative level (DISCL_NONEXCLUSIVE | DISCL_FOREGROUND): the render window only
	// feeds key events while it has focus, and releases all keys when focus is lost

	// set buffer size for storage of buffered key events
	m_pdidKbdDevice = new KeyboardDevice (10);
	return DI_OK;
}

//-----------------------------------------------------------------------------
// Name: DestroyDevices()
// Desc: Releases the DirectInput devices
//-----------------------------------------------------------------------------
void CDIFramework7::DestroyDevices()
{
	if (m_pdidKbdDevice) {
		m_pdidKbdDevice->Unacquire ();
		delete m_pdidKbdDevice;
		m_pdidKbdDevice = NULL;
	}
	// mouse device left out (see Di7frame.h)
	// joystick device left out: controllers are InputDevices (input-bindings)
}
