
// custom: forms skins; widget classes the builder makes for forms (text fitting, buttons that don't toggle, flow layout, glow)

#include "FormWidgets.h"
#include <QEvent>
#include <QFontMetricsF>
#include <QPaintEvent>
#include <QPainter>
#include <QStyle>
#include <QTextDocument>
#include <QVariant>
#include <algorithm>
#include <climits>
#include <cmath>

namespace forms {

namespace {

	// calls a widget's own event () (virtual), from a filter that took the event
	struct EventAccess: QWidget {
		using QWidget::event;
	};

	double Prop (const QWidget *w, const char *name, double def)
	{
		QVariant v = w->property (name);
		bool ok = false;
		double d = v.toDouble (&ok);
		return (v.isValid () && ok && std::isfinite (d) ? d : def);
	}

}

// ---------------------------------------------------------------------------------------------------------
// FormLabel

FormLabel::FormLabel (QWidget *parent): QLabel (parent)
{
	setTextFormat (Qt::PlainText); // QML Text showed names and descriptions as plain text
}

void FormLabel::setMaxLines (int n)
{
	n = std::clamp (n, 0, 1000);
	if (n == m_maxLines) return;
	m_maxLines = n;
	Changed ();
}

void FormLabel::setLineHeight (double f)
{
	f = (std::isfinite (f) ? std::clamp (f, 0.0, 10.0) : 0.0);
	if (f == m_lineHeight) return;
	m_lineHeight = f;
	Changed ();
}

void FormLabel::setElide (bool on)
{
	if (on == m_elide) return;
	m_elide = on;
	Changed ();
}

void FormLabel::Changed ()
{
	cacheWidth = -1;
	cacheKey.clear ();
	updateGeometry ();
	update ();
}

bool FormLabel::event (QEvent *e)
{
	switch (e->type ()) {
	case QEvent::FontChange:
	case QEvent::StyleChange:
	case QEvent::ContentsRectChange:
		cacheWidth = -1;
		cacheKey.clear ();
		break;
	default:
		break;
	}
	return QLabel::event (e);
}

FormLabel::Fit FormLabel::Layout (int width) const
{
	const QString t = text ();
	const QString key = t + QChar (0) + font ().key () + QString::number (font ().letterSpacing ()) + QChar (0) + QString::number (wordWrap ());
	if (width == cacheWidth && key == cacheKey) return cache;
	Fit f;
	QFontMetricsF fm (font ());
	f.spacing = fm.height () * (m_lineHeight > 0.0 ? m_lineHeight : 1.0);
	const bool wrap = wordWrap () || m_maxLines > 1;
	const int maxL = (!wrap ? 1 : m_maxLines > 0 ? m_maxLines : INT_MAX);
	const double w = std::max (1, width);
	if (!wrap) {
		QString line = t;
		const int nl = t.indexOf ('\n');
		if ((m_elide || m_maxLines > 0) && nl >= 0) {
			line = t.left (nl); // as QML Text: one elided line ends at the first line break
			while (line.endsWith (' ')) line.chop (1);
			const QString more = line + QChar (0x2026);
			line = (fm.horizontalAdvance (more) <= w ? more : fm.elidedText (line + ' ' + t.mid (nl + 1), Qt::ElideRight, w));
		} else {
			line.replace ('\n', ' ');
			if (m_elide || m_maxLines > 0) line = fm.elidedText (line, Qt::ElideRight, w);
		}
		f.lines << line;
		f.width = fm.horizontalAdvance (line);
	} else {
		QString lt = t;
		lt.replace ('\n', QChar::LineSeparator); // QTextLayout only breaks at the Unicode line separator
		QTextLayout lay (lt, font ());
		QTextOption opt;
		opt.setWrapMode (QTextOption::WrapAtWordBoundaryOrAnywhere);
		lay.setTextOption (opt);
		lay.beginLayout ();
		std::vector<std::pair<int, int>> spans;
		for (;;) {
			QTextLine line = lay.createLine ();
			if (!line.isValid ()) break;
			line.setLineWidth (w);
			spans.push_back ({line.textStart (), line.textLength ()});
			if ((int)spans.size () >= maxL) break;
		}
		lay.endLayout ();
		for (size_t i = 0; i < spans.size (); i++) {
			const bool last = (i + 1 == spans.size ());
			const int end = spans[i].first + spans[i].second;
			QString s = t.mid (spans[i].first, spans[i].second);
			if (last && end < t.size ()) { // more text than lines: the last line ends with an ellipsis
				QString head = s;
				const bool brk = head.endsWith ('\n'); // as QML: a line that ended at a line break keeps its text
				while (head.endsWith (' ') || head.endsWith ('\n')) head.chop (1);
				if (brk && fm.horizontalAdvance (head + QChar (0x2026)) <= w) s = head + QChar (0x2026);
				else {
					s = t.mid (spans[i].first);
					s.replace ('\n', ' ');
					s = fm.elidedText (s, Qt::ElideRight, w);
				}
			}
			while (s.endsWith (' ') || s.endsWith ('\n')) s.chop (1);
			f.lines << s;
			f.width = std::max (f.width, fm.horizontalAdvance (s));
		}
		if (f.lines.isEmpty ()) f.lines << QString ();
	}
	f.height = f.spacing * f.lines.size ();
	cacheWidth = width;
	cacheKey = key;
	cache = f;
	return f;
}

static bool RichText (const QLabel *l)
{
	return l->textFormat () == Qt::RichText || (l->textFormat () == Qt::AutoText && Qt::mightBeRichText (l->text ()));
}

QSize FormLabel::sizeHint () const
{
	if (!Fitted () || RichText (this)) return QLabel::sizeHint ();
	const QMargins m = contentsMargins ();
	const int mx = m.left () + m.right () + 2 * margin (), my = m.top () + m.bottom () + 2 * margin ();
	QFontMetricsF fm (font ());
	const bool wrap = wordWrap () || m_maxLines > 1;
	double nat = 0.0;
	if (wrap) {
		for (const QString &s : text ().split ('\n')) nat = std::max (nat, fm.horizontalAdvance (s));
	} else {
		QString one = text ();
		one.replace ('\n', ' ');
		nat = fm.horizontalAdvance (one);
	}
	int natural = (int)std::ceil (nat);
	int w = natural;
	if (wrap) w = std::min (natural, std::max (QLabel::sizeHint ().width () - mx, 80));
	Fit f = Layout (w);
	return QSize (w + mx, (int)std::ceil (f.height) + my);
}

QSize FormLabel::minimumSizeHint () const
{
	if (!Fitted () || RichText (this)) return QLabel::minimumSizeHint ();
	const QMargins m = contentsMargins ();
	QFontMetricsF fm (font ());
	const double spacing = fm.height () * (m_lineHeight > 0.0 ? m_lineHeight : 1.0);
	return QSize (m.left () + m.right () + 2 * margin (), (int)std::ceil (spacing) + m.top () + m.bottom () + 2 * margin ());
}

bool FormLabel::hasHeightForWidth () const
{
	if (!Fitted () || RichText (this)) return QLabel::hasHeightForWidth ();
	return wordWrap () || m_maxLines > 1;
}

int FormLabel::heightForWidth (int w) const
{
	if (!Fitted () || RichText (this)) return QLabel::heightForWidth (w);
	const QMargins m = contentsMargins ();
	const int mx = m.left () + m.right () + 2 * margin (), my = m.top () + m.bottom () + 2 * margin ();
	return (int)std::ceil (Layout (std::max (1, w - mx)).height) + my;
}

void FormLabel::paintEvent (QPaintEvent *e)
{
	if (!Fitted () || RichText (this) || !pixmap ().isNull ()) {
		QLabel::paintEvent (e);
		return;
	}
	QPainter p (this);
	drawFrame (&p); // frame and style sheet background, as QLabel
	QRectF cr = contentsRect ().adjusted (margin (), margin (), -margin (), -margin ());
	Fit f = Layout ((int)cr.width ());
	QFontMetricsF fm (font ());
	const Qt::Alignment a = QStyle::visualAlignment (layoutDirection (), alignment ());
	double y = cr.top ();
	if (a & Qt::AlignVCenter) y += (cr.height () - f.height) / 2.0;
	else if (a & Qt::AlignBottom) y += cr.height () - f.height;
	p.setFont (font ());
	p.setPen (palette ().color (isEnabled () ? QPalette::Normal : QPalette::Disabled, foregroundRole ()));
	p.setClipRect (contentsRect ());
	for (int i = 0; i < f.lines.size (); i++) {
		const QString &s = f.lines[i];
		const double lw = fm.horizontalAdvance (s);
		double x = cr.left ();
		if (a & Qt::AlignHCenter) x += (cr.width () - lw) / 2.0;
		else if (a & Qt::AlignRight) x += cr.width () - lw;
		p.drawText (QPointF (x, y + i * f.spacing + fm.ascent ()), s);
	}
}

// ---------------------------------------------------------------------------------------------------------
// FormScrollArea

QSize FormScrollArea::sizeHint () const
{
	if (!property ("fitContent").toBool () || !widget ()) return QScrollArea::sizeHint ();
	const int f = 2 * frameWidth ();
	return widget ()->sizeHint () + QSize (f, f);
}

bool FormScrollArea::eventFilter (QObject *o, QEvent *e)
{
	if (o == widget () && e->type () == QEvent::LayoutRequest && property ("fitContent").toBool ())
		updateGeometry (); // the content changed size: the layout around this area asks again
	return QScrollArea::eventFilter (o, e);
}

// ---------------------------------------------------------------------------------------------------------
// FlowLayout

FlowLayout::FlowLayout (QWidget *parent, int hSpacing, int vSpacing): QLayout (parent), hs (hSpacing), vs (vSpacing)
{
}

FlowLayout::~FlowLayout ()
{
	for (QLayoutItem *it : items) delete it;
}

void FlowLayout::addItem (QLayoutItem *item)
{
	items.push_back (item);
	invalidate ();
}

void FlowLayout::insertWidgetAt (int index, QWidget *w)
{
	addChildWidget (w);
	index = std::clamp (index, 0, (int)items.size ());
	items.insert (items.begin () + index, new QWidgetItem (w));
	invalidate ();
}

QLayoutItem *FlowLayout::itemAt (int index) const
{
	return (index >= 0 && index < (int)items.size () ? items[index] : nullptr);
}

QLayoutItem *FlowLayout::takeAt (int index)
{
	if (index < 0 || index >= (int)items.size ()) return nullptr;
	QLayoutItem *it = items[index];
	items.erase (items.begin () + index);
	invalidate ();
	return it;
}

int FlowLayout::heightForWidth (int width) const
{
	return Place (QRect (0, 0, width, 0), false);
}

void FlowLayout::setGeometry (const QRect &rect)
{
	QLayout::setGeometry (rect);
	Place (rect, true);
}

QSize FlowLayout::sizeHint () const
{
	QSize s;
	int w = 0;
	for (QLayoutItem *it : items) {
		if (it->isEmpty ()) continue;
		QSize h = it->sizeHint ();
		w += (w ? hs : 0) + h.width ();
		s.setHeight (std::max (s.height (), h.height ()));
	}
	s.setWidth (w);
	const QMargins m = contentsMargins ();
	return s + QSize (m.left () + m.right (), m.top () + m.bottom ());
}

QSize FlowLayout::minimumSize () const
{
	QSize s;
	for (QLayoutItem *it : items)
		if (!it->isEmpty ()) s = s.expandedTo (it->minimumSize ());
	const QMargins m = contentsMargins ();
	return s + QSize (m.left () + m.right (), m.top () + m.bottom ());
}

int FlowLayout::Place (const QRect &rect, bool apply) const
{
	const QMargins m = contentsMargins ();
	QRect r = rect.adjusted (m.left (), m.top (), -m.right (), -m.bottom ());
	int x = r.x (), y = r.y (), lineH = 0;
	for (QLayoutItem *it : items) {
		if (it->isEmpty ()) continue;
		QSize h = it->sizeHint ();
		int next = x + h.width ();
		if (next > r.right () + 1 && lineH > 0) {
			x = r.x ();
			y += lineH + vs;
			next = x + h.width ();
			lineH = 0;
		}
		if (apply) it->setGeometry (QRect (QPoint (x, y), h));
		x = next + hs;
		lineH = std::max (lineH, h.height ());
	}
	return y + lineH - rect.y () + m.bottom ();
}

// ---------------------------------------------------------------------------------------------------------
// GlowPainter

GlowPainter::GlowPainter (QWidget *r): QObject (r), root (r)
{
}

void GlowPainter::Clear ()
{
	for (auto &[o, n] : filtered) {
		o->removeEventFilter (this);
		disconnect (o, &QObject::destroyed, this, nullptr);
	}
	filtered.clear ();
	targets.clear ();
}

void GlowPainter::Add (QWidget *target)
{
	std::erase_if (targets, [](const Target &t) { return !t.w; }); // clones that went
	for (const auto &t : targets)
		if (t.w == target) return;
	targets.push_back (Target ());
	Target &t = targets.back ();
	t.w = target;
	Locate (t);
}

QRectF GlowPainter::Rings (const Target &t, double *outset) const
{
	if (!t.w || !t.painter) return QRectF ();
	const int rings = std::clamp ((int)Prop (t.w, "glowRings", 0), 0, 32);
	const double step = std::clamp (Prop (t.w, "glowStep", 3.0), 0.0, 64.0);
	const double o = rings * step;
	if (outset) *outset = o;
	QPoint tl = t.w->mapTo (t.painter, QPoint (0, 0));
	return QRectF (tl, QSizeF (t.w->size ())).adjusted (-o, -o, o, o);
}

void GlowPainter::Locate (Target &t)
{
	auto unwatch = [this](QObject *o) {
		auto it = filtered.find (o);
		if (it == filtered.end ()) return;
		if (--it->second <= 0) {
			o->removeEventFilter (this);
			disconnect (o, &QObject::destroyed, this, nullptr);
			filtered.erase (it);
		}
	};
	auto watch = [this](QObject *o) {
		if (filtered[o]++ == 0) {
			o->installEventFilter (this);
			connect (o, &QObject::destroyed, this, [this, o]() { filtered.erase (o); });
		}
	};
	for (auto &w : t.watched) if (w) unwatch (w);
	if (t.painter) unwatch (t.painter);
	t.watched.clear ();
	t.painter = nullptr;
	if (!t.w || !root) return;
	const int rings = std::clamp ((int)Prop (t.w, "glowRings", 0), 0, 32);
	const double o = rings * std::clamp (Prop (t.w, "glowStep", 3.0), 0.0, 64.0);
	QWidget *c = t.w;
	t.watched.push_back (c);
	for (QWidget *a = c->parentWidget (); a; a = a->parentWidget ()) {
		QPoint tl = t.w->mapTo (a, QPoint (0, 0));
		QRectF r = QRectF (tl, QSizeF (t.w->size ())).adjusted (-o, -o, o, o);
		if (a == root || QRectF (a->rect ()).contains (r)) {
			t.painter = a;
			break;
		}
		t.watched.push_back (a);
	}
	for (auto &w : t.watched) watch (w);
	if (t.painter) watch (t.painter);
}

void GlowPainter::Refresh (Target &t)
{
	if (t.painter && !t.area.isEmpty ()) t.painter->update (t.area);
	Locate (t);
	t.area = Rings (t).toAlignedRect ().adjusted (-1, -1, 1, 1);
	if (t.painter) t.painter->update (t.area);
}

void GlowPainter::Changed (QWidget *target)
{
	for (auto &t : targets)
		if (t.w == target) Refresh (t);
}

bool GlowPainter::eventFilter (QObject *obj, QEvent *e)
{
	const QEvent::Type ty = e->type ();
	if (ty == QEvent::Paint) {
		bool ours = false;
		for (const auto &t : targets)
			if (t.painter == obj) { ours = true; break; }
		if (!ours || painting) return false;
		QWidget *w = static_cast<QWidget*> (obj);
		painting = true;
		bool (QWidget::*ev) (QEvent*) = &EventAccess::event;
		(w->*ev) (e); // the widget's own painting (QFrame draws its style sheet background here), then the rings
		Paint (w, static_cast<QPaintEvent*> (e));
		painting = false;
		return true;
	}
	switch (ty) {
	case QEvent::Move: case QEvent::Resize: case QEvent::Show: case QEvent::Hide:
	case QEvent::Enter: case QEvent::Leave: case QEvent::EnabledChange: case QEvent::ParentChange:
		for (auto &t : targets) {
			bool hit = false;
			for (auto &w : t.watched)
				if (w == obj) hit = true;
			if (hit) Refresh (t);
		}
		break;
	default:
		break;
	}
	return false;
}

void GlowPainter::Paint (QWidget *on, QPaintEvent *e)
{
	QPainter p (on);
	p.setRenderHint (QPainter::Antialiasing);
	p.setClipRegion (e->region ());
	for (const auto &t : targets) {
		if (t.painter != on || !t.w || !t.w->isVisibleTo (on)) continue;
		double a = Prop (t.w, "glowAlpha", 0.0);
		const double hoverA = Prop (t.w, "glowHoverAlpha", -1.0);
		if (hoverA >= 0.0 && t.w->underMouse () && t.w->isEnabled ()) a = hoverA;
		if (!t.w->isEnabled ()) a *= std::clamp (Prop (t.w, "disabledOpacity", 1.0), 0.0, 1.0);
		const int rings = std::clamp ((int)Prop (t.w, "glowRings", 0), 0, 32);
		if (a <= 0.0 || rings <= 0) continue;
		QColor c = t.w->property ("glowColor").value<QColor> ();
		if (!c.isValid ()) c = QColor (255, 255, 255);
		const double step = std::clamp (Prop (t.w, "glowStep", 3.0), 0.0, 64.0);
		const double pw = std::clamp (Prop (t.w, "glowWidth", step), 0.1, 64.0);
		const double radius = std::max (0.0, Prop (t.w, "glowRadius", 0.0));
		const bool halve = (t.w->property ("glowFade").toString () == "halve");
		QPoint tl = t.w->mapTo (on, QPoint (0, 0));
		QRectF body (tl, QSizeF (t.w->size ()));
		for (int i = 0; i < rings; i++) {
			const double o = step * (i + 1);
			const double ai = a * (halve ? std::pow (0.5, i) : 1.0 - double (i) / rings);
			QColor ci = c;
			ci.setAlphaF (std::clamp (c.alphaF () * ai, 0.0, 1.0));
			QRectF r = body.adjusted (-o + pw / 2, -o + pw / 2, o - pw / 2, o - pw / 2);
			const double rr = std::max (0.0, radius + o - pw / 2);
			p.setPen (QPen (ci, pw));
			p.setBrush (Qt::NoBrush);
			p.drawRoundedRect (r, rr, rr);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------

void SetLetterSpacing (QWidget *w, double px)
{
	QFont f;
	f.setLetterSpacing (QFont::AbsoluteSpacing, px);
	w->setFont (f);
}

void Repolish (QWidget *w)
{
	if (!w) return;
	QStyle *s = w->style ();
	s->unpolish (w);
	s->polish (w);
	for (QObject *o : w->children ())
		if (o->isWidgetType ()) Repolish (static_cast<QWidget*> (o));
	w->update ();
}

}
