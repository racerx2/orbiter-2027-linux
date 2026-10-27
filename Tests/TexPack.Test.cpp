// not upstream: texture packs: the package list, the missing check, the zip installer and a file:// download

#include <catch2/catch_test_macros.hpp>
#include "TexPack.h"
#include <QCoreApplication>
#include <QUrl>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <unistd.h>
#include <zlib.h>

namespace fs = std::filesystem;

static void Put (std::string &b, uint64_t v, int n)
{
	for (int i = 0; i < n; i++) b += (char)((v >> (8*i)) & 0xFF);
}

struct ZipItem { std::string name, data; bool deflate = true; };

// minimal zip writer; crcfix != 0 stores a wrong CRC
static void WriteZip (const std::string &path, const std::vector<ZipItem> &items, uint32_t crcfix = 0)
{
	std::string out, cd;
	for (const ZipItem &it : items) {
		std::string comp = it.data;
		if (it.deflate) {
			z_stream zs = {};
			deflateInit2 (&zs, 6, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
			comp.resize (deflateBound (&zs, it.data.size()));
			zs.next_in = (Bytef*)it.data.data();
			zs.avail_in = (uInt)it.data.size();
			zs.next_out = (Bytef*)&comp[0];
			zs.avail_out = (uInt)comp.size();
			deflate (&zs, Z_FINISH);
			comp.resize (zs.total_out);
			deflateEnd (&zs);
		}
		uint32_t crc = crc32 (0, (const Bytef*)it.data.data(), (uInt)it.data.size()) ^ crcfix;
		uint64_t ofs = out.size(), m = it.deflate ? 8 : 0;
		Put (out, 0x04034b50, 4); Put (out, 20, 2); Put (out, 0, 2); Put (out, m, 2); Put (out, 0, 4);
		Put (out, crc, 4); Put (out, comp.size(), 4); Put (out, it.data.size(), 4);
		Put (out, it.name.size(), 2); Put (out, 0, 2);
		out += it.name + comp;
		Put (cd, 0x02014b50, 4); Put (cd, 20, 2); Put (cd, 20, 2); Put (cd, 0, 2); Put (cd, m, 2); Put (cd, 0, 4);
		Put (cd, crc, 4); Put (cd, comp.size(), 4); Put (cd, it.data.size(), 4);
		Put (cd, it.name.size(), 2); Put (cd, 0, 2); Put (cd, 0, 2); Put (cd, 0, 2); Put (cd, 0, 2); Put (cd, 0, 4); Put (cd, ofs, 4);
		cd += it.name;
	}
	uint64_t cdofs = out.size();
	out += cd;
	Put (out, 0x06054b50, 4); Put (out, 0, 2); Put (out, 0, 2); Put (out, items.size(), 2); Put (out, items.size(), 2);
	Put (out, cd.size(), 4); Put (out, cdofs, 4); Put (out, 0, 2);
	std::ofstream (path, std::ios::binary) << out;
}

static std::string Read (const fs::path &p)
{
	std::ifstream f (p, std::ios::binary);
	std::stringstream s;
	s << f.rdbuf();
	return s.str();
}

static void Write (const fs::path &p, const std::string &s)
{
	fs::create_directories (p.parent_path());
	std::ofstream (p, std::ios::binary) << s;
}

// a fresh folder with a Textures/ inside; texdir spelled with a trailing '/'
struct Tmp {
	fs::path root, tex;
	std::string texdir;
	Tmp ()
	{
		static int n = 0;
		root = fs::temp_directory_path() / ("texpack-test-" + std::to_string (getpid()) + "-" + std::to_string (n++));
		fs::remove_all (root);
		tex = root / "Textures";
		fs::create_directories (tex);
		texdir = tex.string() + "/";
	}
	~Tmp () { std::error_code ec; fs::remove_all (root, ec); }
};

static std::string Pattern (size_t n)
{
	std::string s (n, '\0');
	for (size_t i = 0; i < n; i++) s[i] = (char)((i * 7 + i / 1000) & 0xFF);
	return s;
}

static TexPack Pack (const std::string &body, const std::string &layer)
{
	TexPack p;
	p.zip = body + "_" + layer + ".zip";
	p.body = body;
	p.layer = layer;
	return p;
}

TEST_CASE ("The package list reads the URL and one pack per line", "[TexPack]")
{
	Tmp t;
	std::string cfg = (t.root / "packs.cfg").string();
	TexPackList list;
	std::string err;

	Write (cfg, "; comment\nURL = file:///x/y/\n; zip body layer\nA_Surf.zip A Surf 10 20 3 0123456789abcdef0123456789abcdef ; note\n\n");
	REQUIRE (TexPackRead (cfg, list, err));
	CHECK (list.url == "file:///x/y");
	REQUIRE (list.pack.size() == 1);
	CHECK (list.pack[0].zip == "A_Surf.zip");
	CHECK (list.pack[0].body == "A");
	CHECK (list.pack[0].layer == "Surf");
	CHECK (list.pack[0].zipsize == 10);
	CHECK (list.pack[0].rawsize == 20);
	CHECK (list.pack[0].files == 3);

	Write (cfg, "URL = u\nA_Surf.zip A Surf 10 20\n");
	CHECK_FALSE (TexPackRead (cfg, list, err));
	CHECK (err == "line 2: bad pack");
	Write (cfg, "URL = u\nA_Surf.zip A Surf 10 20 3 0123456789abcdef0123456789abcdef extra\n");
	CHECK_FALSE (TexPackRead (cfg, list, err));
	Write (cfg, "URL = u\nA_Surf.zip ../A Surf 10 20 3 0123456789abcdef0123456789abcdef\n");
	CHECK_FALSE (TexPackRead (cfg, list, err));
	Write (cfg, "A_Surf.zip A Surf 10 20 3 0123456789abcdef0123456789abcdef\n");
	CHECK_FALSE (TexPackRead (cfg, list, err));
	CHECK (err.find ("no URL") == 0);
	CHECK_FALSE (TexPackRead ((t.root / "none.cfg").string(), list, err));
}

TEST_CASE ("The shipped package list names every layer once, in our release", "[TexPack]")
{
	TexPackList list;
	std::string err;
	REQUIRE (TexPackRead ("Config/TexturePacks.cfg", list, err));
	CHECK (list.url.find ("https://github.com/racerx2/Orbiter-linux-orbital-bodies-textures/releases/download/") == 0);
	CHECK (list.pack.size() == 40);
	std::set<std::string> seen;
	for (const TexPack &p : list.pack) {
		CHECK (p.zip == p.body + "_" + p.layer + (p.parts.empty() ? ".zip" : ".tree"));
		CHECK (seen.insert (p.body + "/" + p.layer).second);
		CHECK (p.zipsize > 0);
		CHECK (p.rawsize > 0);
		for (const TexPart &t : p.parts) CHECK (t.size <= 1073741824); // GitHub takes release files under 2 GiB
		CHECK (p.body != "Earth"); // Earth streams in (TileStream.cpp)
		CHECK (p.body != "Mars");  // mars-stream: so does Mars
	}
}

TEST_CASE ("A layer is present when its folder has files or its archive tree exists", "[TexPack]")
{
	Tmp t;
	CHECK_FALSE (TexPackPresent (t.texdir, "Moon", "Surf"));
	fs::create_directories (t.tex / "Moon" / "Surf" / "01");
	CHECK_FALSE (TexPackPresent (t.texdir, "Moon", "Surf"));
	Write (t.tex / "Moon" / "Surf" / "01" / "000000" / "000000.dds", "x");
	CHECK (TexPackPresent (t.texdir, "Moon", "Surf"));
	Write (t.tex / "Earth" / "Archive" / "Surf.tree", "x");
	CHECK (TexPackPresent (t.texdir, "Earth", "Surf"));
	CHECK_FALSE (TexPackPresent (t.texdir, "Earth", "Elev"));
	Write (t.tex / "mars" / "surf" / "01" / "a.dds", "x");
	CHECK (TexPackPresent (t.texdir, "Mars", "Surf")); // case-insensitive, as the client finds it

	TexPackList list;
	list.pack = {Pack ("Moon", "Surf"), Pack ("Moon", "Elev"), Pack ("Earth", "Surf"), Pack ("Mars", "Surf")};
	std::vector<TexPack> miss = TexPackMissing (list, t.texdir);
	REQUIRE (miss.size() == 1);
	CHECK (miss[0].zip == "Moon_Elev.zip");
}

TEST_CASE ("A pack unzips into PlanetTexDir/<Body>/<Layer>", "[TexPack]")
{
	Tmp t;
	std::string big = Pattern (600000), small = "stored bytes";
	std::string zip = (t.root / "Moon_Surf.zip").string();
	WriteZip (zip, {{"Textures/", "", false}, {"Textures/Moon/", "", false},
		{"Textures/Moon/Surf/01/000000/000000.dds", big, true}, {"Textures/Moon/Surf/02/000000/000001.dds", small, false}});
	fs::create_directories (t.tex / "Moon" / "Surf" / "empty"); // a folder with no files is replaced
	uint64_t last = 0;
	std::string err;
	REQUIRE (TexPackUnzip (zip, Pack ("Moon", "Surf"), t.texdir, [&](uint64_t d) { last = d; return true; }, err));
	CHECK (Read (t.tex / "Moon" / "Surf" / "01" / "000000" / "000000.dds") == big);
	CHECK (Read (t.tex / "Moon" / "Surf" / "02" / "000000" / "000001.dds") == small);
	CHECK_FALSE (fs::exists (t.tex / "Moon" / "Surf" / "empty"));
	CHECK (last == big.size() + small.size());
	CHECK_FALSE (fs::exists (t.tex / ".texinstall" / "Moon_Surf"));
	CHECK (TexPackPresent (t.texdir, "Moon", "Surf"));
}

TEST_CASE ("A bad pack installs nothing", "[TexPack]")
{
	Tmp t;
	std::string zip = (t.root / "Moon_Surf.zip").string();
	std::string err;
	TexPack p = Pack ("Moon", "Surf");
	auto none = [&]() { return !fs::exists (t.tex / "Moon" / "Surf") && !fs::exists (t.tex / ".texinstall" / "Moon_Surf"); };

	WriteZip (zip, {{"Textures/Moon/Surf/a.dds", "a"}, {"Textures/Earth/Surf/b.dds", "b"}});
	CHECK_FALSE (TexPackUnzip (zip, p, t.texdir, nullptr, err));
	CHECK (err == "unexpected entry Textures/Earth/Surf/b.dds");
	CHECK (none());

	WriteZip (zip, {{"Textures/Moon/Surf/a.dds", "a"}, {"Textures/Moon/Surf/../../../../evil", "e"}});
	CHECK_FALSE (TexPackUnzip (zip, p, t.texdir, nullptr, err));
	CHECK (err.find ("unsafe entry") == 0);
	CHECK (none());
	CHECK_FALSE (fs::exists (t.root / "evil"));
	CHECK_FALSE (fs::exists (t.root.parent_path() / "evil"));

	WriteZip (zip, {{"/etc/evil", "e"}});
	CHECK_FALSE (TexPackUnzip (zip, p, t.texdir, nullptr, err));
	CHECK (none());

	WriteZip (zip, {{"Textures/Moon/Surf/a.dds", Pattern (1000)}}, 1);
	CHECK_FALSE (TexPackUnzip (zip, p, t.texdir, nullptr, err));
	CHECK (err == "CRC mismatch Textures/Moon/Surf/a.dds");
	CHECK (none());

	WriteZip (zip, {{"Textures/Moon/Surf/a.dds", Pattern (600000)}});
	CHECK_FALSE (TexPackUnzip (zip, p, t.texdir, [](uint64_t) { return false; }, err));
	CHECK (err == "cancelled");
	CHECK (none());

	WriteZip (zip, {});
	CHECK_FALSE (TexPackUnzip (zip, p, t.texdir, nullptr, err));
	CHECK (none());

	Write (zip, "not a zip");
	CHECK_FALSE (TexPackUnzip (zip, p, t.texdir, nullptr, err));
	CHECK (none());

	Write (t.tex / "Moon" / "Surf" / "mine.dds", "keep");
	WriteZip (zip, {{"Textures/Moon/Surf/a.dds", "a"}});
	CHECK_FALSE (TexPackUnzip (zip, p, t.texdir, nullptr, err));
	CHECK (err.find ("is in the way") != std::string::npos);
	CHECK (Read (t.tex / "Moon" / "Surf" / "mine.dds") == "keep");
	CHECK_FALSE (fs::exists (t.tex / "Moon" / "Surf" / "a.dds"));
}

TEST_CASE ("md5 of a file", "[TexPack]")
{
	Tmp t;
	Write (t.root / "abc", "abc");
	std::string md5;
	REQUIRE (TexPackMd5 ((t.root / "abc").string(), md5));
	CHECK (md5 == "900150983cd24fb0d6963f7d28e17f72");
	CHECK_FALSE (TexPackMd5 ((t.root / "none").string(), md5));
}

TEST_CASE ("A pack downloads, is checked and installs", "[TexPack]")
{
	int argc = 1;
	char arg0[] = "TexPack.Test", *argv[] = {arg0, nullptr};
	QCoreApplication app (argc, argv); // QNetworkAccessManager needs one

	Tmp t;
	fs::path srv = t.root / "srv";
	fs::create_directories (srv);
	std::string data = Pattern (700000);
	WriteZip ((srv / "Io_Surf.zip").string(), {{"Textures/Io/Surf/01/000000/000000.dds", data}});
	TexPackList list;
	list.url = QUrl::fromLocalFile (QString::fromStdString (srv.string())).toString().toStdString();
	TexPack p = Pack ("Io", "Surf");
	p.zipsize = fs::file_size (srv / "Io_Surf.zip");
	p.rawsize = data.size();
	REQUIRE (TexPackMd5 ((srv / "Io_Surf.zip").string(), p.md5));
	list.pack = {p};
	std::string err;

	TexPack bad = p;
	bad.md5 = std::string (32, '0');
	CHECK_FALSE (TexPackInstall (list, bad, t.texdir, nullptr, nullptr, err));
	CHECK (err == "Io_Surf.zip: md5 mismatch");
	CHECK_FALSE (fs::exists (t.tex / "Io" / "Surf"));
	CHECK_FALSE (fs::exists (t.tex / ".texinstall"));

	TexPack gone = Pack ("Ganymede", "Surf");
	CHECK_FALSE (TexPackInstall (list, gone, t.texdir, nullptr, nullptr, err));
	CHECK_FALSE (fs::exists (t.tex / "Ganymede"));

	uint64_t got = 0, put = 0;
	REQUIRE (TexPackInstall (list, p, t.texdir, [&](uint64_t d) { got = d; return true; }, [&](uint64_t d) { put = d; return true; }, err));
	CHECK (Read (t.tex / "Io" / "Surf" / "01" / "000000" / "000000.dds") == data);
	CHECK (got == p.zipsize);
	CHECK (put == p.rawsize);
	CHECK_FALSE (fs::exists (t.tex / ".texinstall")); // zip and work folder gone
	CHECK (TexPackMissing (list, t.texdir).empty());
}

TEST_CASE ("Archive parts in the package list form one pack", "[TexPack]")
{
	Tmp t;
	std::string cfg = (t.root / "packs.cfg").string(), m (32, 'a');
	TexPackList list;
	std::string err;

	Write (cfg, "URL = u\nA_Surf.zip A Surf 10 20 3 " + m + "\nE_Surf.tree.01 E Surf 4 10 3 " + m +
		"\nE_Surf.tree.02 E Surf 4 10 3 " + m + "\nE_Surf.tree.03 E Surf 2 10 3 " + m + "\n");
	REQUIRE (TexPackRead (cfg, list, err));
	REQUIRE (list.pack.size() == 2);
	const TexPack &a = list.pack[1];
	CHECK (a.zip == "E_Surf.tree");
	CHECK (a.body == "E");
	CHECK (a.layer == "Surf");
	CHECK (a.zipsize == 10);
	CHECK (a.rawsize == 10);
	REQUIRE (a.parts.size() == 3);
	CHECK (a.parts[2].name == "E_Surf.tree.03");
	CHECK (a.parts[2].size == 2);
	CHECK (TexPackSpare (a) == 4);
	CHECK (list.pack[0].parts.empty());

	Write (cfg, "URL = u\nE_Surf.tree.01 E Surf 4 10 3 " + m + "\nE_Surf.tree.02 E Surf 4 10 3 " + m + "\n");
	CHECK_FALSE (TexPackRead (cfg, list, err));
	CHECK (err == "E_Surf.tree: parts missing");
	Write (cfg, "URL = u\nE_Surf.tree.01 E Surf 4 10 3 " + m + "\nE_Surf.tree.03 E Surf 4 10 3 " + m + "\n");
	CHECK_FALSE (TexPackRead (cfg, list, err));
	CHECK (err == "line 3: bad part");
	Write (cfg, "URL = u\nE_Surf.tree.01 E Surf 4 10 3 " + m + "\nE_Surf.tree.02 E Surf 4 10 3 " + m +
		"\nE_Surf.tree.03 E Surf 3 10 3 " + m + "\n");
	CHECK_FALSE (TexPackRead (cfg, list, err));
	Write (cfg, "URL = u\nE_Surf.tree.01 E Surf 4 10 2 " + m + "\nE_Surf.tree.02 E Elev 6 10 2 " + m + "\n");
	CHECK_FALSE (TexPackRead (cfg, list, err));
}

// an archive pack of data cut into parts, served from srv
static TexPack Archive (const fs::path &srv, const std::string &body, const std::string &layer, const std::string &data, const std::vector<size_t> &sizes)
{
	TexPack p;
	p.zip = body + "_" + layer + ".tree";
	p.body = body;
	p.layer = layer;
	p.rawsize = p.zipsize = data.size();
	p.files = sizes.size();
	size_t ofs = 0;
	for (size_t i = 0; i < sizes.size(); i++) {
		TexPart t;
		t.name = p.zip + (i < 9 ? ".0" : ".") + std::to_string (i + 1);
		t.size = sizes[i];
		Write (srv / t.name, data.substr (ofs, sizes[i]));
		REQUIRE (TexPackMd5 ((srv / t.name).string(), t.md5));
		p.parts.push_back (t);
		ofs += sizes[i];
	}
	return p;
}

TEST_CASE ("An archive pack downloads in parts into <Body>/Archive/<Layer>.tree and resumes", "[TexPack]")
{
	int argc = 1;
	char arg0[] = "TexPack.Test", *argv[] = {arg0, nullptr};
	QCoreApplication app (argc, argv);

	Tmp t;
	fs::path srv = t.root / "srv", tree = t.tex / "Earth" / "Archive" / "Surf.tree";
	std::string data = Pattern (700000), err;
	TexPack p = Archive (srv, "Earth", "Surf", data, {300000, 300000, 100000});
	TexPackList list;
	list.url = QUrl::fromLocalFile (QString::fromStdString (srv.string())).toString().toStdString();
	list.pack = {p};

	Write (t.tex / "Earth" / "Surf" / "01" / "000000" / "000000.dds", "low res"); // loose tiles don't count for an archive
	CHECK_FALSE (TexPackPresent (t.texdir, p));
	CHECK (TexPackMissing (list, t.texdir).size() == 1);

	TexPack bad = p;
	bad.parts[1].md5 = std::string (32, '0');
	CHECK_FALSE (TexPackInstall (list, bad, t.texdir, nullptr, nullptr, err));
	CHECK (err == "Earth_Surf.tree.02: md5 mismatch");
	CHECK_FALSE (fs::exists (tree));
	CHECK (TexPackResume (t.texdir, p) == 300000); // part 1 kept
	CHECK_FALSE (fs::exists (t.tex / ".texinstall"));

	CHECK_FALSE (TexPackInstall (list, p, t.texdir, [](uint64_t d) { return d < 450000; }, nullptr, err));
	CHECK (err == "cancelled");
	CHECK (TexPackResume (t.texdir, p) == 300000);

	Write (t.tex / "Earth" / "Archive" / ".Surf.tree.0123abcd.partial", "older release");
	Write (t.tex / "Earth" / "Archive" / ".Mask.tree.0123abcd.partial", "another layer");
	fs::remove (srv / "Earth_Surf.tree.01"); // a resumed install doesn't fetch it again
	uint64_t first = 0, last = 0;
	REQUIRE (TexPackInstall (list, p, t.texdir, [&](uint64_t d) { if (!first) first = d; last = d; return true; }, nullptr, err));
	CHECK (Read (tree) == data);
	CHECK (first > 300000);
	CHECK (last == data.size());
	CHECK (TexPackResume (t.texdir, p) == 0);
	CHECK (TexPackPresent (t.texdir, p));
	CHECK (TexPackMissing (list, t.texdir).empty());
	CHECK_FALSE (fs::exists (t.tex / ".texinstall"));
	CHECK_FALSE (fs::exists (t.tex / "Earth" / "Archive" / ".Surf.tree.0123abcd.partial")); // older release's partial cleared
	CHECK (fs::exists (t.tex / "Earth" / "Archive" / ".Mask.tree.0123abcd.partial")); // another layer's left alone
	fs::remove (t.tex / "Earth" / "Archive" / ".Mask.tree.0123abcd.partial");
	size_t n = 0;
	for (auto &e : fs::directory_iterator (t.tex / "Earth" / "Archive")) { (void)e; n++; }
	CHECK (n == 1); // no partial left

	CHECK_FALSE (TexPackInstall (list, p, t.texdir, nullptr, nullptr, err)); // an existing archive is never replaced
	CHECK (err.find ("is in the way") != std::string::npos);
	CHECK (Read (tree) == data);
}

TEST_CASE ("A part cut off midway is dropped from the partial archive", "[TexPack]")
{
	int argc = 1;
	char arg0[] = "TexPack.Test", *argv[] = {arg0, nullptr};
	QCoreApplication app (argc, argv);

	Tmp t;
	fs::path srv = t.root / "srv";
	std::string data = Pattern (500000), err;
	TexPack p = Archive (srv, "Moon", "Elev", data, {200000, 200000, 100000});
	TexPackList list;
	list.url = QUrl::fromLocalFile (QString::fromStdString (srv.string())).toString().toStdString();
	list.pack = {p};

	CHECK_FALSE (TexPackInstall (list, p, t.texdir, [](uint64_t d) { return d < 250000; }, nullptr, err));
	REQUIRE (TexPackResume (t.texdir, p) == 200000);
	for (auto &e : fs::directory_iterator (t.tex / "Moon" / "Archive"))
		std::ofstream (e.path(), std::ios::binary | std::ios::app) << std::string (5000, 'x'); // half a part, as after a crash
	CHECK (TexPackResume (t.texdir, p) == 200000);
	REQUIRE (TexPackInstall (list, p, t.texdir, nullptr, nullptr, err));
	CHECK (Read (t.tex / "Moon" / "Archive" / "Elev.tree") == data);
}

TEST_CASE ("Partial archives of packs no longer listed and the work folder are removed", "[TexPack]")
{
	Tmp t;
	TexPackList list;
	TexPack moon = Pack ("Moon", "Elev");
	moon.parts.push_back ({"Moon_Elev.tree.01", "0123456789abcdef0123456789abcdef", 1});
	list.pack.push_back (moon);
	Write (t.tex / "Earth" / "Archive" / ".Surf.tree.a8890486.partial", "x");
	Write (t.tex / "Earth" / "Archive" / "Surf.tree", "x");
	Write (t.tex / "Moon" / "Archive" / ".Elev.tree.01234567.partial", "x");
	Write (t.tex / ".texinstall" / "Earth_Surf.tree.01.part", "x");
	std::vector<std::string> gone = TexPackClean (list, t.texdir);
	CHECK (gone.size() == 2);
	CHECK_FALSE (fs::exists (t.tex / "Earth" / "Archive" / ".Surf.tree.a8890486.partial"));
	CHECK (fs::exists (t.tex / "Earth" / "Archive" / "Surf.tree")); // a whole archive is the user's
	CHECK (fs::exists (t.tex / "Moon" / "Archive" / ".Elev.tree.01234567.partial")); // still listed: it resumes
	CHECK_FALSE (fs::exists (t.tex / ".texinstall"));
}

TEST_CASE ("The kernel list groups the parts of each kernel", "[TexPack]")
{
	Tmp t;
	std::string cfg = (t.root / "kernels.cfg").string(), m (32, 'a');
	KernelPackList list;
	std::string err;

	Write (cfg, "; comment\nURL = file:///k/\na.bsp.01 a.bsp 4 6 2 " + m + "\na.bsp.02 a.bsp 2 6 2 " + m + " ; note\nb.bsp.01 b.bsp 3 3 1 " + m + "\n");
	REQUIRE (KernelPackRead (cfg, list, err));
	CHECK (list.url == "file:///k");
	REQUIRE (list.file.size() == 2);
	CHECK (list.file[0].zip == "a.bsp");
	CHECK (list.file[0].rawsize == 6);
	REQUIRE (list.file[0].parts.size() == 2);
	CHECK (list.file[0].parts[1].name == "a.bsp.02");
	CHECK (list.file[0].parts[1].size == 2);
	CHECK (list.file[1].zip == "b.bsp");

	Write (cfg, "URL = u\na.bsp.01 a.bsp 4 6 2 " + m + "\n");
	CHECK_FALSE (KernelPackRead (cfg, list, err));
	CHECK (err == "a.bsp: parts missing");
	Write (cfg, "URL = u\na.bsp.01 a.bsp 4 6 2 " + m + "\na.bsp.03 a.bsp 2 6 2 " + m + "\n");
	CHECK_FALSE (KernelPackRead (cfg, list, err));
	CHECK (err == "line 3: bad part");
	Write (cfg, "URL = u\nx.01 a.bsp 6 6 1 " + m + "\n");
	CHECK_FALSE (KernelPackRead (cfg, list, err));
	Write (cfg, "URL = u\n.a.bsp.01 .a.bsp 6 6 1 " + m + "\n");
	CHECK_FALSE (KernelPackRead (cfg, list, err));
	Write (cfg, "URL = u\na.bsp.01 ../a.bsp 6 6 1 " + m + "\n");
	CHECK_FALSE (KernelPackRead (cfg, list, err));
	Write (cfg, "a.bsp.01 a.bsp 6 6 1 " + m + "\n");
	CHECK_FALSE (KernelPackRead (cfg, list, err));
	CHECK (err.find ("no URL") == 0);
}

TEST_CASE ("The shipped kernel list has orbiter.bsp and kerberos_styx.bsp in our release", "[TexPack]")
{
	KernelPackList list;
	std::string err;
	REQUIRE (KernelPackRead ("Config/KernelPacks.cfg", list, err));
	CHECK (list.url.find ("https://github.com/racerx2/Orbiter-linux-orbital-bodies-textures/releases/download/") == 0);
	REQUIRE (list.file.size() == 2);
	CHECK (list.file[0].zip == "orbiter.bsp");
	CHECK (list.file[0].rawsize == 1637029888);
	CHECK (list.file[1].zip == "kerberos_styx.bsp");
	for (const TexPack &k : list.file)
		for (const TexPart &p : k.parts) CHECK (p.size <= 1073741824); // GitHub takes release files under 2 GiB
}

TEST_CASE ("A kernel downloads in parts into the kernel folder and resumes", "[TexPack]")
{
	int argc = 1;
	char arg0[] = "TexPack.Test", *argv[] = {arg0, nullptr};
	QCoreApplication app (argc, argv);

	Tmp t;
	fs::path srv = t.root / "srv", kdir = t.root / "Kernels", file = kdir / "a.bsp";
	std::string data = Pattern (700000), err, kd = kdir.string();
	TexPack k;
	k.zip = "a.bsp";
	k.rawsize = k.zipsize = data.size();
	size_t ofs = 0;
	for (size_t n : {300000, 300000, 100000}) {
		TexPart p;
		p.name = "a.bsp.0" + std::to_string (k.parts.size() + 1);
		p.size = n;
		Write (srv / p.name, data.substr (ofs, n));
		REQUIRE (TexPackMd5 ((srv / p.name).string(), p.md5));
		k.parts.push_back (p);
		ofs += n;
	}
	k.files = k.parts.size();
	KernelPackList list;
	list.url = QUrl::fromLocalFile (QString::fromStdString (srv.string())).toString().toStdString();
	list.file = {k};
	CHECK_FALSE (KernelPackPresent (kd, k));

	TexPack bad = k;
	bad.parts[1].md5 = std::string (32, '0');
	CHECK_FALSE (KernelPackInstall (list, bad, kd, nullptr, err));
	CHECK (err == "a.bsp.02: md5 mismatch");
	CHECK_FALSE (fs::exists (file));
	CHECK (KernelPackResume (kd, k) == 300000);
	CHECK_FALSE (fs::exists (kdir / ".kernelinstall"));

	CHECK_FALSE (KernelPackInstall (list, k, kd, [](uint64_t d) { return d < 450000; }, err));
	CHECK (err == "cancelled");
	CHECK (KernelPackResume (kd, k) == 300000);

	Write (kdir / ".a.bsp.0123abcd.partial", "older release");
	Write (kdir / ".b.bsp.0123abcd.partial", "another kernel");
	fs::remove (srv / "a.bsp.01"); // a resumed install doesn't fetch it again
	uint64_t first = 0, last = 0;
	REQUIRE (KernelPackInstall (list, k, kd, [&](uint64_t d) { if (!first) first = d; last = d; return true; }, err));
	CHECK (Read (file) == data);
	CHECK (first > 300000);
	CHECK (last == data.size());
	CHECK (KernelPackPresent (kd, k));
	CHECK (KernelPackResume (kd, k) == 0);
	CHECK_FALSE (fs::exists (kdir / ".kernelinstall"));
	CHECK_FALSE (fs::exists (kdir / ".a.bsp.0123abcd.partial"));
	CHECK (fs::exists (kdir / ".b.bsp.0123abcd.partial"));

	CHECK_FALSE (KernelPackInstall (list, k, kd, nullptr, err)); // an installed kernel is never replaced
	CHECK (err.find ("is in the way") != std::string::npos);
}
