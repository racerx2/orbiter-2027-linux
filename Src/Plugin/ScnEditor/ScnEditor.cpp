// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// ==============================================================
//              ORBITER MODULE: Scenario Editor
//                  Part of the ORBITER SDK
//
// ScnEditor.cpp
//
// A plugin module to edit a scenario during the simulation.
// This allows creation, deleting and configuration of vessels.
// ==============================================================

// STRICT left out: windows.h handle type-checking switch
#define ORBITER_MODULE
#include "Orbitersdk.h"
#include "OrbiterResource.h"
#include "resource.h"
#include "Editor.h"
#include "DlgCtrl.h"
#include <QImage>

// ==============================================================
// Global variables and constants
// ==============================================================

ScnEditor *g_editor = 0;   // scenario editor instance pointer
QImage *g_hPause;          // "pause" button bitmap

// ==============================================================
// API interface
// ==============================================================

// ==============================================================
// Initialise module

DLLCLBK void InitModule (void *hDLL)
{
	// InitCommonControlsEx left out: the tree view is a Qt widget
	// Windows tree view control registration

	// Create editor instance
	g_editor = new ScnEditor (hDLL);

	// Register custom dialog controls
	oapiRegisterCustomControls (hDLL);

	// Load the bitmap for the "pause" title button
	g_hPause = oapiLoadResImage (hDLL, IDB_PAUSE);
	if (g_hPause) *g_hPause = g_hPause->scaled (15, 30); // LoadImage size
}

// ==============================================================
// Clean up module

DLLCLBK void ExitModule (void *hDLL)
{
	// Delete editor instance
	delete g_editor;
	g_editor = 0;

	// Unregister custom dialog controls
	oapiUnregisterCustomControls (hDLL);

	// Free bitmap resources
	delete g_hPause;
}

// ==============================================================
// Vessel destruction notification

DLLCLBK void opcDeleteVessel (OBJHANDLE hVessel)
{
	g_editor->VesselDeleted (hVessel);
}

// ==============================================================
// Pause state change notification

DLLCLBK void opcPause (bool pause)
{
	g_editor->Pause (pause);
}
