// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// =======================================================================
// DirectInput user interface class
// =======================================================================

#include "Input.h"
#include "Log.h"
#include "Orbiter.h"

DInput::DInput (Orbiter *pOrbiter)
{
	orbiter = pOrbiter;
	diframe = NULL;
	m_hWnd = NULL;
}

DInput::~DInput ()
{
	Destroy();
}

int DInput::Create (void *hInst)
{
	if (NULL == (diframe = new CDIFramework7())) {
		LOGOUT_ERR ("DirectInput: Could not create DI environment");
		return DIERR_INPUTLOST;
	}
	return diframe->Create (hInst);
}

void DInput::Destroy ()
{
	if (diframe) {
		delete diframe;
		diframe = NULL;
	}
}

void DInput::SetRenderWindow(QWindow *hWnd)
{
	if (diframe)
		diframe->DestroyDevices();
	m_hWnd = hWnd;
}

bool DInput::CreateKbdDevice()
{
	if (!m_hWnd) return false; // no render window defined

	if (diframe->CreateKbdDevice (m_hWnd) != DI_OK) {
		LOGOUT("ERROR: Could not create keyboard device");
		return false; // we need the keyboard, so give up
	}
	GetKbdDevice()->Acquire();
	return true;
}

// CreateJoyDevice left out: controllers are InputDevices (input-bindings)

void DInput::DestroyDevices ()
{
	diframe->DestroyDevices();
}

// OptionChanged, PollJoystick and SetJoystickProperties left out: controllers are InputDevices (input-bindings)
