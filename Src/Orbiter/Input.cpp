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

bool DInput::CreateJoyDevice ()
{
	if (!m_hWnd) return false; // no render window defined

	Config *pcfg = orbiter->Cfg();
	if (!pcfg->CfgJoystickPrm.Joy_idx) return false; // no joystick requested

	if (diframe->CreateJoyDevice (m_hWnd, pcfg->CfgJoystickPrm.Joy_idx-1) != DI_OK) {
		LOGOUT_ERR("Could not create joystick device");
		return false;
	}
	
	GetJoyDevice()->Acquire();
	// DIERR_OTHERAPPHASPRIO retry left out: evdev devices are shared, nothing holds priority

	if (SetJoystickProperties () != DI_OK) {
		LOGOUT_ERR("Could not set joystick properties");
		return false;
	}


	return true;
}

void DInput::DestroyDevices ()
{
	diframe->DestroyDevices();
}

void DInput::OptionChanged(DWORD cat, DWORD item)
{
	if (cat == OPTCAT_JOYSTICK) {
		switch (item) {
		case OPTITEM_JOYSTICK_DEVICE:
			diframe->DestroyJoyDevice();
			CreateJoyDevice();
			break;
		case OPTITEM_JOYSTICK_PARAM:
			SetJoystickProperties();
			break;
		}
	}
}

bool DInput::PollJoystick (JoyState *js)
{
	// todo: return joystick data in device-independent format
	//       allow collecting data from more than one joystick

	JoystickDevice *dev = GetJoyDevice();
	if (!dev) return false;
	int hr = dev->Poll();
	//if (hr == DI_OK || hr == DI_NOEFFECT)     // ignore error flag from poll. appears to occasionally return DIERR_UNPLUGGED
		hr = dev->GetDeviceState (sizeof(JoyState), js);
		if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
			if (dev->Acquire() == DI_OK) {
				dev->Poll();
				hr = dev->GetDeviceState(sizeof(JoyState), js);
			}
		}
	return (hr == DI_OK);
}

int DInput::SetJoystickProperties ()
{
	JoystickDevice *dev = GetJoyDevice();
	if (!dev) return DI_OK;

	joyprop.bRudder = false;
	joyprop.bThrottle = false;
	Config *pcfg = orbiter->Cfg();

	// x-axis range
	if (!dev->SetAxisRange (JoystickDevice::AX_X, -1000, +1000))
		return DIERR_INPUTLOST;

	// x-axis deadzone
	if (!dev->SetAxisDeadzone (JoystickDevice::AX_X, pcfg->CfgJoystickPrm.Deadzone))
		return DIERR_INPUTLOST;

	// y-axis range
	if (!dev->SetAxisRange (JoystickDevice::AX_Y, -1000, +1000))
		return DIERR_INPUTLOST;

	// y-axis deadzone
	if (!dev->SetAxisDeadzone (JoystickDevice::AX_Y, pcfg->CfgJoystickPrm.Deadzone))
		return DIERR_INPUTLOST;

	joyprop.bRudder = true;
	joyprop.bThrottle = true;

	if (!dev->SetAxisRange (JoystickDevice::AX_RZ, -1000, +1000))
		joyprop.bRudder = false;

	if (!dev->SetAxisDeadzone (JoystickDevice::AX_RZ, pcfg->CfgJoystickPrm.Deadzone))
		joyprop.bRudder = false;

	// z-axis range (throttle)
	JoystickDevice::Axis thaxis;
	JoyState js2;
	switch (pcfg->CfgJoystickPrm.ThrottleAxis) {
	case 1:
		LOGOUT ("Joystick throttle: Z-AXIS");
		thaxis = JoystickDevice::AX_Z;
		joyprop.ThrottleOfs = (BYTE*)&js2.lZ - (BYTE*)&js2;
		break;
	case 2:
		LOGOUT ("Joystick throttle: SLIDER 0");
		thaxis = JoystickDevice::AX_SLIDER0;
		joyprop.ThrottleOfs = (BYTE*)&js2.rglSlider[0] - (BYTE*)&js2;
		break;
	case 3:
		LOGOUT ("Joystick throttle: SLIDER 1");
		thaxis = JoystickDevice::AX_SLIDER1;
		joyprop.ThrottleOfs = (BYTE*)&js2.rglSlider[1] - (BYTE*)&js2;
		break;
	default:
		joyprop.bThrottle = false;
		LOGOUT ("Joystick throttle disabled by user");
		return DI_OK;
	}

	if (!dev->SetAxisRange (thaxis, -1000, 0)) {
		joyprop.bThrottle = false;
		LOGOUT("No joystick throttle control detected");
		return DI_OK;
	}
	LOGOUT("Joystick throttle control detected");

	// throttle saturation at extreme ends
	if (!dev->SetAxisSaturation (thaxis, pcfg->CfgJoystickPrm.ThrottleSaturation)) {
		LOGOUT_ERR("Setting joystick throttle saturation failed");
	}
	return DI_OK;
}
