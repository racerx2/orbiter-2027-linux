// WineBridge: Orbiter's interpreter for modules built with the 2010, 2016 and 2024 Interpreter.h (wb_interp.cpp)
#pragma once

// the virtual functions the interpreter calls on itself; Interpreter::wb_v and the others call them by the object's class's version
enum WbVfn { WB_Initialise, WB_WaitExec, WB_EndExec, WB_LoadAPI, WB_LoadVesselAPI, WB_LoadMFDAPI, WB_LoadNTVERTEXAPI, WB_LoadLightEmitterMethods,
	WB_LoadBeaconMethods, WB_LoadCustomCameraMethods, WB_LoadSketchpadAPI, WB_LoadBitAPI, WB_LoadStartupScript, WB_ProcessChunk, WB_RunChunk,
	WB_term_strout, WB_term_out, WB_term_clear, WB_LoadXRSoundAPI, WB_VCOUNT };

class Interpreter;
void WbInterpMade (Interpreter *p, const void *maker); // the constructor: the code that made the object (a module's class's constructor)
void WbInterpGone (Interpreter *p);
