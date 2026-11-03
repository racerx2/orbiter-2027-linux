// not upstream (head-track): ported from linuxtrack's libusb_ifc.c (MIT, LICENSE-linuxtrack); TrackIR 4 and 5 only

#include "HtTirUsb.h"
#include <libusb.h>

namespace ht {

TirUsb::~TirUsb ()
{
	Finish (0);
}

bool TirUsb::Init ()
{
	if (ctx) return true;
	int res = libusb_init (&ctx);
	if (res != 0) {
		err = std::string ("libusb: ") + libusb_error_name (res);
		ctx = nullptr;
		return false;
	}
	return true;
}

TirUsb::Found TirUsb::Find (TirModel &model, uint16_t &pid)
{
	model = TIR_NONE, pid = 0;
	libusb_device **list;
	ssize_t cnt = libusb_get_device_list (ctx, &list);
	if (cnt < 0) {
		err = std::string ("libusb: ") + libusb_error_name ((int)cnt);
		return NONE;
	}
	static const struct { uint16_t pid; TirModel model; } ids[] = { // as upstream: the newest first
		{0x0159, TIR_5V3}, {0x0158, TIR_5V2}, {0x0157, TIR_5}, {0x0156, TIR_4}
	};
	libusb_device *found = nullptr;
	for (auto &id : ids) {
		for (ssize_t i = 0; i < cnt && !found; i++) {
			libusb_device_descriptor desc;
			if (libusb_get_device_descriptor (list[i], &desc)) continue;
			if (desc.idVendor == 0x131D && desc.idProduct == id.pid) found = list[i], model = id.model, pid = id.pid;
		}
		if (found) break;
	}
	Found res = NONE;
	if (found) {
		int e = libusb_open (found, &handle);
		if (e) {
			handle = nullptr;
			err = std::string ("libusb: ") + libusb_error_name (e);
			res = e == LIBUSB_ERROR_ACCESS ? NO_ACCESS : OPEN_FAILED;
		} else res = OPENED;
	}
	libusb_free_device_list (list, 1);
	return res;
}

TirUsb::Prep TirUsb::Prepare (int config, int iface)
{
	int cfg = -1;
	if (libusb_get_configuration (handle, &cfg)) { err = "can't read the device's configuration"; return PREP_FAILED; }
	if (cfg != config) {
		int e = libusb_set_configuration (handle, config);
		if (e) { err = std::string ("can't set the device's configuration: ") + libusb_error_name (e); return e == LIBUSB_ERROR_BUSY ? PREP_BUSY : PREP_FAILED; }
	}
	if (libusb_kernel_driver_active (handle, iface) == 1) {
		kernel_driver = true;
		if (libusb_detach_kernel_driver (handle, iface) != 0) { err = "can't detach the kernel's driver"; return PREP_FAILED; }
	}
	int e = libusb_claim_interface (handle, iface);
	if (e) {
		claimed = false;
		err = std::string ("can't claim the device: ") + libusb_error_name (e);
		return e == LIBUSB_ERROR_BUSY ? PREP_BUSY : PREP_FAILED;
	}
	claimed = true;
	if (libusb_get_configuration (handle, &cfg) || cfg != config) { err = "the device's configuration is wrong"; return PREP_FAILED; }
	return PREP_OK;
}

bool TirUsb::Send (int ep, const unsigned char data[], size_t size)
{
	if (!handle) return false;
	int transferred = 0;
	int res = libusb_bulk_transfer (handle, (unsigned char)ep, const_cast<unsigned char*> (data), (int)size, &transferred, 500);
	if (res) { err = std::string ("write: ") + libusb_error_name (res); return false; }
	return true;
}

bool TirUsb::Receive (int ep, unsigned char data[], size_t size, size_t *transferred, unsigned int timeout_ms)
{
	*transferred = 0;
	if (!handle) return false;
	if (!timeout_ms) timeout_ms = 500;
	int n = 0;
	int res = libusb_bulk_transfer (handle, (unsigned char)ep, data, (int)size, &n, timeout_ms);
	*transferred = n > 0 ? (size_t)n : 0;
	if (res && res != LIBUSB_ERROR_TIMEOUT) { err = std::string ("read: ") + libusb_error_name (res); return false; }
	return true;
}

void TirUsb::Finish (int iface)
{
	if (handle) {
		if (claimed) libusb_release_interface (handle, iface);
		claimed = false;
		if (kernel_driver) {
			libusb_reset_device (handle);
			libusb_attach_kernel_driver (handle, iface);
			kernel_driver = false;
		}
		libusb_close (handle);
		handle = nullptr;
	}
	if (ctx) libusb_exit (ctx);
	ctx = nullptr;
}

} // namespace ht
