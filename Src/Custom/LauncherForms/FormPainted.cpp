
// custom: forms skins; the painted widgets (ports of the QML skins' Canvas items), promoted QLabels in Designer

#include "FormPainted.h"
#include "Orbits.h"
#include <QDateTime>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QWindow>
#include <algorithm>
#include <cmath>

namespace forms {

namespace {

	const double PI = 3.14159265358979323846;

	// JS ToInt32
	int32_t ToInt32 (double d)
	{
		if (!std::isfinite (d)) return 0;
		d = std::trunc (d);
		d = std::fmod (d, 4294967296.0);
		if (d < 0) d += 4294967296.0;
		uint32_t u = (uint32_t)d;
		return (int32_t)u;
	}

	QColor Rgba (int r, int g, int b, double a)
	{
		QColor c (r, g, b);
		c.setAlphaF (std::clamp (a, 0.0, 1.0));
		return c;
	}

	QColor Hex (const char *s)
	{
		return QColor (QString::fromLatin1 (s));
	}

	// Canvas createRadialGradient(x0, y0, r0, x1, y1, r1)
	QRadialGradient Radial (double x0, double y0, double r0, double x1, double y1, double r1)
	{
		return QRadialGradient (QPointF (x1, y1), r1, QPointF (x0, y0), r0);
	}

	QLinearGradient VGrad (double y0, double y1, std::initializer_list<std::pair<double, QColor>> stops)
	{
		QLinearGradient g (0, y0, 0, y1);
		for (const auto &s : stops) g.setColorAt (s.first, s.second);
		return g;
	}

	void FillCircle (QPainter &p, double cx, double cy, double r, const QBrush &b)
	{
		QPainterPath path;
		path.addEllipse (QPointF (cx, cy), r, r);
		p.fillPath (path, b);
	}

	double Ease (double t) // InOutSine
	{
		return -(std::cos (PI * t) - 1) / 2;
	}

	// a 1 -> 2 -> 1 cycle of two eased halves of `half` ms each
	double Cycle (qint64 ms, double half, double from, double to)
	{
		const double ph = std::fmod ((double)ms, 2 * half) / half;
		return ph < 1 ? from + (to - from) * Ease (ph) : to + (from - to) * Ease (ph - 1);
	}

	QPixmap NewPixmap (const QSize &size, qreal dpr)
	{
		QPixmap pm (QSize (std::max (1, (int)std::ceil (size.width () * dpr)), std::max (1, (int)std::ceil (size.height () * dpr))));
		pm.setDevicePixelRatio (dpr);
		pm.fill (Qt::transparent);
		return pm;
	}

}

Mulberry::Mulberry (double seed): a ((uint32_t)ToInt32 (seed))
{
}

double Mulberry::operator() ()
{
	a += 0x6D2B79F5u;
	uint32_t t = (a ^ (a >> 15)) * (1u | a);
	t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
	return double (t ^ (t >> 14)) / 4294967296.0;
}

void CanvasArc (QPainterPath &p, double cx, double cy, double r, double a0, double a1, bool ccw)
{
	QRectF rect (cx - r, cy - r, 2 * r, 2 * r);
	double sweep = a1 - a0;
	if (!ccw && sweep < 0) sweep += 2 * PI * std::ceil (-sweep / (2 * PI));
	if (ccw && sweep > 0) sweep -= 2 * PI * std::ceil (sweep / (2 * PI));
	sweep = std::clamp (sweep, -2 * PI, 2 * PI);
	const double start = -a0 * 180 / PI;
	if (p.elementCount () == 0) p.arcMoveTo (rect, start);
	p.arcTo (rect, start, -sweep * 180 / PI);
}

// ---------------------------------------------------------------------------------------------------------
// Painted

Painted::Painted (QWidget *parent): QWidget (parent)
{
	resizeTimer.setSingleShot (true);
	resizeTimer.setInterval (120);
	connect (&resizeTimer, &QTimer::timeout, this, [this]() { Invalidate (); });
}

void Painted::setActive (bool on)
{
	if (on == m_active) return;
	m_active = on;
	ActiveChanged ();
}

void Painted::Invalidate ()
{
	cache = QPixmap ();
	update ();
}

QPixmap Painted::Cache (const QSize &size)
{
	const qreal dpr = devicePixelRatioF ();
	if (!cache.isNull () && (cacheSize == size || resizeTimer.isActive ()) && cache.devicePixelRatio () == dpr) return cache;
	cache = NewPixmap (size, dpr);
	cacheSize = size;
	QPainter p (&cache);
	p.setRenderHint (QPainter::Antialiasing);
	Draw (p, size);
	return cache;
}

void Painted::resizeEvent (QResizeEvent *e)
{
	QWidget::resizeEvent (e);
	if (!cache.isNull ()) resizeTimer.start ();
}

bool Painted::event (QEvent *e)
{
	if (e->type () == QEvent::DevicePixelRatioChange) Invalidate ();
	return QWidget::event (e);
}

// ---------------------------------------------------------------------------------------------------------
// Starfield

Starfield::Starfield (QWidget *parent): Painted (parent)
{
	timer.setInterval (250);
	connect (&timer, &QTimer::timeout, this, [this]() { Tick (); });
}

void Starfield::setCount (int c)
{
	c = std::clamp (c, 0, 20000);
	if (c != m_count) { m_count = c; Invalidate (); }
}

void Starfield::setAlpha (double a)
{
	a = std::clamp (a, 0.0, 1.0);
	if (a != m_alpha) { m_alpha = a; update (); }
}

void Starfield::setDrift (int px)
{
	px = std::clamp (px, 0, 2000);
	if (px == m_drift) return;
	m_drift = px;
	Invalidate ();
	Sync ();
}

void Starfield::DrawField (QPainter &p, double w, double h, int seed, int count)
{
	Mulberry r (seed);
	const double x0 = w * 0.30, y0 = -h * 0.15, x1 = w * 1.05, y1 = h * 0.80;
	p.setPen (Qt::NoPen);
	for (int i = 0; i < 46; i++) { // Milky Way haze along a diagonal
		const double t = r ();
		const double px = x0 + (x1 - x0) * t + (r () - 0.5) * 120;
		const double py = y0 + (y1 - y0) * t + (r () - 0.5) * 120;
		const double rad = 70 + r () * 150;
		const bool blue = r () < 0.5;
		const double a = 0.035 + r () * 0.03;
		QRadialGradient g = Radial (px, py, 0, px, py, rad);
		g.setColorAt (0, blue ? Rgba (120, 140, 220, a) : Rgba (200, 170, 210, a));
		g.setColorAt (1, blue ? Rgba (120, 140, 220, 0) : Rgba (200, 170, 210, 0));
		p.fillRect (QRectF (px - rad, py - rad, rad * 2, rad * 2), g);
	}
	double nx = -(y1 - y0), ny = (x1 - x0);
	const double nl = std::sqrt (nx * nx + ny * ny);
	nx /= nl;
	ny /= nl;
	for (int i = 0; i < 2600; i++) {
		const double t = r ();
		const double o1 = r (), o2 = r (), o3 = r ();
		const double off = (o1 + o2 + o3 - 1.5) * 110;
		const double px = x0 + (x1 - x0) * t + nx * off, py = y0 + (y1 - y0) * t + ny * off;
		p.fillRect (QRectF (px, py, 1, 1), Rgba (220, 228, 255, 0.08 + r () * 0.22));
	}
	for (int i = 0; i < count; i++) { // field stars
		const double px = r () * w;
		const double py = r () * h;
		const double m = std::pow (r (), 3.2), s = 0.6 + m * 1.7, a = 0.22 + m * 0.78, k = r ();
		const int cr = (k < 0.14 ? 175 : 255), cg = (k < 0.14 ? 205 : k < 0.24 ? 222 : 255), cb = (k < 0.14 ? 255 : k < 0.24 ? 185 : 255);
		if (m > 0.8) {
			QRadialGradient gg = Radial (px, py, 0, px, py, s * 5);
			gg.setColorAt (0, Rgba (cr, cg, cb, 0.22));
			gg.setColorAt (1, Rgba (cr, cg, cb, 0));
			p.fillRect (QRectF (px - s * 5, py - s * 5, s * 10, s * 10), gg);
		}
		if (s < 1.2) p.fillRect (QRectF (px, py, s, s), Rgba (cr, cg, cb, a));
		else FillCircle (p, px, py, s * 0.6, Rgba (cr, cg, cb, a));
	}
}

void Starfield::Draw (QPainter &p, const QSize &size)
{
	DrawField (p, size.width (), size.height (), m_seed, m_count);
}

int Starfield::Offset () const
{
	if (m_drift <= 0) return 0;
	return (int)std::lround (Cycle (elapsed + (clock.isValid () ? clock.elapsed () : 0), m_period, 0, m_drift));
}

void Starfield::paintEvent (QPaintEvent *)
{
	QPainter p (this);
	p.setOpacity (m_alpha);
	shown = Offset ();
	p.drawPixmap (QPointF (-shown, 0), Cache (QSize (width () + m_drift, height ())));
}

void Starfield::Tick ()
{
	if (Offset () != shown) update ();
}

void Starfield::Sync ()
{
	const bool run = (Running () && m_drift > 0);
	if (run && !timer.isActive ()) {
		clock.start ();
		timer.start ();
	} else if (!run && timer.isActive ()) {
		elapsed += clock.elapsed ();
		clock.invalidate ();
		timer.stop ();
	}
}

void Starfield::ActiveChanged () { Sync (); }
void Starfield::showEvent (QShowEvent *e) { Painted::showEvent (e); Sync (); }
void Starfield::hideEvent (QHideEvent *e) { Painted::hideEvent (e); Sync (); }

// ---------------------------------------------------------------------------------------------------------
// Planet

Planet::Planet (QWidget *parent): Painted (parent)
{
	timer.setInterval (200); // 5 Hz: the pulse repaints everything above the glare in software
	connect (&timer, &QTimer::timeout, this, [this]() { update (GlareRect ()); });
}

QPointF Planet::SunPoint (double w, double h)
{
	const double r = h * 1.28, cx = w * 0.74, cy = h * 0.555 + r, sa = -PI / 2 - 0.2;
	return QPointF (cx + (r + 4) * std::cos (sa), cy + (r + 4) * std::sin (sa));
}

void Planet::DrawPlanet (QPainter &p, double width, double height)
{
	const double r = height * 1.28, cx = width * 0.74, cy = height * 0.555 + r, sunAngle = -PI / 2 - 0.2;
	Mulberry rnd (77);
	const double lx = cx + r * 1.02 * std::cos (sunAngle), ly = cy + r * 1.02 * std::sin (sunAngle);
	p.setPen (Qt::NoPen);
	QRadialGradient g = Radial (cx, cy, r * 0.995, cx, cy, r * 1.075); // atmosphere glow all round
	g.setColorAt (0, Rgba (70, 150, 255, 0.42));
	g.setColorAt (0.3, Rgba (50, 110, 230, 0.14));
	g.setColorAt (1, Rgba (0, 0, 0, 0));
	FillCircle (p, cx, cy, r * 1.075, g);
	g = Radial (lx, ly, 0, lx, ly, r * 0.75); // brighter glow near the sun
	g.setColorAt (0, Rgba (160, 215, 255, 0.55));
	g.setColorAt (0.35, Rgba (90, 160, 255, 0.16));
	g.setColorAt (1, Rgba (0, 0, 0, 0));
	{
		QPainterPath ring;
		ring.setFillRule (Qt::OddEvenFill);
		ring.addEllipse (QPointF (cx, cy), r * 1.06, r * 1.06);
		ring.addEllipse (QPointF (cx, cy), r, r);
		p.fillPath (ring, g);
	}
	p.save (); // night disc
	QPainterPath disc;
	disc.addEllipse (QPointF (cx, cy), r, r);
	p.setClipPath (disc, Qt::IntersectClip);
	p.fillRect (QRectF (cx - r, cy - r, r * 2, r * 2), Hex ("#01060e"));
	for (int k = 0; k < 26; k++) { // city lights on the night side
		const double ax = cx + (rnd () - 0.5) * r * 1.6;
		const double ay = cy - r + 60 + rnd () * height * 0.5;
		const double d = std::sqrt ((ax - lx) * (ax - lx) + (ay - ly) * (ay - ly));
		if (d < r * 0.55) continue;
		for (int j = 0; j < 22; j++) {
			const double a = 0.15 + rnd () * 0.45;
			const double x = ax + (rnd () - 0.5) * 60;
			const double y = ay + (rnd () - 0.5) * 22;
			p.fillRect (QRectF (x, y, 1.3, 1.3), Rgba (255, 190, 110, a));
		}
	}
	g = Radial (lx, ly, 0, lx, ly, r * 0.95); // day side near the sun
	g.setColorAt (0, Rgba (125, 195, 250, 1));
	g.setColorAt (0.08, Rgba (58, 128, 200, 1));
	g.setColorAt (0.28, Rgba (18, 58, 110, 0.95));
	g.setColorAt (0.6, Rgba (4, 18, 40, 0.55));
	g.setColorAt (1, Rgba (0, 0, 0, 0));
	p.fillRect (QRectF (cx - r, cy - r, r * 2, r * 2), g);
	for (int k = 0; k < 360; k++) { // cloud streaks on the lit side
		const double t = rnd () * PI * 2;
		const double rr = std::pow (rnd (), 0.7) * r * 0.7;
		const double px = lx + std::cos (t) * rr * 1.7, py = ly + std::fabs (std::sin (t)) * rr * 0.6;
		const double fall = std::max (0.0, 1 - std::sqrt ((px - lx) * (px - lx) + (py - ly) * (py - ly)) / (r * 0.75));
		const double tilt = std::atan2 (py - cy, px - cx) + PI / 2;
		const double rot = tilt + (rnd () - 0.5) * 0.25;
		const double sc = 0.12 + rnd () * 0.14;
		const double a = fall * (0.035 + rnd () * 0.09);
		const double rad = 6 + rnd () * 28;
		p.save ();
		p.translate (px, py);
		p.rotate (rot * 180 / PI);
		p.scale (1, sc);
		FillCircle (p, 0, 0, rad, Rgba (235, 245, 255, a));
		p.restore ();
	}
	p.restore ();
	for (int k = 0; k < 18; k++) { // bright limb arc on the sun side
		const double span = 0.9 - k * 0.045;
		QPainterPath arc;
		CanvasArc (arc, cx, cy, r, sunAngle - span * 0.6, sunAngle + span);
		QPen pen (Rgba (195, 232, 255, 0.035 + k * 0.03), k < 16 ? 3 : 1.6);
		pen.setCapStyle (Qt::FlatCap);
		p.strokePath (arc, pen);
	}
}

void Planet::DrawGlare (QPainter &p, double width, double height)
{
	const double x = width / 2, y = height / 2;
	p.save ();
	p.translate (x, y);
	p.scale (1, 0.03);
	QRadialGradient s = Radial (0, 0, 0, 0, 0, width / 2);
	s.setColorAt (0, Rgba (255, 245, 225, 0.75));
	s.setColorAt (0.35, Rgba (255, 210, 150, 0.18));
	s.setColorAt (1, Rgba (255, 200, 140, 0));
	FillCircle (p, 0, 0, width / 2, s);
	p.restore ();
	QRadialGradient g = Radial (x, y, 0, x, y, 150);
	g.setColorAt (0, Rgba (255, 255, 255, 1));
	g.setColorAt (0.05, Rgba (255, 249, 232, 0.95));
	g.setColorAt (0.14, Rgba (255, 222, 165, 0.42));
	g.setColorAt (0.4, Rgba (255, 190, 120, 0.1));
	g.setColorAt (1, Rgba (255, 180, 110, 0));
	FillCircle (p, x, y, 150, g);
}

void Planet::Draw (QPainter &p, const QSize &size)
{
	DrawPlanet (p, size.width (), size.height ());
}

QRect Planet::GlareRect () const
{
	QPointF s = SunPoint (width (), height ());
	return QRectF (s.x () - 450, s.y () - 150, 900, 300).toAlignedRect ();
}

double Planet::GlareOpacity () const
{
	if (!m_pulse) return 0.91;
	return Cycle (elapsed + (clock.isValid () ? clock.elapsed () : 0), 3200, 1.0, 0.82);
}

void Planet::paintEvent (QPaintEvent *)
{
	QPainter p (this);
	p.drawPixmap (0, 0, Cache (size ()));
	if (!m_glare) return;
	if (glareCache.isNull () || glareCache.devicePixelRatio () != devicePixelRatioF ()) {
		glareCache = NewPixmap (QSize (900, 300), devicePixelRatioF ());
		QPainter g (&glareCache);
		g.setRenderHint (QPainter::Antialiasing);
		DrawGlare (g, 900, 300);
	}
	p.setOpacity (GlareOpacity ());
	p.drawPixmap (GlareRect ().topLeft (), glareCache);
}

void Planet::setPulse (bool on)
{
	if (on == m_pulse) return;
	m_pulse = on;
	Sync ();
	update (GlareRect ());
}

void Planet::Sync ()
{
	const bool run = (Running () && m_pulse && m_glare);
	if (run && !timer.isActive ()) {
		clock.start ();
		timer.start ();
	} else if (!run && timer.isActive ()) {
		elapsed += clock.elapsed ();
		clock.invalidate ();
		timer.stop ();
	}
}

void Planet::ActiveChanged () { Sync (); }
void Planet::showEvent (QShowEvent *e) { Painted::showEvent (e); Sync (); }
void Planet::hideEvent (QHideEvent *e) { Painted::hideEvent (e); Sync (); }

// ---------------------------------------------------------------------------------------------------------
// ScenarioThumb (Horizon Thumb.qml)

void ScenarioThumb::DrawThumb (QPainter &p, double w, double h, const QString &kind, int seed, double k)
{
	Mulberry r (double (seed) * 7919 + kind.size () * 991 + 5);
	auto stars = [&](int n, double maxY) {
		for (int i = 0; i < n; i++) {
			const double a = 0.2 + r () * 0.7;
			const double s = r () < 0.1 ? 1.6 : 1;
			const double x = r () * w;
			const double y = r () * maxY;
			p.fillRect (QRectF (x, y, s, s), Rgba (255, 255, 255, a));
		}
	};
	auto poly = [&](std::initializer_list<QPointF> pts, const QBrush &b) {
		QPainterPath path;
		bool first = true;
		for (const QPointF &q : pts) {
			if (first) path.moveTo (q);
			else path.lineTo (q);
			first = false;
		}
		path.closeSubpath ();
		p.fillPath (path, b);
	};
	auto blob = [&](double x, double y, double sy, double rad, const QBrush &b) { // save, translate, scale, circle, restore
		p.save ();
		p.translate (x, y);
		p.scale (1, sy);
		FillCircle (p, 0, 0, rad, b);
		p.restore ();
	};
	p.setPen (Qt::NoPen);
	QPainterPath clip; // top corners only
	clip.moveTo (0, h);
	clip.lineTo (0, k);
	clip.arcTo (QRectF (0, 0, 2 * k, 2 * k), 180, -90);
	clip.lineTo (w - k, 0);
	clip.arcTo (QRectF (w - 2 * k, 0, 2 * k, 2 * k), 90, -90);
	clip.lineTo (w, h);
	clip.closeSubpath ();
	p.setClipPath (clip);
	const QRectF all (0, 0, w, h);
	if (kind == "orbit" || kind == "hst") {
		p.fillRect (all, VGrad (0, h, {{0, Hex ("#01030a")}, {1, Hex ("#071630")}}));
		stars (60, h * 0.6);
		const double R = h * 2.4, cx = w * 0.45, cy = h * 0.52 + R;
		QRadialGradient g = Radial (cx, cy, R, cx, cy, R * 1.05);
		g.setColorAt (0, Rgba (90, 170, 255, 0.6));
		g.setColorAt (1, Rgba (90, 170, 255, 0));
		FillCircle (p, cx, cy, R * 1.05, g);
		QLinearGradient lg (0, h * 0.52, 0, h);
		lg.setColorAt (0, Hex ("#7fc0f2"));
		lg.setColorAt (0.08, Hex ("#2c6fb4"));
		lg.setColorAt (0.5, Hex ("#123e73"));
		lg.setColorAt (1, Hex ("#0a2446"));
		FillCircle (p, cx, cy, R, lg);
		for (int i = 0; i < 26; i++) {
			const double a = 0.12 + r () * 0.25;
			const double x = r () * w;
			const double y = h * 0.62 + r () * h * 0.38;
			const double rad = 6 + r () * 16;
			blob (x, y, 0.25, rad, Rgba (255, 255, 255, a));
		}
		if (kind == "hst") {
			p.save ();
			p.translate (w * 0.58, h * 0.3);
			p.rotate (-0.35 * 180 / PI);
			p.fillRect (QRectF (-26, -12, 16, 7), Hex ("#6f8fb8"));
			p.fillRect (QRectF (-26, 5, 16, 7), Hex ("#6f8fb8"));
			p.fillRect (QRectF (-12, -5, 30, 10), Hex ("#d9dde2"));
			p.fillRect (QRectF (18, -5, 4, 10), Hex ("#9aa3ad"));
			FillCircle (p, 22, 0, 3, Hex ("#1a1f26"));
			p.restore ();
		} else { // station silhouette
			const double sx = w * 0.6, sy = h * 0.3;
			p.fillRect (QRectF (sx - 46, sy - 1, 92, 2.4), Hex ("#c9d3df"));
			const double pxs[4] = {-44, -32, 24, 36};
			for (double px : pxs) {
				p.fillRect (QRectF (sx + px, sy - 15, 8, 13), Hex ("#b8903f"));
				p.fillRect (QRectF (sx + px, sy + 3.4, 8, 13), Hex ("#b8903f"));
			}
			p.fillRect (QRectF (sx - 8, sy - 4, 16, 8), Hex ("#e3e8ee"));
			p.fillRect (QRectF (sx - 2.5, sy - 12, 5, 24), Hex ("#e3e8ee"));
			p.fillRect (QRectF (sx - 14, sy - 3, 6, 6), Hex ("#9aa6b4"));
		}
	} else if (kind == "runway") {
		p.fillRect (all, VGrad (0, h, {{0, Hex ("#141a3a")}, {0.45, Hex ("#6a3d6a")}, {0.62, Hex ("#f09a5a")}, {0.63, Hex ("#2a2226")}, {1, Hex ("#0c0b10")}}));
		stars (18, h * 0.3);
		poly ({{w * 0.47, h * 0.63}, {w * 0.53, h * 0.63}, {w * 0.85, h}, {w * 0.15, h}}, Hex ("#1c1b21"));
		for (int i = 0; i < 9; i++) {
			const double t = i / 8.0, yy = h * 0.64 + t * t * h * 0.36, half = (0.03 + t * 0.33) * w;
			p.fillRect (QRectF (w / 2 - half - 2, yy, 2 + t * 2, 2 + t * 2), Rgba (255, 210, 140, 0.95));
			p.fillRect (QRectF (w / 2 + half, yy, 2 + t * 2, 2 + t * 2), Rgba (255, 210, 140, 0.95));
			p.fillRect (QRectF (w / 2 - 0.5 - t, yy, 1 + t * 2, 1 + t * 3), Rgba (255, 255, 255, 0.8));
		}
		p.fillRect (QRectF (w * 0.08, h * 0.53, 6, h * 0.1), Hex ("#0d0c11"));
		p.fillRect (QRectF (w * 0.84, h * 0.5, 10, h * 0.13), Hex ("#0d0c11"));
	} else if (kind == "moon") {
		p.fillRect (all, Hex ("#010205"));
		stars (70, h * 0.6);
		const double ex = w * 0.2, ey = h * 0.24;
		FillCircle (p, ex, ey, 11, Hex ("#0a1a33"));
		QRadialGradient g = Radial (ex - 6, ey - 3, 1, ex - 6, ey - 3, 13);
		g.setColorAt (0, Hex ("#9fd0ff"));
		g.setColorAt (0.6, Hex ("#2d6db3"));
		g.setColorAt (1, Rgba (20, 60, 120, 0));
		FillCircle (p, ex, ey, 11, g);
		const double mR = w * 2.2, mcx = w * 0.4, mcy = h * 0.6 + mR;
		FillCircle (p, mcx, mcy, mR, VGrad (h * 0.6, h, {{0, Hex ("#a9a9a6")}, {0.3, Hex ("#6d6c69")}, {1, Hex ("#2b2a28")}}));
		for (int i = 0; i < 16; i++) {
			const double qx = r () * w;
			const double qy = h * 0.66 + r () * h * 0.34;
			const double qr = 3 + r () * 11 * (qy / h);
			p.save ();
			p.translate (qx, qy);
			p.scale (1, 0.32);
			FillCircle (p, 0, 0, qr, Rgba (30, 30, 28, 0.55));
			QPainterPath arc;
			CanvasArc (arc, 0, 0, qr, PI * 0.1, PI * 0.9);
			QPen pen (Rgba (220, 220, 215, 0.35), 1.4);
			pen.setCapStyle (Qt::FlatCap);
			p.strokePath (arc, pen);
			p.restore ();
		}
		const QColor c = Rgba (120, 220, 255, 0.9);
		p.fillRect (QRectF (w * 0.3, h * 0.7, 3, 3), c);
		p.fillRect (QRectF (w * 0.34, h * 0.71, 2, 2), c);
		p.fillRect (QRectF (w * 0.27, h * 0.72, 2, 2), c);
	} else if (kind == "runwayday") {
		p.fillRect (all, VGrad (0, h, {{0, Hex ("#2f6fb4")}, {0.5, Hex ("#a9d0ee")}, {0.52, Hex ("#6f8a5a")}, {1, Hex ("#3c5233")}}));
		for (int i = 0; i < 14; i++) {
			const double a = 0.5 + r () * 0.4;
			const double x = r () * w;
			const double y = h * 0.12 + r () * h * 0.3;
			const double rad = 8 + r () * 22;
			blob (x, y, 0.35, rad, Rgba (255, 255, 255, a));
		}
		poly ({{w * 0.48, h * 0.52}, {w * 0.52, h * 0.52}, {w * 0.9, h}, {w * 0.1, h}}, Hex ("#3a3d40"));
		for (int i = 0; i < 7; i++) {
			const double ty = h * 0.55 + i * i * h * 0.012;
			p.fillRect (QRectF (w / 2 - 1 - i * 0.4, ty, 2 + i * 0.8, 3 + i * 1.5), Rgba (255, 255, 255, 0.85));
		}
		const double gx0 = w * 0.5, gy0 = h * 0.86;
		poly ({{gx0, gy0 - 16}, {gx0 + 34, gy0 + 6}, {gx0 + 10, gy0 + 4}, {gx0, gy0 + 8}, {gx0 - 10, gy0 + 4}, {gx0 - 34, gy0 + 6}}, Hex ("#eef1f4"));
		p.fillRect (QRectF (gx0 - 12, gy0 + 2, 5, 3), Hex ("#a9b2bb"));
		p.fillRect (QRectF (gx0 + 7, gy0 + 2, 5, 3), Hex ("#a9b2bb"));
	} else if (kind == "ice") {
		p.fillRect (all, VGrad (0, h, {{0, Hex ("#6f93b8")}, {0.5, Hex ("#dfe9f2")}, {0.55, Hex ("#f4f8fb")}, {1, Hex ("#b9cad8")}}));
		poly ({{0, h * 0.56}, {w * 0.25, h * 0.48}, {w * 0.4, h * 0.55}, {w * 0.62, h * 0.46}, {w, h * 0.55}, {w, h * 0.58}, {0, h * 0.58}}, Rgba (170, 190, 210, 0.7));
		for (int i = 0; i < 12; i++) {
			const double a = 0.2 + r () * 0.3;
			const double iy = h * 0.62 + r () * h * 0.36;
			const double xa = r () * w;
			const double xb = r () * w;
			const double dy = (r () - 0.5) * 6;
			QPen pen (Rgba (140, 165, 190, a), 1);
			pen.setCapStyle (Qt::FlatCap);
			p.setPen (pen);
			p.drawLine (QPointF (xa, iy), QPointF (xb, iy + dy));
			p.setPen (Qt::NoPen);
		}
		const double fx = w * 0.62, fy = h * 0.74;
		poly ({{fx, fy - 7}, {fx + 20, fy + 3}, {fx - 20, fy + 3}}, Hex ("#e9edf2"));
		p.fillRect (QRectF (w * 0.3, h * 0.7, 3, 8), Hex ("#ff5a3c"));
		p.fillRect (QRectF (w * 0.3 + 3, h * 0.7, 7, 4), Hex ("#ff5a3c"));
	} else if (kind == "mars") {
		p.fillRect (all, VGrad (0, h, {{0, Hex ("#3b2a2a")}, {0.45, Hex ("#c79b7c")}, {0.58, Hex ("#e0b894")}, {0.6, Hex ("#a4552e")}, {1, Hex ("#5a2a16")}}));
		poly ({{0, h * 0.6}, {w * 0.18, h * 0.5}, {w * 0.34, h * 0.58}, {w * 0.52, h * 0.47}, {w * 0.7, h * 0.57}, {w, h * 0.52}, {w, h * 0.62}, {0, h * 0.62}}, Rgba (120, 55, 30, 0.9));
		for (int i = 0; i < 18; i++) {
			const double a = 0.3 + r () * 0.4;
			const double x = r () * w;
			const double y = h * 0.66 + r () * h * 0.34;
			const double rad = 2 + r () * 8;
			blob (x, y, 0.3, rad, Rgba (60, 25, 12, a));
		}
		FillCircle (p, w * 0.78, h * 0.2, 4, Rgba (255, 240, 220, 0.9));
	} else if (kind == "lunarorbit") {
		p.fillRect (all, Hex ("#010205"));
		stars (60, h * 0.7);
		const double ex2 = w * 0.66, ey2 = h * 0.5;
		QRadialGradient g = Radial (ex2 - 5, ey2 - 4, 1, ex2, ey2, 17);
		g.setColorAt (0, Hex ("#bfe3ff"));
		g.setColorAt (0.45, Hex ("#3f86c8"));
		g.setColorAt (0.9, Hex ("#12325c"));
		g.setColorAt (1, Rgba (18, 50, 92, 0));
		FillCircle (p, ex2, ey2, 16, g);
		const double lR = w * 1.6, lcx = w * 0.3, lcy = h * 0.66 + lR;
		FillCircle (p, lcx, lcy, lR, VGrad (h * 0.66, h, {{0, Hex ("#cfcfca")}, {0.25, Hex ("#8e8d88")}, {1, Hex ("#3a3936")}}));
		for (int i = 0; i < 20; i++) {
			const double lx = r () * w;
			const double ly = h * 0.7 + r () * h * 0.3;
			const double lr = 2 + r () * 9 * (ly / h);
			blob (lx, ly, 0.3, lr, Rgba (40, 40, 38, 0.5));
		}
	} else if (kind == "deep") {
		p.fillRect (all, VGrad (0, h, {{0, Hex ("#01030a")}, {1, Hex ("#0a0f24")}}));
		stars (90, h);
		QRadialGradient g = Radial (w * 0.7, h * 0.4, 0, w * 0.7, h * 0.4, w * 0.5);
		g.setColorAt (0, Rgba (120, 110, 200, 0.18));
		g.setColorAt (1, Rgba (120, 110, 200, 0));
		p.fillRect (all, g);
	} else if (kind == "pad") {
		p.fillRect (all, VGrad (0, h, {{0, Hex ("#0a1230")}, {0.5, Hex ("#3a2a5a")}, {0.8, Hex ("#e0804a")}, {0.81, Hex ("#17121a")}, {1, Hex ("#07060a")}}));
		stars (22, h * 0.35);
		const double bx = w * 0.52, by = h * 0.81;
		QRadialGradient g = Radial (bx, by, 0, bx, by, 70);
		g.setColorAt (0, Rgba (255, 240, 200, 0.95));
		g.setColorAt (0.2, Rgba (255, 170, 80, 0.55));
		g.setColorAt (1, Rgba (255, 120, 40, 0));
		p.fillRect (QRectF (bx - 70, by - 70, 140, 140), g);
		p.fillRect (QRectF (bx + 16, by - 60, 7, 60), Hex ("#16121a"));
		for (int i = 0; i < 6; i++) p.fillRect (QRectF (bx + 10, by - 58 + i * 10, 13, 1.5), Hex ("#16121a"));
		p.fillRect (QRectF (bx - 4, by - 58, 9, 50), Hex ("#c56a2c"));
		poly ({{bx - 4, by - 58}, {bx + 0.5, by - 66}, {bx + 5, by - 58}}, Hex ("#c56a2c"));
		p.fillRect (QRectF (bx - 9, by - 50, 4, 42), Hex ("#ece9e4"));
		p.fillRect (QRectF (bx + 6, by - 50, 4, 42), Hex ("#ece9e4"));
		poly ({{bx - 14, by - 22}, {bx - 9, by - 40}, {bx - 9, by - 18}}, Hex ("#ece9e4"));
	} else {
		p.fillRect (all, VGrad (0, h, {{0, Hex ("#23589a")}, {0.52, Hex ("#a9d2f2")}, {0.53, Hex ("#1c6a9c")}, {1, Hex ("#0b3a62")}}));
		QPainterPath land;
		land.moveTo (0, h * 0.53);
		land.lineTo (w * 0.28, h * 0.53);
		land.quadTo (w * 0.36, h * 0.75, w * 0.22, h);
		land.lineTo (0, h);
		land.closeSubpath ();
		p.fillPath (land, Hex ("#3e6b3a"));
		QPainterPath shore;
		shore.moveTo (w * 0.28, h * 0.53);
		shore.quadTo (w * 0.36, h * 0.75, w * 0.22, h);
		QPen pen (Rgba (240, 225, 180, 0.9), 2.2);
		pen.setCapStyle (Qt::FlatCap);
		p.strokePath (shore, pen);
		for (int i = 0; i < 30; i++) {
			const double cyy = h * 0.2 + r () * h * 0.36;
			const double a = 0.35 + r () * 0.45;
			const double x = r () * w;
			const double rad = 6 + r () * 18 * (1 - cyy / h);
			blob (x, cyy, 0.3 + (cyy / h) * 0.2, rad, Rgba (255, 255, 255, a));
		}
		const double gx = w * 0.62, gy = h * 0.34;
		poly ({{gx, gy}, {gx - 22, gy + 7}, {gx - 16, gy + 8}, {gx + 2, gy + 3}}, Hex ("#e9edf2"));
		p.fillRect (QRectF (gx - 20, gy + 5, 26, 3), Hex ("#e9edf2"));
	}
	QLinearGradient bottom (0, h * 0.6, 0, h); // darken the bottom edge so text below sits well
	bottom.setColorAt (0, Rgba (3, 6, 12, 0));
	bottom.setColorAt (1, Rgba (3, 6, 12, 0.55));
	p.fillRect (all, bottom);
}

void ScenarioThumb::Draw (QPainter &p, const QSize &size)
{
	DrawThumb (p, size.width (), size.height (), m_kind, m_seed, m_corner);
}

void ScenarioThumb::paintEvent (QPaintEvent *)
{
	QPainter p (this);
	p.drawPixmap (0, 0, Cache (size ()));
}

// ---------------------------------------------------------------------------------------------------------
// GridBackdrop

void GridBackdrop::DrawGrid (QPainter &p, double width, double height, int step)
{
	QPen pen (Rgba (39, 211, 255, 0.05), 1);
	p.setPen (pen);
	QPainterPath lines;
	for (double x = 0.5; x < width; x += step) { lines.moveTo (x, 0); lines.lineTo (x, height); }
	for (double y = 0.5; y < height; y += step) { lines.moveTo (0, y); lines.lineTo (width, y); }
	p.save ();
	p.setRenderHint (QPainter::Antialiasing, false);
	p.drawPath (lines);
	p.restore ();
	QRadialGradient v = Radial (width / 2, height * 0.45, 0, width / 2, height * 0.45, std::max (width, height) * 0.75);
	v.setColorAt (0, Rgba (10, 40, 70, 0.25));
	v.setColorAt (1, Rgba (0, 0, 0, 0.35));
	p.fillRect (QRectF (0, 0, width, height), v);
}

void GridBackdrop::Draw (QPainter &p, const QSize &size)
{
	DrawGrid (p, size.width (), size.height (), m_step);
}

void GridBackdrop::paintEvent (QPaintEvent *)
{
	QPainter p (this);
	p.drawPixmap (0, 0, Cache (size ()));
}

// ---------------------------------------------------------------------------------------------------------
// MiniOrbit (Planetary Defense MiniOrbit.qml)

void MiniOrbit::DrawMini (QPainter &g, double w, double h, const QString &k, int seed, double corner)
{
	QPainterPath clip;
	clip.addRoundedRect (QRectF (0, 0, w, h), corner, corner);
	g.setClipPath (clip);
	g.fillRect (QRectF (0, 0, w, h), Hex ("#06101e"));
	{
		QPainterPath lines;
		for (double x = 0.5; x < w; x += 22) { lines.moveTo (x, 0); lines.lineTo (x, h); }
		for (double y = 0.5; y < h; y += 22) { lines.moveTo (0, y); lines.lineTo (w, y); }
		g.save ();
		g.setRenderHint (QPainter::Antialiasing, false);
		g.setPen (QPen (Rgba (39, 211, 255, 0.06), 1));
		g.drawPath (lines);
		g.restore ();
	}
	double a = seed;
	auto rnd = [&a]() {
		volatile double prod = a * 1103515245.0; // no fused multiply-add: the JS doubles round here
		volatile double sum = prod + 12345.0;
		a = std::fmod ((double)sum, 2147483648.0);
		return a / 2147483648.0;
	};
	for (int i = 0; i < 40; i++) {
		const double x = rnd () * w;
		const double y = rnd () * h;
		g.fillRect (QRectF (x, y, 1, 1), Rgba (230, 246, 255, 0.7));
	}
	const QStringList parts = k.split ('-');
	const bool landed = (parts.value (0) == "landed");
	const QString body = parts.mid (1).join ('-');
	static const std::pair<const char*, const char*> cols[] = {{"Earth", "#1f8fc4"}, {"Moon", "#9a9a9a"}, {"Mars", "#c4643a"},
		{"Venus", "#e8c27a"}, {"Mercury", "#b5ab9c"}, {"Jupiter", "#c8a27a"}, {"Saturn", "#d8c08a"}, {"Sun", "#ffcc66"}};
	QColor col = Hex ("#8a8fb0");
	for (const auto &c : cols)
		if (body == QLatin1String (c.first)) col = Hex (c.second);
	if (landed) {
		const double R = w * 1.4, cx = w / 2, cy = h + R - h * 0.38;
		FillCircle (g, cx, cy, R, col);
		QPainterPath ring;
		ring.addEllipse (QPointF (cx, cy), R, R);
		g.strokePath (ring, QPen (Rgba (39, 211, 255, 0.8), 1.5));
		QPainterPath tri;
		tri.moveTo (cx, h * 0.62 - 14);
		tri.lineTo (cx - 6, h * 0.62 - 2);
		tri.lineTo (cx + 6, h * 0.62 - 2);
		tri.closeSubpath ();
		g.fillPath (tri, Hex ("#39ff7a"));
	} else {
		const double r = std::min (w, h) * 0.22, px = w * 0.5, py = h * 0.52;
		QRadialGradient grd = Radial (px - r * 0.4, py - r * 0.4, r * 0.1, px, py, r);
		grd.setColorAt (0, col.lighter (160));
		grd.setColorAt (1, col.darker (220));
		FillCircle (g, px, py, r, grd);
		g.save ();
		g.translate (px, py);
		g.rotate (-0.35 * 180 / PI);
		QPainterPath orbit;
		orbit.addEllipse (QRectF (-r * 2.1, -r * 0.6, r * 4.2, r * 1.2));
		g.strokePath (orbit, QPen (Rgba (39, 211, 255, 0.85), 1.4));
		FillCircle (g, r * 2.1 * std::cos (0.8), r * 0.6 * std::sin (0.8), 3, Hex ("#ffc23d"));
		g.restore ();
	}
}

void MiniOrbit::Draw (QPainter &p, const QSize &size)
{
	DrawMini (p, size.width (), size.height (), m_kind, m_seed, m_corner);
}

void MiniOrbit::paintEvent (QPaintEvent *)
{
	QPainter p (this);
	p.drawPixmap (0, 0, Cache (size ()));
}

// ---------------------------------------------------------------------------------------------------------
// OrbitDiagram (Planetary Defense OrbitDiagram.qml: the view area of its panel)

namespace {

	const std::pair<const char*, const char*> PLANET_COLORS[4] = {{"Mercury", "#b5ab9c"}, {"Venus", "#e8c27a"}, {"Earth", "#27d3ff"}, {"Mars", "#ff7a4a"}};
	const std::pair<const char*, const char*> NEO_COLORS[3] = {{"Apophis", "#ffc23d"}, {"Bennu", "#ff9d3d"}, {"Didymos", "#39ffc0"}};

	QColor PlanetColor (const QString &n)
	{
		for (const auto &c : PLANET_COLORS) if (n == QLatin1String (c.first)) return Hex (c.second);
		return Qt::white;
	}

	QColor NeoColor (const QString &n)
	{
		for (const auto &c : NEO_COLORS) if (n == QLatin1String (c.first)) return Hex (c.second);
		return Qt::white;
	}

	double Scale (double w, double h)
	{
		return std::min (w, h) / 2 / (2.272 * 1.04);
	}

	QFont Mono (bool bold)
	{
		QFont f ("monospace");
		f.setStyleHint (QFont::Monospace);
		f.setPixelSize (10);
		f.setBold (bold);
		return f;
	}

	double NowMs ()
	{
		return (double)QDateTime::currentMSecsSinceEpoch ();
	}

}

OrbitDiagramApi::OrbitDiagramApi (OrbitDiagram *diagram): QObject (diagram), d (diagram) {}
bool OrbitDiagramApi::flow () const { return d && d->flow (); }
void OrbitDiagramApi::setFlow (bool on) { if (d) d->setFlow (on); }
double OrbitDiagramApi::offset () const { return d ? d->offset () : 0; }
double OrbitDiagramApi::mjd () const { return d ? d->mjd () : 0; }
double OrbitDiagramApi::baseMjd () const { return d ? d->baseMjd () : 0; }
QString OrbitDiagramApi::focusPlanet () const { return d ? d->FocusPlanet () : QString (); }
QStringList OrbitDiagramApi::hiddenNeos () const { return d ? d->HiddenNeos () : QStringList (); }
bool OrbitDiagramApi::epochOutside () const { return d && (d->mjd () < orbits::MJD_MIN || d->mjd () > orbits::MJD_MAX); }
void OrbitDiagramApi::reset () { if (d) d->Reset (); }

OrbitDiagram::OrbitDiagram (QWidget *parent): Painted (parent), api (new OrbitDiagramApi (this))
{
	timer.setInterval (33);
	connect (&timer, &QTimer::timeout, this, [this]() { Tick (); });
}

void OrbitDiagram::setBaseMjd (double m)
{
	if (!std::isfinite (m) || m == m_base) return;
	m_base = m;
	m_offset = 0;
	startMs = NowMs ();
	startOffset = 0;
	update ();
}

void OrbitDiagram::setFlow (bool on)
{
	if (on == m_flow) return;
	m_flow = on;
	Sync ();
	emit api->flowChanged ();
}

void OrbitDiagram::Reset ()
{
	const bool was = m_flow;
	m_flow = false;
	m_offset = 0;
	Sync ();
	update ();
	if (was) emit api->flowChanged ();
}

QString OrbitDiagram::FocusPlanet () const
{
	const QString &b = m_focus;
	if (b == "Moon") return "Earth";
	if (b == "Phobos" || b == "Deimos") return "Mars";
	return (orbits::IsPlanet (b) || b == "Sun") ? b : QString ();
}

QStringList OrbitDiagram::HiddenNeos () const
{
	QStringList out;
	for (const auto &n : orbits::Neos ())
		if (!orbits::NeoSetAt (n.name, mjd ()).inside) out << QString::fromLatin1 (n.name).toUpper ();
	return out;
}

QString OrbitDiagram::SetKey () const
{
	QString k;
	for (const auto &n : orbits::Neos ()) {
		orbits::SetAt r = orbits::NeoSetAt (n.name, mjd ());
		k += QString::number (r.set ? r.set->epoch : 0, 'g', 17) + (r.inside ? "+" : "-") + ";";
	}
	return k;
}

void OrbitDiagram::Sync ()
{
	const bool run = (m_flow && Running ());
	if (run && !timing) {
		startMs = NowMs ();
		startOffset = m_offset;
		timing = true;
		timer.start ();
	} else if (!run && timing) {
		timing = false;
		timer.stop ();
	}
}

void OrbitDiagram::Tick ()
{
	double o = startOffset + (NowMs () - startMs) / 1000 * 10;
	bool stop = false;
	if (m_base + o >= orbits::MJD_MAX) {
		o = std::max (0.0, orbits::MJD_MAX - m_base);
		stop = true;
	}
	m_offset = o;
	update ();
	if (stop) setFlow (false);
}

void OrbitDiagram::showEvent (QShowEvent *e) { Painted::showEvent (e); Sync (); }
void OrbitDiagram::hideEvent (QHideEvent *e) { Painted::hideEvent (e); Sync (); }

void OrbitDiagram::DrawStatic (QPainter &g, double width, double height, double baseMjd, double mjd)
{
	const double cx = width / 2, cy = height / 2, s = Scale (width, height);
	QPen ringPen (Rgba (39, 211, 255, 0.10), 1);
	for (int k = 1; k <= 2; k++) {
		QPainterPath c;
		c.addEllipse (QPointF (cx, cy), k * s, k * s);
		g.strokePath (c, ringPen);
	}
	g.setFont (Mono (false));
	g.setPen (Rgba (143, 180, 200, 0.55));
	for (int k = 1; k <= 2; k++)
		g.drawText (QPointF (cx - k * s * 0.7071 - 34, cy + k * s * 0.7071 + 12), QString::number (k) + " AU");
	for (const char *n : orbits::PLANETS) {
		std::vector<Vec3> pts = orbits::PlanetPath (n, baseMjd, 180);
		QPainterPath path;
		for (size_t k = 0; k < pts.size (); k++) {
			QPointF q (cx + pts[k].x * s, cy - pts[k].y * s);
			if (k) path.lineTo (q);
			else path.moveTo (q);
		}
		g.setOpacity (0.55);
		g.strokePath (path, QPen (PlanetColor (n), 1.2));
	}
	for (const auto &neo : orbits::Neos ()) {
		orbits::SetAt r = orbits::NeoSetAt (neo.name, mjd);
		std::vector<Vec3> pts = orbits::NeoPath (neo.name, mjd, 180);
		QPainterPath path;
		for (size_t k = 0; k < pts.size (); k++) {
			QPointF q (cx + pts[k].x * s, cy - pts[k].y * s);
			if (k) path.lineTo (q);
			else path.moveTo (q);
		}
		g.setOpacity (r.inside ? 0.8 : 0.22);
		QPen pen (NeoColor (neo.name), 1);
		pen.setDashPattern ({4, 4});
		pen.setCapStyle (Qt::FlatCap);
		g.strokePath (path, pen);
	}
	g.setOpacity (1);
	QRadialGradient sun = Radial (cx, cy, 0, cx, cy, 22);
	sun.setColorAt (0, Rgba (255, 240, 200, 1));
	sun.setColorAt (0.25, Rgba (255, 200, 90, 0.8));
	sun.setColorAt (1, Rgba (255, 160, 40, 0));
	g.fillRect (QRectF (cx - 22, cy - 22, 44, 44), sun);
}

void OrbitDiagram::DrawLive (QPainter &g, double width, double height, double mjd, const QString &focusPlanet)
{
	const double s = Scale (width, height);
	auto at = [&](const Vec3 &v) { return QPointF (width / 2 + v.x * s, height / 2 - v.y * s); };
	QFontMetricsF fb (Mono (true)), fn (Mono (false));
	for (const char *n : orbits::PLANETS) {
		const QPointF q = at (orbits::Planet (n, mjd));
		const QColor c = PlanetColor (n);
		FillCircle (g, q.x (), q.y (), 4.5, c);
		if (focusPlanet == QLatin1String (n)) {
			QPainterPath ring;
			ring.addEllipse (q, 10, 10);
			g.strokePath (ring, QPen (Hex ("#39ff7a"), 2));
		}
		g.setFont (Mono (true));
		g.setPen (c);
		g.drawText (QPointF (q.x () + 9, q.y () - 16 + fb.ascent ()), QString::fromLatin1 (n).toUpper ());
	}
	if (focusPlanet == "Sun") {
		QPainterPath ring;
		ring.addEllipse (QPointF (width / 2, height / 2), 13, 13);
		g.strokePath (ring, QPen (Hex ("#39ff7a"), 2));
	}
	for (const auto &neo : orbits::Neos ()) {
		auto v = orbits::NeoAt (neo.name, mjd);
		if (!v) continue;
		const QPointF q = at (*v);
		const QColor c = NeoColor (neo.name);
		g.save ();
		g.translate (q);
		g.rotate (45);
		g.fillRect (QRectF (-3.5, -3.5, 7, 7), c);
		g.restore ();
		g.setFont (Mono (false));
		g.setPen (c);
		g.drawText (QPointF (q.x () + 8, q.y () + 2 + fn.ascent ()), QString::fromLatin1 (neo.name).toUpper ());
	}
}

void OrbitDiagram::Draw (QPainter &p, const QSize &size)
{
	DrawStatic (p, size.width (), size.height (), m_base, mjd ());
}

void OrbitDiagram::paintEvent (QPaintEvent *)
{
	const QString key = QString::number (m_base, 'g', 17) + "|" + SetKey ();
	if (key != drawnKey) {
		drawnKey = key;
		DropCache ();
	}
	QPainter p (this);
	p.drawPixmap (0, 0, Cache (size ()));
	p.setRenderHint (QPainter::Antialiasing);
	p.setPen (Qt::NoPen);
	DrawLive (p, width (), height (), mjd (), FocusPlanet ());
}

}
