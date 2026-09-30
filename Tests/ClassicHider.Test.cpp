// custom: launcher skins; ClassicHider on IDD_MAIN built offscreen from Orbiter.rc
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QWidget>
#include "ResDialog.h"
#include "resource.h"
#include "Custom/ClassicHider.h"

// stubs, as in ResDialog.Test: ResDialog.cpp logs through Log.cpp and finds module tables through Util.cpp
void *ModuleProc (void *, const char *) { return nullptr; }
void LogOut_Warning (const char*, const char*, int, const char*, ...) {}

static QApplication &App ()
{
	static int argc = 1;
	static char name[] = "ClassicHider.Test", *argv[] = {name, nullptr};
	if (qEnvironmentVariableIsEmpty ("QT_QPA_PLATFORM")) qputenv ("QT_QPA_PLATFORM", "offscreen");
	static QApplication app (argc, argv);
	return app;
}

static void Pump ()
{
	for (int i = 0; i < 5; i++) QCoreApplication::processEvents ();
}

TEST_CASE("ClassicHider hides IDD_MAIN's controls before the first show and restores them", "[launcher]")
{
	App ();
	QWidget *dlg = oapiCreateResDialog (nullptr, IDD_MAIN, nullptr);
	REQUIRE(dlg);
	QWidget *wait = oapiCreateResDialog (nullptr, IDD_PAGE_WAIT2, dlg);
	REQUIRE(wait);
	wait->hide ();
	QWidget *help = new QWidget (dlg, Qt::Window); // like a help window: a child window of the Launchpad
	help->setProperty ("resId", 12345);
	QWidget *launch = oapiResDlgItem (dlg, IDLAUNCH);
	QWidget *exitb = oapiResDlgItem (dlg, IDEXIT);
	REQUIRE(launch);
	REQUIRE(exitb);

	int shown = 0;
	for (QWidget *w : dlg->findChildren<QWidget*> (Qt::FindDirectChildrenOnly))
		if (!w->isWindow () && w != wait && !w->isHidden ()) shown++;

	auto *h = new custom::ClassicHider (dlg, wait);
	CHECK(h->Recorded () > 5);
	h->Hide (); // before the dialog's first show, as Attach runs
	CHECK(h->HiddenCount () == shown);
	dlg->show ();
	help->show ();
	Pump ();
	CHECK(launch->isHidden ());
	CHECK(!exitb->isVisible ());
	CHECK(help->isVisible ());

	launch->show (); // as ShowWaitPage(false) does
	Pump ();
	CHECK(launch->isHidden ());

	wait->show ();
	h->Restore ();
	CHECK(launch->isVisible ());
	CHECK(exitb->isVisible ());
	CHECK(wait->isVisible ());
	CHECK(help->isVisible ());
	int after = 0;
	for (QWidget *w : dlg->findChildren<QWidget*> (Qt::FindDirectChildrenOnly))
		if (!w->isWindow () && w != wait && !w->isHidden ()) after++;
	CHECK(after == shown);

	launch->hide (); // after Restore the hider leaves the controls alone
	launch->show ();
	Pump ();
	CHECK(launch->isVisible ());
	delete dlg;
}
