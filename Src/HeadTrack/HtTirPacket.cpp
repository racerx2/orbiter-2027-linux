// not upstream (head-track): ported from linuxtrack's tir_img.c (MIT, LICENSE-linuxtrack); TrackIR 4 and 5 only

#include "HtTirPacket.h"

namespace ht {

void TirPacketParser::Reset ()
{
	type = -1, limit = 0, pktsize = 0, current_line = 0;
}

void TirPacketParser::StripeTir4 (const unsigned char p[])
{
	Stripe s;
	s.vline = p[0];
	s.hstart = p[1];
	s.hstop = p[2];
	unsigned char rest = p[3];
	if (rest & 0x20) s.vline |= 0x100;
	if (rest & 0x80) s.hstart |= 0x100;
	if (rest & 0x40) s.hstop |= 0x100;
	if (rest & 0x10) s.hstart |= 0x200;
	if (rest & 0x08) s.hstop |= 0x200;
	if (s.hstart > s.hstop) return; // not upstream: the finder refuses it anyway; sum would wrap
	s.sum = s.hstop - s.hstart + 1;
	s.sum_x = (unsigned int)(s.sum * (s.sum - 1) / 2.0);
	s.points = s.sum;
	finder.AddStripe (s);
}

bool TirPacketParser::NextFrameTir4 (const unsigned char p[])
{
	unsigned int vline = p[0];
	if (p[3] & 0x20) vline |= 0x100;
	bool res = vline < current_line;
	current_line = vline;
	return res;
}

void TirPacketParser::StripeTir5 (const unsigned char p[])
{
	Stripe s;
	s.hstart = (((unsigned int)p[0]) << 2) | (((unsigned int)p[1]) >> 6);
	s.vline = ((((unsigned int)p[1]) & 0x3F) << 3) | ((((unsigned int)p[2]) & 0xE0) >> 5);
	s.points = ((((unsigned int)p[2]) & 0x1F) << 5) | (((unsigned int)p[3]) >> 3);
	if (!s.points) return; // not upstream: an empty stripe would end before it starts
	s.hstop = s.points + s.hstart - 1;
	s.sum_x = (((unsigned int)p[3]) & 7) << 17 | (((unsigned int)p[4]) << 9) | ((unsigned int)p[5]) << 1 | ((unsigned int)p[6]) >> 7;
	s.sum = (((unsigned int)p[6]) & 0x7F) << 8 | ((unsigned int)p[7]);
	finder.AddStripe (s);
}

bool TirPacketParser::PacketTir5 (const unsigned char data[], size_t *ptr, unsigned int psize, size_t lim)
{
	unsigned int ps = 0;
	unsigned char t = data[*ptr + 2];
	if (t == 0 || t == 5) {
		if (lim < *ptr + 8) return false; // not upstream: the header and the size need 8 bytes
		if ((data[*ptr] ^ data[*ptr + 1] ^ data[*ptr + 2] ^ data[*ptr + 3]) != 0xAA) return false; // bad header
		ps = data[lim - 4];
		ps = (ps << 8) + data[lim - 3];
		ps = (ps << 8) + data[lim - 2];
		ps = (ps << 8) + data[lim - 1];
		if (ps != psize - 8) return false; // bad size
	}
	switch (t) {
	case 0:
	case 5:
		*ptr += 4;
		for (;;) {
			if (t == 0) {
				if (ps < 4) break;
				StripeTir4 (&data[*ptr]);
				*ptr += 4, ps -= 4;
			} else {
				if (ps < 8) break;
				StripeTir5 (&data[*ptr]);
				*ptr += 8, ps -= 8;
			}
		}
		return true;
	default: // 3: the camera's raw picture, not used
		return false;
	}
}

bool TirPacketParser::PacketTir4 (const unsigned char data[], size_t *ptr, unsigned int psize, size_t lim)
{
	bool have_frame = false;
	for (bool go_on = true; go_on; ) {
		if (*ptr + 4 > lim) { *ptr = lim; break; } // not upstream: no word past the packet
		const unsigned char *w = &data[*ptr];
		if (!w[0] && !w[1] && !w[2] && !w[3]) { // a frame's end
			current_line = 0;
			have_frame = true;
			go_on = false;
			*ptr += 4;
		} else if (NextFrameTir4 (w)) { // the next frame started without one
			have_frame = true;
			go_on = false;
		} else {
			StripeTir4 (w);
			*ptr += 4;
		}
		if (*ptr >= lim) {
			go_on = false;
			if (psize == 0x3E) *ptr += 2;
		}
	}
	return have_frame;
}

bool TirPacketParser::Process (const unsigned char data[], size_t *ptr, size_t size)
{
	bool have_frame;
	for (;;) {
		if (*ptr >= size) return false;
		have_frame = false;
		if (type == -1) {
			if (*ptr + 3 > size) { *ptr = size; return false; } // not upstream: a packet's head past the data read
			type = data[*ptr + 1];
			switch (type) {
			case 0x20: // status
			case 0x40: // device info
			case 0x1C: // TrackIR 4 stripes
				pktsize = data[*ptr];
				limit = *ptr + pktsize;
				*ptr += 2;
				break;
			case 0x10: // TrackIR 5
				if (data[*ptr + 2] == 0 || data[*ptr + 2] == 5) pktsize = (unsigned int)(size - *ptr); // not upstream: the rest of the read (upstream: size, the same when the frame starts it)
				else pktsize = data[*ptr];
				limit = *ptr + pktsize;
				break;
			default: // SmartNav and unknown packets: the rest of the read is dropped
				type = -1;
				*ptr = size;
				return false;
			}
		}
		if (limit > size) { type = -1; *ptr = size; return false; } // not upstream: a packet past the read is dropped, not waited on forever
		switch (type) {
		case 0x20:
		case 0x40:
			*ptr = limit;
			type = -1;
			break;
		case 0x10:
			have_frame = PacketTir5 (data, ptr, pktsize, limit);
			*ptr = limit;
			type = -1;
			break;
		case 0x1C:
			have_frame = PacketTir4 (data, ptr, pktsize, limit);
			if (*ptr >= limit) type = -1;
			break;
		default:
			type = -1;
			break;
		}
		if (have_frame) break;
	}
	return have_frame;
}

} // namespace ht
