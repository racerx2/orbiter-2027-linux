// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#include "DlgCtrl.h"
#include "DlgCtrlLocal.h"
#include "OrbiterResource.h"
#include <QImage>

static QWidget *CreatePropertyList (const RESCONTROL*, QWidget *parent) { return new PropertyListCtrl (parent); }

void RegisterPropertyList (void *hInst)
{
	// Register window class for property list
	oapiRegisterResControl (hInst, "OrbiterCtrl_PropertyList", CreatePropertyList);

	// arrow bitmap from Orbiter's own resources
	PropertyList::hBmpArrows = oapiLoadResImage (NULL, 286);
}

void UnregisterPropertyList (void *hInst)
{
	oapiUnregisterResControl (hInst, "OrbiterCtrl_PropertyList");
	delete PropertyList::hBmpArrows;
	PropertyList::hBmpArrows = NULL;
}
