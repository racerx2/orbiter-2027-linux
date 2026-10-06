// custom: forms skins; unit tests of Copy... in the skin picker: names, folders, links, limits, all or nothing
#include <catch2/catch_test_macros.hpp>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <unistd.h>
#include "Custom/SkinCopy.h"

using namespace custom;

namespace {

	void Put (const QString &path, const QByteArray &text)
	{
		QDir ().mkpath (QFileInfo (path).absolutePath ());
		QFile f (path);
		REQUIRE(f.open (QIODevice::WriteOnly | QIODevice::Truncate));
		f.write (text);
	}

	QByteArray Get (const QString &path)
	{
		QFile f (path);
		return f.open (QIODevice::ReadOnly) ? f.readAll () : QByteArray ();
	}

	SkinManifest Skin (const QString &dir) { return ReadSkin (dir.toStdString ()); }

	QStringList Entries (const QString &dir) { return QDir (dir).entryList (QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System); }

	// a style sheet skin "Source" in <root>/Src
	QString MakeSource (const QString &root, const QByteArray &cfg = "Name = Source\nQss = a.qss\n")
	{
		const QString src = root + "/Src";
		Put (src + "/skin.cfg", cfg);
		Put (src + "/a.qss", "QWidget { color: red; }");
		Put (src + "/sub/img.png", "png");
		return src;
	}

}

TEST_CASE("CopySkin: the copy, its name, links inside kept, links out left out", "[skincopy]")
{
	QTemporaryDir tmp;
	REQUIRE(tmp.isValid ());
	const QString root = QFileInfo (tmp.path ()).canonicalFilePath ();
	const QString src = MakeSource (root);
	REQUIRE(QFile::link ("img.png", src + "/sub/alias.png"));
	REQUIRE(QFile::link ("sub", src + "/images"));
	REQUIRE(QFile::link ("/etc/hostname", src + "/out.png"));
	const SkinManifest m = Skin (src);
	REQUIRE(m.ok);

	SkinCopy c;
	REQUIRE(CopySkin (m, root, "  My   Copy! ", c));
	CHECK(c.id == "My_Copy_");
	CHECK(c.name == "My Copy!");
	const QString dst = root + "/" + c.id;
	const SkinManifest copy = Skin (dst);
	CHECK(copy.ok);
	CHECK(copy.name == "My Copy!");
	CHECK(Get (dst + "/a.qss") == "QWidget { color: red; }");
	CHECK(Get (dst + "/sub/img.png") == "png");
	CHECK(QFileInfo (dst + "/sub/alias.png").isSymLink ());
	CHECK(QFile::symLinkTarget (dst + "/sub/alias.png") == dst + "/sub/img.png");
	CHECK(QFileInfo (dst + "/images").isSymLink ());
	CHECK(QFileInfo (dst + "/images/img.png").canonicalFilePath () == dst + "/sub/img.png");
	CHECK_FALSE(QFileInfo (dst + "/out.png").exists ());
	CHECK_FALSE(QFileInfo (dst + "/out.png").isSymLink ());
	CHECK(c.msg.contains ("out.png"));
	for (const QString &e : Entries (root)) CHECK_FALSE(e.startsWith (".copy-"));
}

TEST_CASE("CopySkin: folders never collide, Classic is taken, names follow the folder", "[skincopy]")
{
	QTemporaryDir tmp;
	REQUIRE(tmp.isValid ());
	const QString root = QFileInfo (tmp.path ()).canonicalFilePath ();
	const SkinManifest m = Skin (MakeSource (root));
	REQUIRE(m.ok);

	SkinCopy a, b, k, e, l;
	REQUIRE(CopySkin (m, root, "My Copy", a));
	REQUIRE(CopySkin (m, root, "My Copy", b));
	CHECK(a.id == "My_Copy");
	CHECK(b.id == "My_Copy_2");
	CHECK(a.name == "My Copy");
	CHECK(b.name == "My Copy 2");
	CHECK(Skin (root + "/My_Copy_2").name == "My Copy 2");
	REQUIRE(CopySkin (m, root, "Classic", k));
	CHECK(k.id == "Classic_2");
	REQUIRE(CopySkin (m, root, "  ", e));
	CHECK(e.name == "My Source");
	REQUIRE(CopySkin (m, root, QString (300, 'x'), l));
	CHECK(l.name.size () == COPY_MAX_NAME);
	CHECK(l.id.size () == COPY_MAX_NAME);
}

TEST_CASE("CopySkin: every Name line, after a BOM, with CRLF", "[skincopy]")
{
	QTemporaryDir tmp;
	REQUIRE(tmp.isValid ());
	const QString root = QFileInfo (tmp.path ()).canonicalFilePath ();
	const SkinManifest m = Skin (MakeSource (root, "\xEF\xBB\xBFName = A\r\nQss = a.qss\r\n; Name = comment\r\nname=B\r\n"));
	REQUIRE(m.ok);
	REQUIRE(m.name == "B");
	SkinCopy c;
	REQUIRE(CopySkin (m, root, "New", c));
	const QByteArray cfg = Get (root + "/" + c.id + "/skin.cfg");
	CHECK(cfg == "Name = New\r\nQss = a.qss\r\n; Name = comment\r\nName = New\r\n");
	CHECK(Skin (root + "/" + c.id).name == "New");
}

TEST_CASE("CopySkin: a failure leaves nothing behind", "[skincopy]")
{
	QTemporaryDir tmp;
	REQUIRE(tmp.isValid ());
	const QString root = QFileInfo (tmp.path ()).canonicalFilePath ();
	const QString src = MakeSource (root);
	const SkinManifest m = Skin (src);
	REQUIRE(m.ok);
	SkinCopy c;

	SkinManifest none = m;
	none.dir.clear (); // QDir ("") is the working folder
	CHECK_FALSE(CopySkin (none, root, "X", c));
	CHECK(Entries (root) == QStringList {"Src"});

	if (geteuid () != 0) {
		Put (src + "/locked.png", "x");
		QFile::setPermissions (src + "/locked.png", QFileDevice::Permissions ());
		CHECK_FALSE(CopySkin (m, root, "Y", c));
		CHECK(c.msg.contains ("locked.png"));
		CHECK(Entries (root) == QStringList {"Src"});
		QFile::setPermissions (src + "/locked.png", QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	}

	for (int i = 0; i <= COPY_MAX_FILES; i++) Put (src + QString ("/many/%1.txt").arg (i), "");
	CHECK_FALSE(CopySkin (m, root, "Z", c));
	CHECK(c.msg.contains ("too big"));
	CHECK(Entries (root) == QStringList {"Src"});
}
