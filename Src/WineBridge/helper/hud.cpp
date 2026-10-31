// WineBridge helper: the HDC HUD of a module's vessel. Orbiter 2010 and 2016 called it on the HUD's DC; here the module draws on an
// enhanced metafile DC and its records are then drawn with Orbiter's HUD sketchpad
#define OAPI_IMPLEMENTATION
#include "Orbitersdk.h"
#include <stdlib.h>
#include <string.h>
#include "wb_helper.h"

using namespace oapi;

namespace {
	HDC g_hud;               // the DC of the HUD call running now
	uint32_t g_hudSkp;       // ... Orbiter's sketchpad it stands for
	const char MARK[] = "WineBridge HUD"; // a GDI comment: the records before it are the helper's setup

	// Orbiter's pens, brushes and fonts made for the modules' logical ones, kept across frames, the least recently used going
	enum { T_PEN, T_BRUSH, T_FONT, T_KINDS };
	const int TOOLS = 64;
	struct Tool { bool used; unsigned tick; uint8_t key[96]; void *tool; };
	Tool g_tool[T_KINDS][TOOLS];
	unsigned g_tick;
	void *g_spare[T_KINDS][256]; // made during a replay with every cached one in use: released after it
	int g_nspare[T_KINDS];

	void Release (int kind, void *t)
	{
		if (kind == T_PEN) oapiReleasePen ((Pen *)t);
		else if (kind == T_BRUSH) oapiReleaseBrush ((Brush *)t);
		else oapiReleaseFont ((Font *)t);
	}

	void *Make (int kind, const uint8_t *key)
	{
		if (kind == T_PEN) { const int *k = (const int *)key; return oapiCreatePen (k[0], k[1], (DWORD)k[2]); }
		if (kind == T_BRUSH) { const int *k = (const int *)key; return oapiCreateBrush ((DWORD)k[0]); }
		const int *k = (const int *)key;
		return oapiCreateFont (k[0], k[1] != 0, (const char *)(k + 4), (FontStyle)k[2], k[3]);
	}

	void *Get (int kind, const uint8_t *key)
	{
		Tool *free = 0, *old = 0;
		for (Tool &t : g_tool[kind]) {
			if (t.used && !memcmp (t.key, key, sizeof t.key)) { t.tick = g_tick; return t.tool; }
			if (!t.used) { if (!free) free = &t; }
			else if (t.tick != g_tick && (!old || t.tick < old->tick)) old = &t;
		}
		if (!free && old) { Release (kind, old->tool); old->used = false; free = old; }
		void *tool = Make (kind, key);
		if (!free) { // all in use in this replay
			if (g_nspare[kind] < 256) g_spare[kind][g_nspare[kind]++] = tool;
			return tool;
		}
		free->used = true; free->tick = g_tick; free->tool = tool;
		memcpy (free->key, key, sizeof free->key);
		return tool;
	}

	// a handle of the metafile: a module's pen, brush or font, or the one Orbiter selected (orig), or none (a null pen or brush)
	struct Obj { int kind = -1; void *tool = 0; bool orig = false; };

	struct State {
		Obj pen, brush, font;
		DWORD text, bk; bool textSet, bkSet;
		int bkMode, align; // GDI values; -1: Orbiter's
		POINT org;
	};

	class Replay {
	public:
		Replay (Sketchpad *s) : skp (s) {}
		void Run (const uint8_t *p, uint32_t n);
	private:
		Sketchpad *skp;
		Obj *obj = 0;
		uint32_t nobj = 0;
		State st {{}, {}, {}, 0, 0, false, false, -1, -1, {}};
		State stack[16];
		int depth = 0;
		POINT pos {};
		bool setup = true;
		Pen *origPen = 0; Brush *origBrush = 0; Font *origFont = 0; DWORD origText = 0, origBk = 0;
		bool penSet = false, brushSet = false, fontSet = false, textSet = false, bkSet = false, modeSet = false, alignSet = false;

		int X (int x) const { return x + st.org.x; }
		int Y (int y) const { return y + st.org.y; }
		void Skip (uint32_t type);
		void Select (const Obj &o);
		void Colour (bool bk, bool set, DWORD col);
		void Mode (int mode);
		void Align (int align);
		void Apply (const State &s);
		void Create (uint32_t ih, int kind, const uint8_t *key);
		void Text (const uint8_t *rec, bool wide);
		template <class P> void Points (const P *pt, uint32_t n, IVECTOR2 *out);
		template <class P> void Poly (uint32_t type, const uint8_t *rec, bool to);
		template <class P> void PolyPoly (bool gon, const uint8_t *rec);
	public:
		void End ();
	};

	void Replay::Skip (uint32_t type)
	{
		static bool told[128];
		if (type < 128 && told[type]) return;
		if (type < 128) told[type] = true;
		wb::Log ("a module's HUD drawing (GDI record %u) has no sketchpad equivalent: left out", type);
	}

	void Replay::Select (const Obj &o)
	{
		if (o.kind == T_PEN || (o.kind < 0 && o.tool == (void *)1)) {
			Pen *p = o.orig ? origPen : (Pen *)o.tool;
			if (o.orig && !penSet) return;
			Pen *prev = skp->SetPen (o.kind < 0 ? 0 : p);
			if (!penSet) { origPen = prev; penSet = true; }
			st.pen = o;
		} else if (o.kind == T_BRUSH || (o.kind < 0 && o.tool == (void *)2)) {
			Brush *b = o.orig ? origBrush : (Brush *)o.tool;
			if (o.orig && !brushSet) return;
			Brush *prev = skp->SetBrush (o.kind < 0 ? 0 : b);
			if (!brushSet) { origBrush = prev; brushSet = true; }
			st.brush = o;
		} else if (o.kind == T_FONT) {
			if (o.orig && !fontSet) return;
			Font *prev = skp->SetFont (o.orig ? origFont : (Font *)o.tool);
			if (!fontSet) { origFont = prev; fontSet = true; }
			st.font = o;
		}
	}

	// set: the module's colour; else Orbiter's again
	void Replay::Colour (bool bk, bool set, DWORD col)
	{
		bool &was = bk ? bkSet : textSet;
		DWORD &orig = bk ? origBk : origText;
		if (!set && !was) return;
		DWORD prev = bk ? skp->SetBackgroundColor (set ? col : orig) : skp->SetTextColor (set ? col : orig);
		if (!was) { orig = prev; was = true; }
		(bk ? st.bkSet : st.textSet) = set;
		(bk ? st.bk : st.text) = col;
	}

	void Replay::Mode (int mode)
	{
		if (mode < 0 && !modeSet) return;
		skp->SetBackgroundMode (mode == OPAQUE ? Sketchpad::BK_OPAQUE : Sketchpad::BK_TRANSPARENT);
		modeSet = true;
		st.bkMode = mode;
	}

	void Replay::Align (int a)
	{
		if (a < 0 && !alignSet) return;
		int v = a < 0 ? 0 : a;
		skp->SetTextAlign ((v & TA_CENTER) == TA_CENTER ? Sketchpad::CENTER : (v & TA_RIGHT) ? Sketchpad::RIGHT : Sketchpad::LEFT,
			(v & TA_BASELINE) == TA_BASELINE ? Sketchpad::BASELINE : (v & TA_BOTTOM) ? Sketchpad::BOTTOM : Sketchpad::TOP);
		alignSet = true;
		st.align = a;
	}

	// RestoreDC: the saved state again
	void Replay::Apply (const State &s)
	{
		Select (s.pen); Select (s.brush); Select (s.font);
		Colour (false, s.textSet, s.text);
		Colour (true, s.bkSet, s.bk);
		Mode (s.bkMode);
		Align (s.align);
		st.org = s.org;
	}

	void Replay::Create (uint32_t ih, int kind, const uint8_t *key)
	{
		if (ih >= nobj) return;
		obj[ih].kind = kind;
		obj[ih].orig = setup; // the helper's own, standing for what Orbiter selected
		obj[ih].tool = setup ? 0 : Get (kind, key);
	}

	void Replay::Text (const uint8_t *rec, bool wide)
	{
		const EMREXTTEXTOUTW *r = (const EMREXTTEXTOUTW *)rec;
		uint32_t n = r->emrtext.nChars;
		if (n > 4096) n = 4096;
		const uint8_t *s = rec + r->emrtext.offString;
		wb::Utf8 tw ((const WCHAR *)s, wide ? n : 0), ta ((const char *)s, wide ? 0 : (int)n); // the record's UTF-16, or the code page's
		const wb::Utf8 &t = wide ? tw : ta;
		if (!t.n) return;
		bool cp = st.align >= 0 && (st.align & TA_UPDATECP);
		int x = cp ? pos.x : X (r->emrtext.ptlReference.x), y = cp ? pos.y : Y (r->emrtext.ptlReference.y);
		skp->Text (x, y, t.s, (int)t.n);
		if (cp) pos.x += (int)skp->GetTextWidth (t.s, (int)t.n);
	}

	template <class P> void Replay::Points (const P *pt, uint32_t n, IVECTOR2 *out)
	{
		for (uint32_t i = 0; i < n; i++) { out[i].x = X (pt[i].x); out[i].y = Y (pt[i].y); }
	}

	// a polyline or polygon of n points (POINTS or POINTL); to: from the current position
	template <class P> void Replay::Poly (uint32_t type, const uint8_t *rec, bool to)
	{
		uint32_t n = *(const DWORD *)(rec + 8 + sizeof (RECTL));
		const P *pt = (const P *)(rec + 12 + sizeof (RECTL));
		if (!n || n > 100000) return;
		IVECTOR2 *v = (IVECTOR2 *)malloc ((n + 1) * sizeof *v);
		if (to) {
			v[0].x = pos.x; v[0].y = pos.y;
			Points (pt, n, v + 1);
			skp->Polyline (v, (int)n + 1);
			pos.x = v[n].x; pos.y = v[n].y;
		} else {
			Points (pt, n, v);
			if (type == EMR_POLYGON || type == EMR_POLYGON16) skp->Polygon (v, (int)n);
			else skp->Polyline (v, (int)n);
		}
		free (v);
	}

	template <class P> void Replay::PolyPoly (bool gon, const uint8_t *rec)
	{
		uint32_t np = *(const DWORD *)(rec + 8 + sizeof (RECTL)), n = *(const DWORD *)(rec + 12 + sizeof (RECTL));
		if (!np || !n || np > 10000 || n > 100000) return;
		const DWORD *cnt = (const DWORD *)(rec + 16 + sizeof (RECTL));
		const P *pt = (const P *)(cnt + np);
		IVECTOR2 *v = (IVECTOR2 *)malloc (n * sizeof *v);
		int *c = (int *)malloc (np * sizeof *c);
		Points (pt, n, v);
		for (uint32_t i = 0; i < np; i++) c[i] = (int)cnt[i];
		if (gon) skp->PolyPolygon (v, c, (int)np);
		else skp->PolyPolyline (v, c, (int)np);
		free (v); free (c);
	}

	void Replay::Run (const uint8_t *p, uint32_t n)
	{
		for (uint32_t at = 0; at + 8 <= n;) {
			const EMR *e = (const EMR *)(p + at);
			if (e->nSize < 8 || at + e->nSize > n) break;
			const uint8_t *rec = p + at;
			at += e->nSize;
			switch (e->iType) {
			case EMR_HEADER: {
				const ENHMETAHEADER *h = (const ENHMETAHEADER *)rec;
				nobj = h->nHandles;
				obj = new Obj[nobj ? nobj : 1];
				continue; }
			case EMR_EOF: return;
			case EMR_GDICOMMENT: {
				const EMRGDICOMMENT *c = (const EMRGDICOMMENT *)rec;
				if (setup && c->cbData == sizeof MARK && !memcmp (c->Data, MARK, sizeof MARK)) setup = false;
				continue; }
			case EMR_CREATEPEN: {
				const EMRCREATEPEN *c = (const EMRCREATEPEN *)rec;
				int k[24] = {};
				UINT style = c->lopn.lopnStyle & PS_STYLE_MASK;
				k[0] = style == PS_NULL ? 0 : style == PS_SOLID || style == PS_INSIDEFRAME ? 1 : 2;
				k[1] = c->lopn.lopnWidth.x ? c->lopn.lopnWidth.x : 1; k[2] = (int)c->lopn.lopnColor;
				Create (c->ihPen, T_PEN, (const uint8_t *)k);
				continue; }
			case EMR_EXTCREATEPEN: {
				const EMREXTCREATEPEN *c = (const EMREXTCREATEPEN *)rec;
				int k[24] = {};
				DWORD style = c->elp.elpPenStyle & PS_STYLE_MASK;
				k[0] = style == PS_NULL ? 0 : style == PS_SOLID || style == PS_INSIDEFRAME ? 1 : 2;
				k[1] = c->elp.elpWidth ? (int)c->elp.elpWidth : 1; k[2] = (int)c->elp.elpColor;
				Create (c->ihPen, T_PEN, (const uint8_t *)k);
				continue; }
			case EMR_CREATEBRUSHINDIRECT: {
				const EMRCREATEBRUSHINDIRECT *c = (const EMRCREATEBRUSHINDIRECT *)rec;
				if (c->ihBrush >= nobj) continue;
				if (c->lb.lbStyle == BS_NULL) { obj[c->ihBrush] = Obj (); obj[c->ihBrush].tool = (void *)2; obj[c->ihBrush].orig = setup; continue; }
				int k[24] = {};
				k[0] = (int)c->lb.lbColor;
				Create (c->ihBrush, T_BRUSH, (const uint8_t *)k);
				continue; }
			case EMR_EXTCREATEFONTINDIRECTW: {
				const EMREXTCREATEFONTINDIRECTW *c = (const EMREXTCREATEFONTINDIRECTW *)rec;
				const LOGFONTW &f = c->elfw.elfLogFont;
				int k[24] = {};
				k[0] = f.lfHeight < 0 ? -f.lfHeight : f.lfHeight;
				k[1] = (f.lfPitchAndFamily & 3) != FIXED_PITCH;
				k[2] = (f.lfWeight >= FW_BOLD ? FONT_BOLD : 0) | (f.lfItalic ? FONT_ITALIC : 0) | (f.lfUnderline ? FONT_UNDERLINE : 0);
				k[3] = f.lfEscapement;
				char *face = (char *)(k + 4);
				for (int i = 0; i < 31 && f.lfFaceName[i]; i++) face[i] = f.lfFaceName[i] < 128 ? (char)f.lfFaceName[i] : '?';
				Create (c->ihFont, T_FONT, (const uint8_t *)k);
				continue; }
			case EMR_DELETEOBJECT: {
				uint32_t ih = ((const EMRDELETEOBJECT *)rec)->ihObject;
				if (ih < nobj) obj[ih] = Obj ();
				continue; }
			default:
				break;
			}
			if (setup) continue; // the helper's own state: Orbiter's is there already
			switch (e->iType) {
			case EMR_SELECTOBJECT: {
				uint32_t ih = ((const EMRSELECTOBJECT *)rec)->ihObject;
				Obj o;
				if (ih & 0x80000000u) { // a stock object: the pens and fonts a module selects back stand for Orbiter's
					uint32_t s = ih & 0x7fffffffu;
					static const DWORD gray[] = {0xffffff, 0xc0c0c0, 0x808080, 0x404040, 0x000000};
					int k[24] = {};
					if (s <= BLACK_BRUSH) { k[0] = (int)gray[s]; o.kind = T_BRUSH; o.tool = Get (T_BRUSH, (const uint8_t *)k); }
					else if (s == NULL_BRUSH) o.tool = (void *)2;
					else if (s == WHITE_PEN) { k[0] = 1; k[1] = 1; k[2] = 0xffffff; o.kind = T_PEN; o.tool = Get (T_PEN, (const uint8_t *)k); }
					else if (s == NULL_PEN) o.tool = (void *)1;
					else if (s == BLACK_PEN || s == DC_PEN) { o.kind = T_PEN; o.orig = true; }
					else if (s == DC_BRUSH) { o.kind = T_BRUSH; o.orig = true; }
					else { o.kind = T_FONT; o.orig = true; }
				} else if (ih < nobj) o = obj[ih];
				else continue;
				Select (o);
				break; }
			case EMR_SETTEXTCOLOR: Colour (false, true, ((const EMRSETTEXTCOLOR *)rec)->crColor); break;
			case EMR_SETBKCOLOR: Colour (true, true, ((const EMRSETBKCOLOR *)rec)->crColor); break;
			case EMR_SETBKMODE: Mode ((int)((const EMRSETBKMODE *)rec)->iMode); break;
			case EMR_SETTEXTALIGN: Align ((int)((const EMRSETTEXTALIGN *)rec)->iMode); break;
			case EMR_SETWINDOWORGEX: { const POINTL &o = ((const EMRSETWINDOWORGEX *)rec)->ptlOrigin; st.org.x = -o.x; st.org.y = -o.y; break; }
			case EMR_SETVIEWPORTORGEX: { const POINTL &o = ((const EMRSETVIEWPORTORGEX *)rec)->ptlOrigin; st.org.x += o.x; st.org.y += o.y; break; }
			case EMR_SAVEDC: if (depth < 16) stack[depth++] = st; break;
			case EMR_RESTOREDC: {
				int rel = ((const EMRRESTOREDC *)rec)->iRelative;
				int to = rel < 0 ? depth + rel : rel - 1;
				if (to >= 0 && to < depth) { depth = to; Apply (stack[to]); }
				break; }
			case EMR_MOVETOEX: { const POINTL &q = ((const EMRMOVETOEX *)rec)->ptl; pos.x = X (q.x); pos.y = Y (q.y); break; }
			case EMR_LINETO: {
				const POINTL &q = ((const EMRLINETO *)rec)->ptl;
				skp->MoveTo (pos.x, pos.y);
				pos.x = X (q.x); pos.y = Y (q.y);
				skp->LineTo (pos.x, pos.y);
				break; }
			case EMR_POLYLINE: case EMR_POLYGON: Poly<POINTL> (e->iType, rec, false); break;
			case EMR_POLYLINE16: case EMR_POLYGON16: Poly<POINTS> (e->iType, rec, false); break;
			case EMR_POLYLINETO: Poly<POINTL> (e->iType, rec, true); break;
			case EMR_POLYLINETO16: Poly<POINTS> (e->iType, rec, true); break;
			case EMR_POLYPOLYLINE: PolyPoly<POINTL> (false, rec); break;
			case EMR_POLYPOLYGON: PolyPoly<POINTL> (true, rec); break;
			case EMR_POLYPOLYLINE16: PolyPoly<POINTS> (false, rec); break;
			case EMR_POLYPOLYGON16: PolyPoly<POINTS> (true, rec); break;
			case EMR_RECTANGLE: { const RECTL &b = ((const EMRRECTANGLE *)rec)->rclBox; skp->Rectangle (X (b.left), Y (b.top), X (b.right), Y (b.bottom)); break; }
			case EMR_ELLIPSE: { const RECTL &b = ((const EMRELLIPSE *)rec)->rclBox; skp->Ellipse (X (b.left), Y (b.top), X (b.right), Y (b.bottom)); break; }
			case EMR_SETPIXELV: { const EMRSETPIXELV *s = (const EMRSETPIXELV *)rec; skp->Pixel (X (s->ptlPixel.x), Y (s->ptlPixel.y), s->crColor); break; }
			case EMR_EXTTEXTOUTW: Text (rec, true); break;
			case EMR_EXTTEXTOUTA: Text (rec, false); break;
			case EMR_SETMAPMODE: if (((const EMRSETMAPMODE *)rec)->iMode != MM_TEXT) Skip (e->iType); break;
			case EMR_SETBRUSHORGEX: case EMR_SETMITERLIMIT: case EMR_SETPOLYFILLMODE: case EMR_SETROP2: case EMR_SETSTRETCHBLTMODE:
			case EMR_SETICMMODE: case EMR_SETLAYOUT: case EMR_SELECTPALETTE: case EMR_REALIZEPALETTE:
				break; // nothing a sketchpad draws differently
			default:
				Skip (e->iType);
				break;
			}
		}
	}

	// Orbiter's sketchpad as the HUD call left it
	void Replay::End ()
	{
		if (penSet) skp->SetPen (origPen);
		if (brushSet) skp->SetBrush (origBrush);
		if (fontSet) skp->SetFont (origFont);
		if (textSet) skp->SetTextColor (origText);
		if (bkSet) skp->SetBackgroundColor (origBk);
		if (modeSet) skp->SetBackgroundMode (Sketchpad::BK_TRANSPARENT);
		if (alignSet) skp->SetTextAlign (Sketchpad::LEFT, Sketchpad::TOP);
		delete []obj;
		obj = 0;
	}

	struct HudCall { VESSEL2 *v; int mode; const HUDPAINTSPEC *hps; uint32_t skp; };

	bool Draw (void *ctx, void *s)
	{
		HudCall &c = *(HudCall *)ctx;
		HDC dc = CreateEnhMetaFileA (0, 0, 0, 0);
		if (!dc) {
			static bool said;
			if (!said) { said = true; wb::Log ("no metafile DC (Windows error %lu): a module's HDC HUD is not drawn", GetLastError ()); }
			return false;
		}
		// as HUD::Draw leaves the HUD's DC: a pen, the HUD font, the HUD colour, a transparent background, no brush
		HPEN pen = CreatePen (PS_SOLID, 1, RGB (0, 255, 0));
		HFONT font = CreateFontA (-(c.hps->H / 50 > 8 ? c.hps->H / 50 : 8), 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
			CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, VARIABLE_PITCH | FF_SWISS, "Arial");
		HGDIOBJ op = SelectObject (dc, pen), of = SelectObject (dc, font), ob = SelectObject (dc, GetStockObject (NULL_BRUSH));
		SetTextColor (dc, RGB (0, 255, 0));
		SetBkMode (dc, TRANSPARENT);
		GdiComment (dc, sizeof MARK, (const BYTE *)MARK);
		HDC prevDc = g_hud;
		uint32_t prevSkp = g_hudSkp;
		g_hud = dc; g_hudSkp = c.skp;
		c.v->clbkDrawHUD (c.mode, c.hps, dc);
		g_hud = prevDc; g_hudSkp = prevSkp;
		SelectObject (dc, op); SelectObject (dc, of); SelectObject (dc, ob);
		HENHMETAFILE mf = CloseEnhMetaFile (dc);
		DeleteObject (pen); DeleteObject (font);
		if (!mf) return false;
		UINT n = GetEnhMetaFileBits (mf, 0, 0);
		uint8_t *bits = n ? (uint8_t *)malloc (n) : 0;
		if (bits && GetEnhMetaFileBits (mf, n, bits) == n) {
			g_tick++;
			Replay r ((Sketchpad *)s);
			r.Run (bits, n);
			r.End ();
			for (int k = 0; k < T_KINDS; k++) { while (g_nspare[k]) Release (k, g_spare[k][--g_nspare[k]]); }
		}
		free (bits);
		DeleteEnhMetaFile (mf);
		return true;
	}
}

bool wb::DrawHUD2 (VESSEL2 *v, int mode, const void *hps, uint32_t surf, uint32_t skp)
{
	HudCall c {v, mode, (const HUDPAINTSPEC *)hps, skp};
	return SkpClbk (false, surf, skp, Draw, &c);
}

// VESSEL2's own HDC HUD (a module calling its base): Orbiter's default HUD, on the sketchpad of the HUD call that DC is for
bool wb::HudDefault (uint32_t vid, int mode, const void *hps, void *hDC)
{
	if (!hDC || hDC != g_hud) return false;
	WbWriter &w = Begin (L_BaseDrawHUD3);
	w.u32 (vid); w.i32 (mode); PutHps (w, hps); w.u32 (g_hudSkp);
	WbReader r = Call ();
	return r.b ();
}
