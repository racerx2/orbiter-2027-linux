
// custom: forms skins; the painted widgets (ports of the QML skins' Canvas items), promoted QLabels in Designer

#ifndef __FORMS_FORMPAINTED_H
#define __FORMS_FORMPAINTED_H

#include <QColor>
#include <QElapsedTimer>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QWidget>
#include <cstdint>

class QPainter;
class QPainterPath;

namespace forms {

	// the mulberry32 generator of the QML skins (JS ToInt32 seed)
	struct Mulberry {
		uint32_t a;
		explicit Mulberry (double seed);
		double operator() ();
	};

	// canvas arc (clockwise on screen from a0 to a1, or back when ccw) added to a path
	void CanvasArc (QPainterPath &p, double cx, double cy, double r, double a0, double a1, bool ccw = false);

	// common part: Launcher.active, a cached picture of the widget's size made 120 ms after the last resize
	class Painted: public QWidget {
		Q_OBJECT
		Q_PROPERTY(bool active READ active WRITE setActive)
	public:
		explicit Painted (QWidget *parent = nullptr);
		bool active () const { return m_active; }
		void setActive (bool on);
		virtual QObject *ScriptObject () { return nullptr; } // what JS sees as w.<name>, if anything

	protected:
		virtual void ActiveChanged () {}
		virtual void Draw (QPainter &p, const QSize &size) = 0; // the cached part
		void Invalidate ();           // redraw the cache at the next paint
		void DropCache () { cache = QPixmap (); } // inside paintEvent: no new update
		QPixmap Cache (const QSize &size); // the cached picture for that size (made if missing)
		bool Running () const { return m_active && isVisible (); }
		void resizeEvent (QResizeEvent *e) override;
		bool event (QEvent *e) override;

	private:
		bool m_active = false;
		QPixmap cache;
		QSize cacheSize;
		QTimer resizeTimer;
	};

	class Starfield: public Painted {
		Q_OBJECT
		Q_PROPERTY(int seed READ seed WRITE setSeed)
		Q_PROPERTY(int count READ count WRITE setCount)
		Q_PROPERTY(double alpha READ alpha WRITE setAlpha)
		Q_PROPERTY(int drift READ drift WRITE setDrift)
		Q_PROPERTY(int driftPeriod READ driftPeriod WRITE setDriftPeriod)
	public:
		explicit Starfield (QWidget *parent = nullptr);
		int seed () const { return m_seed; }
		void setSeed (int s) { if (s != m_seed) { m_seed = s; Invalidate (); } }
		int count () const { return m_count; }
		void setCount (int c);
		double alpha () const { return m_alpha; }
		void setAlpha (double a);
		int drift () const { return m_drift; }
		void setDrift (int px);
		int driftPeriod () const { return m_period; }
		void setDriftPeriod (int ms) { m_period = std::max (1000, ms); }
		static void DrawField (QPainter &p, double w, double h, int seed, int count);

	protected:
		void Draw (QPainter &p, const QSize &size) override;
		void paintEvent (QPaintEvent *e) override;
		void ActiveChanged () override;
		void showEvent (QShowEvent *e) override;
		void hideEvent (QHideEvent *e) override;

	private:
		int Offset () const;
		void Tick ();
		void Sync ();
		int m_seed = 20240, m_count = 900, m_drift = 0, m_period = 90000;
		double m_alpha = 1.0;
		QTimer timer;
		QElapsedTimer clock;
		qint64 elapsed = 0; // drift time, paused while not running
		int shown = 0;
	};

	// Horizon: sunrise over Earth from orbit and the sun glare, pulsing
	class Planet: public Painted {
		Q_OBJECT
		Q_PROPERTY(bool glare READ glare WRITE setGlare)
		Q_PROPERTY(bool pulse READ pulse WRITE setPulse)
	public:
		explicit Planet (QWidget *parent = nullptr);
		bool glare () const { return m_glare; }
		void setGlare (bool on) { if (on != m_glare) { m_glare = on; update (); } }
		bool pulse () const { return m_pulse; }
		void setPulse (bool on);
		static void DrawPlanet (QPainter &p, double w, double h);
		static void DrawGlare (QPainter &p, double w, double h); // the 900 x 300 glare picture
		static QPointF SunPoint (double w, double h);

	protected:
		void Draw (QPainter &p, const QSize &size) override;
		void paintEvent (QPaintEvent *e) override;
		void ActiveChanged () override;
		void showEvent (QShowEvent *e) override;
		void hideEvent (QHideEvent *e) override;

	private:
		QRect GlareRect () const;
		double GlareOpacity () const;
		void Sync ();
		bool m_glare = true, m_pulse = false;
		QPixmap glareCache;
		QTimer timer;
		QElapsedTimer clock;
		qint64 elapsed = 0;
	};

	// Horizon: scenario thumbnail painted from the scenario's kind
	class ScenarioThumb: public Painted {
		Q_OBJECT
		Q_PROPERTY(QString kind READ kind WRITE setKind)
		Q_PROPERTY(int seed READ seed WRITE setSeed)
		Q_PROPERTY(double corner READ corner WRITE setCorner)
	public:
		explicit ScenarioThumb (QWidget *parent = nullptr): Painted (parent) {}
		QString kind () const { return m_kind; }
		void setKind (const QString &k) { if (k != m_kind) { m_kind = k; Invalidate (); } }
		int seed () const { return m_seed; }
		void setSeed (int s) { if (s != m_seed) { m_seed = s; Invalidate (); } }
		double corner () const { return m_corner; }
		void setCorner (double c) { if (c != m_corner) { m_corner = c; Invalidate (); } }
		static void DrawThumb (QPainter &p, double w, double h, const QString &kind, int seed, double corner);

	protected:
		void Draw (QPainter &p, const QSize &size) override;
		void paintEvent (QPaintEvent *e) override;

	private:
		QString m_kind = "orbit";
		int m_seed = 0;
		double m_corner = 12;
	};

	// Planetary Defense: the grid and its vignette
	class GridBackdrop: public Painted {
		Q_OBJECT
		Q_PROPERTY(int step READ step WRITE setStep)
	public:
		explicit GridBackdrop (QWidget *parent = nullptr): Painted (parent) {}
		int step () const { return m_step; }
		void setStep (int s) { s = std::clamp (s, 4, 1000); if (s != m_step) { m_step = s; Invalidate (); } }
		static void DrawGrid (QPainter &p, double w, double h, int step);

	protected:
		void Draw (QPainter &p, const QSize &size) override;
		void paintEvent (QPaintEvent *e) override;

	private:
		int m_step = 44;
	};

	// Planetary Defense: card picture, the focus body with an orbit ring or its surface when landed
	class MiniOrbit: public Painted {
		Q_OBJECT
		Q_PROPERTY(QString kind READ kind WRITE setKind)
		Q_PROPERTY(int seed READ seed WRITE setSeed)
		Q_PROPERTY(double corner READ corner WRITE setCorner)
	public:
		explicit MiniOrbit (QWidget *parent = nullptr): Painted (parent) {}
		QString kind () const { return m_kind; }
		void setKind (const QString &k) { if (k != m_kind) { m_kind = k; Invalidate (); } }
		int seed () const { return m_seed; }
		void setSeed (int s) { if (s != m_seed) { m_seed = s; Invalidate (); } }
		double corner () const { return m_corner; }
		void setCorner (double c) { if (c != m_corner) { m_corner = c; Invalidate (); } }
		static void DrawMini (QPainter &p, double w, double h, const QString &kind, int seed, double corner);

	protected:
		void Draw (QPainter &p, const QSize &size) override;
		void paintEvent (QPaintEvent *e) override;

	private:
		QString m_kind = "orbit-Earth";
		int m_seed = 0;
		double m_corner = 11;
	};

	class OrbitDiagram;

	// what JS sees of a diagram: w.<name>
	class OrbitDiagramApi: public QObject {
		Q_OBJECT
		Q_PROPERTY(bool flow READ flow WRITE setFlow NOTIFY flowChanged)
		Q_PROPERTY(double offset READ offset)
		Q_PROPERTY(double mjd READ mjd)
		Q_PROPERTY(double baseMjd READ baseMjd)
		Q_PROPERTY(QString focusPlanet READ focusPlanet)
		Q_PROPERTY(QStringList hiddenNeos READ hiddenNeos)
		Q_PROPERTY(bool epochOutside READ epochOutside)
	public:
		explicit OrbitDiagramApi (OrbitDiagram *d);
		bool flow () const;
		void setFlow (bool on);
		double offset () const;
		double mjd () const;
		double baseMjd () const;
		QString focusPlanet () const;
		QStringList hiddenNeos () const;
		bool epochOutside () const;
		Q_INVOKABLE void reset ();
	signals:
		void flowChanged ();
	private:
		QPointer<OrbitDiagram> d;
	};

	// Planetary Defense: top view of the inner solar system at an epoch, with time flow
	class OrbitDiagram: public Painted {
		Q_OBJECT
		Q_PROPERTY(double baseMjd READ baseMjd WRITE setBaseMjd)
		Q_PROPERTY(bool fromScenario READ fromScenario WRITE setFromScenario)
		Q_PROPERTY(QString focusBody READ focusBody WRITE setFocusBody)
		Q_PROPERTY(QString system READ system WRITE setSystem)
		Q_PROPERTY(bool flow READ flow WRITE setFlow)
	public:
		explicit OrbitDiagram (QWidget *parent = nullptr);
		double baseMjd () const { return m_base; }
		void setBaseMjd (double m);
		bool fromScenario () const { return m_fromScenario; }
		void setFromScenario (bool on) { m_fromScenario = on; }
		QString focusBody () const { return m_focus; }
		void setFocusBody (const QString &b) { if (b != m_focus) { m_focus = b; update (); } }
		QString system () const { return m_system; }
		void setSystem (const QString &s) { m_system = s; }
		bool flow () const { return m_flow; }
		void setFlow (bool on);
		double offset () const { return m_offset; }
		double mjd () const { return m_base + m_offset; }
		QString FocusPlanet () const;
		QStringList HiddenNeos () const;
		void Reset ();
		QObject *ScriptObject () override { return api; }
		static void DrawStatic (QPainter &p, double w, double h, double baseMjd, double mjd);
		static void DrawLive (QPainter &p, double w, double h, double mjd, const QString &focusPlanet);

	protected:
		void Draw (QPainter &p, const QSize &size) override;
		void paintEvent (QPaintEvent *e) override;
		void ActiveChanged () override { Sync (); }
		void showEvent (QShowEvent *e) override;
		void hideEvent (QHideEvent *e) override;

	private:
		QString SetKey () const;
		void Sync ();
		void Tick ();
		double m_base = 0, m_offset = 0;
		bool m_fromScenario = false, m_flow = false;
		QString m_focus, m_system, drawnKey;
		double startMs = 0, startOffset = 0;
		bool timing = false;
		QTimer timer;
		OrbitDiagramApi *api;
	};

}

#endif // !__FORMS_FORMPAINTED_H
