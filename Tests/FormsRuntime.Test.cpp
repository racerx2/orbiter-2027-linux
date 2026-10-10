// custom: forms skins; the .ui reader's layouts, the form builder and its JavaScript runtime, offscreen

#include <catch2/catch_test_macros.hpp>
#include "FormBuild.h"
#include "FormPainted.h"
#include "FormRuntime.h"
#include "FormWidgets.h"
#include "FormsView.h"
#include "Orbits.h"
#include "UiForm.h"
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>
#include <memory>

using namespace forms;

namespace {

	QApplication &App ()
	{
		static int argc = 1;
		static char arg0[] = "FormsRuntime.Test";
		static char *argv[] = {arg0, nullptr};
		static QApplication app (argc, argv);
		return app;
	}

	void Settle (int ms = 0)
	{
		QApplication::processEvents ();
		if (ms > 0) QTest::qWait (ms);
		for (int i = 0; i < 3; i++) QApplication::processEvents ();
	}

	const QString BASIC = QStringLiteral (FORMS_FIXTURES "/Basic");

	// copies a fixture skin so a test can change it
	QString CopySkin (const QString &from, QTemporaryDir &tmp)
	{
		const QString to = tmp.path () + "/skin";
		QDir ().mkpath (to + "/forms/images");
		for (const QString &f : {"forms/Main.ui", "forms/Logic.js", "forms/skin.qrc", "forms/images/dot.png", "forms/images/preview.png"})
			QFile::copy (from + "/" + f, to + "/" + f);
		return to;
	}

	void Replace (const QString &file, const QString &a, const QString &b)
	{
		QFile f (file);
		REQUIRE (f.open (QIODevice::ReadOnly));
		QString s = QString::fromUtf8 (f.readAll ());
		f.close ();
		REQUIRE (s.contains (a));
		s.replace (a, b);
		REQUIRE (f.open (QIODevice::WriteOnly | QIODevice::Truncate));
		f.write (s.toUtf8 ());
	}

}

// the Launcher object's names, as LauncherApi has them
class FakeLauncher: public QObject {
	Q_OBJECT
	Q_PROPERTY(QString currentScenario READ currentScenario WRITE setCurrentScenario NOTIFY currentScenarioChanged)
	Q_PROPERTY(bool canLaunch READ canLaunch NOTIFY canLaunchChanged)
	Q_PROPERTY(bool startPaused READ startPaused WRITE setStartPaused NOTIFY startPausedChanged)
	Q_PROPERTY(QString page READ page WRITE setPage NOTIFY pageChanged)
	Q_PROPERTY(bool active READ active NOTIFY activeChanged)
	Q_PROPERTY(QVariantList scenarios READ scenarios NOTIFY scenariosChanged)
public:
	QString cur, pg;
	bool can = true, paused = false, act = true;
	QVariantList scn;
	QStringList launched;
	QString currentScenario () const { return cur; }
	void setCurrentScenario (const QString &p) { cur = p; emit currentScenarioChanged (); }
	bool canLaunch () const { return can; }
	bool startPaused () const { return paused; }
	void setStartPaused (bool on) { paused = on; emit startPausedChanged (); }
	QString page () const { return pg; }
	void setPage (const QString &p) { pg = p; emit pageChanged (); }
	bool active () const { return act; }
	void SetActive (bool on) { act = on; emit activeChanged (); }
	QVariantList scenarios () const { return scn; }
	void SetScenarios (int n)
	{
		scn.clear ();
		for (int i = 0; i < n; i++) scn << QVariantMap {{"path", QString ("S%1").arg (i)}, {"name", QString ("Scn %1").arg (i)}, {"isFolder", false}};
		emit scenariosChanged ();
	}
	Q_INVOKABLE QVariantMap scenarioInfo (const QString &p) { return p.isEmpty () ? QVariantMap () : QVariantMap {{"name", "Name of " + p}}; }
	Q_INVOKABLE bool launch (const QString &p) { launched << p; return true; }
signals:
	void currentScenarioChanged ();
	void canLaunchChanged ();
	void startPausedChanged ();
	void pageChanged ();
	void activeChanged ();
	void scenariosChanged ();
};

namespace {

	struct Fix {
		QWidget host;
		FakeLauncher fake;
		std::unique_ptr<FormsView> view;
		QStringList logs;
		bool ok = false;
		QString err;
		explicit Fix (const QString &skin = BASIC, int w = 800, int h = 600)
		{
			App ();
			host.resize (w, h);
			view = std::make_unique<FormsView> (&host, &fake, skin, "forms/Main.ui", [this](const QString &s) { logs << s; });
			view->waitForLoopLevel1 = false;
			view->setGeometry (host.rect ());
			ok = view->Load (err);
			host.show ();
			view->show ();
			Settle ();
		}
		FormRuntime *rt () { return view->Runtime (); }
		template <class T = QWidget> T *find (const char *name) { return view->findChild<T*> (name); }
		QJSValue js (const QString &s) { return rt ()->Engine ()->evaluate (s); }
	};

}

TEST_CASE ("UiForm reads layouts, spacers, stretch and Designer's value types")
{
	App ();
	custom::UiForm f;
	QString err;
	custom::UiLimits lim;
	lim.maxBytes = 4 << 20;
	REQUIRE (custom::ReadUiFile (BASIC + "/forms/Main.ui", f, err, lim));
	CHECK (f.layout);
	CHECK (f.cls == "launcher");
	CHECK (f.resources == QStringList {"skin.qrc"});
	REQUIRE (f.Custom ("Starfield"));
	CHECK (f.Custom ("Starfield")->extends == "QLabel");
	const custom::UiValue *scripts = f.root.Dyn ("scripts");
	REQUIRE (scripts);
	CHECK (scripts->type == custom::UiValue::STRINGLIST);
	CHECK (scripts->list == QStringList {"Logic.js"});
	const custom::UiWidget *chrome = nullptr;
	for (const auto &c : f.root.children) if (c.name == "chrome") chrome = &c;
	REQUIRE (chrome);
	REQUIRE (chrome->layout.size () == 1);
	const custom::UiLayout &l = chrome->layout[0];
	CHECK (l.cls == "QVBoxLayout");
	CHECK (l.stretch == "0,0,0,0,0,1,0");
	REQUIRE (l.items.size () == 7);
	CHECK (l.items[1].kind == custom::UiLayoutItem::LAYOUT);
	REQUIRE (l.items[1].sub.size () == 1);
	CHECK (l.items[1].sub[0].items[2].kind == custom::UiLayoutItem::SPACER);
	CHECK (l.items[0].kind == custom::UiLayoutItem::WIDGET);
	CHECK (chrome->children[l.items[0].widget].name == "title");
	const custom::UiWidget *go = nullptr;
	for (const auto &c : chrome->children) if (c.name == "go") go = &c;
	REQUIRE (go);
	REQUIRE (go->Dyn ("glowColor"));
	CHECK (go->Dyn ("glowColor")->type == custom::UiValue::COLOR);
	CHECK (go->Dyn ("glowColor")->rgba == 0xfff5a524u);
	REQUIRE (go->Prop ("cursor"));
	CHECK (go->Prop ("cursor")->str == "PointingHandCursor");
	const custom::UiWidget &stars = f.root.children[0];
	REQUIRE (stars.Prop ("pixmap"));
	CHECK (stars.Prop ("pixmap")->res == "skin.qrc");
	CHECK (stars.Prop ("pixmap")->str == ":/t/images/preview.png");
}

TEST_CASE ("Forms: the fixture loads without warnings")
{
	Fix f;
	INFO (f.err.toStdString () + " | " + f.logs.join (" | ").toStdString ());
	REQUIRE (f.ok);
	CHECK (f.rt ()->Warnings () == 0);
	CHECK (f.find<QLabel> ("title"));
	CHECK (f.find ("stars")->metaObject ()->className () == QByteArray ("forms::Starfield"));
	CHECK (f.find ("stars")->property ("count").toInt () == 50);
	CHECK (f.find<QLabel> ("title")->font ().letterSpacing () == 2.5);
	CHECK (f.find ("launcher")->styleSheet ().contains (QDir (BASIC).canonicalPath () + "/forms/images/dot.png"));
	CHECK (!f.find ("launcher")->styleSheet ().contains (":/t/"));
}

TEST_CASE ("Forms: bindings follow the Launcher")
{
	Fix f;
	REQUIRE (f.ok);
	CHECK (f.find<QLabel> ("title")->text () == "None");
	f.fake.setCurrentScenario ("Foo");
	Settle ();
	CHECK (f.find<QLabel> ("title")->text () == "Name of Foo");
	CHECK (f.find<QPushButton> ("go")->isEnabled ());
	f.fake.can = false;
	emit f.fake.canLaunchChanged ();
	Settle ();
	CHECK (!f.find<QPushButton> ("go")->isEnabled ());
}

TEST_CASE ("Forms: actions call the Launcher; checkable widgets show the bound state")
{
	Fix f;
	REQUIRE (f.ok);
	f.fake.setCurrentScenario ("Bar");
	Settle ();
	QTest::mouseClick (f.find<QPushButton> ("go"), Qt::LeftButton);
	Settle ();
	CHECK (f.fake.launched == QStringList {"Bar"});
	auto *box = f.find<QCheckBox> ("paused");
	CHECK (!box->isChecked ());
	QTest::mouseClick (box, Qt::LeftButton, {}, QPoint (5, box->height () / 2));
	CHECK (!box->isChecked ()); // no own toggle: the action and the binding decide
	CHECK (f.fake.paused);
	Settle ();
	CHECK (box->isChecked ());
	CHECK (box->focusPolicy () == Qt::NoFocus);
	CHECK (!f.find<QPushButton> ("go")->autoDefault ());
}

TEST_CASE ("Forms: a model line edit writes its expression; Escape clears it")
{
	Fix f;
	REQUIRE (f.ok);
	f.fake.setPage ("A");
	Settle ();
	auto *e = f.find<QLineEdit> ("search");
	e->setFocus ();
	QTest::keyClicks (e, "abc");
	Settle ();
	CHECK (f.js ("ui.query").toString () == "abc");
	CHECK (f.find<QLabel> ("aLabel")->text () == "query: abc");
	QTest::keyClick (e, Qt::Key_Escape);
	Settle ();
	CHECK (f.js ("ui.query").toString () == "");
	CHECK (e->text ().isEmpty ());
	f.js ("ui.query = 'set'");
	f.rt ()->Refresh ();
	CHECK (e->text () == "set");
}

TEST_CASE ("Forms: stacked pages by name; hidden pages are not refreshed")
{
	Fix f;
	REQUIRE (f.ok);
	auto *st = f.find<QStackedWidget> ("pages");
	CHECK (st->currentWidget ()->objectName () == "pageA"); // update() gives "A" for an empty page
	f.fake.setPage ("B");
	Settle ();
	CHECK (st->currentWidget ()->objectName () == "pageB");
	const QString before = f.find<QLabel> ("aLabel")->text ();
	f.js ("ui.query = 'changed'");
	f.rt ()->Refresh ();
	CHECK (f.find<QLabel> ("aLabel")->text () == before);
	f.fake.setPage ("A");
	Settle ();
	CHECK (f.find<QLabel> ("aLabel")->text () == "query: changed");
}

TEST_CASE ("Forms: lists clone their template, reuse clones, fit hides what doesn't fit")
{
	Fix f;
	REQUIRE (f.ok);
	f.fake.SetScenarios (3);
	Settle ();
	auto titles = [&f]() {
		QStringList t;
		for (QLabel *l : f.find ("cards")->findChildren<QLabel*> ("cardTitle")) t << l->text ();
		std::sort (t.begin (), t.end ());
		return t;
	};
	CHECK (titles () == QStringList ({"Scn 0 #0", "Scn 1 #1", "Scn 2 #2"}));
	QList<QFrame*> cards = f.find ("cards")->findChildren<QFrame*> ("card");
	REQUIRE (cards.size () == 3);
	f.fake.SetScenarios (3);
	Settle ();
	CHECK (f.find ("cards")->findChildren<QFrame*> ("card") == cards);
	f.fake.SetScenarios (2);
	Settle ();
	CHECK (f.find ("cards")->findChildren<QFrame*> ("card").size () == 2);
	f.fake.SetScenarios (12);
	Settle ();
	int shown = 0;
	for (QFrame *c : f.find ("cards")->findChildren<QFrame*> ("card")) shown += c->isVisible ();
	CHECK (shown == 7); // (780 + 10) / (100 + 10)
	QFrame *first = nullptr;
	for (QFrame *c : f.find ("cards")->findChildren<QFrame*> ("card"))
		if (c->findChild<QLabel*> ("cardTitle")->text () == "Scn 0 #0") first = c;
	REQUIRE (first);
	CHECK (first->findChild<QLabel*> ("cardTitle")->geometry () == QRect (8, 50, 84, 20));
	auto *e = f.find<QLineEdit> ("search");
	e->setFocus ();
	Settle ();
	QTest::mouseClick (first, Qt::LeftButton, {}, QPoint (10, 10));
	Settle ();
	CHECK (f.fake.cur == "S0");
	CHECK (QApplication::focusWidget () == e); // a click on a card leaves the search box focused
	QTest::mouseDClick (first, Qt::LeftButton, {}, QPoint (10, 10));
	Settle ();
	CHECK (f.fake.launched.contains ("S0"));
}

TEST_CASE ("Forms: a virtual grid makes only the rows in view")
{
	Fix f;
	REQUIRE (f.ok);
	f.js ("ui.many = 500");
	f.rt ()->Refresh ();
	Settle ();
	auto *body = f.find ("gridBody");
	auto *area = f.find<QScrollArea> ("grid");
	const int made = body->findChildren<QFrame*> ("cell").size ();
	CHECK (made > 0);
	CHECK (made < 100);
	CHECK (body->minimumHeight () == ((500 + 5) / 6) * 100);
	area->verticalScrollBar ()->setValue (area->verticalScrollBar ()->maximum ());
	Settle ();
	bool last = false;
	for (QLabel *l : body->findChildren<QLabel*> ("cellText"))
		if (l->isVisibleTo (body) && l->text () == "cell 499") last = true;
	CHECK (last);
	CHECK (body->findChildren<QFrame*> ("cell").size () < 100);
}

TEST_CASE ("Forms: anchors follow the parent; the toast is centred")
{
	Fix f;
	REQUIRE (f.ok);
	f.host.resize (1000, 700);
	f.view->setGeometry (f.host.rect ());
	Settle ();
	CHECK (f.find ("stars")->geometry () == QRect (0, 0, 1000, 700));
	CHECK (f.find ("chrome")->geometry () == QRect (0, 0, 1000, 700));
	f.rt ()->Toast ("hello there");
	Settle ();
	QWidget *t = f.find ("toast");
	CHECK (t->isVisible ());
	CHECK (std::abs (t->geometry ().center ().x () - 500) <= 1);
	CHECK (700 - t->geometry ().bottom () - 1 == 70);
	CHECK (t->testAttribute (Qt::WA_TransparentForMouseEvents));
}

TEST_CASE ("Forms: keys go to the nearest widget with key_ properties")
{
	Fix f;
	REQUIRE (f.ok);
	f.fake.setPage ("A");
	Settle ();
	f.view->setFocus ();
	QTest::keyClick (f.view.get (), Qt::Key_Return);
	Settle ();
	CHECK (f.js ("ui.pressed").toInt () == 1);
	auto *e = f.find<QLineEdit> ("search");
	e->setFocus ();
	QTest::keyClick (e, Qt::Key_Return); // the line edit lets Return through
	Settle ();
	CHECK (f.js ("ui.pressed").toInt () == 2);
	f.view->setFocus ();
	QTest::keyClick (f.view.get (), Qt::Key_Tab);
	CHECK (QApplication::focusWidget () == f.view.get ());
}

TEST_CASE ("Forms: glow rings are painted around the widget, also outside its parent row")
{
	Fix f;
	REQUIRE (f.ok);
	auto *go = f.find<QPushButton> ("go");
	QWidget *root = f.find ("launcher");
	const QPoint p = go->mapTo (root, QPoint (-4, go->height () / 2));
	const QImage on = root->grab ().toImage ();
	f.js ("void 0");
	go->setProperty ("glowAlpha", 0.0);
	root->update ();
	Settle ();
	const QImage off = root->grab ().toImage ();
	CHECK (on.pixel (p) != off.pixel (p));
}

TEST_CASE ("Forms: tick bindings run while the Launchpad is active")
{
	Fix f;
	REQUIRE (f.ok);
	auto *t = f.find<QLabel> ("ticker");
	const QString a = t->text ();
	Settle (350);
	const QString b = t->text ();
	CHECK (a != b);
	f.fake.SetActive (false);
	Settle ();
	const QString c = t->text ();
	Settle (350);
	CHECK (t->text () == c);
	CHECK (!f.find ("stars")->property ("active").toBool ());
}

TEST_CASE ("Forms: script errors fail the load; binding errors are warnings")
{
	App ();
	{
		QTemporaryDir tmp;
		const QString skin = CopySkin (BASIC, tmp);
		Replace (skin + "/forms/Logic.js", "return { cards: cards, many: many };", "return { cards: cards, many: many ;");
		Fix f (skin);
		CHECK (!f.ok);
		CHECK (f.err.contains ("Logic.js line"));
	}
	{
		QTemporaryDir tmp;
		const QString skin = CopySkin (BASIC, tmp);
		Replace (skin + "/forms/Main.ui", "path ? (info.name || path) : 'None'", "nosuch.name");
		Fix f (skin);
		REQUIRE (f.ok);
		CHECK (f.rt ()->Warnings () == 1);
		CHECK (f.logs.join ("\n").contains ("title bind"));
		f.fake.setCurrentScenario ("X");
		Settle ();
		CHECK (f.rt ()->Warnings () == 1); // logged once until it works again
	}
	{
		QTemporaryDir tmp;
		const QString skin = CopySkin (BASIC, tmp);
		Replace (skin + "/forms/Main.ui", "class=\"QLabel\" name=\"bLabel\"", "class=\"QCalendarWidget\" name=\"bLabel\"");
		Fix f (skin);
		CHECK (!f.ok);
		CHECK (f.err.contains ("QCalendarWidget"));
	}
}

TEST_CASE ("Forms: scripts given as a string are split at ; and , only")
{
	App ();
	const QString list = "<stringlist>\n    <string>Logic.js</string>\n   </stringlist>";
	for (const QString &value : {QString ("My Logic.js"), QString (" My Logic.js ; "), QString ("My Logic.js,")}) {
		QTemporaryDir tmp;
		const QString skin = CopySkin (BASIC, tmp);
		REQUIRE (QFile::rename (skin + "/forms/Logic.js", skin + "/forms/My Logic.js"));
		Replace (skin + "/forms/Main.ui", list, "<string>" + value + "</string>"); // a dynamic property Designer adds with + is a string
		Fix f (skin);
		INFO (value.toStdString () + ": " + f.err.toStdString ());
		CHECK (f.ok);
		CHECK ((f.rt () && f.rt ()->Warnings () == 0));
	}
}

TEST_CASE ("Forms: a binding that runs forever is stopped")
{
	App ();
	QTemporaryDir tmp;
	const QString skin = CopySkin (BASIC, tmp);
	Replace (skin + "/forms/Main.ui", "'query: ' + (ui.query || '')", "(function () { for (;;) {} }) ()");
	Fix f (skin);
	REQUIRE (f.ok);
	CHECK (f.logs.join ("\n").contains ("stopped after running 2 s"));
	f.fake.setCurrentScenario ("Y");
	Settle ();
	CHECK (f.find<QLabel> ("title")->text () == "Name of Y");
}

TEST_CASE ("Forms: a reload replaces the form; a broken file keeps the old one")
{
	App ();
	QTemporaryDir tmp;
	const QString skin = CopySkin (BASIC, tmp);
	Fix f (skin);
	REQUIRE (f.ok);
	Replace (skin + "/forms/Main.ui", "<string>Page B</string>", "<string>Page B2</string>");
	QString err;
	REQUIRE (f.view->ReloadNow (err));
	CHECK (f.find<QLabel> ("bLabel")->text () == "Page B2");
	Replace (skin + "/forms/Main.ui", "</ui>", "");
	FormRuntime *before = f.rt ();
	CHECK (!f.view->ReloadNow (err));
	CHECK (f.rt () == before);
	CHECK (f.find<QLabel> ("bLabel")->text () == "Page B2");
}

TEST_CASE ("Forms: a wrapped label breaks at its line breaks; a fitted scroll area follows its content")
{
	App ();
	FormLabel l;
	l.setWordWrap (true);
	l.setMaxLines (4);
	l.setText ("one");
	const int one = l.heightForWidth (300);
	l.setText ("one\ntwo");
	CHECK (l.heightForWidth (300) >= 2 * one - 2);
	l.setText ("one\ntwo\nthree\nfour\nfive");
	CHECK (l.heightForWidth (300) <= 4 * one + 2);
	QWidget host;
	auto *lay = new QVBoxLayout (&host);
	auto *area = new FormScrollArea;
	area->setProperty ("fitContent", true);
	area->setWidgetResizable (true);
	area->setSizePolicy (QSizePolicy::Preferred, QSizePolicy::Maximum);
	auto *body = new QWidget;
	auto *bl = new QVBoxLayout (body);
	auto *first = new QLabel ("first");
	bl->addWidget (first);
	area->setWidget (body);
	lay->addWidget (area);
	lay->addStretch ();
	host.resize (300, 600);
	host.show ();
	Settle ();
	const int before = area->height ();
	for (int i = 0; i < 6; i++) bl->addWidget (new QLabel ("more"));
	Settle ();
	CHECK (area->height () > before + 5 * first->height ());
}

TEST_CASE ("Orbits: the Apophis clock and the date texts")
{
	using namespace forms::orbits;
	const double fly = FlybyMs ();
	Countdown c = CountdownAt (fly - (2 * 86400000.0 + 3 * 3600000.0 + 4 * 60000.0 + 5000.0 + 678));
	CHECK (!c.past);
	CHECK (c.days == 2);
	CHECK (c.hours == 3);
	CHECK (c.minutes == 4);
	CHECK (c.seconds == 5);
	CHECK (c.ms == 678);
	CHECK (CountdownAt (fly + 1000).past);
	CHECK (EpochText (51544.5) == "1 JAN 2000 12:00");
	CHECK (DayText (51544.5) == "2000-01-01");
	CHECK (Pad ("7", 3) == "007");
	CHECK (Pad ("1234", 3) == "1234");
	SetAt s = NeoSetAt ("Apophis", 61200.0);
	REQUIRE (s.set);
	CHECK (s.inside);
	CHECK (std::fabs (s.set->a - 0.9223592206975018) < 1e-15);
	CHECK (!NeoSetAt ("Bennu", 40000).inside);
	CHECK (!NeoAt ("Bennu", 40000));
}

namespace {

	// a rich text document that notes every file Qt's parser and layout ask for
	class Asked: public QTextDocument {
	public:
		QStringList names;
	protected:
		QVariant loadResource (int type, const QUrl &name) override
		{
			names << name.toString ();
			return QTextDocument::loadResource (type, name);
		}
	};

	struct Sandbox {
		QTemporaryDir tmp;
		QString skin, outside;
		BuildEnv env;
		QStringList warnings;
		Sandbox ()
		{
			const QString root = QFileInfo (tmp.path ()).canonicalFilePath ();
			skin = root + "/skin";
			outside = root + "/outside.png";
			QDir ().mkpath (skin + "/forms");
			for (const QString &f : {skin + "/forms/ok.png", outside}) {
				QImage img (4, 4, QImage::Format_ARGB32);
				img.fill (Qt::red);
				img.save (f);
			}
			env.skinDir = skin;
			env.formDir = skin + "/forms";
			env.warn = [this](const QString &s) { warnings << s; };
		}
		// the files a label's rich text would read, after SafeText
		QStringList Reads (const QString &html, bool safe = true)
		{
			Asked d;
			d.setHtml (safe ? SafeText (env, html) : html);
			d.setTextWidth (400);
			d.documentLayout ()->documentSize ();
			QStringList out;
			for (const QString &n : d.names) {
				const QString p = QUrl (n).isLocalFile () ? QUrl (n).toLocalFile () : n;
				if (!p.isEmpty ()) out << p;
			}
			return out;
		}
	};

}

TEST_CASE ("Forms: rich text reads pictures and style sheets from the skin only")
{
	App ();
	Sandbox s;
	const QString o = s.outside;
	const QStringList attacks = {
		"<img src=\"" + o + "\">",
		"<img src=" + o + ">",
		"<img SRC='" + o + "'>",
		"<img s&#114;c=\"" + o + "\">",
		"<img src=\"" + QString (o).replace ("/", "&#47;") + "\">",
		"<img source=\"" + o + "\">",
		"<img src>",
		"<img src=\"${SKIN}/../outside.png\">",
		"<img src=\"file://" + o + "\">",
		"<a/title=\"><img src=" + o + ">\">x</a>",
		"<!-- <a title=\" --> <img src=\"" + o + "\"> \"> -->",
		"<table background=\"" + o + "\"><tr><td background=" + o + ">x</td></tr></table>",
		"<p style=\"background-image: url(" + o + ")\">x</p>",
		"<p style=\"background-image: u\\rl(" + o + ")\">x</p>",
		"<p style=\"background-image: &#117;rl(" + o + ")\">x</p>",
		"<p style='background-image: URL(\"" + o + "\")'>x</p>",
		"<style>p { background-image: url(" + o + ") }</style><p>x</p>",
		"<style>@import \"" + o + "\";</style><p>x</p>",
		"<style>a { }</b>@import \"" + o + "\";</style><p>x</p>",
		"<style>p { background-image: u<!-- -->rl(" + o + ") }</style><p>x</p>",
		"<style>p { background-image: url</x>(" + o + ") }</style><p>x</p>",
		"<style>p { background-image: \\75 rl(" + o + ") }</style><p>x</p>",
		"<link rel=\"stylesheet\" type=\"text/css\" href=\"" + o + "\"><p>x</p>",
		"<l&#105;nk type=\"text/css\" href=\"" + o + "\"><p>x</p>",
	};
	int real = 0; // attacks that read the file without SafeText
	for (const QString &a : attacks) {
		INFO (a.toStdString () << "  ->  " << SafeText (s.env, a).toStdString ());
		for (const QString &p : s.Reads (a)) CHECK (p.startsWith (s.skin + "/"));
		real += s.Reads (a, false).contains (o);
	}
	CHECK (real >= 17); // 19 with Qt 6.10, escapes, entities and split names in styles among them
	const QStringList ok = s.Reads ("<p style=\"background-image: url(ok.png)\">a <img src=\"ok.png\"> b <img src='${SKIN}/forms/ok.png'></p>");
	CHECK (ok.contains (s.skin + "/forms/ok.png"));
	for (const QString &p : ok) CHECK (p == s.skin + "/forms/ok.png");
	CHECK (SafeText (s.env, "plain text, no tags") == "plain text, no tags");
	CHECK (SafeText (s.env, "<b>bold</b> &amp; <i>more</i>") == "<b>bold</b> &amp; <i>more</i>");
	QTextDocument qt6;
	qt6.setHtml ("<p>x</p><ul><li>y</li></ul>");
	const QString designer = qt6.toHtml (); // what Designer writes for rich text: a style element with \2610 markers
	INFO (designer.toStdString ());
	CHECK (designer.contains ("<style"));
	s.warnings.clear ();
	s.env.warned.clear ();
	CHECK (SafeText (s.env, designer) == designer);
	CHECK (s.warnings.isEmpty ());
}

TEST_CASE ("Forms: style sheets read pictures from the skin only and set no unchecked properties")
{
	App ();
	Sandbox s;
	const QString o = s.outside;
	auto urls = [&](const QString &qss) {
		QStringList out;
		static const QRegularExpression re (R"re(url\("([^"]*)"\))re");
		for (auto it = re.globalMatch (RewriteStyle (s.env, qss)); it.hasNext ();) out << it.next ().captured (1);
		return out;
	};
	CHECK (urls ("QWidget { background-image: url(" + o + "); }") == QStringList {""});
	CHECK (urls ("QWidget { image: URL( '" + o + "' ); }") == QStringList {""});
	CHECK (urls ("QWidget { image: u\\rl(" + o + "); }") == QStringList {""});
	CHECK (urls ("QWidget { image: url(\"a)b\"); }") == QStringList {""});
	CHECK (urls ("QWidget { image: url(${SKIN}/../outside.png); }") == QStringList {""});
	CHECK (urls ("QWidget { image: url(ok.png); border-image: url(\"${SKIN}/forms/ok.png\"); }") == QStringList {s.skin + "/forms/ok.png", s.skin + "/forms/ok.png"});
	const QString q = RewriteStyle (s.env, "QLabel { qproperty-text: \"<img src=x>\"; QPROPERTY-toolTip: \"t\"; qproperty-textAlignment: AlignLeft; qproperty-openExternalLinks: true; }");
	CHECK (!q.contains ("qproperty-text:"));
	CHECK (!q.contains ("QPROPERTY-toolTip"));
	CHECK (!q.contains ("qproperty-openExternalLinks"));
	CHECK (q.contains ("qproperty-textAlignment"));

	QLabel l;
	l.setObjectName ("l");
	CHECK (SafeValue (s.env, &l, "openExternalLinks", true).toBool () == false);
	CHECK (SafeValue (s.env, &l, "textFormat", int (Qt::MarkdownText)).toInt () == Qt::PlainText);
	CHECK (SafeValue (s.env, &l, "textFormat", "Qt::MarkdownText").toInt () == Qt::PlainText);
	CHECK (SafeValue (s.env, &l, "textFormat", " 3 ").toInt () == Qt::PlainText);
	CHECK (SafeValue (s.env, &l, "textFormat", "anything").toInt () == Qt::PlainText);
	CHECK (SafeValue (s.env, &l, "textFormat", "Qt::RichText").toInt () == Qt::RichText);
	CHECK (SafeValue (s.env, &l, "textFormat", 2).toInt () == Qt::AutoText);
	CHECK (SafeValue (s.env, &l, "text", "<img src=\"" + o + "\">").toString () == "<img src=\"\">");
	CHECK (SafeValue (s.env, &l, "toolTip", "<img src=\"" + o + "\">").toString () == "<img src=\"\">");
	QPushButton b;
	CHECK (SafeValue (s.env, &b, "text", "<img src=x>").toString () == "<img src=x>"); // buttons show plain text
}

TEST_CASE ("Forms: a forms skin's Qss file gets the checks of the form's style sheets")
{
	App ();
	Sandbox s;
	const QString o = s.outside;
	REQUIRE (QFile::copy (s.skin + "/forms/ok.png", s.skin + "/top.png"));
	const QString qss = "QLabel { qproperty-openExternalLinks: true; qproperty-text: \"<img src='" + o + "'>\"; qproperty-alignment: AlignRight; }\n"
		"QWidget#a { background-image: url(" + o + "); }\nQWidget#b { image: url(top.png); border-image: url(\"${SKIN}/forms/ok.png\"); }";
	QStringList warned;
	const QString f = custom::SkinStyleSheet (s.skin + "/forms/..", qss, true, [&warned](const QString &w) { warned << w; });
	INFO (f.toStdString ());
	CHECK (!f.contains ("qproperty-openExternalLinks"));
	CHECK (!f.contains ("qproperty-text"));
	CHECK (f.contains ("qproperty-alignment"));
	CHECK (f.contains ("QWidget#a { background-image: url(\"\"); }"));
	CHECK (f.contains ("url(\"" + s.skin + "/top.png\")")); // relative to the skin folder
	CHECK (f.contains ("url(\"" + s.skin + "/forms/ok.png\")"));
	CHECK (warned.size () == 1);
	QLabel l ("stock"), raw ("stock");
	l.setStyleSheet (f);
	l.ensurePolished ();
	CHECK (!l.openExternalLinks ());
	CHECK (l.text () == "stock");
	CHECK (l.alignment () & Qt::AlignRight);
	raw.setStyleSheet (qss); // what the Launchpad dialog got before
	raw.ensurePolished ();
	CHECK (raw.openExternalLinks ());

	const QString plain = custom::SkinStyleSheet (s.skin, "QWidget { image: url(\"${SKIN}/top.png\"); }", false, nullptr); // a style-sheet skin: as written
	CHECK (plain == "QWidget { image: url(\"" + s.skin + "/top.png\"); }");
}

TEST_CASE ("Forms: a relative url() in a forms skin's Qss file is looked up from Orbiter's folder before the skin folder")
{
	App ();
	Sandbox s;
	const QString root = QFileInfo (s.skin).path ();
	REQUIRE (QFile::copy (s.skin + "/forms/ok.png", s.skin + "/top.png"));
	REQUIRE (QFile::copy (s.skin + "/forms/ok.png", root + "/top.png"));
	const QString cwd = QDir::currentPath ();
	REQUIRE (QDir::setCurrent (root)); // Orbiter runs in its folder
	QStringList warned;
	const QString f = custom::SkinStyleSheet (s.skin, "#a { image: url(skin/forms/ok.png); }\n#b { image: url(\"top.png\"); }\n#c { image: url(outside.png); }\n#d { image: url(forms/ok.png); }",
		true, [&warned](const QString &w) { warned << w; });
	QDir::setCurrent (cwd);
	INFO (f.toStdString ());
	CHECK (f.contains ("#a { image: url(\"" + s.skin + "/forms/ok.png\"); }")); // as Qt resolved it before
	CHECK (f.contains ("#b { image: url(\"" + s.skin + "/top.png\"); }")); // Orbiter's top.png is outside the skin
	CHECK (f.contains ("#c { image: url(\"\"); }"));
	CHECK (f.contains ("#d { image: url(\"" + s.skin + "/forms/ok.png\"); }"));
	CHECK (warned.join ('|').toStdString () == "outside.png: not a file inside the skin folder");
}

#include "FormsRuntime.Test.moc"
