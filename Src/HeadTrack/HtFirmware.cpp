// not upstream (head-track): ported from linuxtrack's qt_gui/hashing.cpp, hashing.h and spec.txt (MIT, LICENSE-linuxtrack)

#include "HtFirmware.h"
#include <QCryptographicHash>
#include <QByteArray>
#include <zlib.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

namespace ht {

const std::vector<FwSignature> &FirmwareSignatures ()
{
	static const std::vector<FwSignature> sig = {
		{"tir4.fw", 72743, 33248, "5aeebac5fd168abb71acd75fb89dc2b3", "bc4cbf49fa6d4e1c8006a5ce68193f0998ffe367"},
		{"tir5.fw", 36076, 50651, "a140a72d76517a3904d07ec31c1389fc", "dd01a9508d4874340e585fa4e42499cfed1845f5"},
		{"tir5v2.fw", 36145, 63093, "a2abd72e90decae4d3d4ba761b66e289", "de378aad436d0ff8d1affcfbd22ef530712c9237"},
	};
	return sig;
}

uint16_t FastHash::Hash (uint8_t val)
{
	buffer[index] = val;
	index = (index + 1) % length;
	uint16_t tmp = 0;
	for (int i = 0; i < length; ++i) {
		if (tmp & 0x8000) tmp ^= 3;
		tmp = (uint16_t)(tmp << 2);
		tmp ^= buffer[(index + i) % length];
	}
	return tmp;
}

namespace {

bool Matches (const char *data, int64_t n, const FwSignature &s)
{
	QByteArray b = QByteArray::fromRawData (data, (qsizetype)n);
	return QCryptographicHash::hash (b, QCryptographicHash::Md5).toHex () == s.md5 &&
	       QCryptographicHash::hash (b, QCryptographicHash::Sha1).toHex () == s.sha1;
}

bool WriteGz (const std::string &path, const char *data, int64_t n)
{
	std::string tmp = path + ".part";
	gzFile f = gzopen (tmp.c_str (), "wb9");
	if (!f) return false;
	bool ok = gzwrite (f, data, (unsigned)n) == (int)n;
	ok = gzclose (f) == Z_OK && ok;
	std::error_code ec;
	if (ok) fs::rename (tmp, path, ec);
	if (!ok || ec) { fs::remove (tmp, ec); return false; }
	return true;
}

bool ReadFile (const fs::path &p, std::vector<char> &buf, uintmax_t cap)
{
	std::error_code ec;
	uintmax_t sz = fs::file_size (p, ec);
	if (ec || sz > cap) return false;
	std::ifstream f (p, std::ios::binary);
	if (!f) return false;
	buf.resize ((size_t)sz);
	return (bool)f.read (buf.data (), (std::streamsize)sz);
}

bool ReadGz (const fs::path &p, std::vector<char> &buf, size_t cap)
{
	gzFile f = gzopen (p.c_str (), "rb");
	if (!f) return false;
	char tmp[65536];
	for (;;) {
		int n = gzread (f, tmp, sizeof tmp);
		if (n < 0) { gzclose (f); return false; }
		buf.insert (buf.end (), tmp, tmp + n);
		if (buf.size () > cap) { gzclose (f); return false; }
		if (n < (int)sizeof tmp) break;
	}
	gzclose (f);
	return true;
}

std::string Lower (std::string s)
{
	for (char &c : s) c = (char)std::tolower ((unsigned char)c);
	return s;
}

} // namespace

std::vector<FwFound> FindFirmware (const std::string &folder, const std::string &destDir, std::string &err)
{
	return FindFirmware (folder, destDir, err, FirmwareSignatures ());
}

std::vector<FwFound> FindFirmware (const std::string &folder, const std::string &destDir, std::string &err, const std::vector<FwSignature> &sigs)
{
	std::vector<FwFound> found;
	std::set<std::string> have;
	std::error_code ec;
	fs::create_directories (destDir, ec);
	auto take = [&] (const FwSignature &s, const char *data, const fs::path &from) {
		if (have.count (s.name)) return;
		if (!WriteGz (destDir + "/" + s.name + ".gz", data, s.size)) { err = "can't write " + destDir + "/" + s.name + ".gz"; return; }
		have.insert (s.name);
		found.push_back ({s.name, from.string ()});
	};
	fs::recursive_directory_iterator it (folder, fs::directory_options::skip_permission_denied, ec), end;
	if (ec) { err = folder + ": " + ec.message (); return found; }
	std::vector<char> buf;
	for (; it != end && have.size () < sigs.size (); it.increment (ec)) {
		if (ec) { ec.clear (); continue; }
		const fs::directory_entry &e = *it;
		if (e.is_symlink (ec)) { if (e.is_directory (ec)) it.disable_recursion_pending (); continue; }
		if (e.is_directory (ec)) {
			if (Lower (e.path ().filename ().string ()) == "windows") it.disable_recursion_pending (); // as upstream: a Windows tree's own folder
			continue;
		}
		if (!e.is_regular_file (ec)) continue;
		std::string name = Lower (e.path ().filename ().string ());
		buf.clear ();
		for (const auto &s : sigs) { // an extracted one, gzipped (a linuxtrack folder)
			if (name == Lower (std::string (s.name) + ".gz") && ReadGz (e.path (), buf, 1 << 20) &&
				(int64_t)buf.size () == s.size && Matches (buf.data (), s.size, s)) take (s, buf.data (), e.path ());
		}
		std::string ext = e.path ().extension ().string ();
		ext = Lower (ext);
		if (ext != ".dll" && ext != ".exe" && ext != ".dat" && ext != ".fw") continue;
		if (!ReadFile (e.path (), buf, (uintmax_t)512 << 20)) continue;
		FastHash fh;
		for (size_t i = 0; i < buf.size (); i++) {
			uint16_t h = fh.Hash ((uint8_t)buf[i]);
			if (i + 1 < (size_t)FastHash::length) continue;
			size_t start = i + 1 - FastHash::length;
			for (const auto &s : sigs) {
				if (h != s.fast || have.count (s.name) || start + (size_t)s.size > buf.size ()) continue;
				if (Matches (&buf[start], s.size, s)) take (s, &buf[start], e.path ());
			}
		}
	}
	return found;
}

} // namespace ht
