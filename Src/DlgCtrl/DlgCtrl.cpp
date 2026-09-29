// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#include "DlgCtrl.h"
#include "DlgCtrlLocal.h"
#include "OrbiterResource.h"
#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <stdio.h>
#include <string.h>
#include <algorithm>

using std::min;
using std::max;

GDIRES g_GDI;

static QWidget *CreateGauge (const RESCONTROL*, QWidget *parent) { return new GaugeCtrl (parent); }
static QWidget *CreateSwitch (const RESCONTROL*, QWidget *parent) { return new SwitchCtrl (parent); }

void oapiRegisterCustomControls (void *hInst)
{
	QPalette pal = QApplication::palette();
	g_GDI.hPen1 = QColor (0x40, 0x40, 0x40);
	g_GDI.hPen2 = pal.color (QPalette::Dark);    // COLOR_3DSHADOW
	g_GDI.hBrush1 = QColor (0xff, 0x00, 0x00);
	g_GDI.hBrush2 = pal.color (QPalette::Button); // COLOR_3DFACE

	// Register window class for level indicator
	oapiRegisterResControl (hInst, "OrbiterCtrl_Gauge", CreateGauge);

	// Register window class for switch
	oapiRegisterResControl (hInst, "OrbiterCtrl_Switch", CreateSwitch);

	// Register window class for property list
	RegisterPropertyList (hInst);
}

void oapiUnregisterCustomControls (void *hInst)
{
	oapiUnregisterResControl (hInst, "OrbiterCtrl_Gauge");
	oapiUnregisterResControl (hInst, "OrbiterCtrl_Switch");
	UnregisterPropertyList (hInst);
}

// GDI Rectangle (l,t,r,b): outline and fill cover [l,r-1] x [t,b-1]
void GdiRectangle (QPainter &p, int l, int t, int r, int b)
{
	if (p.pen().style() == Qt::NoPen) p.fillRect (l, t, r-l-1, b-t-1, p.brush());
	else p.drawRect (l, t, r-l-1, b-t-1);
}

GaugeCtrl::GaugeCtrl (QWidget *parent): QWidget (parent)
{
	QPalette pal = palette();
	pal.setColor (QPalette::Window, QColor (0xc0, 0xc0, 0xc0)); // LTGRAY_BRUSH class background
	setPalette (pal);
	setAutoFillBackground (true);
	timer = new QTimer (this);
	connect (timer, &QTimer::timeout, this, &GaugeCtrl::OnTimer);
}

void GaugeCtrl::paintEvent (QPaintEvent*)
{
	QPainter p (this);
	int bw, gw, x0, y0, dd;
	int rr = width(), rb = height();
	bool horz  = ((flag & 2) == 0);
	bool enabled = isEnabled();

	p.setPen (g_GDI.hPen1);
	p.setBrush (enabled ? QBrush (Qt::white) : QBrush (Qt::NoBrush));
	if (horz) {
		bw = rb; dd = bw / 4; gw = rr - 2 * bw;
		GdiRectangle (p, 0, 0, bw, bw);
		GdiRectangle (p, rr - bw, 0, rr, bw);
		x0 = dd, y0 = bw / 2;
		p.drawPolyline (QPolygon ({QPoint (x0, y0), QPoint (x0 + dd, y0 - dd), QPoint (x0 + dd, y0 + dd), QPoint (x0, y0)}));
		x0 = rr - dd - 1, y0 = bw / 2;
		p.drawPolyline (QPolygon ({QPoint (x0, y0), QPoint (x0 - dd, y0 - dd), QPoint (x0 - dd, y0 + dd), QPoint (x0, y0)}));
	} else {
		bw = rr; dd = bw / 4;  gw = rb - 2 * bw;
		GdiRectangle (p, 0, 0, bw, bw);
		GdiRectangle (p, 0, rb - bw, bw, rb);
		x0 = bw / 2, y0 = dd;
		p.drawPolyline (QPolygon ({QPoint (x0, y0), QPoint (x0 + dd, y0 + dd), QPoint (x0 - dd, y0 + dd), QPoint (x0, y0)}));
		x0 = bw / 2, y0 = rb - dd - 1;
		p.drawPolyline (QPolygon ({QPoint (x0, y0), QPoint (x0 - dd, y0 - dd), QPoint (x0 + dd, y0 - dd), QPoint (x0, y0)}));
	}

	p.setPen (Qt::NoPen);
	p.setBrush ((flag & 0x40) ? QBrush (g_GDI.hBrush1) : QBrush (enabled ? QColor (Qt::black) : QColor (0x80, 0x80, 0x80)));
	if (rmax > rmin) {
		switch (flag & 3) { // direction flag
		case 0: // left to right
			GdiRectangle (p, bw, 0, bw+(gw*(pos-rmin))/(rmax-rmin) + 1, bw + 1);
			break;
		case 1: // right to left
			GdiRectangle (p, bw+gw-(gw*(pos-rmin))/(rmax-rmin), 0, bw+gw + 1, bw + 1);
			break;
		case 2: // top to bottom
			GdiRectangle (p, 0, bw, bw + 1, bw+(gw*(pos-rmin))/(rmax-rmin) + 1);
			break;
		case 3: // bottom to top
			GdiRectangle (p, 0, bw+gw-(gw*(pos-rmin))/(rmax-rmin), bw + 1, bw+gw + 1);
			break;
		}
	}
}

void GaugeCtrl::mousePressEvent (QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton) return;
	int x = (int)event->position().x();
	int y = (int)event->position().y();
	int bw, gw;
	bool horz = ((flag & 2) == 0);
	if (horz) {
		bw = height(); gw = width() - 2*bw;
		if (x < bw) {
			flag |= 4;                        // flag for left button pressed
			flag |= ((flag & 1) ? 0x20:0x10); // flag for inc/dec button pressed
		} else if (x >= gw+bw) {
			flag |= 8;                        // flag for right button pressed
			flag |= ((flag & 1) ? 0x10:0x20); // flag for inc/dec button pressed
		} else {
			flag |= 12;                       // flag for slider area pressed
		}
	} else {
		bw = width(); gw = height() - 2*bw;
		if (y < bw) {
			flag |= 4;                        // flag for top button pressed
			flag |= ((flag & 1) ? 0x20:0x10); // flag for inc/dec button pressed
		} else if (y >= gw+bw) {
			flag |= 8;                        // flag for bottom button pressed
			flag |= ((flag & 1) ? 0x10:0x20); // flag for inc/dec button pressed
		} else {
			flag |= 12;                       // flag for slider area pressed
		}
	}
	// Qt keeps the mouse grabbed by this widget until the release (SetCapture)
	if ((flag & 12) == 12) {
		DragSlider (x, y);
		emit scrolled (GAUGE_THUMBTRACK, pos);
	} else {
		timer->start (min (1000, max (1, 3000/max (1, rmax-rmin))));
		OnTimer();
	}
}

void GaugeCtrl::mouseReleaseEvent (QMouseEvent*)
{
	timer->stop();
	flag &= 0xFFFFFFC3; // clear mouse selection state
}

void GaugeCtrl::mouseMoveEvent (QMouseEvent *event)
{
	if ((flag & 12) == 12) {
		DragSlider ((int)event->position().x(), (int)event->position().y());
		emit scrolled (GAUGE_THUMBTRACK, pos);
	}
}

void GaugeCtrl::changeEvent (QEvent *event)
{
	if (event->type() == QEvent::EnabledChange) update();
	QWidget::changeEvent (event);
}

void GaugeCtrl::OnTimer ()
{
	switch ((flag >> 4) & 3) {
	case 1: // decrease
		if (pos > rmin) --pos;
		update();
		emit scrolled (GAUGE_LINEDEC, pos);
		break;
	case 2: // increase
		if (pos < rmax) ++pos;
		update();
		emit scrolled (GAUGE_LINEINC, pos);
		break;
	}
}

void GaugeCtrl::DragSlider (int x, int y)
{
	int bw, gw, p;
	bool horz = ((flag & 2) == 0);
	if (horz) {
		bw = height(); gw = width() - 2*bw;
		p = x-bw;
	} else {
		bw = width(); gw = height() - 2*bw;
		p = y-bw;
	}
	if (gw <= 0) return;
	if (p < 0) p = 0; else if (p >= gw) p = gw;
	if (flag & 1) p = gw-p;

	if (rmax > rmin) {
		pos = (p*(rmax-rmin))/gw+rmin;
		update();
	}
}

void oapiSetGaugeParams (QWidget *hCtrl, GAUGEPARAM *gp, bool redraw)
{
	GaugeCtrl *g = qobject_cast<GaugeCtrl*> (hCtrl);
	if (!g) return;
	g->rmin = gp->rangemin;
	g->rmax = gp->rangemax;

	if      (g->pos < gp->rangemin) g->pos = gp->rangemin;
	else if (g->pos > gp->rangemax) g->pos = gp->rangemax;

	DWORD flag = 0;
	switch (gp->base) {
	case GAUGEPARAM::LEFT:   flag = 0; break;
	case GAUGEPARAM::RIGHT:  flag = 1; break;
	case GAUGEPARAM::TOP:    flag = 2; break;
	case GAUGEPARAM::BOTTOM: flag = 3; break;
	}
	switch (gp->color) {
	case GAUGEPARAM::BLACK:                break;
	case GAUGEPARAM::RED:    flag |= 0x40; break;
	}
	g->flag = flag;

	if (redraw) g->update();
}

void oapiSetGaugeRange (QWidget *hCtrl, int rmin, int rmax, bool redraw)
{
	GaugeCtrl *g = qobject_cast<GaugeCtrl*> (hCtrl);
	if (!g) return;
	g->rmin = rmin;
	g->rmax = rmax;

	if      (g->pos < rmin) g->pos = rmin;
	else if (g->pos > rmax) g->pos = rmax;

	if (redraw) g->update();
}

int oapiSetGaugePos (QWidget *hCtrl, int pos, bool redraw)
{
	GaugeCtrl *g = qobject_cast<GaugeCtrl*> (hCtrl);
	if (!g) return pos;
	if      (pos < g->rmin) pos = g->rmin;
	else if (pos > g->rmax) pos = g->rmax;

	if (pos != g->pos) {
		g->pos = pos;
		if (redraw) g->update();
	}

	return pos;
}

int oapiIncGaugePos (QWidget *hCtrl, int dpos, bool redraw)
{
	GaugeCtrl *g = qobject_cast<GaugeCtrl*> (hCtrl);
	if (!g) return 0;
	int pos  = g->pos + dpos;

	if      (pos < g->rmin) pos = g->rmin;
	else if (pos > g->rmax) pos = g->rmax;

	g->pos = pos;
	if (redraw) g->update();

	return pos;
}

int oapiGetGaugePos (QWidget *hCtrl)
{
	GaugeCtrl *g = qobject_cast<GaugeCtrl*> (hCtrl);
	return (g ? g->pos : 0);
}

// ==================================================================================
// ==================================================================================

PropertyItem::PropertyItem (PropertyGroup *grp)
{
	label = NULL;
	value = NULL;
	labelw = -1;
	valuew = -1;
	label_dirty = false;
	value_dirty = false;
	group = grp;
}

PropertyItem::~PropertyItem ()
{
	if (label) delete []label;
	if (value) delete []value;
}

void PropertyItem::SetLabel (const char *newlabel)
{
	label_dirty = false;
	if (newlabel && label) {
		label_dirty = (strcmp (label, newlabel) != 0);
	} else if (newlabel || label) {
		label_dirty = true;
	}
	if (label_dirty) {
		if (label) delete []label;
		if (newlabel) {
			label = new char[strlen(newlabel)+1];
			strcpy (label, newlabel);
		} else label = NULL;
		if (!group->IsExpanded()) label_dirty = false;
	}
}

void PropertyItem::SetValue (const char *newvalue)
{
	value_dirty = false;
	if (newvalue && value) {
		value_dirty = (strcmp (value, newvalue) != 0);
	} else if (newvalue || value) {
		value_dirty = true;
	}
	if (value_dirty) {
		if (value) delete []value;
		if (newvalue) {
			value = new char[strlen(newvalue)+1];
			strcpy (value, newvalue);
		} else value = NULL;
		if (!group->IsExpanded()) value_dirty = false;
	}
}

// ==================================================================================

PropertyGroup::PropertyGroup (PropertyList *list, bool expand)
{
	plist = list;
	item = NULL;
	nitem = 0;
	title = NULL;
	expanded = expand;
}

PropertyGroup::~PropertyGroup ()
{
	if (nitem) {
		for (int i = 0; i < nitem; i++)
			delete item[i];
		delete []item;
	}
	if (title) delete []title;
}

PropertyItem *PropertyGroup::AppendItem ()
{
	PropertyItem *newitem = new PropertyItem (this);
	PropertyItem **tmp = new PropertyItem*[nitem+1];
	if (nitem) {
		memcpy (tmp, item, nitem*sizeof(PropertyItem*));
		delete []item;
	}
	item = tmp;
	item[nitem++] = newitem;
	return newitem;
}

PropertyItem *PropertyGroup::GetItem (int idx)
{
	if (idx >= 0 && idx < nitem)
		return item[idx];
	else
		return NULL;
}

void PropertyGroup::SetTitle (const char *t)
{
	if (title) delete []title;
	if (t) {
		title = new char[strlen(t)+1];
		strcpy (title, t);
	} else
		title = NULL;
}

void PropertyGroup::Expand (bool expand)
{
	if (expand != expanded) {
		expanded = expand;
		// more stuff
	}
}

// ==================================================================================

int PropertyList::titleh = 20;
int PropertyList::itemh = 18;
int PropertyList::gaph = 6;
QImage *PropertyList::hBmpArrows = NULL;

PropertyList::PropertyList ()
{
	npg = 0;
	listh = 0;
	yofs = 0;
	valx0 = 0;
	hFontTitle = NULL;
	hFontItem = NULL;
	hPenLine = NULL;
	hBrushTitle = NULL;
	hItem = NULL;
}

PropertyList::~PropertyList ()
{
	if (npg) {
		for (int i = 0; i < npg; i++)
			delete pg[i];
		delete []pg;
	}
	if (hFontTitle) delete hFontTitle;
	if (hFontItem) delete hFontItem;
	if (hPenLine) delete hPenLine;
	if (hBrushTitle) delete hBrushTitle;
	if (hItem) hItem->plist = NULL;
}

void PropertyList::OnInitDialog (QWidget *hWnd, int nIDDlgItem)
{
	hDlg = hWnd;
	dlgid = nIDDlgItem;
	hItem = qobject_cast<PropertyListCtrl*> (oapiResDlgItem (hDlg, dlgid));
	if (!hItem) return;
	hItem->plist = this;
	winw = hItem->viewport()->width();
	winh = hItem->viewport()->height();
	if (!valx0) valx0 = winw/2;
	hFontTitle = new QFont ("Arial");
	hFontTitle->setPixelSize (13); // 15 pixel cell height
	hFontTitle->setBold (true);
	hFontItem = new QFont ("Arial");
	hFontItem->setPixelSize (13);
	hPenLine = new QPen (QColor (0xD0, 0xD0, 0xD0));
	hBrushTitle = new QBrush (QColor (0xE0, 0xE0, 0xFF));
	hItem->verticalScrollBar()->setRange (0, 0);
	hItem->verticalScrollBar()->setValue (0);
}

// TextOut counterpart: text cell at (x,y) top left, background filled (OPAQUE mode)
static void TextOut (QPainter &p, int x, int y, const char *str)
{
	QFontMetrics fm (p.font());
	QString s = QString::fromLatin1 (str);
	p.fillRect (x, y, fm.horizontalAdvance (s), fm.height(), p.background());
	p.drawText (x, y + fm.ascent(), s);
}

void GdiRectangle (QPainter &p, int l, int t, int r, int b);

void PropertyList::OnPaint (QWidget *hWnd)
{
	int i, j, y;

	QPainter p (hWnd);
	p.setFont (*hFontItem);
	p.setPen (*hPenLine);
	p.setBrush (*hBrushTitle);

	y = -yofs;
	for (i = 0; i < npg; i++) {
		bool expanded = pg[i]->IsExpanded();
		const char *title = pg[i]->GetTitle();
		if (title) {
			GdiRectangle (p, 18, y, winw-4, y+titleh-2);
			p.setFont (*hFontTitle);
			p.setPen (QColor (0x00, 0x00, 0xB0));
			p.setBackground (QColor (0xE0, 0xE0, 0xFF));
			TextOut (p, 20, y+1, title);
			if (hBmpArrows) p.drawImage (QPoint (2, y+2), *hBmpArrows, QRect (28+(expanded ? 0:14), 0, 14, 14));
			y += titleh;
			p.setPen (QColor (0x00, 0x00, 0x00));
			p.setFont (*hFontItem);
			p.setBackground (QColor (0xFF, 0xFF, 0xFF));
		}
		if (expanded) {
			for (j = 0; j < pg[i]->ItemCount(); j++) {
				const char *item = pg[i]->GetItem (j)->GetLabel();
				if (item)
					TextOut (p, 20, y, item);
				const char *value = pg[i]->GetItem (j)->GetValue();
				if (value)
					TextOut (p, valx0, y, value);
				if (j < pg[i]->ItemCount()-1) {
					p.save();
					p.setPen (*hPenLine);
					p.drawLine (20, y+itemh-2, winw-5, y+itemh-2);
					p.restore();
				}
				y += itemh;
			}
			y += gaph;
		}
	}
}

void PropertyList::OnSize (int w, int h)
{
	winw = w;
	winh = h;
	SetListHeight (listh, true);
}

// scroll bar moved to line p (the scroll bar handles line/page/thumb requests itself)
void PropertyList::OnVScroll (unsigned int cmd, int p)
{
	yofs = p * itemh;
	hItem->viewport()->update();
}

void PropertyList::OnLButtonDown (int x, int y)
{
	if (x >= 18) return;
	int i, ytitle = 0;
	y += yofs;
	for (i = 0; i < npg; i++) {
		if (y >= ytitle && y < ytitle+titleh) {
			ExpandGroup (pg[i], !pg[i]->IsExpanded());
			break;
		}
		ytitle += titleh;
		if (pg[i]->IsExpanded())
			ytitle += itemh * pg[i]->ItemCount() + gaph;
	}	
}

void PropertyList::VScrollTo (int pos)
{
	yofs = pos * itemh;
	hItem->verticalScrollBar()->setValue (pos);
	hItem->viewport()->update();
}

void PropertyList::Move (int x, int y, int w, int h)
{
	hItem->setGeometry (x, y, w, h);
}

void PropertyList::Redraw ()
{
	if (hItem) hItem->viewport()->update();
}

// repaints the rows whose label or value changed (the paint draws them from the item strings)
void PropertyList::Update ()
{
	if (!hItem) return;
	QFontMetrics fm (*hFontItem);

	int i, j, y = -yofs;

	for (i = 0; i < npg; i++) {
		y += titleh;
		if (pg[i]->IsExpanded()) {
			for (j = 0; j < pg[i]->ItemCount(); j++) {
				PropertyItem *item = pg[i]->GetItem (j);
				if (item->label_dirty || item->value_dirty) {
					if (y >= -itemh && y < listh) {
						if (item->label_dirty) {
							const char *label = item->GetLabel();
							int oldw = item->labelw;
							if (oldw < 0) oldw = winw;
							item->labelw = (label ? fm.horizontalAdvance (QString::fromLatin1 (label)) : 0);
							hItem->viewport()->update (20, y, max (oldw, item->labelw), itemh-2);
							item->label_dirty = false;
						}
						if (item->value_dirty) {
							const char *value = item->GetValue();
							int oldw = item->valuew;
							if (oldw < 0) oldw = winw;
							item->valuew = (value ? fm.horizontalAdvance (QString::fromLatin1 (value)) : 0);
							hItem->viewport()->update (valx0, y, max (oldw, item->valuew), itemh-2);
							item->value_dirty = false;
						}
					}
				}
				y += itemh;
			}
			y += gaph;
		}
	}
}

void PropertyList::SetColWidth (int col, int w)
{
	if (col) return;        // only col 0 is supported for now
	if (w == valx0) return; // nothing to do
	valx0 = w;
	Redraw();
}

PropertyGroup *PropertyList::AppendGroup (bool expand)
{
	PropertyGroup **tmp = new PropertyGroup*[npg+1];
	if (npg) {
		memcpy (tmp, pg, npg*sizeof(PropertyGroup*));
		delete []pg;
	}
	pg = tmp;
	pg[npg] = new PropertyGroup (this, expand);
	SetListHeight (listh + titleh + gaph);
	return pg[npg++];
}

bool PropertyList::DeleteGroup (PropertyGroup *g)
{
	int i, j, k, h;
	for (i = 0; i < npg; i++) {
		if (pg[i] == g) {
			h = listh - titleh - gaph;
			if (g->IsExpanded()) h -= g->ItemCount()*itemh;
			delete g;
			PropertyGroup **tmp;
			if (npg > 1) {
				tmp = new PropertyGroup*[npg-1];
				for (j = k = 0; j < npg; j++)
					if (j != i) tmp[k++] = pg[j];
			} else tmp = NULL;
			delete []pg;
			pg = tmp;
			npg--;
			SetListHeight (h);
			return true;
		}
	}
	return false;
}

PropertyGroup *PropertyList::GetGroup (int idx)
{
	if (idx >= 0 && idx < npg)
		return pg[idx];
	else
		return NULL;
}

bool PropertyList::ExpandGroup (PropertyGroup *g, bool expand)
{
	if (expand == g->IsExpanded()) return false; // nothing to do

	g->Expand (expand);
	int h = listh, dh = itemh * g->ItemCount() + gaph;
	if (expand) h += dh;
	else        h -= dh;
	SetListHeight (h);
	Redraw();

	return true;
}

void PropertyList::ExpandAll (bool expand)
{
	int h = 0;
	for (int i = 0; i < npg; i++) {
		pg[i]->Expand (expand);
		h += titleh;
		if (expand)
			h += gaph + itemh * pg[i]->ItemCount();
	}
	SetListHeight (h);
	Redraw();
}

void PropertyList::ClearGroups ()
{
	if (npg) {
		for (int i = 0; i < npg; i++)
			delete pg[i];
		delete []pg;
		npg = 0;
		SetListHeight (0);
	}
}

PropertyItem *PropertyList::AppendItem (PropertyGroup *g)
{
	PropertyItem *item = g->AppendItem ();
	if (g->IsExpanded())
		SetListHeight (listh + itemh);
	return item;
}

void PropertyList::SetListHeight (int h, bool force)
{
	if (h == listh && !force) return; // nothing to do
	listh = h;
	int rmax = 0;
	if (listh > winh)
		rmax = (listh-winh+itemh-1)/itemh;
	if (!hItem) return;
	QScrollBar *sb = hItem->verticalScrollBar();
	int pos = sb->value();
	sb->setPageStep (winh/itemh);
	sb->setSingleStep (1);
	sb->setRange (0, rmax);

	int pos2 = sb->value();
	if (pos2 != pos)
		VScrollTo (pos2);
}

// ==================================================================================

PropertyListCtrl::PropertyListCtrl (QWidget *parent): QAbstractScrollArea (parent)
{
	QPalette pal = viewport()->palette();
	pal.setColor (QPalette::Window, Qt::white); // WHITE_BRUSH class background
	viewport()->setPalette (pal);
	viewport()->setAutoFillBackground (true);
	setFrameStyle (QFrame::NoFrame);
}

void PropertyListCtrl::paintEvent (QPaintEvent*)
{
	if (plist) plist->OnPaint (viewport());
}

void PropertyListCtrl::resizeEvent (QResizeEvent *event)
{
	QAbstractScrollArea::resizeEvent (event);
	if (plist) plist->OnSize (viewport()->width(), viewport()->height());
}

void PropertyListCtrl::mousePressEvent (QMouseEvent *event)
{
	if (plist && event->button() == Qt::LeftButton)
		plist->OnLButtonDown ((int)event->position().x(), (int)event->position().y());
}

void PropertyListCtrl::scrollContentsBy (int, int)
{
	if (plist) plist->OnVScroll (0, verticalScrollBar()->value());
}
