// custom: forms skins; the shipped forms skins (Horizon, Planetary Defense) with a Launcher of the full API 1, offscreen

#include <catch2/catch_test_macros.hpp>
#include "FormRuntime.h"
#include "FormsView.h"
#include "Orbits.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJSEngine>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTest>
#include <QUrl>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace forms;

namespace {

	QApplication &App ()
	{
		static int argc = 1;
		static char arg0[] = "FormsSkins.Test";
		static char *argv[] = {arg0, nullptr};
		static QApplication app (argc, argv);
		return app;
	}

	void Settle ()
	{
		for (int i = 0; i < 4; i++) QApplication::processEvents ();
	}

	const QString SKINS = QStringLiteral (SKINS_DIR);

}

// the Launcher object with every name of API 1 (Skins/README.md)
class FullLauncher: public QObject {
	Q_OBJECT
	Q_PROPERTY(int apiVersion READ apiVersion CONSTANT)
	Q_PROPERTY(QString version READ version CONSTANT)
	Q_PROPERTY(QString build READ build CONSTANT)
	Q_PROPERTY(QString skin READ skin NOTIFY skinChanged)
	Q_PROPERTY(QUrl skinUrl READ skinUrl NOTIFY skinChanged)
	Q_PROPERTY(QVariantList skins READ skins NOTIFY skinsChanged)
	Q_PROPERTY(QVariantList scenarios READ scenarios NOTIFY scenariosChanged)
	Q_PROPERTY(QString currentScenario READ currentScenario WRITE setCurrentScenario NOTIFY currentScenarioChanged)
	Q_PROPERTY(bool currentIsScenario READ currentIsScenario NOTIFY currentScenarioChanged)
	Q_PROPERTY(QString currentDescription READ currentDescription NOTIFY currentScenarioChanged)
	Q_PROPERTY(bool canLaunch READ canLaunch NOTIFY canLaunchChanged)
	Q_PROPERTY(bool startPaused READ startPaused WRITE setStartPaused NOTIFY startPausedChanged)
	Q_PROPERTY(QStringList recent READ recent NOTIFY recentChanged)
	Q_PROPERTY(QStringList favourites READ favourites NOTIFY favouritesChanged)
	Q_PROPERTY(QVariantList modules READ modules NOTIFY modulesChanged)
	Q_PROPERTY(QVariantMap setup READ setup NOTIFY setupChanged)
	Q_PROPERTY(bool active READ active NOTIFY activeChanged)
	Q_PROPERTY(QString page READ page WRITE setPage NOTIFY pageChanged)
	Q_PROPERTY(QVariantMap state READ state WRITE setState NOTIFY stateChanged)
public:
	QString skinId, cur, pg;
	QVariantList scn, mods, skinList;
	QStringList rec, fav, calls;
	QVariantMap set, st;
	bool paused = false, act = true;

	FullLauncher ()
	{
		Scenarios (0);
		mods = {QVariantMap {{"name", "ScnEditor"}, {"category", "Tools"}, {"info", "Edits scenarios."}, {"active", true}, {"locked", false}},
			QVariantMap {{"name", "TransX"}, {"category", "Navigation"}, {"info", ""}, {"active", false}, {"locked", false}},
			QVariantMap {{"name", "D3D9Client"}, {"category", "Graphics"}, {"info", "A client <with> tags"}, {"active", true}, {"locked", true}}};
		set = {{"graphicsClient", "Console mode (no engine loaded)"}, {"device", ""}, {"fullscreen", false}, {"width", "1280"}, {"height", "800"},
			{"activeModules", 2}, {"nonsphericalGravity", true}, {"radiationPressure", false}, {"distributedMass", false}, {"atmWind", true}};
		skinList = {QVariantMap {{"id", "Horizon"}, {"name", "Horizon"}, {"author", "me"}, {"version", "2.0"}, {"description", "d"}, {"kind", "forms+qss"}, {"layout", false}, {"compatible", true}, {"reason", ""}},
			QVariantMap {{"id", "Broken"}, {"name", "Broken"}, {"author", ""}, {"version", ""}, {"description", ""}, {"kind", "qml"}, {"layout", true}, {"compatible", false}, {"reason", "no file"}}};
	}
	void Scenarios (int extra)
	{
		scn = {QVariantMap {{"path", "(Current state)"}, {"name", "(Current state)"}, {"folder", ""}, {"isFolder", false}, {"depth", 0}},
			QVariantMap {{"path", "Delta-glider"}, {"name", "Delta-glider"}, {"folder", ""}, {"isFolder", true}, {"depth", 0}},
			QVariantMap {{"path", "Delta-glider/DG-S"}, {"name", "DG-S"}, {"folder", "Delta-glider"}, {"isFolder", true}, {"depth", 1}},
			QVariantMap {{"path", "Delta-glider/DG-S/Smack!"}, {"name", "Smack!"}, {"folder", "Delta-glider/DG-S"}, {"isFolder", false}, {"depth", 2}},
			QVariantMap {{"path", "Delta-glider/Brighton Beach"}, {"name", "Brighton Beach"}, {"folder", "Delta-glider"}, {"isFolder", false}, {"depth", 1}},
			QVariantMap {{"path", "Quicksave"}, {"name", "Quicksave"}, {"folder", ""}, {"isFolder", true}, {"depth", 0}},
			QVariantMap {{"path", "Quicksave/Quicksave 0001"}, {"name", "Quicksave 0001"}, {"folder", "Quicksave"}, {"isFolder", false}, {"depth", 1}}};
		QVariantMap many {{"path", "Many"}, {"name", "Many"}, {"folder", ""}, {"isFolder", true}, {"depth", 0}};
		if (extra) scn << many;
		for (int i = 0; i < extra; i++)
			scn << QVariantMap {{"path", QString ("Many/Scenario %1").arg (i)}, {"name", QString ("Scenario %1").arg (i)}, {"folder", "Many"}, {"isFolder", false}, {"depth", 1}};
		emit scenariosChanged ();
	}
	int apiVersion () const { return 1; }
	QString version () const { return "Orbiter test"; }
	QString build () const { return "Build text"; }
	QString skin () const { return skinId; }
	QUrl skinUrl () const { return QUrl (); }
	QVariantList skins () const { return skinList; }
	QVariantList scenarios () const { return scn; }
	QString currentScenario () const { return cur; }
	void setCurrentScenario (const QString &p) { cur = p; emit currentScenarioChanged (); }
	bool currentIsScenario () const
	{
		for (const QVariant &v : scn) if (v.toMap ()["path"] == cur) return !v.toMap ()["isFolder"].toBool ();
		return false;
	}
	QString currentDescription () const { return cur.isEmpty () ? QString () : "A description of " + cur + " that is long enough to wrap onto a second line in the panel."; }
	bool canLaunch () const { return currentIsScenario (); }
	bool startPaused () const { return paused; }
	void setStartPaused (bool on) { paused = on; emit startPausedChanged (); }
	QStringList recent () const { return rec; }
	QStringList favourites () const { return fav; }
	QVariantList modules () const { return mods; }
	QVariantMap setup () const { return set; }
	bool active () const { return act; }
	QString page () const { return pg; }
	void setPage (const QString &p) { pg = p; emit pageChanged (); }
	QVariantMap state () const { return st; }
	void setState (const QVariantMap &s) { st = s; emit stateChanged (); }

	Q_INVOKABLE QVariantMap scenarioInfo (const QString &p)
	{
		for (const QVariant &v : scn) {
			QVariantMap e = v.toMap ();
			if (e["path"] != p) continue;
			if (e["isFolder"].toBool ()) return {{"name", e["name"]}, {"folder", e["folder"]}, {"isFolder", true}, {"path", p}};
			return {{"name", e["name"]}, {"folder", e["folder"]}, {"isFolder", false}, {"path", p}, {"description", "x"}, {"system", "Sol"},
				{"focus", "GL-01"}, {"focusClass", "DeltaGlider"}, {"focusStatus", "Landed"}, {"focusBody", "Earth"}, {"focusBase", "Habana"},
				{"focusPad", 1}, {"vesselCount", 3}, {"vessels", QVariantList {QVariantMap {{"name", "GL-01"}, {"className", "DeltaGlider"}}}},
				{"mjd", 51982.5}, {"date", "2001-03-14 12:00"}};
		}
		return {};
	}
	Q_INVOKABLE bool launch (const QString &p = QString ()) { calls << "launch " + p; return true; }
	Q_INVOKABLE void showClassic (const QString &p) { calls << "classic " + p; }
	Q_INVOKABLE void help (const QString &p = QString ()) { calls << "help " + p; }
	Q_INVOKABLE void setModuleActive (const QString &n, bool on) { calls << QString ("module %1 %2").arg (n).arg (on); }
	Q_INVOKABLE void deactivateAllModules () { calls << "deactivate"; }
	Q_INVOKABLE void saveCurrentState () { calls << "save"; }
	Q_INVOKABLE void clearQuicksaves () { calls << "clear"; }
	Q_INVOKABLE bool toggleFavourite (const QString &p)
	{
		if (fav.contains (p)) { fav.removeAll (p); emit favouritesChanged (); return false; }
		fav << p;
		emit favouritesChanged ();
		return true;
	}
	Q_INVOKABLE void setSkin (const QString &id) { calls << "skin " + id; }
	Q_INVOKABLE void refreshSetup () {}
	Q_INVOKABLE bool openUrl (const QUrl &u) { calls << "url " + u.toString (); return true; }
	Q_INVOKABLE void quit () { calls << "quit"; }
	Q_INVOKABLE void log (const QString &) {}
signals:
	void skinChanged ();
	void skinsChanged ();
	void scenariosChanged ();
	void currentScenarioChanged ();
	void canLaunchChanged ();
	void startPausedChanged ();
	void recentChanged ();
	void favouritesChanged ();
	void modulesChanged ();
	void setupChanged ();
	void activeChanged ();
	void pageChanged ();
	void stateChanged ();
	void returnedFromClassic ();
};

namespace {

	struct AppFirst {
		AppFirst () { App (); }
	};

	struct Skin: AppFirst {
		QWidget host;
		FullLauncher api;
		std::unique_ptr<FormsView> view;
		QStringList logs;
		bool ok = false;
		QString err;
		Skin (const QString &id, int w = 1400, int h = 860, const QString &hostSheet = QString ())
		{
			App ();
			api.skinId = QFileInfo (id).fileName ();
			host.resize (w, h);
			if (!hostSheet.isEmpty ()) host.setStyleSheet (hostSheet);
			const QString dir = (QDir::isAbsolutePath (id) ? id : SKINS + "/" + id);
			view = std::make_unique<FormsView> (&host, &api, dir, "forms/Main.ui", [this](const QString &s) { logs << s; });
			view->waitForLoopLevel1 = false;
			view->setGeometry (host.rect ());
			ok = view->Load (err);
			host.show ();
			view->show ();
			Settle ();
		}
		FormRuntime *rt () { return view->Runtime (); }
		QString Log () { return (err + "\n" + logs.join ("\n")); }
	};

	const char *const HORIZON_PAGES[] = {"PLAY", "SCENARIOS", "ADDONS", "SETTINGS", "ABOUT"};

	void Visit (Skin &s, const char *const *pages, int n)
	{
		for (int i = 0; i < n; i++) {
			s.api.setPage (pages[i]);
			Settle ();
			auto *st = s.view->findChild<QStackedWidget*> ("pages");
			REQUIRE (st);
			CHECK (st->currentWidget ()->property ("page").toString () == pages[i]);
		}
	}

	double Difference (const QImage &a, const QImage &b)
	{
		if (a.size () != b.size ()) return 1.0;
		long diff = 0;
		for (int y = 0; y < a.height (); y++)
			for (int x = 0; x < a.width (); x++)
				if (a.pixel (x, y) != b.pixel (x, y)) diff++;
		return double (diff) / (a.width () * a.height ());
	}

}

TEST_CASE ("Horizon: the form loads, every page shows, nothing warns")
{
	Skin s ("Horizon");
	INFO (s.Log ().toStdString ());
	REQUIRE (s.ok);
	CHECK (s.rt ()->Warnings () == 0);
	for (const char *path : {"", "Delta-glider", "Delta-glider/DG-S/Smack!", "(Current state)"}) {
		s.api.setCurrentScenario (path);
		Visit (s, HORIZON_PAGES, 5);
	}
	s.api.rec = {"Delta-glider/DG-S/Smack!"};
	s.api.fav = {"Delta-glider/Brighton Beach"};
	emit s.api.recentChanged ();
	s.api.set["graphicsClient"] = "D3D9Client";
	s.api.set["device"] = "GPU";
	emit s.api.setupChanged ();
	Visit (s, HORIZON_PAGES, 5);
	CHECK (s.rt ()->Warnings () == 0);
	s.api.setPage ("PLAY");
	Settle ();
	CHECK (s.view->findChild<QLabel*> ("playTitle")->text () == "(Current state)");
}

TEST_CASE ("Horizon: the Play page's cards, star and keys")
{
	Skin s ("Horizon");
	REQUIRE (s.ok);
	s.api.rec = {"Delta-glider/DG-S/Smack!"};
	s.api.fav = {"Delta-glider/Brighton Beach"};
	emit s.api.recentChanged ();
	Settle ();
	int cards = 0;
	for (QWidget *c : s.view->findChild<QWidget*> ("playCards")->findChildren<QWidget*> ("playCard")) cards += c->isVisible ();
	CHECK (cards == 3);
	s.view->setFocus ();
	QTest::keyClick (s.view.get (), Qt::Key_Right);
	Settle ();
	CHECK (s.api.cur == "(Current state)");
	QTest::keyClick (s.view.get (), Qt::Key_Right);
	Settle ();
	CHECK (s.api.cur == "Delta-glider/DG-S/Smack!");
	QTest::keyClick (s.view.get (), Qt::Key_Return);
	Settle ();
	CHECK (s.api.calls.contains ("launch Delta-glider/DG-S/Smack!"));
	CHECK (s.rt ()->Warnings () == 0);
}

TEST_CASE ("Horizon: the star and its toast; search, Return picks, Return launches; a new page has no search")
{
	Skin s ("Horizon");
	REQUIRE (s.ok);
	s.api.setCurrentScenario ("Delta-glider/DG-S/Smack!");
	Settle ();
	auto *star = s.view->findChild<QPushButton*> ("playStar");
	auto *toast = s.view->findChild<QLabel*> ("toast");
	REQUIRE (star);
	REQUIRE (toast);
	star->click ();
	Settle ();
	CHECK (s.api.fav.contains ("Delta-glider/DG-S/Smack!"));
	CHECK (toast->text () == "Added to favourites");
	CHECK (toast->isVisible ());
	star->click ();
	Settle ();
	CHECK_FALSE (s.api.fav.contains ("Delta-glider/DG-S/Smack!"));
	CHECK (toast->text () == "Removed from favourites");

	s.api.setPage ("SCENARIOS");
	Settle ();
	auto *search = s.view->findChild<QLineEdit*> ("scnSearch");
	REQUIRE (search);
	s.host.activateWindow ();
	search->setFocus ();
	Settle ();
	REQUIRE (QApplication::focusWidget () == search);
	QTest::keyClicks (search, "brighton");
	QTest::keyClick (search, Qt::Key_Return); // in the same batch: the refresh for the typing hasn't run yet
	CHECK (s.api.cur == "Delta-glider/Brighton Beach");
	Settle ();
	QTest::keyClick (search, Qt::Key_Return);
	Settle ();
	CHECK (s.api.calls.contains ("launch Delta-glider/Brighton Beach"));
	s.api.setPage ("PLAY");
	Settle ();
	s.api.setPage ("SCENARIOS");
	Settle ();
	CHECK (search->text ().isEmpty ()); // as the QML Loader made the page again
	CHECK (s.rt ()->Engine ()->evaluate ("ui.query").toString ().isEmpty ());
	CHECK (s.rt ()->Warnings () == 0);
}

TEST_CASE ("Horizon: a big scenario tree refreshes in time")
{
	Skin s ("Horizon");
	REQUIRE (s.ok);
	s.api.Scenarios (1500);
	s.api.setPage ("SCENARIOS");
	s.rt ()->Engine ()->evaluate ("Logic.open('Many')");
	Settle ();
	QElapsedTimer t;
	t.start ();
	for (int i = 0; i < 10; i++) s.rt ()->Refresh ();
	const double ms = t.elapsed () / 10.0;
	INFO ("refresh " << ms << " ms");
	CHECK (ms < 150.0); // about 10 ms in Release; this only catches a refresh that builds every row
	QWidget *body = s.view->findChild<QWidget*> ("scnGridBody");
	REQUIRE (body);
	CHECK (body->findChildren<QWidget*> ("scnCard").size () < 60);
	CHECK (s.rt ()->Warnings () == 0);
}

TEST_CASE ("Horizon: grid cells that change template leave nothing behind; short rows keep the card width")
{
	Skin s ("Horizon");
	REQUIRE (s.ok);
	s.api.setPage ("SCENARIOS");
	s.rt ()->Engine ()->evaluate ("Logic.open('')");
	Settle ();
	s.rt ()->Engine ()->evaluate ("Logic.open('Delta-glider')");
	Settle ();
	QWidget *body = s.view->findChild<QWidget*> ("scnGridBody");
	REQUIRE (body);
	int folders = 0, cards = 0;
	for (QWidget *c : body->findChildren<QWidget*> ("folderCard")) folders += c->isVisible ();
	for (QWidget *c : body->findChildren<QWidget*> ("scnCard")) cards += c->isVisible ();
	CHECK (folders == 1);
	CHECK (cards == 1);
	s.api.setPage ("ADDONS");
	Settle ();
	QList<QWidget*> mods;
	for (QWidget *c : s.view->findChildren<QWidget*> ("modCard")) if (c->isVisible ()) mods << c;
	REQUIRE (mods.size () == 3);
	QWidget *grid = mods[0]->parentWidget ();
	const int cols = std::max (1, (grid->width () + 14) / 384);
	const int want = (grid->width () - 14 * (cols - 1)) / cols;
	for (QWidget *c : mods) CHECK (std::abs (c->width () - want) <= 1);
	CHECK (s.rt ()->Warnings () == 0);
}

TEST_CASE ("Horizon: the classic pages' style sheet doesn't change the launcher")
{
	const QString hostile = "* { color: red; background: red; border: 5px solid red; border-radius: 9px; padding: 9px; margin: 9px; font-size: 30px; }";
	for (const char *page : HORIZON_PAGES) {
		Skin plain ("Horizon"), styled ("Horizon", 1400, 860, hostile);
		REQUIRE (plain.ok);
		REQUIRE (styled.ok);
		plain.api.setPage (page);
		styled.api.setPage (page);
		Settle ();
		for (Skin *k : {&plain, &styled}) // the glare's pulse runs from each planet's own show; the grabs come before a refresh
			for (QWidget *w : k->view->findChildren<QWidget*> ())
				if (w->metaObject ()->indexOfProperty ("pulse") >= 0) w->setProperty ("pulse", false);
		const QImage a = plain.view->grab ().toImage (), b = styled.view->grab ().toImage ();
		const double d = Difference (a, b);
		INFO (page << ": " << d * 100 << " % of the pixels differ");
		if (d > 0.002) b.save (QString ("/tmp/forms_hostile_%1.png").arg (page));
		CHECK (d <= 0.002);
	}
}

namespace {
	const char *const PD_PAGES[] = {"CONTROL", "MISSIONS", "SYSTEMS", "SETTINGS", "DEFENSE"};
}

TEST_CASE ("Planetary Defense: the form loads, every page shows, nothing warns")
{
	Skin s ("PlanetaryDefense");
	INFO (s.Log ().toStdString ());
	REQUIRE (s.ok);
	CHECK (s.rt ()->Warnings () == 0);
	for (const char *path : {"", "Delta-glider", "Delta-glider/DG-S/Smack!", "(Current state)"}) {
		s.api.setCurrentScenario (path);
		Visit (s, PD_PAGES, 5);
	}
	s.api.rec = {"Delta-glider/DG-S/Smack!"};
	s.api.fav = {"Delta-glider/Brighton Beach"};
	emit s.api.recentChanged ();
	s.api.set["graphicsClient"] = "D3D9Client";
	s.api.set["device"] = "GPU";
	emit s.api.setupChanged ();
	Visit (s, PD_PAGES, 5);
	CHECK (s.rt ()->Warnings () == 0);
	s.api.setPage ("CONTROL");
	Settle ();
	CHECK (s.view->findChild<QLabel*> ("missionTitle")->text () == "(Current state)");
	CHECK (s.view->findChild<QLabel*> ("readiness")->text () == "GREEN");
	CHECK (s.view->findChild<QLabel*> ("neoLine") != nullptr);
}

TEST_CASE ("Planetary Defense: the mission board, its keys and the flyby clock")
{
	Skin s ("PlanetaryDefense");
	REQUIRE (s.ok);
	s.api.rec = {"Delta-glider/DG-S/Smack!"};
	s.api.fav = {"Delta-glider/Brighton Beach"};
	emit s.api.recentChanged ();
	Settle ();
	int rows = 0;
	for (QWidget *r : s.view->findChild<QWidget*> ("boardRows")->findChildren<QWidget*> ("boardRow")) rows += r->isVisible ();
	CHECK (rows == 3);
	CHECK (s.view->findChild<QLabel*> ("boardHeadInfo")->text () == "3 ENTRIES");
	s.view->setFocus ();
	QTest::keyClick (s.view.get (), Qt::Key_Down);
	Settle ();
	CHECK (s.api.cur == "(Current state)");
	QTest::keyClick (s.view.get (), Qt::Key_Down);
	Settle ();
	CHECK (s.api.cur == "Delta-glider/DG-S/Smack!");
	QTest::keyClick (s.view.get (), Qt::Key_Return);
	Settle ();
	CHECK (s.api.calls.contains ("launch Delta-glider/DG-S/Smack!"));
	QLabel *ms = s.view->findChild<QLabel*> ("tileMsNumber");
	REQUIRE (ms);
	CHECK (ms->text ().size () == 3);
	const QString before = ms->text ();
	QTest::qWait (350);
	CHECK (ms->text () != before);
	CHECK (s.rt ()->Warnings () == 0);
}

TEST_CASE ("Planetary Defense: time flow moves the diagram's epoch; reset brings it back")
{
	Skin s ("PlanetaryDefense");
	REQUIRE (s.ok);
	s.api.setCurrentScenario ("Delta-glider/Brighton Beach");
	Settle ();
	QLabel *info = s.view->findChild<QLabel*> ("diagramHeadInfo");
	REQUIRE (info);
	CHECK (info->text ().toStdString () == "EPOCH 14 MAR 2001 12:00 · SCENARIO");
	s.rt ()->Engine ()->evaluate ("w.diagram.flow = true");
	Settle ();
	QTest::qWait (400);
	CHECK (s.rt ()->Engine ()->evaluate ("w.diagram.offset").toNumber () > 1.0);
	CHECK (info->text ().startsWith ("+"));
	CHECK (s.view->findChild<QWidget*> ("flowReset")->isEnabled ());
	s.rt ()->Engine ()->evaluate ("w.diagram.flow = false; w.diagram.reset()");
	Settle ();
	QTest::qWait (300);
	CHECK (s.rt ()->Engine ()->evaluate ("w.diagram.offset").toNumber () == 0.0);
	CHECK (info->text ().toStdString () == "EPOCH 14 MAR 2001 12:00 · SCENARIO");
	CHECK (s.rt ()->Warnings () == 0);
}

TEST_CASE ("Planetary Defense: the classic pages' style sheet doesn't change the launcher")
{
	const QString hostile = "* { color: red; background: red; border: 5px solid red; border-radius: 9px; padding: 9px; margin: 9px; font-size: 30px; }";
	for (const char *page : PD_PAGES) {
		Skin plain ("PlanetaryDefense"), styled ("PlanetaryDefense", 1400, 860, hostile);
		REQUIRE (plain.ok);
		REQUIRE (styled.ok);
		for (Skin *k : {&plain, &styled}) {
			k->rt ()->Engine ()->evaluate ("Date.now = function () { return 1790000000000; }"); // one time for both clocks
			k->api.setCurrentScenario ("Delta-glider/Brighton Beach");
			k->api.setPage (page);
		}
		Settle ();
		const QImage a = plain.view->grab ().toImage (), b = styled.view->grab ().toImage ();
		const double d = Difference (a, b);
		INFO (page << ": " << d * 100 << " % of the pixels differ");
		if (d > 0.002) b.save (QString ("/tmp/forms_hostile_pd_%1.png").arg (page));
		CHECK (d <= 0.002);
	}
}

TEST_CASE ("Orbits: the C++ positions and texts agree with the QML skin's Orbits.js")
{
	App ();
	QFile f (SKINS + "/PlanetaryDefenseQml/qml/Orbits.js");
	REQUIRE (f.open (QIODevice::ReadOnly));
	QString src = QString::fromUtf8 (f.readAll ());
	src.replace (".pragma library", "");
	QJSEngine js;
	QJSValue r = js.evaluate (src);
	REQUIRE (!r.isError ());
	OrbitsApi api;
	auto near = [](double a, double b) { return std::fabs (a - b) <= 1e-9 * std::max (1.0, std::fabs (a)); };
	for (double mjd : {-21000.0, 40000.0, 51544.5, 55000.25, 58849.0, 61200.0, 62240.0, 64500.75, 69800.0}) {
		INFO ("mjd " << mjd);
		for (const QString &p : api.planetNames ()) {
			QJSValue a = js.evaluate (QString ("planet('%1', %2)").arg (p).arg (mjd, 0, 'g', 17));
			QVariantMap b = api.planet (p, mjd);
			CHECK (near (a.property ("x").toNumber (), b["x"].toDouble ()));
			CHECK (near (a.property ("y").toNumber (), b["y"].toDouble ()));
			CHECK (near (a.property ("z").toNumber (), b["z"].toDouble ()));
		}
		for (const QString &n : api.neoNames ()) {
			QJSValue a = js.evaluate (QString ("neo('%1', %2)").arg (n).arg (mjd, 0, 'g', 17));
			QVariant b = api.neo (n, mjd);
			CHECK (a.isNull () == !b.isValid ());
			if (!a.isNull () && b.isValid ()) {
				CHECK (near (a.property ("x").toNumber (), b.toMap ()["x"].toDouble ()));
				CHECK (near (a.property ("y").toNumber (), b.toMap ()["y"].toDouble ()));
			}
			QJSValue sa = js.evaluate (QString ("neoSet('%1', %2)").arg (n).arg (mjd, 0, 'g', 17));
			QVariantMap sb = api.neoSet (n, mjd);
			CHECK (sa.property ("inside").toBool () == sb["inside"].toBool ());
			CHECK (sa.property ("set").property ("epoch").toNumber () == sb["set"].toMap ()["epoch"].toDouble ());
		}
		CHECK (js.evaluate (QString ("epochText(%1)").arg (mjd, 0, 'g', 17)).toString () == api.epochText (mjd));
		CHECK (js.evaluate (QString ("dayText(%1)").arg (mjd, 0, 'g', 17)).toString () == api.dayText (mjd));
	}
	const double fly = js.evaluate ("APOPHIS_FLYBY_MS").toNumber ();
	for (double ms : {fly - 1e11, fly - 86399999.0, fly - 1.0, fly, fly + 1.0, fly + 3.7e9}) {
		QJSValue a = js.evaluate (QString ("countdown(%1)").arg (ms, 0, 'g', 17));
		QVariantMap b = api.countdown (ms);
		INFO ("ms " << ms);
		CHECK (a.property ("past").toBool () == b["past"].toBool ());
		for (const char *k : {"days", "hours", "minutes", "seconds", "ms"})
			CHECK (a.property (k).toNumber () == b[k].toDouble ());
	}
	CHECK (js.evaluate ("NEO_YEARS").toString () == api.neoYears ());
}

// a checking aid: FORMS_COMPARE=<skin dir>:<skin dir>:<pages,...> renders both skins' pages and counts the pixels that differ
TEST_CASE ("Forms: compare two skin folders page by page", "[.][compare]")
{
	const QStringList arg = qEnvironmentVariable ("FORMS_COMPARE").split (':');
	REQUIRE (arg.size () == 3);
	for (const QString &page : arg[2].split (',')) {
		Skin a (arg[0]), b (arg[1]);
		INFO (a.Log ().toStdString () << "\n" << b.Log ().toStdString ());
		REQUIRE (a.ok);
		REQUIRE (b.ok);
		for (Skin *k : {&a, &b}) {
			k->rt ()->Engine ()->evaluate ("Date.now = function () { return 1790000000000; }");
			k->api.rec = {"Delta-glider/DG-S/Smack!"};
			emit k->api.recentChanged ();
			k->api.setCurrentScenario ("Delta-glider/Brighton Beach");
			k->api.setPage (page);
		}
		Settle ();
		const QImage ia = a.view->grab ().toImage (), ib = b.view->grab ().toImage ();
		const double d = Difference (ia, ib);
		printf ("%-10s %.4f %% differ, warnings %d / %d\n", qPrintable (page), d * 100, a.rt ()->Warnings (), b.rt ()->Warnings ());
		if (d > 0) {
			ia.save ("/tmp/forms_compare_a_" + page + ".png");
			ib.save ("/tmp/forms_compare_b_" + page + ".png");
		}
		CHECK (b.rt ()->Warnings () == a.rt ()->Warnings ());
	}
}

// a debugging aid: FORMS_DUMP=<skin>[:<page>] prints every shown widget's place in the view
TEST_CASE ("Forms: dump a skin's widget geometry", "[.][dump]")
{
	const QStringList arg = qEnvironmentVariable ("FORMS_DUMP", "PlanetaryDefense").split (':');
	Skin s (arg[0]);
	REQUIRE (s.ok);
	s.api.setCurrentScenario ("(Current state)");
	s.api.rec = {"Delta-glider/DG-S/Smack!"};
	emit s.api.recentChanged ();
	if (arg.size () > 1) s.api.setPage (arg[1]);
	Settle ();
	for (QWidget *w : s.view->findChildren<QWidget*> ()) {
		if (!w->isVisible () || w->objectName ().isEmpty ()) continue;
		const QRect r (w->mapTo (s.view.get (), QPoint (0, 0)), w->size ());
		printf ("%-28s %5d %5d %5d %5d\n", qPrintable (w->objectName ()), r.x (), r.y (), r.width (), r.height ());
	}
}

#include "FormsSkins.Test.moc"
