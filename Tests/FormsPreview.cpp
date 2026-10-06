// custom: forms skins; writes the pictures of the shipped forms skins (icons, Designer previews) with the painted widgets' code; a tool, not a test

#include "FormPainted.h"
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <cmath>
#include <cstdio>

using namespace forms;

namespace {

	const double PI = 3.14159265358979323846;

	QImage Blank (int w, int h)
	{
		QImage img (w, h, QImage::Format_ARGB32_Premultiplied);
		img.fill (Qt::transparent);
		return img;
	}

	bool Save (const QImage &img, const QString &dir, const QString &name, int quality = -1)
	{
		const QString path = dir + "/" + name;
		if (!img.save (path, nullptr, quality)) {
			fprintf (stderr, "can't write %s\n", qPrintable (path));
			return false;
		}
		printf ("%s\n", qPrintable (path));
		return true;
	}

	QColor Rgba (int r, int g, int b, double a)
	{
		QColor c (r, g, b);
		c.setAlphaF (a);
		return c;
	}

	// GhostButton's star (both skins)
	QImage Star (const QColor &stroke, const QColor &fill, bool filled)
	{
		QImage img = Blank (24, 24);
		QPainter p (&img);
		p.setRenderHint (QPainter::Antialiasing);
		QPainterPath path;
		for (int i = 0; i < 10; i++) {
			const double a = -PI / 2 + i * PI / 5, rr = (i % 2) ? 5 : 11;
			const QPointF q (12 + rr * std::cos (a), 12.5 + rr * std::sin (a));
			if (i) path.lineTo (q);
			else path.moveTo (q);
		}
		path.closeSubpath ();
		if (filled) p.fillPath (path, fill);
		QPen pen (filled ? fill : stroke, 1.6);
		pen.setJoinStyle (Qt::RoundJoin);
		p.strokePath (path, pen);
		return img;
	}

	// the launch buttons' triangle, with transparent room on the right for the gap to the label
	QImage Play (int w, int h, int pad, const QColor &c, QPolygonF tri)
	{
		QImage img = Blank (w + pad, h);
		QPainter p (&img);
		p.setRenderHint (QPainter::Antialiasing);
		QPainterPath path;
		path.addPolygon (tri);
		path.closeSubpath ();
		p.fillPath (path, c);
		return img;
	}

	QImage Lens (const QColor &c)
	{
		QImage img = Blank (14, 14);
		QPainter p (&img);
		p.setRenderHint (QPainter::Antialiasing);
		QPen pen (c, 1.6);
		pen.setCapStyle (Qt::FlatCap);
		p.setPen (pen);
		p.drawEllipse (QPointF (6, 6), 4.5, 4.5);
		p.drawLine (QPointF (9.5, 9.5), QPointF (13, 13));
		return img;
	}

	QImage Folder (const QColor &fill, const QColor &stroke)
	{
		QImage img = Blank (64, 50);
		QPainter p (&img);
		p.setRenderHint (QPainter::Antialiasing);
		QPainterPath path;
		path.moveTo (2, 10);
		path.lineTo (22, 10);
		path.lineTo (28, 4);
		path.lineTo (62, 4);
		path.lineTo (62, 48);
		path.lineTo (2, 48);
		path.closeSubpath ();
		p.fillPath (path, fill);
		QPen pen (stroke, 2);
		pen.setJoinStyle (Qt::RoundJoin);
		p.strokePath (path, pen);
		return img;
	}

	QImage Toggle (bool on, bool enabled, const QColor &track, const QColor &knobOn, const QColor &knobOff)
	{
		QImage img = Blank (46, 26);
		QPainter p (&img);
		p.setRenderHint (QPainter::Antialiasing);
		p.setOpacity (enabled ? 1.0 : 0.4);
		QPainterPath t;
		t.addRoundedRect (QRectF (0, 0, 46, 26), 13, 13);
		p.fillPath (t, on ? track : Rgba (255, 255, 255, 0.12));
		QPainterPath k;
		k.addEllipse (QRectF (on ? 23 : 3, 3, 20, 20));
		p.fillPath (k, on ? knobOn : knobOff);
		return img;
	}

	bool Toggles (const QString &dir, const QColor &track, const QColor &knobOn, const QColor &knobOff)
	{
		return Save (Toggle (true, true, track, knobOn, knobOff), dir, "toggle-on.png")
			&& Save (Toggle (false, true, track, knobOn, knobOff), dir, "toggle-off.png")
			&& Save (Toggle (true, false, track, knobOn, knobOff), dir, "toggle-on-disabled.png")
			&& Save (Toggle (false, false, track, knobOn, knobOff), dir, "toggle-off-disabled.png");
	}

	// Horizon OrbitMark.qml
	QImage OrbitMark ()
	{
		QImage img = Blank (34, 34);
		QPainter p (&img);
		p.setRenderHint (QPainter::Antialiasing);
		const QColor text ("#eef3fb"), accent ("#f5a524");
		QPainterPath disc;
		disc.addEllipse (QPointF (17, 17), 6.5, 6.5);
		p.fillPath (disc, text);
		p.save ();
		p.translate (17, 17);
		p.rotate (-0.45 * 180 / PI);
		p.scale (1, 0.38);
		QPainterPath ring;
		ring.addEllipse (QPointF (0, 0), 15, 15);
		p.strokePath (ring, QPen (accent, 3.2));
		p.restore ();
		QPainterPath top;
		CanvasArc (top, 17, 17, 6.5, PI * 1.05, PI * 1.95);
		top.closeSubpath ();
		p.fillPath (top, text);
		return img;
	}

	// Planetary Defense Emblem.qml
	QImage Emblem (int w, int h)
	{
		QImage img = Blank (w, h);
		QPainter g (&img);
		g.setRenderHint (QPainter::Antialiasing);
		const double cx = w / 2.0, cy = h / 2.0, R = std::min (w, h) * 0.24;
		QRadialGradient earth (QPointF (cx, cy), R, QPointF (cx - R * 0.35, cy - R * 0.35), R * 0.1);
		earth.setColorAt (0, QColor ("#7fe6ff"));
		earth.setColorAt (0.55, QColor ("#1f8fc4"));
		earth.setColorAt (1, QColor ("#0a2a44"));
		QPainterPath disc;
		disc.addEllipse (QPointF (cx, cy), R, R);
		g.fillPath (disc, earth);
		QPainterPath shade;
		shade.setFillRule (Qt::WindingFill);
		CanvasArc (shade, cx, cy, R, -PI * 0.15, PI * 0.85);
		CanvasArc (shade, cx + R * 0.35, cy + R * 0.1, R * 0.9, PI * 0.85, -PI * 0.15, true);
		shade.closeSubpath ();
		g.fillPath (shade, Rgba (2, 10, 20, 0.55));
		g.save ();
		g.translate (cx, cy);
		g.rotate (-0.42 * 180 / PI);
		QPainterPath orbit;
		orbit.addEllipse (QRectF (-w * 0.44, -h * 0.17, w * 0.88, h * 0.34));
		g.strokePath (orbit, QPen (Rgba (39, 211, 255, 0.9), std::max (1.2, w / 40.0)));
		const double ax = w * 0.44 * std::cos (-0.6), ay = h * 0.17 * std::sin (-0.6);
		QPainterPath arc;
		CanvasArc (arc, ax - w * 0.2, ay + h * 0.02, w * 0.2, -0.25, 0.35);
		QPen ap (QColor ("#39ff7a"), std::max (1.4, w / 32.0));
		ap.setCapStyle (Qt::FlatCap);
		g.strokePath (arc, ap);
		QPainterPath rock;
		rock.addEllipse (QPointF (ax, ay), std::max (2.0, w / 16.0), std::max (2.0, w / 16.0));
		g.fillPath (rock, QColor ("#ffc23d"));
		g.restore ();
		return img;
	}

	bool Horizon (const QString &dir)
	{
		const QColor text ("#eef3fb"), dim ("#a3b0c4"), accent ("#f5a524"), ink ("#1b1203");
		QImage stars (1400, 860, QImage::Format_RGB32);
		stars.fill (QColor ("#03060c"));
		{
			QPainter p (&stars);
			p.setRenderHint (QPainter::Antialiasing);
			Starfield::DrawField (p, 1460, 860, 20240, 900);
		}
		QImage planet = Blank (700, 430);
		{
			QPainter p (&planet);
			p.setRenderHint (QPainter::Antialiasing);
			p.scale (0.5, 0.5);
			Planet::DrawPlanet (p, 1400, 860);
			QImage glare = Blank (900, 300);
			QPainter g (&glare);
			g.setRenderHint (QPainter::Antialiasing);
			Planet::DrawGlare (g, 900, 300);
			g.end ();
			const QPointF s = Planet::SunPoint (1400, 860);
			p.setOpacity (0.91);
			p.drawImage (QPointF (s.x () - 450, s.y () - 150), glare);
		}
		QImage thumb = Blank (248, 108);
		{
			QPainter p (&thumb);
			p.setRenderHint (QPainter::Antialiasing);
			ScenarioThumb::DrawThumb (p, 248, 108, "orbit", 4242, 11);
		}
		return Save (stars, dir, "preview-stars.jpg", 88) && Save (planet, dir, "preview-planet.png") && Save (thumb, dir, "preview-thumb.png")
			&& Save (OrbitMark (), dir, "orbitmark.png")
			&& Save (Play (15, 18, 10, ink, QPolygonF ({{1, 1}, {15, 9}, {1, 17}})), dir, "play.png")
			&& Save (Star (text, accent, false), dir, "star.png") && Save (Star (text, accent, true), dir, "star-on.png")
			&& Save (Lens (dim), dir, "lens.png") && Save (Folder (Rgba (245, 165, 36, 0.22), accent), dir, "folder.png")
			&& Toggles (dir, accent, ink, dim);
	}

	bool Defense (const QString &dir)
	{
		const QColor text ("#e6f6ff"), dim ("#8fb4c8"), accent ("#27d3ff"), ink ("#02121c");
		QImage stars (1400, 860, QImage::Format_RGB32);
		stars.fill (QColor ("#040a14"));
		{
			QPainter p (&stars);
			p.setRenderHint (QPainter::Antialiasing);
			QImage field = Blank (1400, 860);
			QPainter f (&field);
			f.setRenderHint (QPainter::Antialiasing);
			Starfield::DrawField (f, 1400, 860, 20240, 600);
			f.end ();
			p.setOpacity (0.55);
			p.drawImage (0, 0, field);
			p.setOpacity (1);
			GridBackdrop::DrawGrid (p, 1400, 860, 44);
		}
		QImage mini = Blank (289, 86);
		{
			QPainter p (&mini);
			p.setRenderHint (QPainter::Antialiasing);
			MiniOrbit::DrawMini (p, 289, 86, "orbit-Earth", 4242, 11);
		}
		QImage diagram = Blank (568, 690);
		{
			QPainter p (&diagram);
			p.setRenderHint (QPainter::Antialiasing);
			const double mjd = 61315.0; // 2026-10-01
			OrbitDiagram::DrawStatic (p, 568, 690, mjd, mjd);
			OrbitDiagram::DrawLive (p, 568, 690, mjd, "Earth");
		}
		return Save (stars, dir, "preview-backdrop.jpg", 88) && Save (mini, dir, "preview-mini.png") && Save (diagram, dir, "preview-diagram.png")
			&& Save (Emblem (46, 46), dir, "emblem.png")
			&& Save (Play (13, 16, 8, Qt::white, QPolygonF ({{1, 1}, {13, 8}, {1, 15}})), dir, "play.png")
			&& Save (Star (text, accent, false), dir, "star.png") && Save (Star (text, accent, true), dir, "star-on.png")
			&& Save (Lens (dim), dir, "lens.png") && Save (Folder (Rgba (39, 211, 255, 0.22), accent), dir, "folder.png")
			&& Toggles (dir, accent, ink, dim);
	}

}

int main (int argc, char **argv)
{
	QGuiApplication app (argc, argv);
	if (argc != 3) {
		fprintf (stderr, "usage: FormsPreview horizon|defense <images folder>\n");
		return 2;
	}
	const QString which = QString::fromLocal8Bit (argv[1]), dir = QString::fromLocal8Bit (argv[2]);
	const bool ok = (which == "horizon" ? Horizon (dir) : which == "defense" ? Defense (dir) : false);
	return ok ? 0 : 1;
}
