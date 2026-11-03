// not upstream (head-track): the TrackIR on libusb, ported from linuxtrack's libusb_ifc.c (MIT, LICENSE-linuxtrack)

#ifndef __HTTIRUSB_H
#define __HTTIRUSB_H

#include "HtTirPacket.h"
#include <cstddef>
#include <cstdint>
#include <string>

struct libusb_context;
struct libusb_device_handle;

namespace ht {

class TirUsb {
public:
	enum Found { NONE, OPENED, NO_ACCESS, OPEN_FAILED };
	~TirUsb ();
	bool Init ();                                  // libusb
	Found Find (TirModel &model, uint16_t &pid);   // the first TrackIR 4 or 5, opened
	enum Prep { PREP_OK, PREP_BUSY, PREP_FAILED };
	Prep Prepare (int config, int iface);          // its configuration and interface claimed; BUSY: another program has it
	bool Send (int ep, const unsigned char data[], size_t size);
	bool Receive (int ep, unsigned char data[], size_t size, size_t *transferred, unsigned int timeout_ms); // a timeout is no error (0 read)
	void Finish (int iface);                       // released, closed, libusb ended
	const std::string &Error () const { return err; }

private:
	libusb_context *ctx = nullptr;
	libusb_device_handle *handle = nullptr;
	bool claimed = false, kernel_driver = false;
	std::string err;
};

} // namespace ht

#endif // !__HTTIRUSB_H
