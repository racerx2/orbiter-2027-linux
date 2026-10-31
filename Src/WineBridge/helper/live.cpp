// WineBridge helper: memory of the modules that Orbiter reads every frame. Every message the helper sends carries what changed.
#define OAPI_IMPLEMENTATION
#include "Orbitersdk.h"
#include <stdlib.h>
#include "wb_helper.h"

namespace {
	struct Item { uint32_t id, vid; int kind; const void *addr; uint32_t n; int tagkind; uint32_t tag; uint8_t last[256]; uint32_t lastlen; };
	Item *g_items;
	uint32_t g_nitems, g_cap, g_nextid = 1;

	uint32_t Snapshot (const Item &it, uint8_t *out) // the item's current value on the wire
	{
		switch (it.kind) {
		case WB_LIVE_DOUBLE: memcpy (out, it.addr, 8); return 8;
		case WB_LIVE_VECTOR3: memcpy (out, it.addr, 24); return 24;
		case WB_LIVE_BEACON: {
			const BEACONLIGHTSPEC *b = (const BEACONLIGHTSPEC *)it.addr;
			static const double zero[3] = {0, 0, 0};
			uint8_t *p = out;
			uint32_t shape = b->shape;
			memcpy (p, &shape, 4); p += 4;
			memcpy (p, b->pos ? b->pos->data : zero, 24); p += 24;
			memcpy (p, b->col ? b->col->data : zero, 24); p += 24;
			double d[5] = {b->size, b->falloff, b->period, b->duration, b->tofs};
			memcpy (p, d, 40); p += 40;
			*p++ = b->active ? 1 : 0;
			return (uint32_t)(p - out); }
		case WB_LIVE_BYTES: memcpy (out, it.addr, it.n); return it.n;
		case WB_LIVE_STRING: { // the debug line, in UTF-8 cut at a character to Orbiter's 255 bytes
			const char *s = (const char *)it.addr;
			uint32_t k = 0;
			while (k < it.n - 1 && s[k]) k++;
			wb::Utf8 t (s, (int)k);
			k = wb::Utf8Cut (t.s, t.n, it.n - 1 < 255 ? it.n - 1 : 255);
			memcpy (out, t.s, k);
			return k; }
		}
		return 0;
	}
}

uint32_t wb::Live (uint32_t vid, int kind, const void *addr, uint32_t n)
{
	if (g_nitems == g_cap) {
		g_cap = g_cap ? g_cap * 2 : 64;
		g_items = (Item *)realloc (g_items, g_cap * sizeof (Item));
	}
	Item &it = g_items[g_nitems++];
	it.id = g_nextid++;
	it.vid = vid;
	it.kind = kind;
	it.addr = addr;
	it.n = n;
	it.tagkind = TAG_NONE;
	it.tag = 0;
	it.lastlen = Snapshot (it, it.last); // the registering call carries this first value (LiveValue)
	return it.id;
}

void wb::LiveTag (uint32_t id, int tagkind, uint32_t tag)
{
	for (uint32_t i = 0; i < g_nitems; i++)
		if (g_items[i].id == id) { g_items[i].tagkind = tagkind; g_items[i].tag = tag; }
}

// as Orbiter: a deleted exhaust or stream stops reading the module's memory, which the module may then free
void wb::LiveDropTag (uint32_t vid, int tagkind, uint32_t tag)
{
	uint32_t k = 0;
	for (uint32_t i = 0; i < g_nitems; i++)
		if (g_items[i].vid != vid || g_items[i].tagkind != tagkind || g_items[i].tag != tag) g_items[k++] = g_items[i];
	g_nitems = k;
}

const uint8_t *wb::LiveValue (uint32_t id, uint32_t *len)
{
	for (uint32_t i = 0; i < g_nitems; i++)
		if (g_items[i].id == id) { *len = g_items[i].lastlen; return g_items[i].last; }
	*len = 0;
	return 0;
}

void wb::LiveClear ()
{
	g_nitems = 0;
}

void wb::LiveDrop (uint32_t vid)
{
	uint32_t k = 0;
	for (uint32_t i = 0; i < g_nitems; i++)
		if (g_items[i].vid != vid) g_items[k++] = g_items[i];
	g_nitems = k;
}

uint32_t wb::LiveSync (uint8_t *out, uint32_t room)
{
	uint8_t *p = out, cur[256];
	for (uint32_t i = 0; i < g_nitems; i++) {
		Item &it = g_items[i];
		uint32_t n = Snapshot (it, cur);
		if (n == it.lastlen && !memcmp (cur, it.last, n)) continue;
		if ((uint32_t)(p - out) + 8 + n > room) break; // the next message sends the rest
		WbSync s = {it.id, n};
		memcpy (p, &s, 8); p += 8;
		memcpy (p, cur, n); p += n;
		memcpy (it.last, cur, n);
		it.lastlen = n;
	}
	return (uint32_t)(p - out);
}

// the item of the vessel's memory at addr, of that kind (0: none)
uint32_t wb::LiveFind (uint32_t vid, int kind, const void *addr)
{
	for (uint32_t i = 0; i < g_nitems; i++)
		if (g_items[i].vid == vid && g_items[i].kind == kind && g_items[i].addr == addr) return g_items[i].id;
	return 0;
}

// the module's memory an item reads (null: none)
const void *wb::LiveAddr (uint32_t id)
{
	for (uint32_t i = 0; i < g_nitems; i++) if (g_items[i].id == id) return g_items[i].addr;
	return 0;
}

// the vessel's items of a kind of tag, whatever the tag
void wb::LiveDropTagKind (uint32_t vid, int tagkind)
{
	uint32_t k = 0;
	for (uint32_t i = 0; i < g_nitems; i++)
		if (g_items[i].vid != vid || g_items[i].tagkind != tagkind) g_items[k++] = g_items[i];
	g_nitems = k;
}

