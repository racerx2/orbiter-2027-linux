// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#ifndef __HELP_H
#define __HELP_H

class QWidget;

void OpenHelp (QWidget *hWnd, const char *file, const char *topic);

void OpenDefaultHelp (QWidget *hWnd, const char *topic);
// use this only for opening a help window outside the simulation,
// e.g. from the Launchpad dialog. For in-game help, use the mechanism in
// Dialogs.cpp instead.

#endif // !__HELP_H
