// not upstream (head-track): ported from linuxtrack's image_process.c (MIT, LICENSE-linuxtrack)

#include "HtBlobs.h"

namespace ht {

void BlobFinder::Prepare (int pw, int ph, float pratio)
{
	w = pw, h = ph, ratio = pratio;
	size_t n = (size_t)((w * ratio) / 2) + 2; // a line's stripes, each a pixel and a gap at least
	current.ranges.resize (n);
	next.ranges.resize (n);
	current.limit = next.limit = 0;
	current_vline = (unsigned int)-2;
	pool.clear (), done.clear ();
}

void BlobFinder::Store (bool all)
{
	for (int i = 0; i < current.limit; ++i) {
		Preblob *pb = current.ranges[i].pb;
		if (!pb->matched && !pb->added) {
			pb->added = true;
			done.push_back (pb);
		}
	}
	for (int i = 0; i < next.limit; ++i) {
		Preblob *pb = next.ranges[i].pb;
		pb->matched = false;
		if (all && !pb->added) {
			pb->added = true;
			done.push_back (pb);
		}
	}
}

bool BlobFinder::AddStripe (const Stripe &s)
{
	if (s.vline > (unsigned int)h) return false;
	if (s.hstart > (unsigned int)(w * ratio)) return false;
	if (s.hstop > (unsigned int)(w * ratio)) return false;
	if (s.hstart > s.hstop) return false;

	if (current_vline != s.vline) { // a new line: the next one, or a gap
		if (current_vline + 1 != s.vline) {
			Store (true);
			current.limit = 0;
			next.limit = 0;
		} else {
			Store (false);
			std::swap (current, next);
			next.limit = 0;
		}
		current_vline = s.vline;
	}
	if (next.limit >= (int)next.ranges.size ()) return false; // not upstream: more stripes on a line than it can hold (a garbled packet)

	Range *rng = nullptr;
	for (int i = 0; i < current.limit; ++i) {
		Range &r = current.ranges[i];
		if ((int)s.hstart - 1 <= (int)r.x2 && s.hstop + 1 >= r.x1) { // the stripe touches the range above it
			r.pb->matched = true;
			if (!rng) rng = &r;
			else if (rng->pb != r.pb) {
				Preblob *merged = r.pb;
				rng->pb->sum_x += merged->sum_x;
				rng->pb->sum_y += merged->sum_y;
				rng->pb->sum += merged->sum;
				rng->pb->points += merged->points;
				for (int j = 0; j < current.limit; ++j) if (current.ranges[j].pb == merged) current.ranges[j].pb = rng->pb;
				for (int j = 0; j < next.limit; ++j) if (next.ranges[j].pb == merged) next.ranges[j].pb = rng->pb;
				merged->added = true; // not upstream: freed there; here never counted again
			}
		}
	}
	Preblob *pb;
	if (!rng) {
		pool.push_back (std::make_unique<Preblob> ());
		pb = pool.back ().get ();
		pb->sum_x = ((float)s.sum * s.hstart) + s.sum_x;
		pb->sum_y = (float)s.sum * s.vline;
		pb->sum = s.sum;
		pb->points = s.points;
		pb->matched = true;
	} else {
		pb = rng->pb;
		pb->sum_x += ((float)s.sum * s.hstart) + s.sum_x;
		pb->sum_y += (float)s.sum * s.vline;
		pb->sum += s.sum;
		pb->points += s.points;
	}
	next.ranges[next.limit++] = Range{s.hstart, s.hstop, pb};
	return true;
}

std::vector<Blob> BlobFinder::Finish (unsigned int maxBlobs, int minPts, int maxPts)
{
	Store (true);
	current.limit = 0;
	next.limit = 0;
	current_vline = (unsigned int)-2;
	std::vector<Blob> res;
	for (Preblob *pb : done) {
		if (pb->points < minPts || pb->points > maxPts || pb->sum <= 0) continue;
		if (res.size () >= maxBlobs) break;
		float x = pb->sum_x / pb->sum;
		float y = pb->sum_y / pb->sum;
		Blob b;
		b.x = ((w - 1) / 2.0f) - (x / ratio);
		b.y = ((h - 1) / 2.0f) - y;
		b.score = pb->points;
		res.push_back (b);
	}
	pool.clear (), done.clear ();
	return res;
}

} // namespace ht
