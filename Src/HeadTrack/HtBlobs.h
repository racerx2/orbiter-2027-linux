// not upstream (head-track): the camera's stripes joined into blobs (the LEDs or reflectors), ported from linuxtrack's image_process.c (MIT, LICENSE-linuxtrack)

#ifndef __HTBLOBS_H
#define __HTBLOBS_H

#include <memory>
#include <vector>

namespace ht {

struct Blob {
	float x = 0, y = 0;      // from the image's centre: +x right, +y up (as the user sees the camera)
	unsigned int score = 0;  // pixels
};

struct Stripe {
	unsigned int vline = 0, hstart = 0, hstop = 0;
	unsigned int sum_x = 0, sum = 0, points = 0;
};

class BlobFinder {
public:
	void Prepare (int w, int h, float ratio); // the image's size; ratio: its width's upsampling (TrackIR 4: 2)
	bool AddStripe (const Stripe &s);         // in line order; false: outside the image (ignored)
	std::vector<Blob> Finish (unsigned int maxBlobs, int minPts, int maxPts); // the frame's blobs, in the order they ended

private:
	struct Preblob { float sum_x = 0, sum_y = 0; int sum = 0, points = 0; bool added = false, matched = false; };
	struct Range { unsigned int x1, x2; Preblob *pb; };
	struct Lines { std::vector<Range> ranges; int limit = 0; };
	void Store (bool all);

	int w = 0, h = 0;
	float ratio = 1.0f;
	Lines current, next;
	unsigned int current_vline = (unsigned int)-2;
	std::vector<std::unique_ptr<Preblob>> pool; // this frame's, freed at its end
	std::vector<Preblob*> done;                 // the finished ones, in order
};

} // namespace ht

#endif // !__HTBLOBS_H
