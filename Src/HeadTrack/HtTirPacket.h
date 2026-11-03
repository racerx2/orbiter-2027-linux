// not upstream (head-track): the TrackIR's data packets read into stripes, ported from linuxtrack's tir_img.c (MIT, LICENSE-linuxtrack)

#ifndef __HTTIRPACKET_H
#define __HTTIRPACKET_H

#include "HtBlobs.h"
#include <cstddef>

namespace ht {

enum TirModel { TIR_NONE, TIR_4, TIR_5, TIR_5V2, TIR_5V3 };

class TirPacketParser {
public:
	explicit TirPacketParser (BlobFinder &f) : finder (f) {}
	void Reset ();
	// the stripes of data[*ptr..size) go to the finder; true when a frame is complete (*ptr then at the next one's data)
	bool Process (const unsigned char data[], size_t *ptr, size_t size);

private:
	bool PacketTir5 (const unsigned char data[], size_t *ptr, unsigned int pktsize, size_t limit);
	bool PacketTir4 (const unsigned char data[], size_t *ptr, unsigned int pktsize, size_t limit);
	void StripeTir4 (const unsigned char s[]);
	void StripeTir5 (const unsigned char s[]);
	bool NextFrameTir4 (const unsigned char s[]);

	BlobFinder &finder;
	int type = -1;
	size_t limit = 0;
	unsigned int pktsize = 0;
	unsigned int current_line = 0;
};

} // namespace ht

#endif // !__HTTIRPACKET_H
