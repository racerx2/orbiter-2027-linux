// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// Custom dialog control classes

#ifndef __CUSTOMCONTROLS_H
#define __CUSTOMCONTROLS_H

#include "OrbiterPlatform.h"
#include <QObject>
#include <QWidget>

class QMouseEvent;

// attaches to an OrbiterDlgCtrl widget and receives its events (window procedure counterpart)
class CustomCtrl: public QObject {
public:
	CustomCtrl ();
	CustomCtrl (QWidget *hCtrl);
	void SetHwnd (QWidget *hCtrl);
	QWidget *HWnd() const { return hWnd; }
	virtual bool WndProc (QWidget *hWnd, QEvent *event); // true: event handled
	static void RegisterClass(void *hInstance);

protected:
	QWidget *hWnd = nullptr;
	QWidget *hParent = nullptr;

private:
	bool eventFilter (QObject *obj, QEvent *event) override;
};

class GenericCtrl : public CustomCtrl {
public:
	GenericCtrl();
	GenericCtrl(QWidget *hCtrl);
};

class SplitterCtrl: public CustomCtrl {
public:
	enum PaneId { PANE_NONE=0, PANE1=1, PANE2=2 };
	SplitterCtrl ();
	SplitterCtrl (QWidget *hCtrl);
	void SetHwnd (QWidget *hCtrl, QWidget *hPane1, QWidget *hPane2);
	void SetStaticPane (PaneId which, int width=0);
	int GetPaneWidth (PaneId which);
	bool WndProc (QWidget *hWnd, QEvent *event) override;

protected:
	BOOL OnSize (QWidget *hWnd, int w, int h);
	BOOL OnLButtonDown (QWidget *hWnd, Qt::KeyboardModifiers modifier, short x, short y);
	BOOL OnLButtonUp (QWidget *hWnd, Qt::KeyboardModifiers modifier, short x, short y);
	BOOL OnMouseMove (QWidget *hWnd, short x, short y);
	void Refresh ();

private:
	QWidget *hPane[2];
	int paneW[2];
	int splitterW;
	int totalW;
	PaneId staticPane;
	double widthRatio;
	bool isPushing;
	short mouseX, mouseY;
};

#endif // !__CUSTOMCONTROLS_H
