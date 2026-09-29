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
	bool CreateJoyDevice ();
	void DestroyDevices ();

	inline CDIFramework7 *GetDIFrame() const { return diframe; }
	inline KeyboardDevice *GetKbdDevice() const { return diframe->GetKbdDevice(); }
	inline JoystickDevice *GetJoyDevice() const { return diframe->GetJoyDevice(); }

	void OptionChanged(DWORD cat, DWORD item);

	bool PollJoystick (JoyState *js);

	struct JoyProp {
		bool bThrottle;  // joystick has throttle control
		bool bRudder;    // joystick has rudder control
		int ThrottleOfs; // throttle data offset
	};

protected:
	int SetJoystickProperties ();

private:
	Orbiter *orbiter;
	CDIFramework7 *diframe;
	JoyProp joyprop;
	QWindow *m_hWnd;
};

#endif // !__INPUT_H
