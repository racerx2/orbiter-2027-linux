// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// ====================================================================================
// File: Di7frame.h
// Desc: Class to manage the DirectInput environment objects
// ====================================================================================

#ifndef DI7FRAME_H
#define DI7FRAME_H
// DirectInput 8 counterpart: keyboard state fed from the render window's key events (controllers: InputDevices)
#include "OrbiterPlatform.h"
#include <vector>

// JoyDeviceInstance and JoyState left out: controllers are InputDevices (input-bindings)

// DIDEVICEOBJECTDATA counterpart: one buffered key transition
struct KeyData {
	DWORD dwOfs;               // DIK scan code (OAPI_KEY_*)
	DWORD dwData;              // 0x80 = pressed
	DWORD dwTimeStamp;         // ms
	DWORD dwSequence;
};

// return codes of the device methods (DI_OK / DIERR_NOTACQUIRED counterparts)
#define DI_OK               0
#define DIERR_NOTACQUIRED  -1
#define DIERR_INPUTLOST    -2

// DirectInput keyboard device counterpart; the render window feeds it with KeyEvent
class KeyboardDevice {
public:
	KeyboardDevice (DWORD bufsize);
	void KeyEvent (int evcode, bool down);       // evdev key code (Qt nativeScanCode()-8 on X11 and Wayland)
	int  Acquire ();
	void Unacquire ();                           // focus lost: all keys released, like DISCL_FOREGROUND
	int  GetDeviceState (DWORD size, void *state);
	int  GetDeviceData (DWORD size, KeyData *dod, DWORD *n, DWORD flags);
	static DWORD DIKCode (int evcode);           // evdev key code -> DIK scan code (0 if none)
private:
	bool acquired;
	char kstate[256];
	std::vector<KeyData> buf;
	DWORD bufsize, seq;
};

// JoystickDevice left out: InputDevices reads every controller (input-bindings)

//-----------------------------------------------------------------------------
// Name: CDIFramework7
// Desc: The DirectInput framework class for DX7. Maintains the DI devices
//-----------------------------------------------------------------------------
class CDIFramework7
{
	// LPDIRECTINPUT8 m_pDI left out: evdev needs no input system object
	KeyboardDevice*      m_pdidKbdDevice;   // keyboard device
	// m_pdidMouseDevice left out: mouse input arrives as window events
	// m_pdidJoyDevice, jList and EnumJoysticksCallback left out: controllers are InputDevices (input-bindings)
	BOOL                 m_bUseKbd;

public:
	CDIFramework7();
	~CDIFramework7();

	int Create (void *hInst);
	// Initialize the DirectInput objects

	void Destroy ();
	// Destroys devices and DI object

	// generic CreateDevice left out: each device type is created by its own function

	int CreateKbdDevice (QWindow *hWnd);
	// CreateMouseDevice left out: unused upstream; the mouse arrives as window events

	void DestroyDevices();

	// accessor functions
	inline KeyboardDevice *GetKbdDevice() { return m_pdidKbdDevice; }
};

#endif // !DI7FRAME_H
