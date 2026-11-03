// not upstream (head-track): ported from linuxtrack's tir_hw.c, tir_img.c's frame reading and tir_driver.c (MIT, LICENSE-linuxtrack); TrackIR 4 and 5 only

#include "HtTirDevice.h"
#include <zlib.h>
#include <chrono>
#include <cstring>
#include <thread>

namespace ht {

namespace {

constexpr int TIR_CONFIGURATION = 1, TIR_INTERFACE = 0;
constexpr int TIR_OUT_EP = 0x01, TIR_IN_EP = 0x82;
constexpr unsigned char TIR_LED_RED = 0x10, TIR_LED_GREEN = 0x20, TIR_LED_BLUE = 0x40, TIR_LED_IR = 0x80;
constexpr size_t TIR_PACKET_SIZE = 16384;

// known packets
const unsigned char Video_off[] = {0x14, 0x01};
const unsigned char Video_on[] = {0x14, 0x00};
const unsigned char Fifo_flush[] = {0x12};
const unsigned char Camera_stop[] = {0x13};
const unsigned char Fpga_init[] = {0x1b};
const unsigned char Cfg_reload[] = {0x20};
const unsigned char Get_status[] = {0x1d};
const unsigned char Get_conf[] = {0x17};
const unsigned char Precision_mode[] = {0x19, 0x03, 0x10, 0x00, 0x05};
// unknown ones
const unsigned char unk_2[] = {0x12, 0x01};
const unsigned char unk_3[] = {0x13, 0x01};
const unsigned char unk_7[] = {0x19, 0x05, 0x10, 0x10, 0x00};

double NowMs ()
{
	return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now ().time_since_epoch ()).count ();
}

} // namespace

const char *TirModelName (TirModel m)
{
	switch (m) {
	case TIR_4: return "TrackIR 4";
	case TIR_5: return "TrackIR 5";
	case TIR_5V2: return "TrackIR 5 (revision 2)";
	case TIR_5V3: return "TrackIR 5 (revision 3)";
	default: return "no TrackIR";
	}
}

const char *TirFirmwareFile (TirModel m)
{
	switch (m) {
	case TIR_4: return "tir4.fw.gz";
	case TIR_5: return "tir5.fw.gz";
	case TIR_5V2: return "tir5v2.fw.gz";
	default: return nullptr;
	}
}

void TirDevice::Sleep (int usec)
{
	std::this_thread::sleep_for (std::chrono::microseconds (usec));
}

bool TirDevice::ReadStatus (Status &s)
{
	size_t t = 0;
	double t1 = NowMs ();
	for (int counter = 0; counter < 20; counter++) {
		if (!Send (Get_status, sizeof Get_status)) return false;
		for (;;) {
			if (!usb.Receive (in_ep, packet.data (), packet.size (), &t, 500)) return false;
			if (t > 6 && packet[0] == 0x07 && packet[1] == 0x20) break; // not upstream: 7 bytes read below (upstream: t > 2)
			double t2 = NowMs ();
			if (t2 - t1 > 100) { t1 = t2; break; } // asked again
		}
		if (t > 6 && packet[0] == 0x07 && packet[1] == 0x20) break;
	}
	if (!(t > 6 && packet[0] == 0x07 && packet[1] == 0x20)) { err = "the device gave no status"; return false; } // not upstream: upstream read the last packet as one
	s.fw_loaded = packet[3] == 1;
	s.cfg_flag = packet[6];
	s.fw_cksum = (((unsigned int)packet[4]) << 8) + (unsigned int)packet[5];
	return true;
}

bool TirDevice::ReadRomData ()
{
	size_t t = 0;
	do usb.Receive (in_ep, packet.data (), packet.size (), &t, 100); while (t > 0); // flush
	double t1 = NowMs ();
	for (int counter = 0; counter < 10; counter++) {
		if (!Send (Get_conf, sizeof Get_conf)) return false;
		for (;;) {
			if (!usb.Receive (in_ep, packet.data (), packet.size (), &t, 500)) return false;
			if (t > 2 && packet[1] == 0x40) break; // TrackIR 4 and 5: 0x14
			double t2 = NowMs ();
			if (t2 - t1 > 100) { t1 = t2; break; }
		}
		if (t > 2 && packet[1] == 0x40) break;
	}
	return true;
}

bool TirDevice::LoadFirmware (Firmware &fw, const std::string &path)
{
	gzFile f = gzopen (path.c_str (), "rb");
	if (!f) return false;
	unsigned char buf[50000];
	for (;;) {
		int n = gzread (f, buf, sizeof buf);
		if (n < 0) { gzclose (f); return false; }
		fw.data.insert (fw.data.end (), buf, buf + n);
		if (n < (int)sizeof buf) break;
	}
	gzclose (f);
	if (fw.data.empty ()) return false;
	unsigned int cksum = 0;
	for (unsigned char c : fw.data) {
		unsigned int byte = c;
		cksum += byte;
		byte <<= 4;
		cksum ^= byte;
	}
	fw.cksum = cksum & 0xffff;
	return true;
}

bool TirDevice::UploadFirmware (const Firmware &fw)
{
	if (!Send (Fpga_init, sizeof Fpga_init)) return false;
	unsigned char buf[62];
	buf[0] = 0x1c;
	size_t left = fw.data.size (), at = 0;
	while (left > 0) {
		unsigned char len = (unsigned char)(left > 60 ? 60 : left);
		left -= len;
		buf[1] = len;
		std::memcpy (&buf[2], &fw.data[at], len);
		at += len;
		if (!Send (buf, len + 2u)) return false;
	}
	return true;
}

bool TirDevice::WiggleLeds (unsigned char leds, unsigned char mask)
{
	unsigned char msg[3] = {0x10, leds, mask};
	if (!Send (msg, sizeof msg)) return false;
	Sleep (2000);
	return true;
}

bool TirDevice::FlushFifo ()
{
	if (model == TIR_5 || model == TIR_5V2) Sleep (100000);
	return Send (Fifo_flush, sizeof Fifo_flush);
}

void TirDevice::SetThreshold (int val)
{
	if (model == TIR_5V3) { ThresholdV3 ((unsigned int)val); return; }
	if (val > 253) val = 253;
	if (val < 30) val = 30;
	unsigned char pkt[] = {0x15, (unsigned char)val, 0x01, 0x00};
	Send (pkt, sizeof pkt);
}

bool TirDevice::StatusLedTir5 (bool running)
{
	unsigned char pkt[] = {0x19, 0x04, 0x10, (unsigned char)prm.statusBrightness, (unsigned char)(running ? 0x22 : 0x33)};
	return Send (pkt, sizeof pkt);
}

bool TirDevice::StatusLedTir4 (bool running)
{
	if (running) LedOff (TIR_LED_RED), LedOn (TIR_LED_GREEN);
	else LedOff (TIR_LED_GREEN), LedOn (TIR_LED_RED);
	return true;
}

bool TirDevice::ControlStatusLed (bool s1, bool s2)
{
	unsigned char pkt[] = {0x19, 0x04, 0x10, (unsigned char)prm.statusBrightness, (unsigned char)((s1 ? 0x20 : 0) | (s2 ? 0x02 : 0))};
	return Send (pkt, sizeof pkt);
}

bool TirDevice::ControlIrLed (bool ir)
{
	unsigned char pkt[] = {0x19, 0x09, 0x10, (unsigned char)(ir ? prm.irBrightness : 0), (unsigned char)(ir ? 0x01 : 0)};
	return Send (pkt, sizeof pkt);
}

bool TirDevice::SetExposure (unsigned int exp)
{
	unsigned char h[] = {0x23, 0x42, 0x08, (unsigned char)(exp >> 8), 0x00, 0x00};
	unsigned char l[] = {0x23, 0x42, 0x10, (unsigned char)(exp & 0xFF), 0x00, 0x00};
	return Send (h, sizeof h) && Send (l, sizeof l);
}

void TirDevice::StopTir4 ()
{
	Send (Video_off, sizeof Video_off);
	FlushFifo ();
	LedOff (TIR_LED_IR);
	LedOff (TIR_LED_RED);
	LedOff (TIR_LED_GREEN);
	LedOff (TIR_LED_BLUE);
	Send (Camera_stop, sizeof Camera_stop);
	Sleep (50000);
}

void TirDevice::StopTir5 ()
{
	unsigned char off[] = {0x19, 0x04, 0x10, 0x00, 0x00}; // the status LED off
	Send (off, sizeof off);
	ControlIrLed (false);
	Sleep (50000);
	for (int i = 0; i < 5; i++) Send (Video_off, sizeof Video_off), Sleep (50000);
	Send (unk_2, sizeof unk_2);
	Send (unk_3, sizeof unk_3);
	for (int i = 0; i < 5; i++) Send (Video_off, sizeof Video_off), Sleep (50000);
	FlushFifo ();
	Send (Camera_stop, sizeof Camera_stop);
	Send (off, sizeof off);
	Sleep (50000);
	Send (off, sizeof off);
	ControlIrLed (false);
	LedOff (TIR_LED_IR);
}

void TirDevice::StartTir4 ()
{
	StopTir4 ();
	Send (Video_on, sizeof Video_on);
	if (prm.irOn) LedOn (TIR_LED_IR);
	FlushFifo ();
	if (prm.statusLed) StatusLedTir4 (true);
}

void TirDevice::StartTir5 ()
{
	StopTir5 ();
	Send (Video_on, sizeof Video_on);
	ControlIrLed (prm.irOn);
	if (prm.statusLed) StatusLedTir5 (true);
}

TirDevice::Result TirDevice::InitTir4 (const std::string &fwPath)
{
	StopTir4 ();
	size_t t = 0;
	do usb.Receive (in_ep, packet.data (), packet.size (), &t, 100); while (t > 0);
	if (!ReadRomData ()) return FAILED;
	Status status;
	if (!ReadStatus (status)) return FAILED;
	Firmware fw;
	if (!LoadFirmware (fw, fwPath)) { err = "no firmware: " + fwPath; return NO_FIRMWARE; }
	if (!status.fw_loaded || status.fw_cksum != fw.cksum) UploadFirmware (fw);
	if (!ReadStatus (status)) return FAILED;
	if (status.fw_cksum != fw.cksum) { err = "the firmware didn't load"; return FAILED; }
	if (status.cfg_flag == 1) {
		Send (Cfg_reload, sizeof Cfg_reload);
		for (int n = 0; status.cfg_flag != 2; n++) { // not upstream: asked 50 times at most (upstream: until it answers)
			if (n >= 50 || !ReadStatus (status)) { err = "the device didn't take its configuration"; return FAILED; }
		}
		SetThreshold (0x82);
	} else if (status.cfg_flag != 2) {
		err = "the device's configuration is wrong";
		return FAILED;
	}
	return OK;
}

TirDevice::Result TirDevice::InitTir5 (const std::string &fwPath)
{
	StopTir5 ();
	ReadRomData ();
	ControlIrLed (true);
	FlushFifo ();
	SetExposure (0x18F);
	Status status;
	ReadStatus (status);
	Firmware fw;
	if (!LoadFirmware (fw, fwPath)) { err = "no firmware: " + fwPath; return NO_FIRMWARE; }
	UploadFirmware (fw);
	Send (unk_7, sizeof unk_7);
	FlushFifo ();
	Send (Camera_stop, sizeof Camera_stop);
	ReadStatus (status);
	Send (Cfg_reload, sizeof Cfg_reload);
	SetThreshold (0x96);
	SetExposure (0x18F);
	ControlIrLed (true);
	ControlStatusLed (false, false);
	ControlStatusLed (false, false);
	Send (Precision_mode, sizeof Precision_mode);
	ControlStatusLed (false, false);
	ControlIrLed (true);
	SetThreshold (0x76);
	SetExposure (0x15E);
	ControlStatusLed (false, false);
	Send (Precision_mode, sizeof Precision_mode);
	FlushFifo ();
	Send (Camera_stop, sizeof Camera_stop);
	ControlIrLed (true);
	Send (Video_on, sizeof Video_on);
	return OK;
}

void TirDevice::CloseTir5 ()
{
	StopTir5 ();
	SetThreshold (0x96);
	SetExposure (0x18F);
	ControlIrLed (true);
	ControlStatusLed (false, false);
	ControlStatusLed (false, false);
	Send (Precision_mode, sizeof Precision_mode);
	LedOff (TIR_LED_IR);
	Send (Fpga_init, sizeof Fpga_init);
}

// ----- TrackIR 5 revision 3

int TirDevice::RandRange (int l, int h)
{
	if (h <= l) return h;
	return std::uniform_int_distribution<int> (l, h) (rng);
}

bool TirDevice::SendV3 (unsigned char p[24], int wait)
{
	uint8_t r1 = (uint8_t)((RandRange (1, 15) << 4) + RandRange (1, 15));
	p[0] ^= 0x69 ^ p[r1 >> 4] ^ p[r1 & 0x0F];
	p[17] = r1;
	if (!usb.Send (out_ep, p, 24)) return false;
	if (wait > 0) Sleep (wait);
	return true;
}

bool TirDevice::SendV3_4 (uint8_t v1, uint8_t v2, uint8_t v3, uint8_t v4, int wait)
{
	unsigned char p[24];
	p[0] = v1;
	for (int i = 1; i < 24; ++i) p[i] = (unsigned char)rng ();
	uint8_t r1 = (uint8_t)RandRange (6, 14);
	p[1] ^= (p[1] ^ r1) & 8;
	p[2] ^= (p[2] ^ r1) & 4;
	p[3] ^= (p[3] ^ r1) & 2;
	p[4] ^= (p[4] ^ r1) & 1;
	p[r1] = p[16] ^ v2;
	p[r1 - 1] = p[19] ^ v3;
	p[r1 + 1] = p[18] ^ v4;
	return SendV3 (p, wait);
}

bool TirDevice::SendV3_2 (uint8_t v1, uint8_t v2, int wait)
{
	unsigned char p[24];
	p[0] = v1;
	for (int i = 1; i < 24; ++i) p[i] = (unsigned char)rng ();
	uint8_t r1 = (uint8_t)RandRange (2, 14);
	uint8_t r2 = (uint8_t)RandRange (0, 3);
	p[1] ^= (p[1] ^ r1) & 8;
	p[2] ^= (p[2] ^ r1) & 4;
	p[3] ^= (p[3] ^ r1) & 2;
	p[4] ^= (p[4] ^ r1) & 1;
	p[r1] = (unsigned char)(((v2 << 4) | (p[r1] & 0x0F)) ^ (p[16] & 0xF0));
	p[r1 + 1] = (unsigned char)((r2 << 6) | (p[r1 + 1] & 0x3F));
	return SendV3 (p, wait);
}

bool TirDevice::SendV3_1 (uint8_t v1, int wait)
{
	unsigned char p[24];
	p[0] = v1;
	for (int i = 1; i < 24; ++i) p[i] = (unsigned char)rng ();
	return SendV3 (p, wait);
}

bool TirDevice::IrBrightnessRawV3 (unsigned int b)
{
	bool res = true;
	res &= SendV3_4 (0x23, 0x35, 0x02, b & 0xFF, 50000);
	res &= SendV3_4 (0x23, 0x35, 0x01, (b >> 8) & 0xFF, 50000);
	res &= SendV3_4 (0x23, 0x35, 0x00, 0x00, 50000);
	res &= SendV3_4 (0x23, 0x3B, 0x8F, (b >> 4) & 0xFF, 50000);
	res &= SendV3_4 (0x23, 0x3B, 0x8E, (b >> 12) & 0xFF, 50000);
	return res;
}

bool TirDevice::IrBrightnessV3 (unsigned int b)
{
	static const unsigned int value_map[] = {150, 250, 350};
	if (b < 5 || b > 7) return false;
	return IrBrightnessRawV3 (value_map[b - 5] << 4);
}

bool TirDevice::StatusLedV3 (bool running)
{
	return SendV3_4 (0x19, 0x04, (uint8_t)prm.statusBrightness, running ? 0x22 : 0x33, 50000);
}

bool TirDevice::ReadStatusV3 (Status &s)
{
	s = Status ();
	size_t t = 0;
	double t1 = NowMs ();
	for (int counter = 0; counter < 20; counter++) {
		if (!SendV3_2 (0x1A, 0x07, 50000)) return false;
		for (;;) {
			if (!usb.Receive (in_ep, packet.data (), packet.size (), &t, 500)) return false;
			if (t > 3 && packet[0] >= 0x07 && packet[1] == 0x20) break; // not upstream: packet[3] read below (upstream: t > 2)
			double t2 = NowMs ();
			if (t2 - t1 > 100) { t1 = t2; break; }
		}
		if (t > 3 && packet[0] >= 0x07 && packet[1] == 0x20) {
			s.fw_loaded = packet[3] == 1;
			return true;
		}
	}
	return true; // as upstream: no status counts as not loaded
}

bool TirDevice::ReadRomDataV3 ()
{
	size_t t = 0;
	do usb.Receive (in_ep, packet.data (), packet.size (), &t, 100); while (t > 0);
	double t1 = NowMs ();
	for (int counter = 0; counter < 10; counter++) {
		if (!SendV3_1 (Get_conf[0], 50000)) return false;
		for (;;) {
			if (!usb.Receive (in_ep, packet.data (), packet.size (), &t, 500)) return false;
			if (t > 2 && packet[1] == 0x40) break;
			double t2 = NowMs ();
			if (t2 - t1 > 100) { t1 = t2; break; }
		}
		if (t > 2 && packet[1] == 0x40) break;
	}
	return true;
}

TirDevice::Result TirDevice::InitV3 ()
{
	Status status;
	SendV3_2 (0x1A, 0x00, 50000);
	SendV3_2 (0x1A, 0x00, 50000);
	SendV3_1 (0x13, 50000);
	ReadStatusV3 (status);
	SendV3_2 (0x1A, 0x01, 50000);
	ReadStatusV3 (status);
	SendV3_2 (0x1A, 0x02, 50000);
	ReadStatusV3 (status);
	for (int cntr = 3; cntr; --cntr) {
		SendV3_2 (0x1A, 0x03, 50000);
		ReadStatusV3 (status);
		if (status.fw_loaded) break;
	}
	ReadRomDataV3 ();
	ThresholdV3 (0x96);
	IrBrightnessRawV3 (0x780);
	IrLedV3 (true);
	SendV3_4 (0x19, 0x03, 0x00, 0x05, 50000);
	SendV3_4 (0x19, 0x04, 0x00, 0x00, 50000);
	return OK;
}

void TirDevice::StartV3 ()
{
	IrLedV3 (prm.irOn);
	IrBrightnessV3 ((unsigned int)prm.irBrightness);
	ThresholdV3 ((unsigned int)prm.threshold);
	ThresholdV3 ((unsigned int)prm.threshold);
	SendV3_4 (0x19, 0x04, 0x00, 0x00, 50000);
	SendV3_4 (0x19, 0x03, 0x00, 0x05, 50000);
	SendV3_2 (0x1A, 0x04, 50000);
	IrLedV3 (prm.irOn);
	if (prm.statusLed) StatusLedV3 (true);
}

void TirDevice::StopV3 ()
{
	SendV3_2 (0x1A, 0x05, 50000);
	IrLedV3 (false);
	SendV3_2 (0x1A, 0x06, 50000);
	SendV3_1 (0x13, 50000);
}

void TirDevice::CloseV3 ()
{
	StopV3 ();
	ThresholdV3 (0x96);
	IrBrightnessRawV3 (0x780);
	IrLedV3 (true);
	SendV3_4 (0x19, 0x03, 0x00, 0x05, 50000);
	IrLedV3 (false);
	SendV3_2 (0x1A, 0x06, 50000);
	SendV3_1 (0x13, 50000);
	SendV3_2 (0x1A, 0x00, 50000);
}

// ----- open, close, frames

TirDevice::Result TirDevice::Open (const TirParams &p, const std::string &fwDir)
{
	Close ();
	prm = p;
	err.clear ();
	packet.assign (TIR_PACKET_SIZE, 0);
	size = ptr = 0;
	if (!usb.Init ()) { err = usb.Error (); return FAILED; }
	switch (usb.Find (model, pid)) {
	case TirUsb::NONE: usb.Finish (TIR_INTERFACE); return NO_DEVICE;
	case TirUsb::NO_ACCESS: err = usb.Error (); usb.Finish (TIR_INTERFACE); return NO_ACCESS;
	case TirUsb::OPEN_FAILED: err = usb.Error (); usb.Finish (TIR_INTERFACE); return FAILED;
	case TirUsb::OPENED: break;
	}
	out_ep = TIR_OUT_EP, in_ep = TIR_IN_EP;
	switch (usb.Prepare (TIR_CONFIGURATION, TIR_INTERFACE)) {
	case TirUsb::PREP_BUSY: err = usb.Error (); usb.Finish (TIR_INTERFACE); return BUSY;
	case TirUsb::PREP_FAILED: err = usb.Error (); usb.Finish (TIR_INTERFACE); return FAILED;
	case TirUsb::PREP_OK: break;
	}
	if (model == TIR_4) width = 400, height = 300, hf = 2.0f;
	else width = 640, height = 480, hf = 1.0f;
	const char *fw = TirFirmwareFile (model);
	std::string fwPath = fw ? fwDir + "/" + fw : std::string ();
	Result r;
	switch (model) {
	case TIR_4: r = InitTir4 (fwPath); break;
	case TIR_5: case TIR_5V2: r = InitTir5 (fwPath); break;
	default: r = InitV3 (); break;
	}
	if (r != OK) {
		if (err.empty ()) err = usb.Error ();
		usb.Finish (TIR_INTERFACE);
		return r;
	}
	switch (model) {
	case TIR_4: StartTir4 (); break;
	case TIR_5: case TIR_5V2: StartTir5 (); break;
	default: StartV3 (); break;
	}
	finder.Prepare (width, height, hf);
	parser.Reset ();
	open = true;
	return OK;
}

void TirDevice::Close ()
{
	if (!open) return;
	switch (model) {
	case TIR_4: StopTir4 (); break;
	case TIR_5: case TIR_5V2: CloseTir5 (); break;
	default: CloseV3 (); break;
	}
	usb.Finish (TIR_INTERFACE);
	open = false;
}

int TirDevice::ReadFrame (std::vector<Blob> &blobs, unsigned int maxBlobs)
{
	if (!open) return -1;
	for (;;) {
		if (ptr >= size) {
			ptr = 0;
			if (!usb.Receive (in_ep, packet.data (), packet.size (), &size, 1000)) { err = usb.Error (); return -1; }
			if (!size) return 0; // nothing in a second: back to the caller, which may stop
		}
		if (parser.Process (packet.data (), &ptr, size)) break;
	}
	blobs = finder.Finish (maxBlobs, prm.minBlob, prm.maxBlob);
	return 1;
}

} // namespace ht
