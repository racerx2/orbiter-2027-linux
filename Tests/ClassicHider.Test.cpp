// custom: launcher skins; ClassicHider and ResetKey on IDD_MAIN built offscreen from Orbiter.rc
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QDialog>
#include <QKeyEvent>
#include <QPushButton>
#include <QShortcut>
#include <QTest>
#include <QWidget>
#include "ResDialog.h"
#include "resource.h"
#include "Custom/ClassicHider.h"
#include "Custom/ResetKey.h"

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

TEST_CASE("IsResetKey matches Ctrl+Shift+L only", "[launcher]")
{
	using custom::IsResetKey;
	const Qt::KeyboardModifiers cs = Qt::ControlModifier | Qt::ShiftModifier;
	CHECK(IsResetKey (Qt::Key_L, cs));
	CHECK(IsResetKey (Qt::Key_L, cs | Qt::KeypadModifier));
	CHECK(!IsResetKey (Qt::Key_L, Qt::ControlModifier));
	CHECK(!IsResetKey (Qt::Key_L, Qt::ShiftModifier));
	CHECK(!IsResetKey (Qt::Key_L, cs | Qt::AltModifier));
	CHECK(!IsResetKey (Qt::Key_L, cs | Qt::MetaModifier));
	CHECK(!IsResetKey (Qt::Key_K, cs));
	CHECK(!IsResetKey (Qt::Key_L, Qt::NoModifier));
}

namespace {
	struct KeyCount: QObject { // counts the L keys that reach a widget
		int press = 0, release = 0;
		bool eventFilter (QObject *, QEvent *e) override {
			if (e->type () == QEvent::KeyPress && static_cast<QKeyEvent*> (e)->key () == Qt::Key_L) press++;
			if (e->type () == QEvent::KeyRelease && static_cast<QKeyEvent*> (e)->key () == Qt::Key_L) release++;
			return false;
		}
	};
}

TEST_CASE("ResetKey takes Ctrl+Shift+L in the Launchpad window before shortcuts and widgets", "[launcher]")
{
	App ();
	QWidget *dlg = oapiCreateResDialog (nullptr, IDD_MAIN, nullptr);
	REQUIRE(dlg);
	auto *launch = qobject_cast<QPushButton*> (oapiResDlgItem (dlg, IDLAUNCH));
	REQUIRE(launch);
	dlg->show ();
	dlg->activateWindow ();
	Pump ();
	launch->setFocus ();
	Pump ();
	int shortcuts = 0, resets = 0;
	auto *sc = new QShortcut (QKeySequence ("Ctrl+Shift+L"), dlg);
	QObject::connect (sc, &QShortcut::activated, [&]() { shortcuts++; });
	KeyCount seen;
	launch->installEventFilter (&seen);
	const Qt::KeyboardModifiers cs = Qt::ControlModifier | Qt::ShiftModifier;

	auto *rk = new custom::ResetKey (dlg, [&]() { resets++; });
	QTest::keyClick (launch, Qt::Key_L, cs); // not installed: the classic Launchpad's own handling
	CHECK(shortcuts == 1);
	CHECK(resets == 0);

	rk->Install ();
	rk->Install (); // idempotent
	CHECK(rk->Installed ());
	seen.press = seen.release = 0;
	QTest::keyClick (launch, Qt::Key_L, cs);
	CHECK(shortcuts == 1);
	CHECK(resets == 1);
	CHECK(seen.press == 0);
	CHECK(seen.release == 0); // the release after a reset is taken too

	QTest::keyClick (launch, Qt::Key_L, Qt::ControlModifier);
	QTest::keyClick (launch, Qt::Key_L, cs | Qt::AltModifier);
	CHECK(resets == 1);
	CHECK(seen.press == 2);
	CHECK(seen.release == 2);

	QKeyEvent rep (QEvent::KeyPress, Qt::Key_L, cs, QString (), true);
	QApplication::sendEvent (launch, &rep); // auto-repeat: taken, no second reset
	CHECK(resets == 1);
	CHECK(seen.press == 2);

	auto *child = new QDialog (dlg); // another window, such as the skin picker or an add-on dialog
	auto *ok = new QPushButton ("ok", child);
	KeyCount seenOk;
	ok->installEventFilter (&seenOk);
	child->show ();
	Pump ();
	QTest::keyClick (ok, Qt::Key_L, cs);
	CHECK(resets == 1);
	CHECK(seenOk.press == 1); // left alone, not swallowed
	CHECK(seenOk.release == 1);
	ok->removeEventFilter (&seenOk);
	child->hide ();

	rk->Remove ();
	CHECK(!rk->Installed ());
	QTest::keyClick (launch, Qt::Key_L, cs);
	CHECK(resets == 1);

	rk->Install ();
	delete rk; // removes itself
	QTest::keyClick (launch, Qt::Key_L, cs);
	CHECK(resets == 1);
	delete dlg;
}
