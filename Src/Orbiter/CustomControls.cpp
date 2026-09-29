// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#include "CustomControls.h"
#include "OrbiterResource.h"
#include "Util.h"
#include <QMouseEvent>
#include <QResizeEvent>
#include <algorithm>

using std::min;
using std::max;

// ===========================================================================

CustomCtrl::CustomCtrl ()
{
	hWnd = NULL;
	hParent = NULL;
}

// ---------------------------------------------------------------------------

CustomCtrl::CustomCtrl (QWidget *hCtrl)
{
	SetHwnd (hCtrl);
}

// ---------------------------------------------------------------------------

void CustomCtrl::SetHwnd (QWidget *hCtrl)
{
	if (hWnd) hWnd->removeEventFilter (this);
	hWnd = hCtrl;
	hWnd->installEventFilter (this);

	hParent = hCtrl->parentWidget();
}

// ---------------------------------------------------------------------------

bool CustomCtrl::WndProc (QWidget *hWnd, QEvent *event)
{
	return false;
}

// ---------------------------------------------------------------------------

static QWidget *CreateDlgCtrl (const RESCONTROL*, QWidget *parent)
{
	return new QWidget (parent);
}

void CustomCtrl::RegisterClass(void *hInstance)
{
	oapiRegisterResControl (hInstance, "OrbiterDlgCtrl", CreateDlgCtrl);
}

// ---------------------------------------------------------------------------

bool CustomCtrl::eventFilter (QObject *obj, QEvent *event)
{
	if (obj == hWnd) return WndProc (hWnd, event);
	return false;
}

// ===========================================================================

GenericCtrl::GenericCtrl()
	: CustomCtrl()
{
}

GenericCtrl::GenericCtrl(QWidget *hCtrl)
	: CustomCtrl(hCtrl)
{
}

// ===========================================================================

SplitterCtrl::SplitterCtrl (): CustomCtrl ()
{
	staticPane = PANE_NONE;
	splitterW = 6;
	widthRatio = 0.5;
	isPushing = false;
}

// ---------------------------------------------------------------------------

SplitterCtrl::SplitterCtrl (QWidget *hCtrl): CustomCtrl (hCtrl)
{
	staticPane = PANE_NONE;
	splitterW = 6;
	widthRatio = 0.5;
	isPushing = false;
}

// ---------------------------------------------------------------------------

void SplitterCtrl::SetHwnd (QWidget *hCtrl, QWidget *hPane1, QWidget *hPane2)
{
	CustomCtrl::SetHwnd (hCtrl);
	hCtrl->setCursor (Qt::SizeHorCursor); // WM_SETCURSOR
	hPane[0] = hPane1;
	hPane[1] = hPane2;
	totalW = hCtrl->width();
	paneW[0] = min (hPane1->width(), totalW-splitterW-4);
	paneW[1] = totalW-splitterW-paneW[0];
	widthRatio = (double)paneW[0]/(double)(totalW-splitterW);
	Refresh();
}

// ---------------------------------------------------------------------------

void SplitterCtrl::SetStaticPane (PaneId which, int width)
{
	staticPane = which;
	if (which != PANE_NONE) {
		if (!width) { // use current width
			width = hPane[which-1]->width();
		}
		paneW[which-1] = width;
		paneW[2-which] = totalW-splitterW-width;
		Refresh();
	}
}

// ---------------------------------------------------------------------------

int SplitterCtrl::GetPaneWidth (PaneId which)
{
	switch (which) {
	case PANE_NONE: return totalW;
	default:        return paneW[which-1];
	}
}

// ---------------------------------------------------------------------------

void SplitterCtrl::Refresh ()
{
	RECT r1, r2;
	r1 = r2 = GetClientPos (hParent, hWnd);
	r1.right = r1.left+paneW[0];
	r2.left = r2.right-paneW[1];
	SetClientPos (hParent, hPane[0], r1);
	SetClientPos (hParent, hPane[1], r2);
}

// ---------------------------------------------------------------------------

BOOL SplitterCtrl::OnSize (QWidget *hWnd, int w, int h)
{
	int w1, w2;
	switch (staticPane) {
	case PANE_NONE: // retain relative widths
		w1 = (int)((w-splitterW)*widthRatio);
		w2 = w-splitterW-w1;
		break;
	case PANE1:
		w1 = min (paneW[0], w-splitterW-4);
		w2 = w-splitterW-w1;
		break;
	case PANE2:
		w2 = min (paneW[1], w-splitterW-4);
		w1 = w-splitterW-w2;
		break;
	}
	paneW[0] = w1;
	paneW[1] = w2;
	totalW = w;
	Refresh ();
	return 0;
}

// ---------------------------------------------------------------------------

BOOL SplitterCtrl::OnLButtonDown (QWidget *hWnd, Qt::KeyboardModifiers modifier, short x, short y)
{
	isPushing = true;
	mouseX = x;
	mouseY = y;
	return 0; // Qt grabs the mouse for the pressed widget (SetCapture)
}

// ---------------------------------------------------------------------------

BOOL SplitterCtrl::OnLButtonUp (QWidget *hWnd, Qt::KeyboardModifiers modifier, short x, short y)
{
	isPushing = false;
	return 0;
}

// ---------------------------------------------------------------------------

BOOL SplitterCtrl::OnMouseMove (QWidget *hWnd, short x, short y)
{
	if (isPushing) {
		short dx = x - mouseX;
		if (dx) {
			if (dx < 0) {
				paneW[0] = max (4, paneW[0]+dx);
				paneW[1] = totalW-splitterW-paneW[0];
			} else {
				paneW[1] = max (4, paneW[1]-dx);
				paneW[0] = totalW-splitterW-paneW[1];
			}
			Refresh ();
			mouseX = x;
		}
	}
	return 0;
}

// ---------------------------------------------------------------------------

bool SplitterCtrl::WndProc (QWidget *hWnd, QEvent *event)
{
	QMouseEvent *me = static_cast<QMouseEvent*> (event);
	switch (event->type()) {
	case QEvent::Resize: {
		QSize s = static_cast<QResizeEvent*> (event)->size();
		OnSize (hWnd, s.width(), s.height());
		} return false;
	case QEvent::MouseButtonPress:
		if (me->button() != Qt::LeftButton) break;
		OnLButtonDown (hWnd, me->modifiers(), (short)me->position().x(), (short)me->position().y());
		return true;
	case QEvent::MouseButtonRelease:
		if (me->button() != Qt::LeftButton) break;
		OnLButtonUp (hWnd, me->modifiers(), (short)me->position().x(), (short)me->position().y());
		return true;
	case QEvent::MouseMove:
		OnMouseMove (hWnd, (short)me->position().x(), (short)me->position().y());
		return true;
	default:
		break;
	}
	return CustomCtrl::WndProc (hWnd, event);
}
