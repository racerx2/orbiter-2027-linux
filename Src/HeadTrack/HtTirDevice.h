// not upstream (head-track): the TrackIR 4 and 5 (all revisions): start, stop, LEDs, firmware and frames, ported from linuxtrack's tir_hw.c and tir_img.c (MIT, LICENSE-linuxtrack)

#ifndef __HTTIRDEVICE_H
#define __HTTIRDEVICE_H

#include "HtBlobs.h"
#include "HtTirPacket.h"
#include "HtTirUsb.h"
#include <random>
#include <string>
#include <vector>

namespace ht {

const char *TirModelName (TirModel m);     // "TrackIR 5 (revision 3)"
const char *TirFirmwareFile (TirModel m);  // "tir5.fw.gz"; nullptr: needs none

struct TirParams {
	int threshold = 140;       // 30..253
	int irBrightness = 7;      // 5..7
	int statusBrightness = 0;  // 0..3
	bool statusLed = true;     // the green LED while tracking
	bool irOn = true;          // the illuminators: off for active LEDs (TrackClip PRO)
	int minBlob = 4, maxBlob = 2500; // pixels
};

class TirDevice {
public:
	enum Result { OK, NO_DEVICE, NO_ACCESS, BUSY, NO_FIRMWARE, FAILED };
	Result Open (const TirParams &p, const std::string &fwDir); // found, firmware, configured, started
	void Close ();                                               // stopped, the LEDs off, released
	TirModel Model () const { return model; }
	uint16_t ProductId () const { return pid; }
	int Width () const { return width; }
	int Height () const { return height; }
	const std::string &Error () const { return err; }
	void SetThreshold (int t);
	int ReadFrame (std::vector<Blob> &blobs, unsigned int maxBlobs); // 1 a frame, 0 none yet (time out), -1 the device is gone

private:
	struct Status { bool fw_loaded = false; int cfg_flag = 0; unsigned int fw_cksum = 0; };
	struct Firmware { std::vector<unsigned char> data; unsigned int cksum = 0; };

	bool Send (const unsigned char *p, size_t n) { return usb.Send (out_ep, p, n); }
	void Sleep (int usec);
	bool ReadStatus (Status &s);
	bool ReadRomData ();
	bool LoadFirmware (Firmware &fw, const std::string &path);
	bool UploadFirmware (const Firmware &fw);
	bool WiggleLeds (unsigned char leds, unsigned char mask);
	bool LedOff (unsigned char leds) { return WiggleLeds (0, leds & 0xf0); }
	bool LedOn (unsigned char leds) { return WiggleLeds (leds & 0xf0, leds & 0xf0); }
	bool FlushFifo ();
	bool ControlStatusLed (bool s1, bool s2);
	bool StatusLedTir5 (bool running);
	bool StatusLedTir4 (bool running);
	bool ControlIrLed (bool ir);
	bool SetExposure (unsigned int exp);
	Result InitTir4 (const std::string &fw);
	Result InitTir5 (const std::string &fw);
	void StartTir4 (), StartTir5 (), StopTir4 (), StopTir5 (), CloseTir5 ();
	// TrackIR 5 revision 3: scrambled 24 byte commands, no firmware
	int RandRange (int l, int h);
	bool SendV3 (unsigned char packet[24], int wait);
	bool SendV3_4 (uint8_t v1, uint8_t v2, uint8_t v3, uint8_t v4, int wait);
	bool SendV3_2 (uint8_t v1, uint8_t v2, int wait);
	bool SendV3_1 (uint8_t v1, int wait);
	bool ThresholdV3 (unsigned int t) { return SendV3_4 (0x19, 0x05, t >> 7, (t << 1) & 0xFF, 50000); }
	bool IrLedV3 (bool ir) { return SendV3_4 (0x19, 0x09, 0x00, ir ? 0x01 : 0x00, 50000); }
	bool IrBrightnessRawV3 (unsigned int b);
	bool IrBrightnessV3 (unsigned int b);
	bool StatusLedV3 (bool running);
	bool ReadStatusV3 (Status &s);
	bool ReadRomDataV3 ();
	Result InitV3 ();
	void StartV3 (), StopV3 (), CloseV3 ();

	TirUsb usb;
	TirParams prm;
	TirModel model = TIR_NONE;
	uint16_t pid = 0;
	int out_ep = 0x01, in_ep = 0x82;
	int width = 0, height = 0;
	float hf = 1.0f;
	bool open = false;
	std::string err;
	std::vector<unsigned char> packet; // the last read
	size_t size = 0, ptr = 0;
	BlobFinder finder;
	TirPacketParser parser {finder};
	std::mt19937 rng {std::random_device {} ()};
};

} // namespace ht

#endif // !__HTTIRDEVICE_H
