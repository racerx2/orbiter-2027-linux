// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#include "DlgCtrl.h"
#include "DlgCtrlLocal.h"
#include <QMouseEvent>
#include <QPainter>

extern GDIRES g_GDI;
void GdiRectangle (QPainter &p, int l, int t, int r, int b);

SwitchCtrl::SwitchCtrl (QWidget *parent): QWidget (parent)
{
	QPalette pal = palette();
	pal.setColor (QPalette::Window, g_GDI.hBrush2); // class background
	setPalette (pal);
	setAutoFillBackground (true);
}

// GDI Ellipse (l,t,r,b) with its [l,r-1] x [t,b-1] bounding box
static void GdiEllipse (QPainter &p, int l, int t, int r, int b)
{
	p.drawEllipse (l, t, r-l-1, b-t-1);
}

void SwitchCtrl::paintEvent (QPaintEvent*)
{
	QPoint pt[6];
	int w, h, xc, yc;
	QPainter p (this);
	w = width(); h = height();
	xc = w/2; yc = h/2;
	const QBrush ltgray (QColor (0xc0, 0xc0, 0xc0)), gray (QColor (0x80, 0x80, 0x80)), white (Qt::white);
	bool vert = (!(flag & 0x4));
	if (vert) {
		int rad = (xc*8)/10;
		int irad = xc/2;
		int d = (int)(0.57735*(xc-1));
		int e = (int)(1.1547*(xc-1));
		int f = (xc*2)/5;
		int f2 = (xc*5)/10;
		int f3 = (xc*6)/10;
		int g1 = yc-1;
		int g2 = g1-xc/3;
		pt[0].setX (1);         pt[5].setX (1);         pt[0].setY (yc-d); pt[2].setY (yc-d);
		pt[1].setX (xc);        pt[4].setX (xc);        pt[5].setY (yc+d); pt[3].setY (yc+d);
		pt[2].setX (2*xc-1);    pt[3].setX (2*xc-1);    pt[1].setY (yc-e); pt[4].setY (yc+e);
		p.setBrush (ltgray);
		p.setPen (Qt::white);
		p.drawPolygon (pt, 6);
		p.setPen (g_GDI.hPen2);
		p.drawPolyline (pt+2, 4);
		p.setPen (Qt::white);
		p.setBrush (ltgray);
		GdiEllipse (p, xc-rad, yc-rad, xc+rad+1, yc+rad+1);
		p.setBrush (gray);
		GdiEllipse (p, xc-irad, yc-irad, xc+irad+1, yc+irad+1);
		p.setPen (g_GDI.hPen2);
		p.drawArc (xc-rad, yc-rad, 2*rad, 2*rad, 225*16, 180*16);   // lower right half
		p.drawArc (xc-irad, yc-irad, 2*irad, 2*irad, 45*16, 180*16); // upper left half
		if (pos == 0) {
			p.setPen (g_GDI.hPen2);
			p.setBrush (ltgray);
			pt[0].setX (xc-f); pt[1].setX (xc-f2); pt[2].setX (xc+f2); pt[3].setX (xc+f);
			pt[0].setY (yc+1); pt[3].setY (yc+1); pt[1].setY (yc-g2); pt[2].setY (yc-g2);
			p.drawPolygon (pt, 4);
			p.setBrush (white);
			GdiRectangle (p, xc-f2, yc-g1, xc+f2+1, yc-g2);
			p.setPen (Qt::white);
			p.drawPolyline (QPolygon ({QPoint (xc-f, yc+1), QPoint (xc-f2, yc-g2), QPoint (xc-f2, yc-g1), QPoint (xc+f2, yc-g1)}));
		} else if (pos == 1) {
			p.setPen (g_GDI.hPen2);
			p.setBrush (white);
			pt[0].setX (xc-f); pt[1].setX (xc-f2); pt[2].setX (xc+f2); pt[3].setX (xc+f);
			pt[0].setY (yc-1); pt[3].setY (yc-1); pt[1].setY (yc+g2); pt[2].setY (yc+g2);
			p.drawPolygon (pt, 4);
			p.setBrush (ltgray);
			GdiRectangle (p, xc-f2, yc+g2, xc+f2+1, yc+g1);
			p.setPen (Qt::white);
			p.drawPolyline (QPolygon ({QPoint (xc-f, yc-1), QPoint (xc-f2, yc+g2), QPoint (xc-f2, yc+g1), QPoint (xc+f2, yc+g1)}));
		} else {
			p.setPen (g_GDI.hPen2);
			pt[0].setX (xc-f); pt[1].setX (xc-f3); pt[2].setX (xc+f3); pt[3].setX (xc+f);
			pt[0].setY (yc-(xc/2)); pt[3].setY (yc-(xc/2)); pt[1].setY (yc-xc/4); pt[2].setY (yc-xc/4);
			p.setBrush (ltgray);
			p.drawPolygon (pt, 4);
			pt[0].setY (yc+xc/2); pt[3].setY (yc+xc/2); pt[1].setY (yc+xc/4); pt[2].setY (yc+xc/4);
			p.setBrush (gray);
			p.drawPolygon (pt, 4);
			p.setBrush (white);
			GdiRectangle (p, xc-f3, yc-xc/4, xc+f3+1, yc+xc/4+1);
			p.setPen (Qt::white);
		}
	}
}

void SwitchCtrl::mousePressEvent (QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton) return;
	int y = (int)event->position().y();
	int npos = pos;
	bool is3 = ((flag & 0x1) != 0);
	if (y >= height()/2) {
		if (pos == 0) npos = (is3 ? 2 : 1);
		else if (pos == 2) npos = 1;
	} else {
		if (pos == 1) npos = (is3 ? 2 : 0);
		else if (pos == 2) npos = 0;
	}
	if (pos != npos) {
		pos = npos;
		update();
		emit clicked (npos);
	}
}

void oapiSetSwitchParams (QWidget *hCtrl, SWITCHPARAM *sp, bool redraw)
{
	SwitchCtrl *s = qobject_cast<SwitchCtrl*> (hCtrl);
	if (!s) return;
	DWORD flag = 0;
	if (sp->mode  == SWITCHPARAM::THREESTATE) flag |= 0x1;
	if (sp->align == SWITCHPARAM::HORIZONTAL) flag |= 0x4;
	s->pos = 0;
	s->flag = flag;
}

int oapiSetSwitchState (QWidget *hCtrl, int state, bool redraw)
{
	SwitchCtrl *s = qobject_cast<SwitchCtrl*> (hCtrl);
	if (!s) return -1;
	if (state < 0 || state > 2) return -1;
	if (state == 2) {
		if (!(s->flag & 0x1)) return -1;
	}
	s->pos = state;
	if (redraw) s->update();
	return state;
}

int oapiGetSwitchState (QWidget *hCtrl)
{
	SwitchCtrl *s = qobject_cast<SwitchCtrl*> (hCtrl);
	return (s ? s->pos : 0);
}
