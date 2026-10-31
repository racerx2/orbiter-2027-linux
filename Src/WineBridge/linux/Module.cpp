// WineBridge: the vessel module Orbiter loads for a class whose cfg says Module = WineBridge
#define ORBITER_MODULE
#include "Orbitersdk.h"
#include "Bridge.h"
#include "WbVessel.h"

DLLCLBK VESSEL *ovcInit (OBJHANDLE hvessel, int flightmodel)
{
	std::lock_guard<std::recursive_mutex> g (WbBridge::Get ().Lock ());
	WbVessel *v = new WbVessel (hvessel, flightmodel);
	v->Attach ();
	return v;
}

DLLCLBK void ovcExit (VESSEL *vessel)
{
	delete static_cast<WbVessel *> (vessel);
}

// once per session, after the last vessel of this module: the helper ends once no Windows module is open
DLLCLBK void ExitModule (HINSTANCE)
{
	std::lock_guard<std::recursive_mutex> g (WbBridge::Get ().Lock ());
	WbBridge::Get ().StopUnused ();
}
