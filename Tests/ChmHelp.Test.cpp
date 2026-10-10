// not upstream: help viewer (ChmHelp.cpp): .chm (zip) pages and the help window beside modal dialogs, offscreen
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QColor>
#include <QDialog>
#include <QPointer>
#include <QPalette>
#include <QTest>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextFrame>
#include <QTimer>
#include <QWindow>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <zlib.h>
#include "ChmHelp.h"

namespace fs = std::filesystem;

static QApplication &App ()
{
	static int argc = 1;
	static char name[] = "ChmHelp.Test", *argv[] = {name, nullptr};
	if (qEnvironmentVariableIsEmpty ("QT_QPA_PLATFORM")) qputenv ("QT_QPA_PLATFORM", "offscreen");
	static QApplication app (argc, argv);
	return app;
}

static QByteArray Deflate (const QByteArray &in) // raw deflate, as zip entries hold it
{
	QByteArray out (compressBound (in.size()) + 64, '\0');
	z_stream zs = {};
	deflateInit2 (&zs, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
	zs.next_in = (Bytef*)in.data();
	zs.avail_in = in.size();
	zs.next_out = (Bytef*)out.data();
	zs.avail_out = out.size();
	deflate (&zs, Z_FINISH);
	out.resize (zs.total_out);
	deflateEnd (&zs);
	return out;
}

static void Put16 (QByteArray &b, quint32 v) { b.append (char(v & 0xff)); b.append (char(v >> 8 & 0xff)); }
static void Put32 (QByteArray &b, quint32 v) { Put16 (b, v & 0xffff); Put16 (b, v >> 16); }

struct ZipEntry { QByteArray name, data; quint32 usize; }; // data deflated; usize as the headers state it

static QByteArray Zip (const std::vector<ZipEntry> &entries)
{
	QByteArray z, cd;
	for (const ZipEntry &e : entries) {
		quint32 lh = z.size();
		Put32 (z, 0x04034b50); Put16 (z, 20); Put16 (z, 0); Put16 (z, 8); Put32 (z, 0); Put32 (z, 0); // version, flags, deflate, time, crc
		Put32 (z, e.data.size()); Put32 (z, e.usize); Put16 (z, e.name.size()); Put16 (z, 0);
		z += e.name + e.data;
		Put32 (cd, 0x02014b50); Put16 (cd, 20); Put16 (cd, 20); Put16 (cd, 0); Put16 (cd, 8); Put32 (cd, 0); Put32 (cd, 0);
		Put32 (cd, e.data.size()); Put32 (cd, e.usize); Put16 (cd, e.name.size()); Put16 (cd, 0); Put16 (cd, 0);
		Put16 (cd, 0); Put16 (cd, 0); Put32 (cd, 0); Put32 (cd, lh);
		cd += e.name;
	}
	quint32 cdofs = z.size();
	z += cd;
	Put32 (z, 0x06054b50); Put16 (z, 0); Put16 (z, 0); Put16 (z, entries.size()); Put16 (z, entries.size());
	Put32 (z, cd.size()); Put32 (z, cdofs); Put16 (z, 0);
	return z;
}

// a help file in a temp folder: a.htm with a header size 100 bytes too large, b.htm with a 4 GB header size, c.htm as it should be
struct TmpChm {
	fs::path dir, file;
	QByteArray page = QByteArray ("<html><body><p>Help page</p></body></html>\n").repeated (8);
	TmpChm ()
	{
		char tmpl[] = "/tmp/ob_chm_XXXXXX";
		dir = mkdtemp (tmpl);
		file = dir / "test.chm";
		QByteArray z = Zip ({{"a.htm", Deflate (page), (quint32)page.size() + 100},
		                     {"b.htm", Deflate (page), 0xFFFFFFF0u},
		                     {"c.htm", Deflate (page), (quint32)page.size()}});
		std::ofstream (file, std::ios::binary).write (z.constData(), z.size());
	}
	~TmpChm () { fs::remove_all (dir); }
};

TEST_CASE("help file pages read back at their real size", "[chmhelp]")
{
	App();
	TmpChm c;
	ChmBrowser b;
	auto read = [&](const char *topic) {
		return b.loadResource (QTextDocument::ImageResource, ChmUrl (QString::fromStdString (c.file.string()), topic)).toByteArray();
	};
	REQUIRE(read ("c.htm") == c.page);
	REQUIRE(read ("a.htm") == c.page); // no uninitialised tail after a stream shorter than its header
	REQUIRE(read ("b.htm").isEmpty()); // no 4 GB buffer for a header deflate can't have produced
}

TEST_CASE("the help window takes input while a modal dialog runs", "[chmhelp]")
{
	App();
	TmpChm c;
	QWidget lp;
	lp.resize (300, 200);
	lp.show();
	REQUIRE(HtmlHelp ((QWidget*)nullptr, c.file.string().c_str(), "c.htm")); // open before the dialog, like oapiOpenLaunchpadHelp
	QWidget *help = nullptr;
	for (QWidget *w : QApplication::topLevelWidgets())
		if (w->isVisible() && w->findChild<QTextBrowser*>()) help = w;
	REQUIRE(help);
	struct PressCounter: QObject {
		QWidget *root = nullptr;
		int n = 0;
		bool eventFilter (QObject *o, QEvent *e) override
		{
			if (e->type() == QEvent::MouseButtonPress && o->isWidgetType() && (o == root || root->isAncestorOf (static_cast<QWidget*> (o)))) n++;
			return false;
		}
	} presses;
	presses.root = help;
	qApp->installEventFilter (&presses);
	QDialog dlg (&lp);
	dlg.resize (100, 50);
	int during = -1;
	QTimer::singleShot (0, &dlg, [&]() {
		HtmlHelp (&dlg, c.file.string().c_str(), "c.htm"); // the dialog's Help button
		QTest::mouseClick (help->windowHandle(), Qt::LeftButton, Qt::NoModifier, QPoint (20, 20));
		during = presses.n;
		dlg.reject();
	});
	QWindow *before = help->windowHandle()->transientParent();
	dlg.exec();
	qApp->removeEventFilter (&presses);
	REQUIRE(during == 1);
	REQUIRE(help->windowHandle()->transientParent() == before); // back from the closed dialog
	delete help; // not left to the static QApplication's exit
}

TEST_CASE("the help window is owned by the render window and closes with it", "[chmhelp]")
{
	App();
	TmpChm c;
	QWindow *rw = new QWindow; // hwndCaller: the render window
	rw->resize (300, 200);
	rw->show();
	REQUIRE(HtmlHelp (rw, c.file.string().c_str(), "c.htm"));
	QPointer<QWidget> help;
	for (QWidget *w : QApplication::topLevelWidgets())
		if (w->isVisible() && w->findChild<QTextBrowser*>()) help = w;
	REQUIRE(help);
	REQUIRE(help->windowHandle()->transientParent() == rw);
	QWindow *rw2 = new QWindow; // another owner while it is shown
	rw2->resize (300, 200);
	rw2->show();
	REQUIRE(HtmlHelp (rw2, c.file.string().c_str(), "c.htm"));
	REQUIRE(help->isVisible());
	REQUIRE(help->windowHandle()->transientParent() == rw2);
	delete rw; // an earlier owner does not close it
	QCoreApplication::sendPostedEvents (nullptr, QEvent::DeferredDelete);
	REQUIRE(help);
	REQUIRE(help->isVisible());
	delete rw2; // its owner does
	QCoreApplication::sendPostedEvents (nullptr, QEvent::DeferredDelete);
	REQUIRE(!help);
}

TEST_CASE("pages lose their own colours and keep everything else", "[chmhelp]")
{
	const QString page = "<html><head><style type=\"text/css\">h1 { color: #000080; background-color: #E6E6FF; border-color: red; font-size: 150% }</style></head>"
		"<body BGCOLOR=#FFFFFF TEXT=#000000 link=\"#0000ff\" vlink='#800080'><p style=\"color: red; margin: 2px\">red text=color</p>"
		"<font color=\"#0000ff\" face=Arial>blue</font><input type=text title=\"a color=x\"><table bgcolor=#E0E0E0><tr><td style='background: white; font-family:\"Arial\"'>c</td></tr></table></body></html>";
	const QString out = ThemeHtml (page);
	CHECK(!out.contains ("BGCOLOR", Qt::CaseInsensitive));
	CHECK(!out.contains ("TEXT=#000000"));
	CHECK(!out.contains ("link=", Qt::CaseInsensitive));
	CHECK(!out.contains ("#0000ff", Qt::CaseInsensitive));
	CHECK(!out.contains ("#E6E6FF", Qt::CaseInsensitive));
	CHECK(out.contains ("<p style=\"margin: 2px\">")); // the colour went from the style attribute
	CHECK(!out.contains ("background: white"));
	CHECK(out.contains ("border-color: red")); // other colour properties stay
	CHECK(out.contains ("font-size: 150%"));
	CHECK(out.contains ("margin: 2px"));
	CHECK(out.contains ("face=Arial"));
	CHECK(out.contains ("red text=color")); // page text is never touched
	CHECK(out.contains ("type=text"));
	CHECK(out.contains ("title=\"a color=x\""));
	CHECK(out.contains ("style='font-family:\"Arial\"'")); // the value keeps its quotes
	CHECK(ThemeCss ("body { font-family: Arial; color: #000 } h1{color:blue;background-color:#E0E0FF;padding:0.1em}") == "body { font-family: Arial; } h1{padding:0.1em}");
}

static QPalette Dark ()
{
	QPalette p;
	p.setColor (QPalette::Base, QColor (0x20, 0x22, 0x25));
	p.setColor (QPalette::Text, QColor (0xfc, 0xfc, 0xfc));
	p.setColor (QPalette::AlternateBase, QColor (0x29, 0x2c, 0x30));
	p.setColor (QPalette::Link, QColor (0x1d, 0x99, 0xf3));
	return p;
}

TEST_CASE("pages show in the browser's palette and follow a theme change", "[chmhelp]")
{
	App();
	ChmBrowser b;
	b.setPalette (Dark());
	b.SetPageHtml ("<body BGCOLOR=#FFFFFF TEXT=#000000><h1>Title</h1><p>Text <font color=\"#000000\">black</font> <a href=\"x.htm\">link</a></p></body>");
	QTextDocument *doc = b.document();
	CHECK(!doc->rootFrame()->frameFormat().hasProperty (QTextFormat::BackgroundBrush)); // no page background of its own: the palette's Base
	bool heading = false, link = false;
	for (QTextBlock blk = doc->begin(); blk.isValid(); blk = blk.next()) {
		if (blk.text() == "Title") heading = blk.blockFormat().background().color() == QColor (0x29, 0x2c, 0x30);
		for (auto it = blk.begin(); !it.atEnd(); ++it) {
			QTextCharFormat f = it.fragment().charFormat();
			if (f.isAnchor()) link = f.foreground().color() == QColor (0x1d, 0x99, 0xf3);
			else CHECK(!f.hasProperty (QTextFormat::ForegroundBrush)); // text in the palette's Text
		}
	}
	CHECK(heading);
	CHECK(link);
	QPalette light = Dark();
	light.setColor (QPalette::AlternateBase, QColor (0xf0, 0xf0, 0xf0));
	b.setPalette (light); // a theme change re-renders the page in the new colours
	heading = false;
	for (QTextBlock blk = b.document()->begin(); blk.isValid(); blk = blk.next())
		if (blk.text() == "Title") heading = blk.blockFormat().background().color() == QColor (0xf0, 0xf0, 0xf0);
	CHECK(heading);
}
