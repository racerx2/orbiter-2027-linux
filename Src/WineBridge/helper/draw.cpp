// WineBridge helper: drawing. Fonts, pens, brushes and sketchpads stand for Orbiter's; GDI draws into a copy of the
// surface (a DIB section) whose changed pixels go back to it
#define OAPI_IMPLEMENTATION
#include "Orbitersdk.h"
#include "DrawAPI.h"
#include <stdlib.h>
#include "wb_helper.h"
#include "compat2010.h"

using namespace oapi;

// ---------------------------------------------------------------------
// the Sketchpad base (DrawAPI.cpp of the Windows source)

Sketchpad::Sketchpad (SURFHANDLE s)
{
	surf = s;
}

Sketchpad::~Sketchpad ()
{
}

void Sketchpad::Rectangle (int x0, int y0, int x1, int y1)
{
	MoveTo (x0, y0);
	LineTo (x1, y0);
	LineTo (x1, y1);
	LineTo (x0, y1);
	LineTo (x0, y0);
}

void Sketchpad::PolyPolyline (const oapi::IVECTOR2 *pt, const int *npt, const int nline)
{
	int i, ofs = 0;
	for (i = 0; i < nline; i++) {
		Polyline (pt+ofs, npt[i]);
		ofs += npt[i];
	}
}

void Sketchpad::PolyPolygon (const oapi::IVECTOR2 *pt, const int *npt, const int nline)
{
	int i, ofs = 0;
	for (i = 0; i < nline; i++) {
		Polygon (pt+ofs, npt[i]);
		ofs += npt[i];
	}
}

bool Sketchpad::TextBox (int x1, int y1, int x2, int y2, const char *str, int len)
{
	static int buflen = 1024;
	static char *buf = new char[buflen];

	if (len >= buflen) {
		delete []buf;
		buf = new char[buflen = len+1];
	}
	strncpy (buf, str, len);  buf[len] = '\0';

	int w0 = x2-x1, w;

	DWORD h = GetCharSize() & 0xFFFF;

	const char *p0 = str, *p, *pp;
	for (p0 = str, pp = str, p = str; *p0; ) {
		if (*p == '\0' || *p == '\n' || *p == '\r' || *p == ' ' || *p == '-') {
			w = (p > p0 ? GetTextWidth (p0, p-p0) : 0);
			if (w <= w0 && *p != '\0' && *p != '\n' && *p != '\r') {
				pp = p;
				p++;
			} else {
				if ((*p == '\0' || *p == '\r' || *p == '\n') && w <= w0)
					pp = p;
				if (pp == p0) pp = p;
				if (pp > p0) Text (x1, y1, p0, pp-p0);
				p0 = pp;
				if (*p0 == '\r') p0++;
				if (*p0 == '\n') p0++;
				pp = p0;
				p = p0;
				y1 += h;
			}
		} else p++;
	}
	return true;
}

// ---------------------------------------------------------------------
// fonts, pens and brushes: each stands for Orbiter's object of that id; a font also has the GDI font of the 2010 clients

namespace {
	class WbFont : public Font {
	public:
		WbFont (uint32_t id, HFONT hf) : Font (0, false, ""), id (id), hf (hf) {}
		~WbFont () { if (hf) DeleteObject (hf); }
		HFONT GetGDIFont () const { return hf; }
		uint32_t id;
		HFONT hf;
	};
	class WbPen : public Pen {
	public:
		WbPen (uint32_t id) : Pen (0, 0, 0), id (id) {}
		uint32_t id;
	};
	class WbBrush : public Brush {
	public:
		WbBrush (uint32_t id) : Brush (0), id (id) {}
		uint32_t id;
	};

	// one object per id, also for those Orbiter made itself (a sketchpad's previous font): SetFont may return them
	template <class T> struct Tools {
		struct E { uint32_t id; T *t; };
		E *e = 0;
		uint32_t n = 0, cap = 0;
		T *Find (uint32_t id) { for (uint32_t i = 0; i < n; i++) if (e[i].id == id) return e[i].t; return 0; }
		void Add (uint32_t id, T *t)
		{
			if (n == cap) { cap = cap ? cap * 2 : 32; e = (E *)realloc (e, cap * sizeof (E)); }
			e[n++] = {id, t};
		}
		void Drop (T *t) { for (uint32_t i = 0; i < n; i++) if (e[i].t == t) { e[i] = e[--n]; return; } }
	};
	Tools<WbFont> g_fonts;
	Tools<WbPen> g_pens;
	Tools<WbBrush> g_brushes;

	WbFont *FontOf (uint32_t id)
	{
		if (!id) return 0;
		if (WbFont *f = g_fonts.Find (id)) return f;
		WbFont *f = new WbFont (id, 0);
		g_fonts.Add (id, f);
		return f;
	}
	WbPen *PenOf (uint32_t id)
	{
		if (!id) return 0;
		if (WbPen *p = g_pens.Find (id)) return p;
		WbPen *p = new WbPen (id);
		g_pens.Add (id, p);
		return p;
	}
	WbBrush *BrushOf (uint32_t id)
	{
		if (!id) return 0;
		if (WbBrush *b = g_brushes.Find (id)) return b;
		WbBrush *b = new WbBrush (id);
		g_brushes.Add (id, b);
		return b;
	}
}

Font *wb::FontOf (uint32_t id) { return ::FontOf (id); }
Pen *wb::PenOf (uint32_t id) { return ::PenOf (id); }

// the GDI font the 2010 graphics clients made for a font (their sketchpad font class in the Windows source)
static HFONT GdiFont (int height, bool prop, const char *face, FontStyle style, int orientation)
{
	const char *def_fixedface = "Courier New";
	const char *def_sansface = "Arial";
	const char *def_serifface = "Times New Roman";

	if (face[0]!='*') {
		if (!_stricmp (face, "fixed")) face = def_fixedface;
		else if (!_stricmp (face, "sans")) face = def_sansface;
		else if (!_stricmp (face, "serif")) face = def_serifface;
		else if (_stricmp (face, def_fixedface) && _stricmp (face, def_sansface) && _stricmp (face, def_serifface)) face = (prop ? def_sansface : def_fixedface);
	}
	else face++;

	int weight = (style & FONT_BOLD) ? FW_BOLD : FW_NORMAL;
	DWORD italic = (style & FONT_ITALIC) ? TRUE : FALSE;
	DWORD underline = (style & FONT_UNDERLINE) ? TRUE : FALSE;
	DWORD Quality = NONANTIALIASED_QUALITY;

	HFONT hFont = CreateFontA(height, 0, orientation, orientation, weight, italic, underline, FALSE, 0, 0, 2, Quality, 49, face);
	if (hFont==NULL) {
		face  = (prop ? def_sansface : def_fixedface);
		hFont = CreateFontA(height, 0, orientation, orientation, weight, italic, underline, FALSE, 0, 0, 2, Quality, 49, face);
	}
	return hFont;
}

void *wb::GdiFont (int height, bool prop, const char *face, int style, int orientation)
{
	return ::GdiFont (height, prop, face, (FontStyle)style, orientation);
}

static Font *MakeFont (int height, bool prop, const char *face, FontStyle style, int orientation)
{
	WbWriter &w = wb::Begin (L_CreateFont); w.i32 (height); w.b (prop); w.str (face); w.i32 (style); w.i32 (orientation);
	WbReader r = wb::Call ();
	uint32_t id = r.u32 ();
	if (!id) return 0;
	WbFont *f = new WbFont (id, GdiFont (height, prop, face ? face : "", style, orientation));
	g_fonts.Add (id, f);
	return f;
}

// Orbiter's font by its width, weight and spacing; the GDI font of the 2010 clients by the same height, width and weight
Font *oapiCreateFontEx (int height, char *face, int width, int weight, FontStyle style, float spacing)
{
	WbWriter &w = wb::Begin (L_CreateFontEx); w.i32 (height); w.str (face); w.i32 (width); w.i32 (weight); w.i32 (style); w.f64 (spacing);
	WbReader r = wb::Call ();
	uint32_t id = r.u32 ();
	if (!id) return 0;
	HFONT hf = CreateFontA (height, width, 0, 0, weight, (style & FONT_ITALIC) != 0, (style & FONT_UNDERLINE) != 0, (style & FONT_STRIKEOUT) != 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, face ? face : "");
	WbFont *f = new WbFont (id, hf);
	g_fonts.Add (id, f);
	return f;
}

Font *oapiCreateFont (int height, bool prop, char *face, FontStyle style)
{
	return MakeFont (height, prop, face, style, 0);
}

Font *oapiCreateFont (int height, bool prop, const char *face, FontStyle style, int orientation)
{
	return MakeFont (height, prop, face, style, orientation);
}

Pen *oapiCreatePen (int style, int width, DWORD col)
{
	WbWriter &w = wb::Begin (L_CreatePen); w.i32 (style); w.i32 (width); w.u32 (col);
	WbReader r = wb::Call ();
	return PenOf (r.u32 ());
}

Brush *oapiCreateBrush (DWORD col)
{
	WbWriter &w = wb::Begin (L_CreateBrush); w.u32 (col);
	WbReader r = wb::Call ();
	return BrushOf (r.u32 ());
}

void oapiReleaseFont (Font *font)
{
	if (!font) return;
	WbFont *f = (WbFont *)font;
	wb::Begin (L_ReleaseFont).u32 (f->id);
	wb::Call ();
	g_fonts.Drop (f);
	delete f;
}

void oapiReleasePen (Pen *pen)
{
	if (!pen) return;
	WbPen *p = (WbPen *)pen;
	wb::Begin (L_ReleasePen).u32 (p->id);
	wb::Call ();
	g_pens.Drop (p);
	delete p;
}

void oapiReleaseBrush (Brush *brush)
{
	if (!brush) return;
	WbBrush *b = (WbBrush *)brush;
	wb::Begin (L_ReleaseBrush).u32 (b->id);
	wb::Call ();
	g_brushes.Drop (b);
	delete b;
}

// ---------------------------------------------------------------------
// sketchpads: each method is the one of Orbiter's sketchpad of that id; a module gets the layout of its SDK (compat2010.h)

namespace {
	// the calls, the same in both layouts
	struct SkpCalls {
		uint32_t id;
		WbWriter &Call (int m) const { WbWriter &w = wb::Begin (L_Skp); w.u32 (id); w.i32 (m); return w; }
		static void Points (WbWriter &w, const IVECTOR2 *pt, int n) { w.i32 (n); for (int i = 0; i < n; i++) { w.i32 (pt[i].x); w.i32 (pt[i].y); } }
		// a module's text in UTF-8 and its length: a length above 0 counts the converted bytes, 0 and less keep their meaning (the whole string)
		static void PutText (WbWriter &w, const char *str, int len)
		{
			wb::Utf8 t (str ? str : "", len > 0 ? len : -1);
			w.i32 (len > 0 ? (int32_t)t.n : len); w.u32 (t.n); w.raw (t.s, t.n);
		}

		Font *SetFont (Font *font) const { Call (S_SetFont).u32 (font ? ((WbFont *)font)->id : 0); WbReader r = wb::Call (); return FontOf (r.u32 ()); }
		Pen *SetPen (Pen *pen) const { Call (S_SetPen).u32 (pen ? ((WbPen *)pen)->id : 0); WbReader r = wb::Call (); return PenOf (r.u32 ()); }
		Brush *SetBrush (Brush *brush) const { Call (S_SetBrush).u32 (brush ? ((WbBrush *)brush)->id : 0); WbReader r = wb::Call (); return BrushOf (r.u32 ()); }
		void SetTextAlign (Sketchpad::TAlign_horizontal tah, Sketchpad::TAlign_vertical tav) { WbWriter &w = Call (S_SetTextAlign); w.i32 (tah); w.i32 (tav); wb::Call (); }
		DWORD SetTextColor (DWORD col) { Call (S_SetTextColor).u32 (col); WbReader r = wb::Call (); return r.u32 (); }
		DWORD SetBackgroundColor (DWORD col) { Call (S_SetBackgroundColor).u32 (col); WbReader r = wb::Call (); return r.u32 (); }
		void SetBackgroundMode (Sketchpad::BkgMode mode) { Call (S_SetBackgroundMode).i32 (mode); wb::Call (); }
		DWORD GetCharSize () { Call (S_GetCharSize); WbReader r = wb::Call (); return r.u32 (); }
		DWORD GetTextWidth (const char *str, int len) { PutText (Call (S_GetTextWidth), str, len); WbReader r = wb::Call (); return r.u32 (); }
		void SetOrigin (int x, int y) { WbWriter &w = Call (S_SetOrigin); w.i32 (x); w.i32 (y); wb::Call (); }
		bool Text (int x, int y, const char *str, int len)
		{
			WbWriter &w = Call (S_Text); w.i32 (x); w.i32 (y); PutText (w, str, len);
			WbReader r = wb::Call (); return r.b ();
		}
		bool TextBox (int x1, int y1, int x2, int y2, const char *str, int len)
		{
			WbWriter &w = Call (S_TextBox); w.i32 (x1); w.i32 (y1); w.i32 (x2); w.i32 (y2); PutText (w, str, len);
			WbReader r = wb::Call (); return r.b ();
		}
		void Pixel (int x, int y, DWORD col) { WbWriter &w = Call (S_Pixel); w.i32 (x); w.i32 (y); w.u32 (col); wb::Call (); }
		void MoveTo (int x, int y) { WbWriter &w = Call (S_MoveTo); w.i32 (x); w.i32 (y); wb::Call (); }
		void LineTo (int x, int y) { WbWriter &w = Call (S_LineTo); w.i32 (x); w.i32 (y); wb::Call (); }
		void Line (int x0, int y0, int x1, int y1) { WbWriter &w = Call (S_Line); w.i32 (x0); w.i32 (y0); w.i32 (x1); w.i32 (y1); wb::Call (); }
		void Rectangle (int x0, int y0, int x1, int y1) { WbWriter &w = Call (S_Rectangle); w.i32 (x0); w.i32 (y0); w.i32 (x1); w.i32 (y1); wb::Call (); }
		void Ellipse (int x0, int y0, int x1, int y1) { WbWriter &w = Call (S_Ellipse); w.i32 (x0); w.i32 (y0); w.i32 (x1); w.i32 (y1); wb::Call (); }
		void Polygon (const IVECTOR2 *pt, int npt) { Points (Call (S_Polygon), pt, npt); wb::Call (); }
		void Polyline (const IVECTOR2 *pt, int npt) { Points (Call (S_Polyline), pt, npt); wb::Call (); }
		void PolyPolygon (const IVECTOR2 *pt, const int *npt, const int nline) { Poly (S_PolyPolygon, pt, npt, nline); }
		void PolyPolyline (const IVECTOR2 *pt, const int *npt, const int nline) { Poly (S_PolyPolyline, pt, npt, nline); }
		void GetOrigin (int *x, int *y) const { Call (S_GetOrigin); WbReader r = wb::Call (); *x = r.i32 (); *y = r.i32 (); }
		void Poly (int m, const IVECTOR2 *pt, const int *npt, int nline)
		{
			int total = 0;
			for (int i = 0; i < nline; i++) total += npt[i];
			WbWriter &w = Call (m);
			w.i32 (nline);
			for (int i = 0; i < nline; i++) w.i32 (npt[i]);
			Points (w, pt, total);
			wb::Call ();
		}
	};

	// the 2016 SDK's (the start of 2024's)
	class WbSkp : public Sketchpad {
	public:
		WbSkp (SURFHANDLE s, uint32_t id) : Sketchpad (s), c {id} {}
		SkpCalls c;
		Font *SetFont (Font *font) override { return c.SetFont (font); }
		Pen *SetPen (Pen *pen) override { return c.SetPen (pen); }
		Brush *SetBrush (Brush *brush) override { return c.SetBrush (brush); }
		void SetTextAlign (TAlign_horizontal tah, TAlign_vertical tav) override { c.SetTextAlign (tah, tav); }
		DWORD SetTextColor (DWORD col) override { return c.SetTextColor (col); }
		DWORD SetBackgroundColor (DWORD col) override { return c.SetBackgroundColor (col); }
		void SetBackgroundMode (BkgMode mode) override { c.SetBackgroundMode (mode); }
		DWORD GetCharSize () override { return c.GetCharSize (); }
		DWORD GetTextWidth (const char *str, int len) override { return c.GetTextWidth (str, len); }
		void SetOrigin (int x, int y) override { c.SetOrigin (x, y); }
		void GetOrigin (int *x, int *y) const override { c.GetOrigin (x, y); }
		bool Text (int x, int y, const char *str, int len) override { return c.Text (x, y, str, len); }
		bool TextBox (int x1, int y1, int x2, int y2, const char *str, int len) override { return c.TextBox (x1, y1, x2, y2, str, len); }
		void Pixel (int x, int y, DWORD col) override { c.Pixel (x, y, col); }
		void MoveTo (int x, int y) override { c.MoveTo (x, y); }
		void LineTo (int x, int y) override { c.LineTo (x, y); }
		void Line (int x0, int y0, int x1, int y1) override { c.Line (x0, y0, x1, y1); }
		void Rectangle (int x0, int y0, int x1, int y1) override { c.Rectangle (x0, y0, x1, y1); }
		void Ellipse (int x0, int y0, int x1, int y1) override { c.Ellipse (x0, y0, x1, y1); }
		void Polygon (const IVECTOR2 *pt, int npt) override { c.Polygon (pt, npt); }
		void Polyline (const IVECTOR2 *pt, int npt) override { c.Polyline (pt, npt); }
		void PolyPolygon (const IVECTOR2 *pt, const int *npt, const int nline) override { c.PolyPolygon (pt, npt, nline); }
		void PolyPolyline (const IVECTOR2 *pt, const int *npt, const int nline) override { c.PolyPolyline (pt, npt, nline); }
		HDC GetDC () override { return NULL; } // as the SDK allows for a client without GDI, and Orbiter's Windows graphics client returns
	};

	// the 2010 SDK's
	class WbSkp2010 : public wb::Sketchpad2010 {
	public:
		WbSkp2010 (SURFHANDLE s, uint32_t id) : Sketchpad2010 (s), c {id} {}
		SkpCalls c;
		Font *SetFont (Font *font) const override { return c.SetFont (font); }
		Pen *SetPen (Pen *pen) const override { return c.SetPen (pen); }
		Brush *SetBrush (Brush *brush) const override { return c.SetBrush (brush); }
		void SetTextAlign (Sketchpad::TAlign_horizontal tah, Sketchpad::TAlign_vertical tav) override { c.SetTextAlign (tah, tav); }
		DWORD SetTextColor (DWORD col) override { return c.SetTextColor (col); }
		DWORD SetBackgroundColor (DWORD col) override { return c.SetBackgroundColor (col); }
		void SetBackgroundMode (Sketchpad::BkgMode mode) override { c.SetBackgroundMode (mode); }
		DWORD GetCharSize () override { return c.GetCharSize (); }
		DWORD GetTextWidth (const char *str, int len) override { return c.GetTextWidth (str, len); }
		void SetOrigin (int x, int y) override { c.SetOrigin (x, y); }
		bool Text (int x, int y, const char *str, int len) override { return c.Text (x, y, str, len); }
		bool TextBox (int x1, int y1, int x2, int y2, const char *str, int len) override { return c.TextBox (x1, y1, x2, y2, str, len); }
		void Pixel (int x, int y, DWORD col) override { c.Pixel (x, y, col); }
		void MoveTo (int x, int y) override { c.MoveTo (x, y); }
		void LineTo (int x, int y) override { c.LineTo (x, y); }
		void Line (int x0, int y0, int x1, int y1) override { c.Line (x0, y0, x1, y1); }
		void Rectangle (int x0, int y0, int x1, int y1) override { c.Rectangle (x0, y0, x1, y1); }
		void Ellipse (int x0, int y0, int x1, int y1) override { c.Ellipse (x0, y0, x1, y1); }
		void Polygon (const IVECTOR2 *pt, int npt) override { c.Polygon (pt, npt); }
		void Polyline (const IVECTOR2 *pt, int npt) override { c.Polyline (pt, npt); }
		void PolyPolygon (const IVECTOR2 *pt, const int *npt, const int nline) override { c.PolyPolygon (pt, npt, nline); }
		void PolyPolyline (const IVECTOR2 *pt, const int *npt, const int nline) override { c.PolyPolyline (pt, npt, nline); }
		HDC GetDC () override { return NULL; }
	};

	// the helper's sketchpads by address, those Orbiter passes to a callback included while it runs
	struct SkpRec { const void *p; uint32_t id; bool old; };
	SkpRec *g_skp;
	uint32_t g_nskp, g_capskp;
	void SkpAdd (const void *p, uint32_t id, bool old)
	{
		if (g_nskp == g_capskp) { g_capskp = g_capskp ? g_capskp * 2 : 16; g_skp = (SkpRec *)realloc (g_skp, g_capskp * sizeof *g_skp); }
		g_skp[g_nskp++] = {p, id, old};
	}
	uint32_t SkpDrop (const void *p, bool *old = 0) // its id; 0: not one of the helper's
	{
		for (uint32_t i = 0; i < g_nskp; i++)
			if (g_skp[i].p == p) { uint32_t id = g_skp[i].id; if (old) *old = g_skp[i].old; g_skp[i] = g_skp[--g_nskp]; return id; }
		return 0;
	}
	uint32_t SkpId (const void *p)
	{
		for (uint32_t i = 0; p && i < g_nskp; i++) if (g_skp[i].p == p) return g_skp[i].id;
		return 0;
	}
}

Sketchpad *oapiGetSketchpad (SURFHANDLE surf)
{
	bool old = wb::CallerApi (__builtin_return_address (0)) < wb::API_2016;
	wb::Begin (L_GetSketchpad).u32 (wb::Id (surf));
	WbReader r = wb::Call ();
	uint32_t id = r.u32 ();
	if (!id) return 0;
	void *p = old ? (void *)new WbSkp2010 (surf, id) : (void *)static_cast<Sketchpad *> (new WbSkp (surf, id));
	SkpAdd (p, id, old);
	return (Sketchpad *)p;
}

void oapiReleaseSketchpad (Sketchpad *skp)
{
	bool old = false;
	uint32_t id = SkpDrop (skp, &old);
	if (!id) return;
	wb::Begin (L_ReleaseSketchpad).u32 (id);
	wb::Call ();
	if (old) delete (WbSkp2010 *)(void *)skp;
	else delete static_cast<WbSkp *> (skp);
}

// a sketchpad Orbiter passes to a callback, for that call, in the layout of the module's SDK
bool wb::SkpClbk (bool old, uint32_t surf, uint32_t skp, bool (*f) (void *ctx, void *skp), void *ctx)
{
	bool res;
	if (old) {
		WbSkp2010 s ((SURFHANDLE)Ptr (surf), skp);
		SkpAdd (&s, skp, true);
		res = f (ctx, &s);
		SkpDrop (&s);
	} else {
		WbSkp s ((SURFHANDLE)Ptr (surf), skp);
		SkpAdd (static_cast<Sketchpad *> (&s), skp, false);
		res = f (ctx, static_cast<Sketchpad *> (&s));
		SkpDrop (static_cast<Sketchpad *> (&s));
	}
	return res;
}

bool wb::DrawHUD3 (VESSEL3 *v, bool old, int mode, const void *hps, uint32_t surf, uint32_t skp)
{
	struct C { VESSEL3 *v; int mode; const HUDPAINTSPEC *hps; } c {v, mode, (const HUDPAINTSPEC *)hps};
	return SkpClbk (old, surf, skp, [] (void *x, void *s) { C &c = *(C *)x; return c.v->clbkDrawHUD (c.mode, c.hps, (Sketchpad *)s); }, &c);
}

uint32_t wb::SketchpadId (void *skp)
{
	return SkpId (skp);
}

// ---------------------------------------------------------------------
// GDI: oapiGetDC gives a memory DC on a copy of the surface; oapiReleaseDC sends back the pixels GDI changed, keeping their alpha.
// A callback handed an HDC (an MFD's Update) gets one the same way, on the image under Orbiter's painter.

namespace {
	struct DC { uint32_t surf; HDC dc; HBITMAP bmp; HGDIOBJ old; uint32_t *bits, *orig; int w, h; };
	DC g_dc[16];
	int g_ndc;

	void Unchanged (SURFHANDLE surf)
	{
		WbWriter &w = wb::Begin (L_ReleaseDC); w.u32 (wb::Id (surf)); w.b (false);
		wb::Call ();
	}

	// a memory DC on a copy of the W x H image whose rows the reader holds; 0 if it can't be made
	HDC DcMake (WbReader &r, uint32_t surf, int W, int H)
	{
		if (r.bad || W <= 0 || H <= 0 || (uint64_t)W * H * 4 > WB_MSG_MAX || g_ndc == 16) return 0;
		BITMAPINFO bi;
		memset (&bi, 0, sizeof bi);
		bi.bmiHeader.biSize = sizeof bi.bmiHeader;
		bi.bmiHeader.biWidth = W;
		bi.bmiHeader.biHeight = -H; // top-down, as the surface's rows
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		void *bits = 0;
		HDC dc = CreateCompatibleDC (0);
		HBITMAP bmp = dc ? CreateDIBSection (dc, &bi, DIB_RGB_COLORS, &bits, 0, 0) : 0;
		uint32_t *orig = (uint32_t *)malloc ((size_t)W * H * 4);
		if (!bmp || !orig) {
			if (bmp) DeleteObject (bmp);
			if (dc) DeleteDC (dc);
			free (orig);
			return 0;
		}
		r.raw (bits, (uint32_t)W * H * 4);
		memcpy (orig, bits, (size_t)W * H * 4);
		DC &d = g_dc[g_ndc++];
		d.surf = surf; d.dc = dc; d.bmp = bmp; d.bits = (uint32_t *)bits; d.orig = orig; d.w = W; d.h = H;
		d.old = SelectObject (dc, bmp);
		return dc;
	}

	// the DC's record, taken from the table; false if it isn't one
	bool Take (HDC hDC, DC *out)
	{
		int k = -1;
		for (int i = 0; i < g_ndc; i++) if (g_dc[i].dc == hDC) k = i;
		if (k < 0) return false;
		*out = g_dc[k];
		g_dc[k] = g_dc[--g_ndc];
		return true;
	}

	// the rectangle of the pixels whose colour GDI changed (b, then x, y, w, h and its rows), then the DC freed
	void Close (DC &d, WbWriter &w)
	{
		GdiFlush ();
		int x0 = d.w, y0 = d.h, x1 = -1, y1 = -1;
		for (int y = 0; y < d.h; y++) {
			const uint32_t *a = d.bits + (size_t)y * d.w, *b = d.orig + (size_t)y * d.w;
			for (int x = 0; x < d.w; x++)
				if ((a[x] ^ b[x]) & 0x00ffffffu) {
					if (x < x0) x0 = x;
					if (x > x1) x1 = x;
					if (y < y0) y0 = y;
					y1 = y;
				}
		}
		w.b (x1 >= 0);
		if (x1 >= 0) {
			int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
			w.i32 (x0); w.i32 (y0); w.i32 (cw); w.i32 (ch);
			for (int y = y0; y <= y1; y++)
				for (int x = x0; x <= x1; x++) {
					uint32_t c = (d.bits[(size_t)y * d.w + x] & 0x00ffffffu) | (d.orig[(size_t)y * d.w + x] & 0xff000000u); // GDI writes no alpha
					w.u32 (c);
				}
		}
		SelectObject (d.dc, d.old);
		DeleteObject (d.bmp);
		DeleteDC (d.dc);
		free (d.orig);
	}
}

HDC oapiGetDC (SURFHANDLE surf)
{
	WbWriter &w = wb::Begin (L_GetDC); w.u32 (wb::Id (surf));
	WbReader r = wb::Call ();
	bool ok = r.b ();
	int W = r.i32 (), H = r.i32 ();
	if (!ok || r.bad) return 0;
	HDC dc = DcMake (r, wb::Id (surf), W, H);
	if (!dc) Unchanged (surf); // Orbiter's DC is open: closed again
	return dc;
}

void oapiReleaseDC (SURFHANDLE surf, HDC hDC)
{
	DC d;
	if (!Take (hDC, &d)) return;
	WbWriter &w = wb::Begin (L_ReleaseDC);
	w.u32 (d.surf);
	Close (d, w);
	wb::Call ();
}

void *wb::DcOpen (WbReader &r)
{
	int W = r.i32 (), H = r.i32 ();
	return DcMake (r, 0, W, H);
}

void wb::DcClose (void *hDC, WbWriter &w)
{
	DC d;
	if (Take ((HDC)hDC, &d)) Close (d, w);
	else w.b (false);
}

// ---------------------------------------------------------------------
// bitmaps: their pixels make Orbiter's surface or panel background; as Orbiter, it deletes the bitmap

// as Orbiter copies a bitmap with GDI onto a surface: no alpha (a 32-bit device bitmap's fourth byte is 0, not transparency)
static bool BitmapPixels (HBITMAP hBmp, int *W, int *H, uint32_t **px)
{
	BITMAP bm;
	if (!hBmp || !GetObjectA (hBmp, sizeof bm, &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) return false;
	if ((uint64_t)bm.bmWidth * bm.bmHeight * 4 > WB_MSG_MAX - 64) return false;
	BITMAPINFO bi;
	memset (&bi, 0, sizeof bi);
	bi.bmiHeader.biSize = sizeof bi.bmiHeader;
	bi.bmiHeader.biWidth = bm.bmWidth;
	bi.bmiHeader.biHeight = -bm.bmHeight;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	uint32_t *p = (uint32_t *)malloc ((size_t)bm.bmWidth * bm.bmHeight * 4);
	HDC dc = CreateCompatibleDC (0);
	int rows = p && dc ? GetDIBits (dc, hBmp, 0, bm.bmHeight, p, &bi, DIB_RGB_COLORS) : 0;
	if (dc) DeleteDC (dc);
	if (rows != bm.bmHeight) { free (p); return false; }
	for (size_t i = 0; i < (size_t)bm.bmWidth * bm.bmHeight; i++) p[i] |= 0xff000000u;
	*W = bm.bmWidth; *H = bm.bmHeight; *px = p;
	return true;
}

static void PutBitmap (WbWriter &w, int W, int H, const uint32_t *px)
{
	w.i32 (W); w.i32 (H);
	w.raw (px, (uint32_t)W * H * 4);
}

SURFHANDLE oapiCreateSurface (HBITMAP hBmp, bool release_bmp)
{
	int W, H;
	uint32_t *px;
	SURFHANDLE surf = 0;
	if (BitmapPixels (hBmp, &W, &H, &px)) {
		WbWriter &w = wb::Begin (L_CreateSurfaceBmp); PutBitmap (w, W, H, px);
		free (px);
		WbReader r = wb::Call ();
		surf = (SURFHANDLE)wb::Ptr (r.u32 ());
	}
	if (release_bmp) DeleteObject ((HGDIOBJ)hBmp);
	return surf;
}

void oapiRegisterPanelBackground (HBITMAP hBmp, DWORD flag, DWORD ck)
{
	int W, H;
	uint32_t *px;
	if (BitmapPixels (hBmp, &W, &H, &px)) {
		WbWriter &w = wb::Begin (L_RegisterPanelBackground); PutBitmap (w, W, H, px); w.u32 (flag); w.u32 (ck);
		free (px);
		wb::Call ();
	}
	DeleteObject ((HGDIOBJ)hBmp);
}
