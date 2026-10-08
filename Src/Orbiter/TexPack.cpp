// not upstream: planet texture packs (Config/TexturePacks.cfg), the missing check and the installer

#include "TexPack.h"
#include "OrbiterAPI.h"
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <zlib.h>

namespace fs = std::filesystem;

static std::string Join (const std::string &dir, const std::string &name)
{
	if (dir.empty() || dir.back() == '/' || dir.back() == '\\') return dir + name;
	return dir + '/' + name;
}

static std::string Resolve (const std::string &path)
{
	return oapiResolvePath (path.c_str());
}

// one path component: no separators, control characters, "." or ".."
static bool Plain (const std::string &s)
{
	if (s.empty() || s == "." || s == "..") return false;
	for (char c : s) if (c == '/' || c == '\\' || (unsigned char)c < 32) return false;
	return true;
}

static bool HasFile (const std::string &dir)
{
	std::error_code ec;
	if (!fs::is_directory (dir, ec)) return false;
	for (fs::recursive_directory_iterator it (dir, ec), end; !ec && it != end; it.increment (ec))
		if (it->is_regular_file (ec)) return true;
	return false;
}

bool TexPackRead (const std::string &cfgfile, TexPackList &list, std::string &err)
{
	list = TexPackList();
	std::ifstream ifs (Resolve (cfgfile));
	if (!ifs) { err = "cannot read " + cfgfile; return false; }
	std::string line;
	for (int ln = 1; std::getline (ifs, line); ln++) {
		size_t c = line.find (';');
		if (c != std::string::npos) line.erase (c);
		std::istringstream is (line);
		std::string w;
		if (!(is >> w)) continue;
		if (w == "URL") {
			std::string eq;
			if (!(is >> eq >> list.url) || eq != "=") { err = "line " + std::to_string (ln) + ": bad URL"; return false; }
			while (!list.url.empty() && list.url.back() == '/') list.url.pop_back();
			continue;
		}
		TexPack p;
		p.zip = w;
		std::string extra;
		if (!(is >> p.body >> p.layer >> p.zipsize >> p.rawsize >> p.files >> p.md5) || (is >> extra) ||
			!Plain (p.zip) || !Plain (p.body) || !Plain (p.layer) || p.md5.size() != 32) {
			err = "line " + std::to_string (ln) + ": bad pack";
			return false;
		}
		list.pack.push_back (p);
	}
	if (list.url.empty()) { err = "no URL in " + cfgfile; return false; }
	return true;
}

bool TexPackPresent (const std::string &texdir, const std::string &body, const std::string &layer)
{
	std::string b = Join (texdir, body);
	if (HasFile (Resolve (Join (b, layer)))) return true;
	std::error_code ec;
	return fs::is_regular_file (Resolve (Join (Join (b, "Archive"), layer + ".tree")), ec); // ZTreeMgr archive
}

std::vector<TexPack> TexPackMissing (const TexPackList &list, const std::string &texdir)
{
	std::vector<TexPack> v;
	for (const TexPack &p : list.pack)
		if (!TexPackPresent (texdir, p.body, p.layer)) v.push_back (p);
	return v;
}

std::string TexPackWorkDir (const std::string &texdir)
{
	return Resolve (Join (texdir, ".texinstall"));
}

bool TexPackMd5 (const std::string &file, std::string &md5)
{
	QFile f (QFile::decodeName (file.c_str()));
	if (!f.open (QIODevice::ReadOnly)) return false;
	QCryptographicHash h (QCryptographicHash::Md5);
	if (!h.addData (&f)) return false;
	md5 = h.result().toHex().toStdString();
	return true;
}

// zip reader: stored or deflated entries, sizes and CRCs from the central directory, no zip64
struct ZipEntry {
	std::string name;
	uint32_t flags = 0, method = 0, crc = 0;
	uint64_t csize = 0, usize = 0, lho = 0;
};

static uint32_t U16 (const unsigned char *p) { return p[0] | p[1] << 8; }
static uint32_t U32 (const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static bool ReadAt (FILE *f, uint64_t ofs, void *buf, size_t n)
{
	return fseeko (f, (off_t)ofs, SEEK_SET) == 0 && fread (buf, 1, n, f) == n;
}

static bool ZipDir (FILE *f, std::vector<ZipEntry> &ents, std::string &err)
{
	err = "bad zip directory";
	if (fseeko (f, 0, SEEK_END)) return false;
	uint64_t n = (uint64_t)ftello (f);
	uint64_t tail = std::min<uint64_t> (n, 22 + 65535);
	std::vector<unsigned char> b (tail);
	if (tail < 22 || !ReadAt (f, n - tail, b.data(), tail)) return false;
	int64_t e = -1;
	for (int64_t i = (int64_t)tail - 22; i >= 0; i--)
		if (U32 (&b[i]) == 0x06054b50) { e = i; break; }
	if (e < 0) { err = "not a zip file"; return false; }
	uint32_t count = U16 (&b[e + 10]), cdsize = U32 (&b[e + 12]), cdofs = U32 (&b[e + 16]);
	if (count == 0xFFFF || cdsize == 0xFFFFFFFF || cdofs == 0xFFFFFFFF) { err = "zip64 not supported"; return false; }
	if ((uint64_t)cdofs + cdsize > n) return false;
	std::vector<unsigned char> cd (cdsize);
	if (cdsize && !ReadAt (f, cdofs, cd.data(), cdsize)) return false;
	size_t p = 0;
	for (uint32_t k = 0; k < count; k++) {
		if (p + 46 > cd.size() || U32 (&cd[p]) != 0x02014b50) return false;
		ZipEntry z;
		z.flags = U16 (&cd[p + 8]);
		z.method = U16 (&cd[p + 10]);
		z.crc = U32 (&cd[p + 16]);
		z.csize = U32 (&cd[p + 20]);
		z.usize = U32 (&cd[p + 24]);
		uint32_t nlen = U16 (&cd[p + 28]), xlen = U16 (&cd[p + 30]), clen = U16 (&cd[p + 32]);
		z.lho = U32 (&cd[p + 42]);
		if (p + 46 + nlen + xlen + clen > cd.size()) return false;
		if (z.csize == 0xFFFFFFFF || z.usize == 0xFFFFFFFF || z.lho == 0xFFFFFFFF) { err = "zip64 not supported"; return false; }
		z.name.assign ((const char*)&cd[p + 46], nlen);
		ents.push_back (z);
		p += 46 + nlen + xlen + clen;
	}
	err.clear();
	return true;
}

// writes one entry to dest, checking its size and CRC; done counts unpacked bytes
static bool ZipExtract (FILE *f, const ZipEntry &z, const std::string &dest, uint64_t &done, const TexPackProgress &progress, std::string &err)
{
	unsigned char lh[30];
	if (!ReadAt (f, z.lho, lh, 30) || U32 (lh) != 0x04034b50 || fseeko (f, (off_t)(z.lho + 30 + U16 (lh + 26) + U16 (lh + 28)), SEEK_SET)) {
		err = "bad zip entry " + z.name;
		return false;
	}
	FILE *o = fopen (dest.c_str(), "wb");
	if (!o) { err = "cannot write " + dest; return false; }
	static const size_t CHUNK = 1 << 18;
	std::vector<unsigned char> in (CHUNK), out (CHUNK);
	uint64_t left = z.csize, outn = 0;
	uLong crc = crc32 (0, Z_NULL, 0);
	z_stream zs = {};
	bool ok = (z.method == 0 ? z.csize == z.usize : inflateInit2 (&zs, -MAX_WBITS) == Z_OK);
	bool end = false;
	if (!ok) err = "bad zip entry " + z.name;
	while (ok && !end) {
		size_t n = (size_t)std::min<uint64_t> (left, CHUNK);
		if (n && fread (in.data(), 1, n, f) != n) { err = "cannot read " + z.name; ok = false; break; }
		left -= n;
		auto put = [&](const unsigned char *d, size_t k) {
			outn += k;
			if (outn > z.usize) { err = "bad size " + z.name; return false; }
			crc = crc32 (crc, d, (uInt)k);
			if (k && fwrite (d, 1, k, o) != k) { err = "cannot write " + dest; return false; }
			done += k;
			if (progress && !progress (done)) { err = "cancelled"; return false; }
			return true;
		};
		if (z.method == 0) {
			ok = put (in.data(), n);
			end = (left == 0);
			continue;
		}
		zs.next_in = in.data();
		zs.avail_in = (uInt)n;
		do {
			zs.next_out = out.data();
			zs.avail_out = (uInt)CHUNK;
			int r = inflate (&zs, Z_NO_FLUSH);
			if (r != Z_OK && r != Z_STREAM_END && !(r == Z_BUF_ERROR && zs.avail_out)) { err = "bad data " + z.name; ok = false; break; }
			ok = put (out.data(), CHUNK - zs.avail_out);
			if (r == Z_STREAM_END) end = true;
		} while (ok && !end && zs.avail_out == 0);
		if (ok && !end && left == 0 && zs.avail_in == 0) { err = "truncated " + z.name; ok = false; }
	}
	if (z.method == 8) inflateEnd (&zs);
	if (fclose (o) && ok) { err = "cannot write " + dest; ok = false; }
	if (ok && (outn != z.usize || crc != z.crc)) { err = "CRC mismatch " + z.name; ok = false; }
	return ok;
}

// rest of an entry name below Textures/<Body>/<Layer>/, checked one component at a time
static bool SafeRest (const std::string &rest)
{
	size_t i = 0;
	while (i < rest.size()) {
		size_t j = rest.find ('/', i);
		if (j == std::string::npos) j = rest.size();
		if (!Plain (rest.substr (i, j - i))) return false;
		i = j + 1;
	}
	return true;
}

// unpacks into a staging folder, then renames it to PlanetTexDir/<Body>/<Layer>
bool TexPackUnzip (const std::string &zipfile, const TexPack &p, const std::string &texdir, const TexPackProgress &progress, std::string &err)
{
	std::error_code ec;
	std::string stage = Join (TexPackWorkDir (texdir), p.body + "_" + p.layer);
	fs::remove_all (stage, ec);
	FILE *f = fopen (zipfile.c_str(), "rb");
	if (!f) { err = "cannot open " + zipfile; return false; }
	std::vector<ZipEntry> ents;
	bool ok = ZipDir (f, ents, err);
	std::string prefix = "Textures/" + p.body + "/" + p.layer + "/";
	uint64_t done = 0, nfiles = 0;
	for (size_t i = 0; ok && i < ents.size(); i++) {
		const ZipEntry &z = ents[i];
		if (z.name.compare (0, prefix.size(), prefix)) {
			bool parent = !z.name.empty() && z.name.back() == '/' && !prefix.compare (0, z.name.size(), z.name);
			if (parent) continue; // "Textures/", "Textures/<Body>/"
			err = "unexpected entry " + z.name;
			ok = false;
			break;
		}
		std::string rest = z.name.substr (prefix.size());
		bool isdir = rest.empty() || rest.back() == '/';
		if (isdir && !rest.empty()) rest.pop_back();
		if (!rest.empty() && !SafeRest (rest)) { err = "unsafe entry " + z.name; ok = false; break; }
		std::string dest = rest.empty() ? stage : Join (stage, rest);
		if (isdir) {
			fs::create_directories (dest, ec);
			if (ec) { err = "cannot create " + dest; ok = false; }
			continue;
		}
		if ((z.flags & 1) || (z.method != 0 && z.method != 8)) { err = "unsupported entry " + z.name; ok = false; break; }
		fs::create_directories (fs::path (dest).parent_path(), ec);
		if (ec) { err = "cannot create " + fs::path (dest).parent_path().string(); ok = false; break; }
		ok = ZipExtract (f, z, dest, done, progress, err);
		nfiles++;
	}
	fclose (f);
	if (ok && nfiles == 0) { err = "no files in " + p.zip; ok = false; }
	if (ok) {
		std::string target = Resolve (Join (Join (texdir, p.body), p.layer));
		if (fs::exists (fs::symlink_status (target, ec))) {
			if (!fs::is_directory (fs::symlink_status (target, ec)) || HasFile (target)) { err = target + " is in the way"; ok = false; }
			else fs::remove_all (target, ec); // an empty folder, no textures in it
		}
		if (ok) {
			fs::create_directories (fs::path (target).parent_path(), ec);
			ec.clear();
			fs::rename (stage, target, ec);
			if (ec) { err = "cannot move into " + target + ": " + ec.message(); ok = false; }
		}
	}
	if (!ok) fs::remove_all (stage, ec);
	return ok;
}

bool TexPackDownload (const std::string &url, const std::string &file, const TexPackProgress &progress, std::string &err)
{
	QFile out (QFile::decodeName (file.c_str()));
	if (!out.open (QIODevice::WriteOnly | QIODevice::Truncate)) { err = "cannot write " + file; return false; }
	QNetworkAccessManager nam;
	QNetworkRequest req {QUrl (QString::fromStdString (url))};
	req.setAttribute (QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	req.setHeader (QNetworkRequest::UserAgentHeader, "Orbiter-2027-Linux");
	req.setTransferTimeout (60000); // no data for a minute
	QNetworkReply *r = nam.get (req);
	bool cancelled = false, werr = false;
	uint64_t done = 0;
	auto take = [&]() {
		QByteArray b = r->readAll();
		if (b.isEmpty()) return;
		if (out.write (b) != b.size()) { werr = true; r->abort(); return; }
		done += b.size();
	};
	auto poll = [&]() {
		if (!cancelled && progress && !progress (done)) { cancelled = true; r->abort(); }
	};
	QEventLoop loop;
	QTimer tick; // cancel works while no data arrives
	tick.setInterval (200);
	QObject::connect (&tick, &QTimer::timeout, poll);
	QObject::connect (r, &QNetworkReply::readyRead, [&]() { take(); poll(); });
	QObject::connect (r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	tick.start();
	if (!r->isFinished()) loop.exec();
	tick.stop();
	if (!cancelled && !werr && r->error() == QNetworkReply::NoError) { take(); poll(); }
	int status = r->attribute (QNetworkRequest::HttpStatusCodeAttribute).toInt();
	bool ok = false;
	if (cancelled) err = "cancelled";
	else if (werr) err = "cannot write " + file;
	else if (r->error() != QNetworkReply::NoError) err = r->errorString().toStdString();
	else if (status && status != 200) err = "HTTP " + std::to_string (status);
	else ok = true;
	out.close();
	if (ok && out.error() != QFileDevice::NoError) { err = "cannot write " + file; ok = false; }
	if (!ok) out.remove();
	return ok;
}

bool TexPackInstall (const TexPackList &list, const TexPack &p, const std::string &texdir,
	const TexPackProgress &download, const TexPackProgress &unpack, std::string &err)
{
	std::error_code ec;
	std::string work = TexPackWorkDir (texdir);
	fs::create_directories (work, ec);
	if (ec) { err = "cannot create " + work + ": " + ec.message(); return false; }
	std::string zip = Join (work, p.zip + ".part");
	std::string md5;
	bool ok = TexPackDownload (list.url + "/" + p.zip, zip, download, err);
	if (ok && fs::file_size (zip, ec) != p.zipsize) { err = p.zip + ": wrong size"; ok = false; }
	if (ok && (!TexPackMd5 (zip, md5) || md5 != p.md5)) { err = p.zip + ": md5 mismatch"; ok = false; }
	if (ok) ok = TexPackUnzip (zip, p, texdir, unpack, err);
	fs::remove (zip, ec);
	fs::remove (work, ec); // only when empty
	return ok;
}
