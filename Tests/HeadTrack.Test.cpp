// not upstream (head-track): the TrackIR's packets, blobs, pose, axes, firmware search and settings, without the device

#include "HtAxis.h"
#include "HtBlobs.h"
#include "HtFirmware.h"
#include "HtMath.h"
#include "HtPose.h"
#include "HtSettings.h"
#include "HtTirPacket.h"
#include <QCryptographicHash>
#include <zlib.h>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "catch2/catch_all.hpp"

using namespace ht;
namespace fs = std::filesystem;
using Catch::Approx;

namespace {

// a TrackIR 5 stripe as the camera sends it: every pixel weighs 1
void PutStripe (std::vector<unsigned char> &p, unsigned hstart, unsigned vline, unsigned points)
{
	unsigned sum = points, sum_x = points * (points - 1) / 2;
	p.push_back ((unsigned char)(hstart >> 2));
	p.push_back ((unsigned char)(((hstart & 3) << 6) | (vline >> 3)));
	p.push_back ((unsigned char)(((vline & 7) << 5) | (points >> 5)));
	p.push_back ((unsigned char)(((points & 0x1F) << 3) | ((sum_x >> 17) & 7)));
	p.push_back ((unsigned char)((sum_x >> 9) & 0xFF));
	p.push_back ((unsigned char)((sum_x >> 1) & 0xFF));
	p.push_back ((unsigned char)(((sum_x & 1) << 7) | ((sum >> 8) & 0x7F)));
	p.push_back ((unsigned char)(sum & 0xFF));
}

struct Square { unsigned x, y, n; }; // top left, side

std::vector<unsigned char> Tir5Packet (const std::vector<Square> &sq)
{
	std::vector<unsigned char> p = {0x00, 0x10, 0x05, 0xAA ^ 0x10 ^ 0x05}; // the header's bytes XOR to 0xAA
	for (unsigned line = 0; line < 480; line++)
		for (const Square &s : sq)
			if (line >= s.y && line < s.y + s.n) PutStripe (p, s.x, line, s.n);
	uint32_t ps = (uint32_t)(p.size () + 4 - 8);
	p.push_back ((unsigned char)(ps >> 24)), p.push_back ((unsigned char)(ps >> 16)), p.push_back ((unsigned char)(ps >> 8)), p.push_back ((unsigned char)ps);
	return p;
}

std::vector<Blob> Project (const Model &m, const double R[3][3], const double C[3], double f)
{
	std::vector<Blob> b;
	for (const double *p : {m.p0, m.p1, m.p2}) {
		double v[3], r[3];
		MakeVec (p, m.hc, v);
		MatrixTimesVec (R, v, r);
		double x = r[0] + C[0], y = r[1] + C[1], z = r[2] + C[2];
		Blob bl;
		bl.x = (float)(f * x / z), bl.y = (float)(f * y / z), bl.score = 30;
		b.push_back (bl);
	}
	return b;
}

} // namespace

TEST_CASE("TrackIR 5 packets become stripes and blobs", "[HeadTrack]")
{
	BlobFinder f;
	f.Prepare (640, 480, 1.0f);
	TirPacketParser parser (f);
	auto pkt = Tir5Packet ({{100, 50, 5}, {300, 200, 7}, {500, 400, 3}});
	size_t ptr = 0;
	REQUIRE(parser.Process (pkt.data (), &ptr, pkt.size ()));
	CHECK(ptr == pkt.size ());
	auto blobs = f.Finish (10, 4, 2500);
	REQUIRE(blobs.size () == 3);
	auto near = [&] (float cx, float cy, unsigned score) { // the centre as the finder gives it: from the image's centre, x mirrored
		for (const Blob &b : blobs)
			if (std::fabs (b.x - ((639 / 2.0f) - cx)) < 0.01f && std::fabs (b.y - ((479 / 2.0f) - cy)) < 0.01f) return b.score == score;
		return false;
	};
	CHECK(near (102.0f, 52.0f, 25));
	CHECK(near (303.0f, 203.0f, 49));
	CHECK(near (501.0f, 401.0f, 9));

	SECTION("a bad header or size is no frame") {
		auto bad = pkt;
		bad[0] ^= 1;
		ptr = 0;
		CHECK_FALSE(parser.Process (bad.data (), &ptr, bad.size ()));
		bad = pkt;
		bad[bad.size () - 1] ^= 1;
		ptr = 0;
		CHECK_FALSE(parser.Process (bad.data (), &ptr, bad.size ()));
	}
	SECTION("small blobs are left out") {
		ptr = 0;
		REQUIRE(parser.Process (pkt.data (), &ptr, pkt.size ()));
		CHECK(f.Finish (10, 10, 2500).size () == 2);
	}
	SECTION("a truncated read never reads past it") {
		for (size_t n = 0; n < pkt.size (); n += 7) {
			parser.Reset ();
			ptr = 0;
			parser.Process (pkt.data (), &ptr, n);
			CHECK(ptr <= n);
			f.Finish (10, 0, 2500);
		}
	}
}

TEST_CASE("Touching stripes join into one blob", "[HeadTrack]")
{
	BlobFinder f;
	f.Prepare (640, 480, 1.0f);
	// a U: two legs joined by a bar at the bottom, then a separate point
	for (unsigned l = 10; l < 15; l++) { f.AddStripe ({l, 100, 101, 1, 2, 2}); f.AddStripe ({l, 110, 111, 1, 2, 2}); }
	f.AddStripe ({15, 100, 111, 66, 12, 12});
	f.AddStripe ({40, 200, 202, 3, 3, 3});
	auto b = f.Finish (10, 1, 2500);
	REQUIRE(b.size () == 2);
	CHECK(b[0].score + b[1].score == 32 + 3);
}

TEST_CASE("The pose of a clip and a cap, from their projections", "[HeadTrack]")
{
	for (Model m : {ClipModel (40, 110, 30, 50, -100, -100, 50), CapModel (67, 54, 96, 160, 50)}) {
		PoseSolver s;
		REQUIRE(s.Init (m, 660.0));
		double I[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, C0[3] = {0, 0, 700};
		auto b0 = Project (m, I, C0, 660.0);
		std::swap (b0[0], b0[2]); // the camera's order is not the model's
		s.SortBlobs (b0);
		RawPose p;
		REQUIRE(s.Process (b0, true, p));
		CHECK(p.yaw == Approx (0).margin (0.05));
		CHECK(p.pitch == Approx (0).margin (0.05));
		CHECK(p.tx == Approx (0).margin (0.05));

		double R[3][3], C1[3] = {20, -10, 680};
		EulerToMatrix (8 * M_PI / 180, 15 * M_PI / 180, 4 * M_PI / 180, R);
		auto b1 = Project (m, R, C1, 660.0);
		std::swap (b1[1], b1[2]);
		s.SortBlobs (b1);
		REQUIRE(s.Process (b1, false, p));
		CHECK(p.pitch == Approx (8).margin (0.5));
		CHECK(p.yaw == Approx (15).margin (0.5));
		CHECK(p.roll == Approx (4).margin (0.5));
		CHECK(p.tx == Approx (20).margin (2));
		CHECK(p.ty == Approx (-10).margin (2));
		CHECK(p.tz == Approx (-20).margin (3));
	}
	PoseSolver bad;
	CHECK_FALSE(bad.Init (ClipModel (0, 0, 0, 0, 0, 0, 0)));
}

TEST_CASE("Axis curves and filters", "[HeadTrack]")
{
	AxisDef a = DefaultAxis (AX_YAW);   // 5x, 130 degrees, straight
	CHECK(ValOnAxis (a, 10) == Approx (50));
	CHECK(ValOnAxis (a, -10) == Approx (-50));
	CHECK(ValOnAxis (a, 40) == Approx (130));   // the limit
	a.inverted = true;
	CHECK(ValOnAxis (a, 10) == Approx (-50));
	a.inverted = false, a.deadzone = 0.5f;
	CHECK(ValOnAxis (a, 10) == Approx (0));     // inside the dead zone (50 of 130 is under half)
	CHECK(ValOnAxis (a, 26) == Approx (130));
	a.deadzone = 0, a.rcurv = 0.9f;             // steeper at first
	CHECK(ValOnAxis (a, 5) > 25);
	a.enabled = false;
	CHECK(ValOnAxis (a, 10) == 0);
	float y = 0;
	AxisDef f = DefaultAxis (AX_PITCH);
	float y1 = FilterAxis (f, AX_PITCH, 10, &y);
	CHECK(y1 > 0);
	CHECK(y1 < 10);
	for (int i = 0; i < 200; i++) FilterAxis (f, AX_PITCH, 10, &y);
	CHECK(y == Approx (10).margin (0.5));
}

TEST_CASE("Firmware found by its signature", "[HeadTrack]")
{
	fs::path dir = fs::temp_directory_path () / ("htfw" + std::to_string (::getpid ()));
	fs::remove_all (dir);
	fs::create_directories (dir / "TrackIR5" / "windows");
	std::mt19937 rng (7);
	std::vector<char> block (1000), junk (5000);
	for (char &c : block) c = (char)rng ();
	for (char &c : junk) c = (char)rng ();
	FastHash fh;
	uint16_t fast = 0;
	for (int i = 0; i < FastHash::length; i++) fast = fh.Hash ((uint8_t)block[i]);
	QByteArray qb (block.data (), (qsizetype)block.size ());
	std::string md5 = QCryptographicHash::hash (qb, QCryptographicHash::Md5).toHex ().toStdString ();
	std::string sha1 = QCryptographicHash::hash (qb, QCryptographicHash::Sha1).toHex ().toStdString ();
	std::vector<FwSignature> sigs = {{"test.fw", (int64_t)block.size (), fast, md5.c_str (), sha1.c_str ()}};
	auto put = [&] (const fs::path &p) {
		std::ofstream o (p, std::ios::binary);
		o.write (junk.data (), 777);
		o.write (block.data (), (std::streamsize)block.size ());
		o.write (junk.data (), (std::streamsize)junk.size ());
	};
	put (dir / "TrackIR5" / "windows" / "skipped.dll"); // a Windows tree's own folder is left out
	std::string err;
	CHECK(FindFirmware ((dir / "TrackIR5").string (), (dir / "out").string (), err, sigs).empty ());
	put (dir / "TrackIR5" / "TrackIR5.exe");
	auto found = FindFirmware ((dir / "TrackIR5").string (), (dir / "out").string (), err, sigs);
	REQUIRE(found.size () == 1);
	CHECK(found[0].name == "test.fw");
	gzFile g = gzopen ((dir / "out" / "test.fw.gz").c_str (), "rb");
	REQUIRE(g);
	std::vector<char> back (2000);
	int n = gzread (g, back.data (), (unsigned)back.size ());
	gzclose (g);
	REQUIRE(n == (int)block.size ());
	CHECK(std::equal (block.begin (), block.end (), back.begin ()));

	SECTION("an extracted one, gzipped, as linuxtrack keeps it") {
		fs::create_directories (dir / "ltr");
		fs::copy_file (dir / "out" / "test.fw.gz", dir / "ltr" / "test.fw.gz");
		CHECK(FindFirmware ((dir / "ltr").string (), (dir / "out2").string (), err, sigs).size () == 1);
	}
	fs::remove_all (dir);
}

TEST_CASE("Settings read and written", "[HeadTrack]")
{
	Settings s;
	s.enable = false, s.keysAdded = true, s.preset = MP_CUSTOM_CAP, s.active = false;
	s.cap[0] = 70, s.orientation = ORIENT_FLIP_X | ORIENT_FROM_BEHIND, s.align = false;
	s.tir.threshold = 120, s.tir.irBrightness = 6, s.tir.statusLed = false, s.tir.statusBrightness = 2, s.tir.minBlob = 5, s.tir.maxBlob = 900;
	s.axes[AX_ROLL].enabled = false, s.axes[AX_YAW].factor = 3.5f, s.axes[AX_TZ].rlimit = 40;
	s.cmode = 0x1F, s.vcRot = false, s.vcRotRange = 2.0, s.vcPos = true, s.vcPosRange = 0.3, s.vcRoll = false;
	s.freeze = true, s.freezeT = 1.5, s.trkRot = false, s.trkByPos = true, s.trkZoom = false, s.trkDead = 0.3, s.trkSpeed = 4;
	std::string text = s.Write () + "SOMETHING = kept\n";
	Settings r;
	REQUIRE(r.Read (text));
	CHECK_FALSE(r.enable);
	CHECK(r.keysAdded);
	CHECK(r.preset == MP_CUSTOM_CAP);
	CHECK_FALSE(r.active);
	CHECK(r.IrOn ());
	CHECK(r.cap[0] == 70);
	CHECK(r.orientation == (ORIENT_FLIP_X | ORIENT_FROM_BEHIND));
	CHECK_FALSE(r.align);
	CHECK(r.tir.threshold == 120);
	CHECK(r.tir.irBrightness == 6);
	CHECK_FALSE(r.tir.statusLed);
	CHECK(r.tir.statusBrightness == 2);
	CHECK(r.tir.minBlob == 5);
	CHECK(r.tir.maxBlob == 900);
	CHECK_FALSE(r.axes[AX_ROLL].enabled);
	CHECK(r.axes[AX_YAW].factor == Approx (3.5));
	CHECK(r.axes[AX_TZ].rlimit == Approx (40));
	CHECK(r.cmode == 0x1F);
	CHECK_FALSE(r.vcRot);
	CHECK(r.vcRotRange == Approx (2.0));
	CHECK(r.vcPosRange == Approx (0.3));
	CHECK_FALSE(r.vcRoll);
	CHECK(r.freeze);
	CHECK(r.freezeT == Approx (1.5));
	CHECK_FALSE(r.trkRot);
	CHECK(r.trkByPos);
	CHECK_FALSE(r.trkZoom);
	CHECK(r.trkDead == Approx (0.3));
	CHECK(r.trkSpeed == Approx (4));
	CHECK(r.Write ().find ("SOMETHING = kept") != std::string::npos);

	SECTION("upstream's file: its limits, the rest left at the defaults") {
		Settings u;
		REQUIRE(u.Read ("CMODE = 7\nVCROT = 1 471.238898\nVCPOS = 1 5.0\nFREEZE = 1 2.000000\n"));
		CHECK(u.cmode == 7);
		CHECK(u.vcRotRange == Approx (M_PI));
		CHECK(u.vcPosRange == Approx (2.0));
		CHECK(u.enable);
		CHECK(u.preset == MP_TRACKCLIP_PRO);
		CHECK_FALSE(u.IrOn ());
	}
}
