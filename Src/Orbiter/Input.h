// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// =======================================================================
// DirectInput user interface class
// =======================================================================

#ifndef __INPUT_H
#define __INPUT_H

#include "Di7frame.h"

class Orbiter; // g++: a friend declaration doesn't introduce the name

class DInput {
	friend class Orbiter;

public:
	DInput (Orbiter *pOrbiter);
	~DInput ();

	int Create (void *hInst);
	void Destroy ();

	void SetRenderWindow(QWindow *hWnd);

	bool CreateKbdDevice();
	// CreateJoyDevice left out: controllers are InputDevices, open all the time (input-bindings)
	void DestroyDevices ();

	inline CDIFramework7 *GetDIFrame() const { return diframe; }
	inline KeyboardDevice *GetKbdDevice() const { return diframe->GetKbdDevice(); }
	// GetJoyDevice, OptionChanged, PollJoystick, JoyProp and SetJoystickProperties left out (input-bindings)

private:
	Orbiter *orbiter;
	CDIFramework7 *diframe;
	QWindow *m_hWnd;
};

#endif // !__INPUT_H
