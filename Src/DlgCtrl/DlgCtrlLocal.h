// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#ifndef __DLGCTRLLOCAL_H
#define __DLGCTRLLOCAL_H

#include <QColor>

// pen and brush colours the controls draw with
typedef struct {
	QColor hPen1, hPen2;
	QColor hBrush1, hBrush2;
} GDIRES;

void RegisterPropertyList (void *hInst);
void UnregisterPropertyList (void *hInst);

#endif // !__DLGCTRLLOCAL_H
