// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// ======================================================================
//                     ORBITER SOFTWARE DEVELOPMENT KIT
// ScnEditorAPI.h
// Scenario editor plugin interface
// ======================================================================

#ifndef __SCNEDITORAPI_H
#define __SCNEDITORAPI_H

#include "OrbiterAPI.h"
#include <QWidget>
#include <QVariant>

// WM_SCNEDITOR (WM_USER) left out: no window messages, the requests go through ScnEditorMsg below

#define SE_ADDFUNCBUTTON 0x01
#define SE_ADDPAGEBUTTON 0x02
#define SE_GETVESSEL     0x04

typedef void (*CustomButtonFunc)(OBJHANDLE);

typedef struct {
	char btnlabel[32];
	CustomButtonFunc func;
} EditorFuncSpec;

// TabProc: called once when the page is built, with the edited vessel (OBJHANDLE) as context (WM_INITDIALOG lParam)
typedef struct {
	char btnlabel[32];
	void *hDLL;
	WORD ResId;
	DLGINIT TabProc;
} EditorPageSpec;

// not upstream: SendMessage (hWnd, WM_SCNEDITOR, wParam, lParam) counterpart for the page passed to secInit or a custom page
typedef INT_PTR (*SCNEDITORMSG)(QWidget *hWnd, WPARAM wParam, LPARAM lParam);
inline INT_PTR ScnEditorMsg (QWidget *hWnd, WPARAM wParam, LPARAM lParam)
{
	SCNEDITORMSG msgproc = (SCNEDITORMSG)hWnd->property ("ScnEditorMsg").value<void*>();
	return (msgproc ? msgproc (hWnd, wParam, lParam) : 0);
}

#endif // !__SCNEDITORAPI_H
