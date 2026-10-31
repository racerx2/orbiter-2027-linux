// WineBridge: the helper's drawing calls into Orbiter: fonts, pens, brushes, sketchpads, GDI copies of surfaces, bitmaps
#include "Orbitersdk.h"
#include "DrawAPI.h"
#include "Bridge.h"
#include "WbVessel.h"
#include "Manual.h"
#include <QImage>
#include <QPainter>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

static std::string Str (WbReader &r) { uint32_t n; const char *s = r.str (&n); return s ? std::string (s, n) : std::string (); }

namespace {
	// what the helper holds: Orbiter's objects it made, those Orbiter handed it, and the open DCs (never destroyed, as Manual.cpp's state)
	std::unordered_set<const void *> &g_tools = *new std::unordered_set<const void *>;   // fonts, pens and brushes made through the bridge
	std::unordered_set<const void *> &g_foreign = *new std::unordered_set<const void *>; // tools a sketchpad returned
	std::unordered_set<const void *> &g_skps = *new std::unordered_set<const void *>;    // sketchpads made through the bridge
	std::unordered_set<const void *> &g_clbkSkps = *new std::unordered_set<const void *>; // sketchpads of a callback now running
	std::unordered_map<const void *, QPainter *> &g_dcs = *new std::unordered_map<const void *, QPainter *>; // by surface

	template <class T> T *Tool (uint32_t id)
	{
		const void *p = WbBridge::Get ().Ptr (id);
		return p && (g_tools.count (p) || g_foreign.count (p)) ? (T *)p : nullptr;
	}
	uint32_t Returned (const void *p)
	{
		if (p && !g_tools.count (p)) g_foreign.insert (p);
		return WbBridge::Get ().Id (p);
	}
	oapi::Sketchpad *Skp (uint32_t id)
	{
		const void *p = WbBridge::Get ().Ptr (id);
		return p && (g_skps.count (p) || g_clbkSkps.count (p)) ? (oapi::Sketchpad *)p : nullptr;
	}

	std::vector<oapi::IVECTOR2> Points (WbReader &r, int n)
	{
		std::vector<oapi::IVECTOR2> pt;
		if (n < 0 || (uint64_t)n * 8 > WB_MSG_MAX) { r.bad = true; return pt; }
		pt.resize (n);
		for (auto &p : pt) { p.x = r.i32 (); p.y = r.i32 (); }
		return pt;
	}

	// a bitmap's pixels (32 bits, rows top-down, as GetDIBits gives them), opaque as Orbiter's GDI copy of it
	QImage *Bitmap (WbReader &r)
	{
		int W = r.i32 (), H = r.i32 ();
		if (r.bad || W <= 0 || H <= 0 || (uint64_t)W * H * 4 > WB_MSG_MAX) { r.bad = true; return nullptr; }
		QImage *img = new QImage (W, H, QImage::Format_RGB32);
		for (int y = 0; y < H; y++) r.raw (img->scanLine (y), (uint32_t)W * 4);
		if (r.bad) { delete img; return nullptr; }
		return img;
	}

	void SkpCall (oapi::Sketchpad *s, int m, WbReader &r, WbWriter &w)
	{
		switch (m) {
		case S_SetFont: { oapi::Font *f = Tool<oapi::Font> (r.u32 ()); if (s && !r.bad) w.u32 (Returned (s->SetFont (f))); break; }
		case S_SetPen: { oapi::Pen *p = Tool<oapi::Pen> (r.u32 ()); if (s && !r.bad) w.u32 (Returned (s->SetPen (p))); break; }
		case S_SetBrush: { oapi::Brush *b = Tool<oapi::Brush> (r.u32 ()); if (s && !r.bad) w.u32 (Returned (s->SetBrush (b))); break; }
		case S_SetTextAlign: {
			int h = r.i32 (), v = r.i32 ();
			if (s && !r.bad) s->SetTextAlign ((oapi::Sketchpad::TAlign_horizontal)h, (oapi::Sketchpad::TAlign_vertical)v);
			break; }
		case S_SetTextColor: { DWORD c = r.u32 (); if (s && !r.bad) w.u32 (s->SetTextColor (c)); break; }
		case S_SetBackgroundColor: { DWORD c = r.u32 (); if (s && !r.bad) w.u32 (s->SetBackgroundColor (c)); break; }
		case S_SetBackgroundMode: { int b = r.i32 (); if (s && !r.bad) s->SetBackgroundMode ((oapi::Sketchpad::BkgMode)b); break; }
		case S_GetCharSize: if (s) w.u32 (s->GetCharSize ()); break;
		case S_GetTextWidth: { int len = r.i32 (); std::string t = Str (r); if (s && !r.bad) w.u32 (s->GetTextWidth (t.c_str (), len)); break; }
		case S_SetOrigin: { int x = r.i32 (), y = r.i32 (); if (s && !r.bad) s->SetOrigin (x, y); break; }
		case S_GetOrigin: { int x = 0, y = 0; if (s) s->GetOrigin (&x, &y); w.i32 (x); w.i32 (y); break; }
		case S_Text: {
			int x = r.i32 (), y = r.i32 (), len = r.i32 ();
			std::string t = Str (r); // the whole string for a length of 0 or less, as the module passed it
			if (s && !r.bad) w.b (s->Text (x, y, t.c_str (), len));
			break; }
		case S_TextBox: {
			int x1 = r.i32 (), y1 = r.i32 (), x2 = r.i32 (), y2 = r.i32 (), len = r.i32 ();
			std::string t = Str (r);
			if (s && !r.bad) w.b (s->TextBox (x1, y1, x2, y2, t.c_str (), len));
			break; }
		case S_Pixel: { int x = r.i32 (), y = r.i32 (); DWORD c = r.u32 (); if (s && !r.bad) s->Pixel (x, y, c); break; }
		case S_MoveTo: { int x = r.i32 (), y = r.i32 (); if (s && !r.bad) s->MoveTo (x, y); break; }
		case S_LineTo: { int x = r.i32 (), y = r.i32 (); if (s && !r.bad) s->LineTo (x, y); break; }
		case S_Line: case S_Rectangle: case S_Ellipse: {
			int x0 = r.i32 (), y0 = r.i32 (), x1 = r.i32 (), y1 = r.i32 ();
			if (!s || r.bad) break;
			if (m == S_Line) s->Line (x0, y0, x1, y1);
			else if (m == S_Rectangle) s->Rectangle (x0, y0, x1, y1);
			else s->Ellipse (x0, y0, x1, y1);
			break; }
		case S_Polygon: case S_Polyline: {
			int n = r.i32 ();
			std::vector<oapi::IVECTOR2> pt = Points (r, n);
			if (!s || r.bad) break;
			if (m == S_Polygon) s->Polygon (pt.data (), n); else s->Polyline (pt.data (), n);
			break; }
		case S_PolyPolygon: case S_PolyPolyline: {
			int nline = r.i32 ();
			if (nline < 0 || (uint64_t)nline * 4 > WB_MSG_MAX) { r.bad = true; break; }
			std::vector<int> npt (nline);
			int total = 0;
			for (int &k : npt) { k = r.i32 (); total += k; }
			std::vector<oapi::IVECTOR2> pt = Points (r, total);
			if (!s || r.bad) break;
			if (m == S_PolyPolygon) s->PolyPolygon (pt.data (), npt.data (), nline); else s->PolyPolyline (pt.data (), npt.data (), nline);
			break; }
		default:
			r.bad = true;
			break;
		}
	}

	// the pixels GDI changed in the helper's copy: b, then i32 x, y, w, h and the rows
	struct Changed { bool has = false; int x = 0, y = 0; QImage part; };
	Changed ReadChanged (WbReader &r)
	{
		Changed c;
		c.has = r.b ();
		if (!c.has || r.bad) return c;
		c.x = r.i32 (); c.y = r.i32 ();
		int cw = r.i32 (), ch = r.i32 ();
		if (cw <= 0 || ch <= 0 || (uint64_t)cw * ch * 4 > WB_MSG_MAX) { r.bad = true; return c; }
		c.part = QImage (cw, ch, QImage::Format_ARGB32);
		for (int y = 0; y < ch; y++) r.raw (c.part.scanLine (y), (uint32_t)cw * 4);
		return c;
	}
	void Apply (const Changed &c, QPainter *p)
	{
		if (!c.has) return;
		p->save ();
		p->setCompositionMode (QPainter::CompositionMode_Source); // the pixels as GDI left them
		p->drawImage (c.x, c.y, c.part);
		p->restore ();
	}

	HUDPAINTSPEC Hps (WbReader &r)
	{
		HUDPAINTSPEC h {};
		h.W = r.i32 (); h.H = r.i32 (); h.CX = r.i32 (); h.CY = r.i32 (); h.Scale = r.f64 (); h.Markersize = r.i32 ();
		return h;
	}
}

void WbGetHps (WbReader &r, void *hps)
{
	*(HUDPAINTSPEC *)hps = Hps (r);
}

void WbPutHps (WbWriter &w, const void *hps)
{
	HUDPAINTSPEC z {};
	const HUDPAINTSPEC *h = hps ? (const HUDPAINTSPEC *)hps : &z;
	w.i32 (h->W); w.i32 (h->H); w.i32 (h->CX); w.i32 (h->CY); w.f64 (h->Scale); w.i32 (h->Markersize);
}

QImage *WbPainterImage (QPainter *p)
{
	QPaintDevice *d = p ? p->device () : nullptr;
	return d && d->devType () == QInternal::Image ? static_cast<QImage *> (d) : nullptr;
}

void WbPutImage (WbWriter &w, const QImage &image)
{
	const QImage *img = &image;
	QImage conv;
	if (img->format () != QImage::Format_ARGB32 && img->format () != QImage::Format_RGB32) { conv = img->convertToFormat (QImage::Format_ARGB32); img = &conv; }
	w.i32 (img->width ()); w.i32 (img->height ());
	for (int y = 0; y < img->height (); y++) w.raw (img->constScanLine (y), (uint32_t)img->width () * 4);
}

void WbDrawChanged (WbReader &r, QPainter *p)
{
	Changed c = ReadChanged (r);
	if (!r.bad) Apply (c, p);
}

uint32_t WbToolId (const void *tool) { return Returned (tool); }
oapi::Sketchpad *WbSkpOf (uint32_t id) { return Skp (id); }

void WbSkpBegin (oapi::Sketchpad *skp) { if (skp) g_clbkSkps.insert (skp); }
void WbSkpEnd (oapi::Sketchpad *skp) { g_clbkSkps.erase (skp); }

bool WbServeDraw (uint32_t fn, WbReader &r, WbWriter &w)
{
	WbBridge &b = WbBridge::Get ();
	if ((fn < L_CreateFont || fn > L_BaseRenderHUD) && fn != L_CreateFontEx) return false;
	switch (fn) {
	case L_CreateFont: {
		int height = r.i32 ();
		bool prop = r.b ();
		std::string face = Str (r);
		int style = r.i32 (), orientation = r.i32 ();
		if (r.bad) break;
		oapi::Font *f = oapiCreateFont (height, prop, face.c_str (), (FontStyle)style, orientation);
		if (f) g_tools.insert (f);
		w.u32 (b.Id (f));
		break; }
	case L_CreateFontEx: {
		int height = r.i32 ();
		std::string face = Str (r);
		int width = r.i32 (), weight = r.i32 (), style = r.i32 ();
		double spacing = r.f64 ();
		if (r.bad) break;
		oapi::Font *f = oapiCreateFontEx (height, face.c_str (), width, weight, (FontStyle)style, (float)spacing);
		if (f) g_tools.insert (f);
		w.u32 (b.Id (f));
		break; }
	case L_CreatePen: {
		int style = r.i32 (), width = r.i32 ();
		DWORD col = r.u32 ();
		if (r.bad) break;
		oapi::Pen *p = oapiCreatePen (style, width, col);
		if (p) g_tools.insert (p);
		w.u32 (b.Id (p));
		break; }
	case L_CreateBrush: {
		DWORD col = r.u32 ();
		if (r.bad) break;
		oapi::Brush *br = oapiCreateBrush (col);
		if (br) g_tools.insert (br);
		w.u32 (b.Id (br));
		break; }
	case L_ReleaseFont: case L_ReleasePen: case L_ReleaseBrush: {
		const void *p = b.Ptr (r.u32 ());
		if (r.bad || !p || !g_tools.erase (p)) break; // only what the bridge made, once
		if (fn == L_ReleaseFont) oapiReleaseFont ((oapi::Font *)p);
		else if (fn == L_ReleasePen) oapiReleasePen ((oapi::Pen *)p);
		else oapiReleaseBrush ((oapi::Brush *)p);
		break; }
	case L_GetSketchpad: {
		SURFHANDLE surf = (SURFHANDLE)b.Ptr (r.u32 ());
		if (r.bad) break;
		oapi::Sketchpad *s = oapiGetSketchpad (surf);
		if (s) g_skps.insert (s);
		w.u32 (b.Id (s));
		break; }
	case L_ReleaseSketchpad: {
		const void *p = b.Ptr (r.u32 ());
		if (!r.bad && p && g_skps.erase (p)) oapiReleaseSketchpad ((oapi::Sketchpad *)p);
		break; }
	case L_Skp: {
		oapi::Sketchpad *s = Skp (r.u32 ());
		int m = r.i32 ();
		if (!r.bad) SkpCall (s, m, r, w);
		break; }
	case L_GetDC: {
		SURFHANDLE surf = (SURFHANDLE)b.Ptr (r.u32 ());
		if (r.bad) break;
		QPainter *p = g_dcs.count (surf) ? nullptr : oapiGetDC (surf); // one DC per surface, as Orbiter's
		QImage *img = p ? WbPainterImage (p) : nullptr;
		if (!img) { if (p) oapiReleaseDC (surf, p); w.b (false); break; }
		g_dcs[surf] = p;
		w.b (true);
		WbPutImage (w, *img);
		break; }
	case L_ReleaseDC: {
		SURFHANDLE surf = (SURFHANDLE)b.Ptr (r.u32 ());
		Changed part = ReadChanged (r);
		auto it = g_dcs.find (surf);
		if (it == g_dcs.end ()) break;
		QPainter *p = it->second;
		g_dcs.erase (it);
		if (!r.bad) Apply (part, p);
		oapiReleaseDC (surf, p);
		break; }
	case L_CreateSurfaceBmp: {
		QImage *img = Bitmap (r);
		if (img) w.u32 (b.Id (oapiCreateSurface (img, true))); // Orbiter deletes the image
		break; }
	case L_RegisterPanelBackground: {
		QImage *img = Bitmap (r);
		DWORD flag = r.u32 (), ck = r.u32 ();
		if (!img) break;
		if (r.bad) { delete img; break; }
		oapiRegisterPanelBackground (img, flag, ck); // Orbiter deletes the image
		break; }
	case L_BaseDrawHUD3: {
		uint32_t vid = r.u32 ();
		int mode = r.i32 ();
		HUDPAINTSPEC h = Hps (r);
		oapi::Sketchpad *s = Skp (r.u32 ());
		WbVessel *bv = b.Bridged (vid);
		if (bv && s && !r.bad) w.b (bv->BaseDrawHUD3 (mode, &h, s));
		else if (!r.bad && !bv) b.NoVessel (fn, vid);
		break; }
	case L_BaseRenderHUD: {
		uint32_t vid = r.u32 ();
		int mode = r.i32 ();
		HUDPAINTSPEC h = Hps (r);
		SURFHANDLE tex = (SURFHANDLE)b.Ptr (r.u32 ());
		WbVessel *bv = b.Bridged (vid);
		if (bv && !r.bad) bv->BaseRenderHUD (mode, &h, tex);
		else if (!r.bad) b.NoVessel (fn, vid);
		break; }
	default:
		return false;
	}
	if (r.bad) b.BadArgs (fn);
	return true;
}

void WbDrawClear ()
{
	g_tools.clear ();
	g_foreign.clear ();
	g_skps.clear ();
	g_clbkSkps.clear ();
	g_dcs.clear ();
}
