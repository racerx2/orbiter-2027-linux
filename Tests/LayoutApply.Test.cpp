// custom: launcher layouts; export, the .ui reader and writer, applying and undoing layouts on Orbiter.rc dialogs offscreen
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QWidget>
#include <functional>
#include <map>
#include "ResDialog.h"
#include "resource.h"
#include "Custom/LayoutApply.h"
#include "Custom/LayoutHook.h"
#include "Custom/UiForm.h"

using namespace custom;

void *ModuleProc (void *, const char *) { return nullptr; }
void LogOut_Warning (const char*, const char*, int, const char*, ...) {}

static QApplication &App ()
{
	static int argc = 1;
	static char name[] = "LayoutApply.Test", *argv[] = {name, nullptr};
	if (qEnvironmentVariableIsEmpty ("QT_QPA_PLATFORM")) qputenv ("QT_QPA_PLATFORM", "offscreen");
	static QApplication app (argc, argv);
	return app;
}

static void Pump ()
{
	for (int i = 0; i < 5; i++) QCoreApplication::processEvents ();
}

struct Built {
	QWidget *dlg = nullptr;
	const RESDIALOG *d = nullptr;
	UiForm form;
	QTemporaryDir tmp;
	QString skin;     // a skin folder with the exported pictures in ui/images
	~Built () { delete dlg; }
};

// a fresh stock dialog and its exported form, read back through the writer
static void Build (Built &b, int resId)
{
	App ();
	b.d = oapiFindResDialog (nullptr, resId);
	REQUIRE(b.d);
	b.dlg = CreateResDialog (nullptr, resId, nullptr, nullptr, false);
	REQUIRE(b.dlg);
	QWidget *ex = CreateResDialog (nullptr, resId, nullptr, nullptr, false);
	std::vector<LayoutImage> images;
	UiForm f = ExportLayout (ex, b.d, nullptr, images);
	delete ex;
	QString err;
	REQUIRE(ReadUiForm (WriteUiForm (f), b.form, err));
	REQUIRE(b.tmp.isValid ());
	b.skin = QDir (b.tmp.path ()).canonicalPath ();
	QDir ().mkpath (b.skin + "/ui/images");
	for (const LayoutImage &i : images)
		REQUIRE(i.image.save (b.skin + "/ui/" + i.path));
}

// the next widget Tab moves to
static QWidget *NextTab (QWidget *w)
{
	for (QWidget *x = w->nextInFocusChain (); x && x != w; x = x->nextInFocusChain ())
		if ((x->focusPolicy () & Qt::TabFocus) && !w->isAncestorOf (x)) return x;
	return nullptr;
}

static UiWidget *Find (UiWidget &w, const QString &name)
{
	if (w.name == name) return &w;
	for (auto &c : w.children)
		if (UiWidget *r = Find (c, name)) return r;
	return nullptr;
}

static bool Remove (UiWidget &w, const QString &name)
{
	for (size_t i = 0; i < w.children.size (); i++) {
		if (w.children[i].name == name) {
			w.children.erase (w.children.begin () + i);
			return true;
		}
		if (Remove (w.children[i], name)) return true;
	}
	return false;
}

static void Names (const UiWidget &w, QStringList &out)
{
	for (const auto &c : w.children) {
		out << c.name;
		Names (c, out);
	}
}

static QRect Geo (const UiWidget *w)
{
	const UiValue *v = w->Prop ("geometry");
	return (v ? v->rect : QRect ());
}

static void Move (UiWidget *w, int dx, int dy)
{
	w->SetProp ("geometry", UiValue::Rect (Geo (w).translated (dx, dy)));
}

static bool Apply (Built &b, QStringList *log = nullptr, QString skin = QString (), LayoutInfo *pinfo = nullptr)
{
	if (skin.isEmpty ()) skin = b.skin;
	LayoutInfo info;
	QString err;
	bool ok = ApplyLayout (b.dlg, b.d, b.form, skin, skin + "/ui/x.ui", [log](const QString &s) { if (log) log->append (s); }, info, err);
	if (pinfo) *pinfo = info;
	if (!ok && log) log->append ("ERROR " + err);
	return ok;
}

static QStringList Grep (const QStringList &log, const QString &s)
{
	QStringList out;
	for (const QString &l : log)
		if (l.contains (s)) out << l;
	return out;
}

TEST_CASE("Export: classes, unique names, pairs, nesting, notes and tab stops", "[layout]")
{
	Built m;
	Build (m, IDD_MAIN);
	CHECK(m.form.root.cls == "QDialog");
	CHECK(m.form.root.name == "IDD_MAIN");
	CHECK(Geo (&m.form.root).size () == m.dlg->size ());
	REQUIRE(m.form.root.Prop ("minimumSize"));
	CHECK(m.form.root.Prop ("minimumSize")->rect.size () == QSize (550, 350));
	REQUIRE(m.form.root.Dyn ("orbiterControls"));
	CHECK(m.form.root.Dyn ("orbiterControls")->str.split (',').size () == m.d->nctrl);
	CHECK(m.form.root.Dyn ("orbiterBaseX")->num == m.dlg->property ("resBaseX").toDouble ());
	UiWidget *launch = Find (m.form.root, "IDLAUNCH");
	REQUIRE(launch);
	CHECK(launch->cls == "QPushButton");
	CHECK(Geo (launch) == oapiResDlgItem (m.dlg, IDLAUNCH)->geometry ());
	CHECK(launch->Prop ("text")->str == "&Launch Orbiter");
	CHECK(launch->Dyn ("orbiterNote"));
	CHECK(!launch->Prop ("toolTip"));
	UiWidget *scn = Find (m.form.root, "IDC_MNU_SCN");
	REQUIRE(scn);
	CHECK(scn->Prop ("styleSheet")); // BS_RIGHT: the stock style sheet
	UiWidget *page = Find (m.form.root, "IDC_MNU_PAGECONTAINER");
	REQUIRE(page);
	CHECK(page->cls == "QFrame");
	CHECK(page->Dyn ("orbiterStandIn"));
	UiWidget *logo = Find (m.form.root, "IDC_LOGO");
	REQUIRE(logo);
	CHECK(logo->Prop ("pixmap")->str == "images/IDB_BANNER.png");

	Built s;
	Build (s, IDD_PAGE_SCN);
	CHECK(s.form.root.cls == "QWidget");
	UiWidget *split = Find (s.form.root, "IDC_SCN_SPLIT1");
	REQUIRE(split);
	QStringList inSplit;
	Names (*split, inSplit);
	CHECK(inSplit.contains ("IDC_SCN_LIST"));
	CHECK(inSplit.contains ("IDC_SCN_HTML"));
	CHECK(inSplit.contains ("IDC_SCN_DESC"));
	CHECK(!inSplit.contains ("IDC_SCN_SAVE"));
	CHECK(Geo (Find (s.form.root, "IDC_SCN_LIST")).topLeft () == oapiResDlgItem (s.dlg, IDC_SCN_LIST)->pos () - oapiResDlgItem (s.dlg, IDC_SCN_SPLIT1)->pos ());

	Built v;
	Build (v, IDD_SAVESCN);
	QStringList names;
	Names (v.form.root, names);
	CHECK(names.contains ("IDC_STATIC"));
	CHECK(names.contains ("IDC_STATIC_2"));
	CHECK(names.removeDuplicates () == 0);
	CHECK(v.form.tabstops == QStringList ({"IDOK", "IDCANCEL", "IDC_SAVE_NAME", "IDC_SAVE_DESC"}));

	Built a;
	Build (a, IDD_PAGE_ABT);
	UiWidget *grp = Find (a.form.root, "IDC_ABT_GRP_ORBITER");
	REQUIRE(grp);
	QStringList inGrp;
	Names (*grp, inGrp);
	CHECK(inGrp.contains ("IDC_ABT_TXT_NAME"));
	CHECK(inGrp.contains ("IDC_ABT_ICON_DG"));
	CHECK(a.form.root.children.front ().cls == "QGroupBox"); // group boxes first
}

TEST_CASE("Export: uic compiles every form", "[layout]")
{
	App ();
	QTemporaryDir tmp;
	REQUIRE(tmp.isValid ());
	const QString uic = "/usr/lib/qt6/libexec/uic";
	int n = 0;
	for (const QString &name : LayoutDialogs ()) {
		const RESDIALOG *d = nullptr;
		const RESTABLE *t = OrbiterResources ();
		for (size_t i = 0; i < t->ndlg; i++)
			if (name == t->dlg[i].name) d = t->dlg + i;
		INFO(name.toStdString ());
		REQUIRE(d);
		QWidget *w = CreateResDialog (nullptr, d->id, nullptr, nullptr, false);
		REQUIRE(w);
		std::vector<LayoutImage> images;
		UiForm f = ExportLayout (w, d, nullptr, images);
		delete w;
		QString path = tmp.filePath (name + ".ui");
		QFile out (path);
		REQUIRE(out.open (QIODevice::WriteOnly));
		out.write (WriteUiForm (f));
		out.close ();
		UiForm back;
		QString err;
		CHECK(ReadUiFile (path, back, err));
		CHECK(back.widgets == f.widgets);
		if (QFileInfo (uic).isExecutable ()) {
			QProcess p;
			p.start (uic, {path, "-o", tmp.filePath (name + ".h")});
			REQUIRE(p.waitForFinished (20000));
			INFO(p.readAllStandardError ().toStdString ());
			CHECK(p.exitCode () == 0);
		}
		n++;
	}
	CHECK(n == 32);
	if (!QFileInfo (uic).isExecutable ()) WARN("uic not found; forms not compiled");
}

TEST_CASE("Hook: called before the show, with the template", "[layout]")
{
	App ();
	static QString seen;
	static bool visible = true;
	g_resDialogHook = [](QWidget *dlg, const RESDIALOG *d, void *) { seen = d->name; visible = dlg->isVisible (); };
	QWidget *w = oapiCreateResDialog (nullptr, IDD_EXTRA_SHUTDOWN, nullptr); // WS_VISIBLE: shown by the call
	g_resDialogHook = nullptr;
	REQUIRE(w);
	CHECK(seen == "IDD_EXTRA_SHUTDOWN");
	CHECK(!visible);
	CHECK(w->isVisible ());
	delete w;
}

TEST_CASE("Dialogs under a style sheet keep the dialog font", "[layout]")
{
	App ();
	QWidget top;
	QFont big = QApplication::font ();
	big.setPointSize (big.pointSize () + 6);
	top.setFont (big);
	top.setStyleSheet ("QWidget#nothing { background: red; }"); // a skin's style sheet, set before the pages are built
	QWidget *page = CreateResDialog (nullptr, IDD_PAGE_SCN, &top, nullptr, false);
	REQUIRE(page);
	const int pt = page->font ().pointSize ();
	CHECK(pt != big.pointSize ());
	CHECK(oapiResDlgItem (page, -1)->font ().pointSize () == pt);
	CHECK(oapiResDlgItem (page, IDC_SCN_LIST)->font ().pointSize () == pt);
	QWidget *popup = CreateResDialog (nullptr, IDD_SAVESCN, &top, nullptr, false);
	REQUIRE(popup);
	CHECK(oapiResDlgItem (popup, IDOK)->font ().pointSize () == popup->font ().pointSize ());
	delete popup;
	QWidget *main = CreateResDialog (nullptr, IDD_MAIN, nullptr, nullptr, false);
	REQUIRE(main);
	main->setStyleSheet ("QDialog { background: #102030; }"); // a skin's style sheet after the build
	QCoreApplication::processEvents ();
	CHECK(oapiResDlgItem (main, IDC_MNU_SCN)->font ().pointSize () == main->font ().pointSize ()); // has a style sheet of its own
	CHECK(oapiResDlgItem (main, IDLAUNCH)->font ().pointSize () == main->font ().pointSize ());
	main->setStyleSheet ("QDialog { background: #203040; } QLabel { color: white; }"); // another skin
	QCoreApplication::processEvents ();
	CHECK(oapiResDlgItem (main, IDC_BLACKBOX)->font ().pointSize () == main->font ().pointSize ());
	delete main;
}

TEST_CASE("Apply: an unchanged export changes nothing", "[layout]")
{
	for (int id : {IDD_MAIN, IDD_PAGE_SCN, IDD_PAGE_ABT, IDD_SAVESCN, IDD_PAGE_DEV, IDD_OPTIONS_INSTRUMENT, IDD_PAGE_WAIT2, IDD_MSG}) {
		Built b;
		Build (b, id);
		auto look = [](QWidget *w) {
			QString s = w->objectName () + "|" + QString::number (w->isHidden ()) + "|" + w->styleSheet () + "|" + w->font ().toString () + "|" + w->toolTip ();
			if (QLabel *l = qobject_cast<QLabel*> (w)) {
				QImage img = l->pixmap ().toImage ().convertToFormat (QImage::Format_ARGB32);
				s += "|" + l->text () + "|" + QString::number (l->alignment ().toInt ()) + "|" + QString::number (l->wordWrap ()) + "|" + QString::number (l->textFormat ())
					+ "|" + QString::number (qChecksum (QByteArrayView ((const char*)img.constBits (), img.sizeInBytes ())));
			}
			if (QLineEdit *e = qobject_cast<QLineEdit*> (w)) s += "|" + QString::number (e->alignment ().toInt ());
			if (QAbstractButton *p = qobject_cast<QAbstractButton*> (w)) s += "|" + p->text ();
			if (QGroupBox *g = qobject_cast<QGroupBox*> (w)) s += "|" + g->title () + "|" + QString::number (g->isFlat ());
			return s;
		};
		std::map<QWidget*, std::pair<QRect, QString>> before;
		for (QObject *o : b.dlg->children ())
			if (QWidget *w = qobject_cast<QWidget*> (o)) before[w] = {w->geometry (), look (w)};
		const QString title = b.dlg->windowTitle (), dfont = b.dlg->font ().toString ();
		QSize size = b.dlg->size ();
		QStringList log;
		REQUIRE(Apply (b, &log));
		INFO(log.join ("\n").toStdString ());
		CHECK(log.isEmpty ());
		size_t k = 0;
		for (QObject *o : b.dlg->children ())
			if (QWidget *w = qobject_cast<QWidget*> (o)) {
				REQUIRE(before.count (w));
				CHECK(w->geometry () == before[w].first);
				CHECK(look (w).toStdString () == before[w].second.toStdString ());
				k++;
			}
		CHECK(k == before.size ());
		CHECK(b.dlg->size () == size);
		CHECK(b.dlg->windowTitle () == title);
		CHECK(b.dlg->font ().toString () == dfont);
		CHECK(b.dlg->styleSheet ().isEmpty ());
	}
}

TEST_CASE("Apply: moves, texts, fonts, deletes and the safety rules", "[layout]")
{
	Built b;
	Build (b, IDD_MAIN);
	UiWidget *launch = Find (b.form.root, "IDLAUNCH");
	launch->name = "GoButton"; // the name is only a label
	Move (launch, 10, -5);
	launch->SetProp ("text", UiValue::String ("<b>Go</b>"));
	launch->SetDyn ("resId", UiValue::Number (4242)); // dynamic properties are never copied
	launch->SetProp ("enabled", UiValue::Bool (false)); // not in the list: ignored
	UiValue big;
	big.type = UiValue::FONT;
	big.pointSize = b.form.root.Prop ("font")->pointSize + 3;
	b.form.root.SetProp ("font", big);
	UiWidget *ver = Find (b.form.root, "IDC_VERSION");
	UiValue bold;
	bold.type = UiValue::FONT;
	bold.bold = 1;
	ver->SetProp ("font", bold);
	ver->SetProp ("text", UiValue::String ("<i>v</i>"));
	UiWidget *exitb = Find (b.form.root, "IDEXIT");
	exitb->SetProp ("geometry", UiValue::Rect (QRect (5000, 10, 40, 20))); // off the form
	UiWidget *help = Find (b.form.root, "id9");
	help->SetProp ("geometry", UiValue::Rect (QRect (10, 10, 4, 4))); // too small
	REQUIRE(Remove (b.form.root, "IDC_BLACKBOX"));
	REQUIRE(Remove (b.form.root, "IDC_MNU_ABT")); // can't be removed
	REQUIRE(Remove (b.form.root, "IDC_MNU_PAGECONTAINER"));
	REQUIRE(Remove (b.form.root, "IDC_SHADOW"));
	UiWidget copy = *Find (b.form.root, "IDC_MNU_MOD"); // a pasted copy: the first wins
	copy.name = "IDC_MNU_MOD_copy";
	Move (&copy, 0, 100);
	b.form.root.children.push_back (copy);

	QWidget *wl = oapiResDlgItem (b.dlg, IDLAUNCH), *we = oapiResDlgItem (b.dlg, IDEXIT), *wh = oapiResDlgItem (b.dlg, 9);
	QRect l0 = wl->geometry (), e0 = we->geometry (), h0 = wh->geometry (), mod0 = oapiResDlgItem (b.dlg, IDC_MNU_MOD)->geometry ();
	QStringList log;
	REQUIRE(Apply (b, &log));
	CHECK(wl->geometry () == l0.translated (10, -5));
	CHECK(static_cast<QPushButton*> (wl)->text () == "<b>Go</b>");
	CHECK(wl->property ("resId").toInt () == IDLAUNCH);
	CHECK(wl->isEnabled ());
	QLabel *lv = static_cast<QLabel*> (oapiResDlgItem (b.dlg, IDC_VERSION));
	CHECK(lv->font ().bold ());
	CHECK(lv->font ().pointSize () == big.pointSize);
	CHECK(wl->font ().pointSize () == big.pointSize); // the form's font
	CHECK(b.dlg->font ().pointSize () == big.pointSize);
	CHECK(lv->textFormat () == Qt::PlainText);
	CHECK(we->geometry () == e0);
	CHECK(wh->geometry () == h0);
	CHECK(Grep (log, "IDEXIT").size () == 1);
	CHECK(Grep (log, "second copy").size () == 1);
	CHECK(oapiResDlgItem (b.dlg, IDC_MNU_MOD)->geometry () == mod0);
	QWidget *box = oapiResDlgItem (b.dlg, IDC_BLACKBOX);
	CHECK(box->isHidden ());
	box->show (); // the classic code shows it: hidden again
	Pump ();
	CHECK(box->isHidden ());
	CHECK(!oapiResDlgItem (b.dlg, IDC_MNU_ABT)->isHidden ());
	CHECK(!oapiResDlgItem (b.dlg, IDC_MNU_PAGECONTAINER)->isHidden ());
	CHECK(Grep (log, "IDC_MNU_ABT: can't be removed").size () == 1);
	CHECK(Grep (log, "IDC_MNU_PAGECONTAINER: can't be removed").size () == 1);
	CHECK(oapiResDlgItem (b.dlg, IDC_SHADOW)->isHidden ());
}

TEST_CASE("Apply: nesting, scaling, stand-ins, tab order, controls an old layout doesn't know", "[layout]")
{
	Built a;
	Build (a, IDD_PAGE_ABT);
	Move (Find (a.form.root, "IDC_ABT_GRP_ORBITER"), 6, 4);
	QWidget *name = oapiResDlgItem (a.dlg, IDC_ABT_TXT_NAME);
	QRect n0 = name->geometry ();
	REQUIRE(Apply (a));
	CHECK(name->geometry () == n0.translated (6, 4));
	CHECK(oapiResDlgItem (a.dlg, IDC_ABT_GRP_ORBITER)->parentWidget () == a.dlg); // still a direct child

	Built s;
	Build (s, IDD_PAGE_SCN);
	UiWidget *split = Find (s.form.root, "IDC_SCN_SPLIT1");
	Move (split, 0, 3);
	split->SetProp ("toolTip", UiValue::String ("x"));
	double bx = s.dlg->property ("resBaseX").toDouble ();
	s.form.root.SetDyn ("orbiterBaseX", UiValue::Double (bx / 2)); // the export had half the base unit: twice as far
	QWidget *save = oapiResDlgItem (s.dlg, IDC_SCN_SAVE), *paused = oapiResDlgItem (s.dlg, IDC_SCN_PAUSED);
	QRect sp0 = oapiResDlgItem (s.dlg, IDC_SCN_SPLIT1)->geometry (), p0 = paused->geometry ();
	QRect form0 = Geo (&s.form.root);
	REQUIRE(Remove (s.form.root, "IDC_SCN_SAVE"));
	QStringList ctl = s.form.root.Dyn ("orbiterControls")->str.split (',');
	ctl.removeIf ([](const QString &p) { return p.endsWith (":" + QString::number (IDC_SCN_SAVE)); });
	s.form.root.SetDyn ("orbiterControls", UiValue::String (ctl.join (','))); // a layout from before Save existed
	QStringList log;
	REQUIRE(Apply (s, &log));
	QWidget *sw = oapiResDlgItem (s.dlg, IDC_SCN_SPLIT1);
	CHECK(sw->geometry ().y () == sp0.y () + 3);
	CHECK(sw->toolTip ().isEmpty ());
	CHECK(paused->x () == (int)std::lround (p0.x () * 2.0));
	CHECK(s.dlg->width () == (int)std::lround (form0.width () * 2.0));
	CHECK(!save->isHidden ());
	CHECK(Grep (log, "IDC_SCN_SAVE: not in this layout").size () == 1);

	Built v;
	Build (v, IDD_SAVESCN);
	v.form.tabstops = QStringList ({"IDC_SAVE_NAME", "IDC_SAVE_DESC", "IDOK", "IDCANCEL"});
	REQUIRE(Apply (v));
	CHECK(NextTab (oapiResDlgItem (v.dlg, IDC_SAVE_NAME)) == oapiResDlgItem (v.dlg, IDC_SAVE_DESC));
	CHECK(NextTab (oapiResDlgItem (v.dlg, IDC_SAVE_DESC)) == oapiResDlgItem (v.dlg, IDOK));
	Built w;
	Build (w, IDD_SAVESCN);
	REQUIRE(Apply (w)); // the exported order is Orbiter's: no change
	CHECK(NextTab (oapiResDlgItem (w.dlg, IDOK)) == oapiResDlgItem (w.dlg, IDCANCEL));
}

TEST_CASE("Apply: decorations, anchors, pictures", "[layout]")
{
	QTemporaryDir tmp;
	REQUIRE(tmp.isValid ());
	const QString skin = QDir (tmp.path ()).canonicalPath () + "/skin";
	QDir ().mkpath (skin + "/ui/images");
	QImage img (8, 6, QImage::Format_ARGB32);
	img.fill (Qt::red);
	REQUIRE(img.save (skin + "/ui/images/red.png"));
	REQUIRE(img.save (tmp.path () + "/outside.png"));

	Built b;
	Build (b, IDD_MAIN);
	UiWidget panel;
	panel.cls = "QFrame";
	panel.name = "panel";
	panel.SetProp ("geometry", UiValue::Rect (QRect (0, 0, 60, 60)));
	panel.SetProp ("styleSheet", UiValue::String ("background: url(${SKIN}/ui/images/red.png);"));
	b.form.root.children.insert (b.form.root.children.begin (), panel); // first: behind everything
	UiWidget pic;
	pic.cls = "QLabel";
	pic.name = "pic";
	pic.SetProp ("geometry", UiValue::Rect (QRect (500, 2, 8, 6)));
	pic.SetProp ("pixmap", UiValue::Pixmap ("images/red.png"));
	pic.SetDyn ("anchor", UiValue::String ("Right"));
	b.form.root.children.push_back (pic); // last: on top
	UiWidget bad = pic;
	bad.name = "bad";
	bad.SetProp ("pixmap", UiValue::Pixmap ("../../outside.png"));
	bad.dyn.clear ();
	b.form.root.children.push_back (bad);
	UiWidget res = bad;
	res.name = "res";
	res.SetProp ("pixmap", UiValue::Pixmap (":/images/x.png"));
	b.form.root.children.push_back (res);
	UiWidget button;
	button.cls = "QPushButton";
	button.name = "extra";
	button.SetProp ("geometry", UiValue::Rect (QRect (5, 5, 50, 20)));
	b.form.root.children.push_back (button);
	REQUIRE(Find (b.form.root, "IDC_MNU_SCN"));
	b.form.root.SetProp ("styleSheet", UiValue::String ("QDialog { background: #102030; }"));

	QStringList log;
	LayoutInfo info;
	REQUIRE(Apply (b, &log, skin, &info));
	QLabel *p = b.dlg->findChild<QLabel*> ("pic");
	QWidget *f = b.dlg->findChild<QWidget*> ("panel");
	REQUIRE(p);
	REQUIRE(f);
	CHECK(!p->pixmap ().isNull ());
	CHECK(p->testAttribute (Qt::WA_TransparentForMouseEvents));
	CHECK(f->styleSheet ().contains (skin + "/ui/images/red.png"));
	CHECK(!b.dlg->findChild<QWidget*> ("extra"));
	CHECK(Grep (log, "extra:").size () == 1);
	CHECK(Grep (log, "outside the skin folder").size () == 1);
	CHECK(Grep (log, "not a file in the skin folder").size () == 1);
	QObjectList kids = b.dlg->children ();
	CHECK(kids.indexOf (f) < kids.indexOf (oapiResDlgItem (b.dlg, IDC_LOGO))); // under the stock controls
	CHECK(kids.indexOf (p) > kids.indexOf (oapiResDlgItem (b.dlg, IDLAUNCH)));
	CHECK(info.rootStyle == "QDialog { background: #102030; }");
	CHECK(b.dlg->styleSheet () == info.rootStyle);
	CHECK(info.minSize.width () >= 550);
	b.dlg->show (); // resize events wait for the show
	Pump ();
	int x0 = p->x ();
	b.dlg->resize (b.dlg->width () + 100, b.dlg->height ());
	Pump ();
	CHECK(p->x () == x0 + 100);
	b.dlg->hide ();
}

TEST_CASE("Apply: stacking follows the form, the minimum size is in pixels", "[layout]")
{
	Built v;
	Build (v, IDD_SAVESCN);
	UiWidget bg;
	bg.cls = "QFrame";
	bg.name = "bg";
	bg.SetProp ("geometry", UiValue::Rect (QRect (4, 4, 290, 300)));
	bg.SetProp ("styleSheet", UiValue::String ("background: #304050;"));
	UiWidget desc = *Find (v.form.root, "IDC_SAVE_DESC"); // dragged into the panel in Designer
	REQUIRE(Remove (v.form.root, "IDC_SAVE_DESC"));
	desc.SetProp ("geometry", UiValue::Rect (Geo (&desc).translated (-4, -4)));
	bg.children.push_back (desc);
	v.form.root.children.push_back (bg);
	REQUIRE(Apply (v));
	QWidget *panel = v.dlg->findChild<QWidget*> ("bg");
	REQUIRE(panel);
	QObjectList kids = v.dlg->children ();
	CHECK(kids.indexOf (panel) < kids.indexOf (oapiResDlgItem (v.dlg, IDC_SAVE_DESC)));
	CHECK(kids.indexOf (panel) > kids.indexOf (oapiResDlgItem (v.dlg, IDOK)));

	Built m;
	Build (m, IDD_MAIN);
	m.form.root.SetDyn ("orbiterBaseY", UiValue::Number (m.dlg->property ("resBaseY").toInt () / 2)); // twice the base units here
	LayoutInfo info;
	REQUIRE(Apply (m, nullptr, QString (), &info));
	CHECK(info.minSize.height () == LAYOUT_MINH);
	CHECK(info.refSize.height () > 2 * LAYOUT_MINH);
}

TEST_CASE("CopyStyleFiles: only files inside the source, only into the new folder", "[layout]")
{
	QTemporaryDir tmp;
	REQUIRE(tmp.isValid ());
	const QString t = QDir (tmp.path ()).canonicalPath ();
	QDir ().mkpath (t + "/src/a/b/c/e");
	QDir ().mkpath (t + "/src/img");
	QDir ().mkpath (t + "/src/.config");
	QDir ().mkpath (t + "/dst");
	auto put = [](const QString &f) { QFile x (f); REQUIRE(x.open (QIODevice::WriteOnly)); x.write ("x"); };
	put (t + "/src/img/ok.png");
	put (t + "/src/a/b/c/e/x.png");
	put (t + "/src/.config/evil.desktop");
	put (t + "/outside.png");
	REQUIRE(QFile::link (t + "/src/a/b/c/e", t + "/src/d"));
	REQUIRE(QFile::link (t + "/outside.png", t + "/src/out.png"));
	const QString qss = "a { image: url(\"${SKIN}/img/ok.png\"); } b { image: url(${SKIN}/d/x.png); }"
		" c { image: url(${SKIN}/d/../../../../.config/evil.desktop); } d { image: url(${SKIN}/out.png); }"
		" e { image: url(${SKIN}//etc/passwd); }";
	QStringList problems;
	CHECK(CopyStyleFiles (qss, t + "/src", t + "/dst", problems) == 2);
	CHECK(QFileInfo::exists (t + "/dst/img/ok.png"));
	CHECK(QFileInfo::exists (t + "/dst/d/x.png"));
	CHECK(!QFileInfo::exists (t + "/.config/evil.desktop"));
	CHECK(!QFileInfo::exists (t + "/dst/out.png"));
	CHECK(problems.size () == 3);
}

TEST_CASE("Apply: forms refused as a whole", "[layout]")
{
	Built l;
	Build (l, IDD_SAVESCN);
	l.form.layout = true;
	QStringList log;
	CHECK(!Apply (l, &log));
	CHECK(Grep (log, "Break Layout").size () == 1);

	Built m;
	Build (m, IDD_MAIN);
	m.form.root.SetProp ("geometry", UiValue::Rect (QRect (0, 0, 500, 300)));
	CHECK(!Apply (m));

	UiForm f;
	QString err;
	CHECK(!ReadUiForm ("<ui><widget class=\"QWidget\" name=\"x\"><property name=\"a\"></widget></ui>", f, err));
	CHECK(!err.isEmpty ());
	CHECK(!ReadUiForm ("<notui/>", f, err));
	CHECK(!ReadUiForm (QByteArray (UI_MAX_BYTES + 10, ' '), f, err));
	QByteArray many = "<ui><widget class=\"QWidget\" name=\"r\">";
	for (int i = 0; i < UI_MAX_WIDGETS + 5; i++) many += "<widget class=\"QLabel\" name=\"l\"/>";
	many += "</widget></ui>";
	CHECK(!ReadUiForm (many, f, err));
}

TEST_CASE("Reader: what Qt Designer writes", "[layout]")
{
	const char *ui = R"(<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>IDD_SAVESCN</class>
 <widget class="QDialog" name="IDD_SAVESCN">
  <property name="geometry"><rect><x>0</x><y>0</y><width>300</width><height>280</height></rect></property>
  <property name="windowTitle"><string notr="true">Save</string></property>
  <widget class="QLabel" name="IDC_STATIC">
   <property name="geometry"><rect><x>10</x><y>11</y><width>80</width><height>13</height></rect></property>
   <property name="font"><font><bold>true</bold></font></property>
   <property name="alignment"><set>Qt::AlignmentFlag::AlignLeading|Qt::AlignmentFlag::AlignLeft|Qt::AlignmentFlag::AlignVCenter</set></property>
   <property name="text"><string comment="c">Name</string></property>
   <property name="orbiterCtl" stdset="0"><string>3:65535</string></property>
  </widget>
  <widget class="Line" name="line">
   <property name="geometry"><rect><x>10</x><y>30</y><width>100</width><height>3</height></rect></property>
   <property name="orientation"><enum>Qt::Orientation::Horizontal</enum></property>
  </widget>
  <customwidget/>
  <zorder>line</zorder>
  <zorder>IDC_STATIC</zorder>
 </widget>
 <customwidgets><customwidget><class>X</class></customwidget></customwidgets>
 <resources><include location="a.qrc"/></resources>
 <connections><connection><sender>a</sender></connection></connections>
</ui>
)";
	UiForm f;
	QString err;
	REQUIRE(ReadUiForm (ui, f, err));
	CHECK(f.widgets == 2);
	CHECK(f.root.Prop ("windowTitle")->str == "Save");
	const UiWidget &l = f.root.children[0];
	CHECK(l.Prop ("font")->bold == 1);
	CHECK(l.Prop ("font")->pointSize == 0);
	CHECK(UiSetNames (l.Prop ("alignment")->str) == QStringList ({"AlignLeft", "AlignLeft", "AlignVCenter"}));
	CHECK(l.Dyn ("orbiterCtl")->str == "3:65535");
	CHECK(f.root.PaintOrder ().back ()->name == "IDC_STATIC");
}

// forms our export wrote, edited by hand, then opened in Qt Designer 6.10, moved with the keyboard and saved there
TEST_CASE("Apply: forms saved by Qt Designer", "[layout]")
{
	const QString dir = QString (LAYOUT_FIXTURES);
	auto scaled = [](Built &b, int x, int y, int w, int h) {
		double sx = b.dlg->property ("resBaseX").toDouble () / b.form.root.Dyn ("orbiterBaseX")->num;
		double sy = b.dlg->property ("resBaseY").toDouble () / b.form.root.Dyn ("orbiterBaseY")->num;
		return QRect ((int)std::lround (x * sx), (int)std::lround (y * sy), (int)std::lround (w * sx), (int)std::lround (h * sy));
	};
	const QString uic = "/usr/lib/qt6/libexec/uic";
	QTemporaryDir tmp;
	for (const char *f : {"IDD_SAVESCN.ui", "IDD_PAGE_SCN.ui"})
		if (QFileInfo (uic).isExecutable ()) {
			QProcess p;
			p.start (uic, {dir + "/ui/" + f, "-o", tmp.filePath ("x.h")});
			REQUIRE(p.waitForFinished (20000));
			CHECK(p.exitCode () == 0);
		}

	Built v;
	Build (v, IDD_SAVESCN);
	QString err;
	REQUIRE(ReadUiFile (dir + "/ui/IDD_SAVESCN.ui", v.form, err));
	QStringList log;
	REQUIRE(Apply (v, &log, dir));
	INFO(log.join ("\n").toStdString ());
	CHECK(log.isEmpty ());
	CHECK(oapiResDlgItem (v.dlg, IDOK)->geometry () == scaled (v, 130, 300, 80, 26));
	CHECK(oapiResDlgItem (v.dlg, IDCANCEL)->geometry () == scaled (v, 207, 290, 80, 26));
	CHECK(oapiResDlgItem (v.dlg, IDC_SAVE_NAME)->geometry () == scaled (v, 11, 34, 275, 26)); // inside grpName at 6, 25
	CHECK(static_cast<QLineEdit*> (oapiResDlgItem (v.dlg, IDC_SAVE_NAME))->alignment () == Qt::AlignLeft);
	QGroupBox *grp = v.dlg->findChild<QGroupBox*> ("grpName");
	QFrame *line = v.dlg->findChild<QFrame*> ("line");
	QLabel *pic = v.dlg->findChild<QLabel*> ("pic");
	REQUIRE(grp);
	REQUIRE(line);
	REQUIRE(pic);
	CHECK(line->frameShape () == QFrame::HLine);
	CHECK(!pic->pixmap ().isNull ());
	CHECK(pic->hasScaledContents ());
	QObjectList kids = v.dlg->children ();
	CHECK(kids.indexOf (grp) < kids.indexOf (oapiResDlgItem (v.dlg, IDC_SAVE_NAME))); // behind what Designer drew after it

	Built s;
	Build (s, IDD_PAGE_SCN);
	REQUIRE(ReadUiFile (dir + "/ui/IDD_PAGE_SCN.ui", s.form, err));
	log.clear ();
	REQUIRE(Apply (s, &log, dir));
	CHECK(oapiResDlgItem (s.dlg, IDC_SCN_SPLIT1)->geometry () == scaled (s, 0, 43, 437, 394));
	CHECK(oapiResDlgItem (s.dlg, IDC_SCN_LIST)->geometry () == scaled (s, 0, 43, 160, 414));
	CHECK(oapiResDlgItem (s.dlg, IDC_SCN_INFO)->x () == scaled (s, 370, 0, 0, 0).x ());
	CHECK(oapiResDlgItem (s.dlg, IDC_SCN_DELQS)->isHidden ());
	QFrame *panel = s.dlg->findChild<QFrame*> ("panel");
	REQUIRE(panel);
	CHECK(panel->styleSheet () == "background: #203040;");
	kids = s.dlg->children ();
	CHECK(kids.indexOf (panel) < kids.indexOf (oapiResDlgItem (s.dlg, -1))); // "Simulation scenarios"
	CHECK(kids.indexOf (panel) < kids.indexOf (oapiResDlgItem (s.dlg, IDC_SCN_PAUSED)));
}

// the undo is once per run: the case starts and ends a run of its own
TEST_CASE("Undo: looks back, texts the code set kept, managed controls stay hidden", "[layout]")
{
	ResetLayoutState ();
	Built b;
	Build (b, IDD_MAIN);
	UiWidget *logoForm = Find (b.form.root, "IDC_LOGO");
	std::erase_if (logoForm->props, [](const auto &p) { return p.first == "pixmap"; }); // the picture replaced by a text
	logoForm->SetProp ("text", UiValue::String ("LOGO TEXT"));
	Find (b.form.root, "IDC_BLACKBOX")->SetProp ("pixmap", UiValue::Pixmap ("images/IDB_BANNER.png"));
	UiWidget deco;
	deco.cls = "QLabel";
	deco.name = "deco";
	deco.SetProp ("geometry", UiValue::Rect (QRect (100, 100, 80, 20)));
	deco.SetProp ("text", UiValue::String ("Hello"));
	b.form.root.children.push_back (deco);
	Find (b.form.root, "IDLAUNCH")->SetProp ("text", UiValue::String ("Go"));
	Find (b.form.root, "IDC_VERSION")->SetProp ("text", UiValue::String ("layout version"));
	Find (b.form.root, "IDC_MNU_SCN")->SetProp ("styleSheet", UiValue::String ("color: red;"));
	UiValue bold;
	bold.type = UiValue::FONT;
	bold.bold = 1;
	b.form.root.SetProp ("font", bold);
	REQUIRE(Remove (b.form.root, "IDC_SHADOW"));
	QWidget *scn = oapiResDlgItem (b.dlg, IDC_MNU_SCN);
	const QString scnStyle = scn->styleSheet ();
	REQUIRE(Apply (b));
	CHECK(b.dlg->font ().bold ());
	QWidget *ver = oapiResDlgItem (b.dlg, IDC_VERSION);
	oapiSetDlgText (ver, "v.code"); // the code sets its text after the layout
	QWidget *abt = oapiResDlgItem (b.dlg, IDC_SHADOW);
	CHECK(abt->isHidden ());
	QLabel *logo = static_cast<QLabel*> (oapiResDlgItem (b.dlg, IDC_LOGO)), *box = static_cast<QLabel*> (oapiResDlgItem (b.dlg, IDC_BLACKBOX));
	const QString boxText = "\norbit.medphys.ucl.ac.uk\n(c) 2000-2016\nMartin Schweiger";
	CHECK(logo->text () == "LOGO TEXT");
	CHECK(!box->pixmap ().isNull ());

	Built s;
	Build (s, IDD_PAGE_SCN);
	REQUIRE(Remove (s.form.root, "IDC_SCN_INFO"));
	REQUIRE(Apply (s));
	QWidget *info = oapiResDlgItem (s.dlg, IDC_SCN_INFO);
	CHECK(info->isHidden ());

	UndoLayouts ();
	QCoreApplication::sendPostedEvents (nullptr, QEvent::DeferredDelete);
	Pump ();
	CHECK(!b.dlg->findChild<QWidget*> ("deco"));
	CHECK(static_cast<QPushButton*> (oapiResDlgItem (b.dlg, IDLAUNCH))->text () == "&Launch Orbiter");
	CHECK(static_cast<QLabel*> (ver)->text () == "v.code");
	CHECK(scn->styleSheet () == scnStyle);
	CHECK(!b.dlg->font ().bold ());
	CHECK(!abt->isHidden ());
	abt->hide ();
	abt->show ();
	Pump ();
	CHECK(!abt->isHidden ()); // the stay-hidden filter is gone
	CHECK(info->isHidden ());  // the code decides when the Info button shows

	CHECK(!logo->pixmap ().isNull ());
	CHECK(box->pixmap ().isNull ());
	CHECK(box->text () == boxText);

	Built c;
	Build (c, IDD_SAVESCN);
	CHECK(!Apply (c)); // after an undo the rest of the run is stock
	ResetLayoutState ();
}
