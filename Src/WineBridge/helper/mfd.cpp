// WineBridge helper: MFD modes, MFDs and external MFDs (MfdUser.cpp, MFDAPI.cpp, Mfd.cpp of the Windows source). Orbiter's
// instrument of each object is on the Linux side; the modules' objects are here, each with a proxy there.
#define OAPI_IMPLEMENTATION
#include "Orbitersdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "wb_helper.h"
#include "compat2010.h"

#define MAXDEFCOL 5 // Orbiter's default MFD colours (Instrument::draw)

typedef OAPI_MSGTYPE (*MsgProc) (UINT, UINT, WPARAM, LPARAM);

// ---------------------------------------------------------------------
// Orbiter's instrument of an MFD object: MFD::instr, by the name MFD makes its friend

class Instrument_User {
public:
	uint32_t num, link; // the object's number, its Linux proxy's link
	MFD *mfd;           // 0 once the object or its module is gone
	MFD2 *mfd2;         // the object as an MFD2 (dynamic_cast), else 0
	int module;         // the module of its class
	bool old;           // its class is of the 2010 SDK: 2010 sketchpads
	int IW, IH;
	DWORD cw, ch;
	HFONT font[4];      // the GDI fonts of Orbiter's MFD fonts (Instrument::SetSize), made when first used
	oapi::Pen *pen[MAXDEFCOL][2][2]; bool hasPen[MAXDEFCOL][2][2];
	oapi::Font *dfont[4]; bool hasFont[4];
	DWORD col[MAXDEFCOL][2]; bool hasCol[MAXDEFCOL][2];

	static void Attach (MFD *m, Instrument_User *i) { m->instr = i; i->Sync (); }
	static Instrument_User *Of (const MFD *m) { return m->instr; }
	static void Detach (MFD *m) { m->instr = 0; }
	void Sync () { if (mfd) mfd->cw = cw, mfd->ch = ch; } // as Orbiter sets them after the mode made the object
};

namespace {
	Instrument_User **g_ins;
	uint32_t g_nins, g_capins, g_nextIns;

	Instrument_User *Ins (uint32_t num)
	{
		for (uint32_t i = 0; i < g_nins; i++) if (g_ins[i]->num == num) return g_ins[i];
		return 0;
	}
	void InsDrop (Instrument_User *in)
	{
		for (uint32_t i = 0; i < g_nins; i++) if (g_ins[i] == in) { g_ins[i] = g_ins[--g_nins]; break; }
		if (in->mfd) Instrument_User::Detach (in->mfd);
		for (int i = 0; i < 4; i++) if (in->font[i]) DeleteObject (in->font[i]);
		free (in);
	}

	// Orbiter's MFD fonts: h = IH*9/200 (Instrument::SetSize), as GDI fonts the way the clients made them
	HFONT InsFont (Instrument_User *in, int i)
	{
		if (!in->font[i]) {
			int h = (in->IH * 9) / 200;
			static const struct { int num, den; bool prop; const char *face; int orient; } spec[4] = {
				{1, 1, false, "Fixed", 0}, {3, 4, true, "Sans", 0}, {3, 4, true, "Sans", 900}, {1, 1, true, "Sans", 0}};
			in->font[i] = (HFONT)wb::GdiFont (-(h * spec[i].num) / spec[i].den, spec[i].prop, spec[i].face, FONT_NORMAL, spec[i].orient);
		}
		return in->font[i];
	}

	// Orbiter's deprecated GDI pens (Instrument::GlobalInit)
	HPEN DefPen (int i)
	{
		static HPEN pen[6];
		static const COLORREF col[6] = {RGB (0,255,0), RGB (0,255,0), RGB (0,128,0), RGB (128,128,0), RGB (64,64,0), RGB (128,128,128)};
		if (!pen[i]) pen[i] = CreatePen (PS_SOLID, 0, col[i]);
		return pen[i];
	}

	const COLORREF col_grey1 = RGB (224,224,224); // Instrument::col_grey1

	WbWriter &Back (const Instrument_User *in, int m) { WbWriter &w = wb::Begin (L_Mfd); w.u32 (in->link); w.i32 (m); return w; }

	// as dynamic_cast<MFD2 *> in Orbiter (MSVC RTTI): the complete object locator before the object's vtable, its class
	// hierarchy's base classes; every pointer read lies in the image of the module the vtable is in, else it isn't one
	struct RttiTd { const void *vft; const void *spare; char name[1]; };
	struct RttiBcd { const RttiTd *type; DWORD contained; int mdisp, pdisp, vdisp; DWORD attributes; };
	struct RttiChd { DWORD signature, attributes, nbases; const RttiBcd *const *bases; };
	struct RttiCol { DWORD signature, offset, cdOffset; const RttiTd *type; const RttiChd *hierarchy; };

	MFD2 *AsMfd2 (MFD *m)
	{
		const uint8_t *vt = *(const uint8_t *const *)m;
		HMODULE h = 0;
		if (!GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)vt, &h) || !h) return 0;
		const uint8_t *base = (const uint8_t *)h;
		const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
		const uint8_t *end = base + nt->OptionalHeader.SizeOfImage;
		auto in = [&] (const void *p, size_t n) { const uint8_t *q = (const uint8_t *)p; return q >= base && q <= end && n <= (size_t)(end - q); };
		if (!in (vt - 4, 4)) return 0;
		const RttiCol *col = *(const RttiCol *const *)(vt - 4);
		if (!in (col, sizeof *col) || col->signature != 0) return 0;
		const RttiChd *chd = col->hierarchy;
		if (!in (chd, sizeof *chd) || chd->nbases > 256 || !in (chd->bases, chd->nbases * sizeof *chd->bases)) return 0;
		uint8_t *complete = (uint8_t *)m - col->offset;
		static const char want[] = ".?AVMFD2@@";
		for (DWORD i = 0; i < chd->nbases; i++) {
			const RttiBcd *b = chd->bases[i];
			if (!in (b, sizeof *b) || !in (b->type, offsetof (RttiTd, name) + sizeof want)) return 0;
			if (memcmp (b->type->name, want, sizeof want)) continue;
			if (b->attributes & 3) return 0; // not visible or ambiguous: the cast fails
			int off = b->mdisp;
			if (b->pdisp >= 0) off += *(const int *)(*(const uint8_t *const *)(complete + b->pdisp) + b->vdisp) + b->pdisp;
			return (MFD2 *)(complete + off);
		}
		return 0;
	}
}

// ---------------------------------------------------------------------
// MFD, MFD2 (MfdUser.cpp of the Windows source)

MFD::MFD (DWORD w, DWORD h, VESSEL *vessel)
{
	instr = 0;
	W = w, H = h;
	pV = vessel;
}

MFD::~MFD ()
{
	if (instr) instr->mfd = instr->mfd2 = 0; // its proxy draws nothing from now
}

void MFD::Title (HDC hDC, const char *title) const
{
	// Instrument::DisplayTitle (GDI)
	if (!instr || !title) return;
	SetTextColor (hDC, col_grey1);
	TextOutA (hDC, instr->cw / 2, 0, title, (int)strlen (title));
}

void MFD::InvalidateDisplay ()
{
	if (!instr) return;
	Back (instr, B_InvalidateDisplay);
	wb::Call ();
}

void MFD::InvalidateButtons ()
{
	if (!instr) return;
	Back (instr, B_InvalidateButtons);
	wb::Call ();
}

HPEN MFD::SelectDefaultPen (HDC hDC, DWORD i) const
{
	return (i < 6 ? (HPEN)SelectObject (hDC, DefPen (i)) : 0);
}

HFONT MFD::SelectDefaultFont (HDC hDC, DWORD i) const
{
	return (i < 4 && instr ? (HFONT)SelectObject (hDC, InsFont (instr, i)) : 0);
}

bool MFD2::Update (oapi::Sketchpad *skp)
{
	return false;
}

void MFD2::Title (oapi::Sketchpad *skp, const char *title) const
{
	if (!instr) return;
	WbWriter &w = Back (instr, B_Title); w.u32 (wb::SketchpadId (skp)); w.str (title);
	wb::Call ();
}

// Orbiter's default pens, fonts and colours never change for an instrument: asked once
oapi::Pen *MFD2::GetDefaultPen (DWORD colidx, DWORD intens, DWORD style) const
{
	if (!instr || colidx >= MAXDEFCOL || intens >= 2 || style < 1 || style > 2) return 0;
	Instrument_User *in = instr;
	if (!in->hasPen[colidx][intens][style - 1]) {
		WbWriter &w = Back (in, B_Pen); w.u32 (colidx); w.u32 (intens); w.u32 (style);
		WbReader r = wb::Call ();
		uint32_t id = r.u32 ();
		if (r.bad) return 0;
		in->pen[colidx][intens][style - 1] = wb::PenOf (id);
		in->hasPen[colidx][intens][style - 1] = true;
	}
	return in->pen[colidx][intens][style - 1];
}

oapi::Font *MFD2::GetDefaultFont (DWORD fontidx) const
{
	if (!instr || fontidx >= 4) return 0;
	Instrument_User *in = instr;
	if (!in->hasFont[fontidx]) {
		Back (in, B_Font).u32 (fontidx);
		WbReader r = wb::Call ();
		uint32_t id = r.u32 ();
		if (r.bad) return 0;
		in->dfont[fontidx] = wb::FontOf (id);
		in->hasFont[fontidx] = true;
	}
	return in->dfont[fontidx];
}

DWORD MFD2::GetDefaultColour (DWORD colidx, DWORD intens) const
{
	if (!instr || colidx >= MAXDEFCOL || intens >= 2) return 0;
	Instrument_User *in = instr;
	if (!in->hasCol[colidx][intens]) {
		WbWriter &w = Back (in, B_Colour); w.u32 (colidx); w.u32 (intens);
		WbReader r = wb::Call ();
		DWORD c = r.u32 ();
		if (r.bad) return 0;
		in->col[colidx][intens] = c;
		in->hasCol[colidx][intens] = true;
	}
	return in->col[colidx][intens];
}

// ---------------------------------------------------------------------
// GraphMFD (MfdUser.cpp of the Windows source)

// an axis title with its scale, as the source's ostringstream wrote it (a double: %g)
static const char *AxisTitle (char *buf, size_t n, const char *title, float tickscale)
{
	if (tickscale != 1.0f) snprintf (buf, n, "%s x %g", title, 1.0/tickscale);
	else snprintf (buf, n, "%s", title);
	return buf;
}

GraphMFD::GraphMFD (DWORD w, DWORD h, VESSEL *vessel)
: MFD (w, h, vessel)
{
	ngraph  = 0;
}

GraphMFD::~GraphMFD ()
{
	if (ngraph) {
		for (int g = 0; g < ngraph; g++) {
			if (graph[g].nplot) {
				delete []graph[g].plot;
				graph[g].plot = NULL;
			}
		}
		delete []graph;
		graph = NULL;
	}
}

int GraphMFD::AddGraph (void)
{
	GRAPH *tmp = new GRAPH[ngraph+1];
	if (ngraph) {
		memcpy (tmp, graph, ngraph*sizeof(GRAPH));
		delete []graph;
	}
	graph = tmp;
	graph[ngraph].nplot = 0;
	graph[ngraph].absc_title[0] = graph[ngraph].absc_title[63] = '\0';
	graph[ngraph].data_title[0] = graph[ngraph].data_title[63] = '\0';
	SetRange (ngraph, 0, -1.0, 1.0); // arbitrary default
	SetRange (ngraph, 1, -1.0, 1.0); // arbitrary default
	return ngraph++;
}

void GraphMFD::AddPlot (int g, float *absc, float *data, int ndata, int col, int *ofs)
{
	int np = graph[g].nplot;
	GRAPH::PLOT *tmp_plot = new GRAPH::PLOT[np+1];
	if (np) {
		memcpy (tmp_plot, graph[g].plot, np*sizeof(GRAPH::PLOT));
		delete []graph[g].plot;
		graph[g].plot = NULL;
	}
	tmp_plot[np].absc  = absc;
	tmp_plot[np].data  = data;
	tmp_plot[np].ndata = ndata;
	tmp_plot[np].col   = col;
	tmp_plot[np].ofs   = ofs;
	graph[g].plot      = tmp_plot;
	graph[g].nplot++;
}

void GraphMFD::SetRange (int g, int axis, float rmin, float rmax)
{
	switch (axis) {
	case 0:
		graph[g].absc_min = rmin;
		graph[g].absc_max = rmax;
		break;
	case 1:
		graph[g].data_min = rmin;
		graph[g].data_max = rmax;
		break;
	}
	SetAutoTicks (g, axis);
}

void GraphMFD::SetAutoRange (int g, int axis, int p)
{
	float rmin, rmax, r1, r2;
	int i;
	int p0 = (p >= 0 ? p : 0);
	int p1 = (p >= 0 ? p+1 : graph[g].nplot);

	switch (axis) {
	case 0:
		for (i = p0; i < p1; i++) {
			FindRange (graph[g].plot[i].absc, graph[g].plot[i].ndata, r1, r2);
			if (i == p0 || r1 < rmin) rmin = r1;
			if (i == p0 || r2 > rmax) rmax = r2;
		}
		if (rmin == rmax) rmin -= 0.5f, rmax += 0.5f;
		SetRange (g, axis, rmin, rmax);
		break;
	case 1:
		for (i = p0; i < p1; i++) {
			FindRange (graph[g].plot[i].data, graph[g].plot[i].ndata, r1, r2);
			if (i == p0 || r1 < rmin) rmin = r1;
			if (i == p0 || r2 > rmax) rmax = r2;
		}
		if (rmin == rmax) rmin -= 0.5f, rmax += 0.5f;
		SetRange (g, axis, rmin, rmax);
		break;
	}
}

void GraphMFD::SetAutoTicks (int g, int axis)
{
	GRAPH &gf = graph[g];
	float rmin, rmax, dr, scale;
	int mintick = 1;

	rmin = (axis ? gf.data_min : gf.absc_min);
	rmax = (axis ? gf.data_max : gf.absc_max);
	dr = rmax-rmin;
	scale = 1.0f;
	while (dr <= 1.0f) dr *= 10.0f, scale *= 10.0f;
	while (dr > 10.0f) dr *= 0.1f, scale *= 0.1f;
	if (dr < 2.0) mintick = 10;
	else if (dr < 4.0) mintick = 5;
	else if (dr < 8.0) mintick = 2;
	if (axis) {
		gf.data_tickscale = scale;
		gf.data_dtick = 1.0f/scale;
		gf.data_tickmin = (float)ceil(rmin*scale)/scale;
		gf.data_minortick = mintick;
	} else {
		gf.absc_tickscale = scale;
		gf.absc_dtick = 1.0f/scale;
		gf.absc_tickmin = (float)ceil(rmin*scale)/scale;
		gf.absc_minortick = mintick;
	}
}

void GraphMFD::SetAxisTitle (int g, int axis, char *title)
{
	if (axis)
		strncpy (graph[g].data_title, title, 63);
	else
		strncpy (graph[g].absc_title, title, 63);
}

void GraphMFD::Plot (HDC hDC, int g, int h0, int h1, const char *title)
{
	GRAPH &gf = graph[g];
	char cbuf[64];
	float minx, maxx, miny, maxy, ixrange, iyrange, f;
	int i, j, pl, x0, y0, x1, y1, x, y;
	minx = gf.absc_min; maxx = gf.absc_max;
	miny = gf.data_min; maxy = gf.data_max;
	x0 = cw*3, x1 = W-cw;
	y0 = h1-(3*ch)/2, y1 = h0+ch/2;
	ixrange = (x1-x0)/(maxx-minx);
	iyrange = (y0-y1)/(maxy-miny);

	SelectDefaultFont (hDC, 1);
	SetTextColor (hDC, 0x00A000);

	// abscissa ticks/labels
	SelectDefaultPen (hDC, 3);
	SetTextAlign (hDC, TA_CENTER);
	for (f = gf.absc_tickmin; f <= gf.absc_max; f += gf.absc_dtick) {
		x = x0 + (int)((f-minx)*ixrange+0.5);
		MoveToEx (hDC, x, y0, 0);
		LineTo (hDC, x, y1);
		sprintf (cbuf, "%0.0f", f*gf.absc_tickscale);
		TextOut (hDC, x, y0, cbuf, strlen(cbuf));
	}
	if (gf.absc_minortick > 1) {
		SelectDefaultPen (hDC, 4);
		for (f = gf.absc_tickmin, i = 0; f > gf.absc_min; f -= gf.absc_dtick/(float)gf.absc_minortick, i++) {
			if (!(i%gf.absc_minortick)) continue;
			x = x0 + (int)((f-minx)*ixrange+0.5);
			MoveToEx (hDC, x, y0, 0); LineTo (hDC, x, y1);
		}
		for (f = gf.absc_tickmin, i = 0; f < gf.absc_max; f += gf.absc_dtick/(float)gf.absc_minortick, i++) {
			if (!(i%gf.absc_minortick)) continue;
			x = x0 + (int)((f-minx)*ixrange+0.5);
			MoveToEx (hDC, x, y0, 0); LineTo (hDC, x, y1);
		}
	}
	if (gf.absc_title[0]) {
		char tbuf[96];
		AxisTitle (tbuf, sizeof tbuf, gf.absc_title, gf.absc_tickscale);
		TextOut (hDC, (x0+x1)/2, y0+(3*ch)/4, tbuf, strlen(tbuf));
	}

	// ordinate ticks/labels
	SelectDefaultPen (hDC, 3);
	SetTextAlign (hDC, TA_RIGHT);
	for (f = gf.data_tickmin; f <= gf.data_max; f += gf.data_dtick) {
		y = y0 - (int)((f-miny)*iyrange+0.5);
		MoveToEx (hDC, x0, y, 0);
		LineTo (hDC, x1, y);
		sprintf (cbuf, "%0.0f", f*gf.data_tickscale);
		TextOut (hDC, x0, y-ch/2, cbuf, strlen(cbuf));
	}
	if (gf.data_minortick > 1) {
		SelectDefaultPen (hDC, 4);
		for (f = gf.data_tickmin, i = 0; f > gf.data_min; f -= gf.data_dtick/(float)gf.data_minortick, i++) {
			if (!(i%gf.data_minortick)) continue;
			y = y0 - (int)((f-miny)*iyrange+0.5);
			MoveToEx (hDC, x0, y, 0); LineTo (hDC, x1, y);
		}
		for (f = gf.data_tickmin, i = 0; f < gf.data_max; f += gf.data_dtick/(float)gf.data_minortick, i++) {
			if (!(i%gf.data_minortick)) continue;
			y = y0 - (int)((f-miny)*iyrange+0.5);
			MoveToEx (hDC, x0, y, 0); LineTo (hDC, x1, y);
		}
	}
	SetTextAlign (hDC, TA_CENTER);
	if (gf.data_title[0]) {
		SelectDefaultFont (hDC, 2);
		char tbuf[96];
		AxisTitle (tbuf, sizeof tbuf, gf.data_title, gf.data_tickscale);
		TextOut (hDC, 0, (y0+y1)/2, tbuf, strlen(tbuf));
	}

	// plot frame
	SelectDefaultPen (hDC, 2);
	Rectangle (hDC, x0, y1, x1+1, y0+1);

	// plot line
	for (pl = 0; pl < gf.nplot; pl++) {
		GRAPH::PLOT &p = gf.plot[pl];
		SelectDefaultPen (hDC, p.col);
		float fx, fy, pfx=0, pfy=0, xa, ya, xb, yb;
		bool clip, vis;
		int sample = (p.ofs ? *p.ofs : 0);
		for (i = 0; i < p.ndata; i++) {
			j = (i+sample) % p.ndata;
			fx = xb = p.absc[j];	xa = pfx;
			fy = yb = p.data[j];	ya = pfy;

			if (!i) {
				clip = true;
			} else {
				if (clip) { // clip first point
					vis = true;
					if (ya < miny) {        // clip at lower edge
						if (yb < miny) vis = false;
						else xa += (miny-ya)/(yb-ya)*(xb-xa), ya = miny;
					} else if (ya > maxy) { // clip at upper edge
						if (yb > maxy) vis = false;
						else xa += (maxy-ya)/(yb-ya)*(xb-xa), ya = maxy;
					}
					if (xa < minx) {        // clip at left edge
						if (xb < minx) vis = false;
						else ya += (minx-xa)/(xb-xa)*(yb-ya), xa = minx;
					} else if (xa > maxx) { // clip at right edge
						if (xb > maxx) vis = false;
						else ya += (maxx-xa)/(xb-xa)*(yb-ya), xa = maxx;
					}
					if (vis) MoveToEx (hDC, x0 + (int)((xa-minx)*ixrange), y0 - (int)((ya-miny)*iyrange), 0);
				} else vis = true;

				if (vis) { // clip second point
					clip = false;
					if (yb < miny) {        // clip at lower edge
						if (ya < miny) vis = false;
						else xb += (miny-yb)/(ya-yb)*(xa-xb), yb = miny;
						clip = true;
					} else if (yb > maxy) { // clip at upper edge
						if (ya > maxy) vis = false;
						else xb += (maxy-yb)/(ya-yb)*(xa-xb), yb = maxy;
						clip = true;
					}
					if (xb < minx) {        // clip at left edge
						if (xa < minx) vis = false;
						else yb += (minx-xb)/(xa-xb)*(ya-yb), xb = minx;
						clip = true;
					} else if (xb > maxx) { // clip at right edge
						if (xa > maxx) vis = false;
						else yb += (maxx-xb)/(xa-xb)*(ya-yb), xb = maxx;
						clip = true;
					}
					if (vis) LineTo (hDC, x0 + (int)((xb-minx)*ixrange), y0 - (int)((yb-miny)*iyrange));
				} else clip = true;
			}
			pfx = fx, pfy = fy;
		}
	}

	if (title) {
		SelectDefaultFont (hDC, 1);
		SetTextColor (hDC, 0x00FF00);
		TextOut (hDC, (x0+x1)/2, y1, title, strlen(title));
	}
}

void GraphMFD::FindRange (float *d, int ndata, float &dmin, float &dmax) const
{
	dmin = dmax = d[0];
	for (int i = 1; i < ndata; i++) {
		if      (d[i] < dmin) dmin = d[i];
		else if (d[i] > dmax) dmax = d[i];
	}
}

// ---------------------------------------------------------------------
// modes: the module's spec kept here as Orbiter keeps its copy; Orbiter's mode has the same name and key

namespace {
	struct Mode {
		int num, id, module;
		uint32_t vessel;    // 0: every vessel's
		bool dead;          // its module is gone
		MFDMODESPECEX spec; // what the module sees as Orbiter's copy: the name copied, Orbiter's key
		MFDMODESPEC old;    // ... the old spec of it (oapiGetMFDModeSpec)
	};
	Mode **g_mode;
	uint32_t g_nmode, g_capmode;
	int g_nextMode;

	Mode *ModeOf (int num) { for (uint32_t i = 0; i < g_nmode; i++) if (g_mode[i]->num == num) return g_mode[i]; return 0; }

	// Orbiter's modes the bridge didn't register (built-in, native modules'): a copy for the module, kept per mode id
	OAPI_MSGTYPE Unbridged (UINT, UINT, WPARAM, LPARAM) { return 0; } // a Windows module can't make that mode's MFD
	struct Foreign { int id; MFDMODESPECEX spec; MFDMODESPEC old; };
	Foreign **g_foreign;
	uint32_t g_nforeign, g_capforeign;

	char *Dup (const char *s, uint32_t n)
	{
		char *d = (char *)malloc (n + 1);
		memcpy (d, s, n);
		d[n] = 0;
		return d;
	}
}

int wb::RegisterMode (const char *name, unsigned long key, void *context, const void *msgproc, uint32_t vessel)
{
	if (!name) name = "";
	WbWriter &w = wb::Begin (L_RegisterMFDMode);
	int num = ++g_nextMode;
	w.u32 (num); w.str (name); w.u32 (key); w.u32 (vessel);
	WbReader r = wb::Call ();
	int id = r.i32 ();
	DWORD k = r.u32 ();
	if (r.bad) return -1;
	Mode *m = (Mode *)calloc (1, sizeof *m);
	m->num = num; m->id = id; m->vessel = vessel;
	m->module = AddrModule (msgproc);
	m->spec.name = Dup (name, (uint32_t)strlen (name));
	m->spec.key = k;
	m->spec.context = context;
	m->spec.msgproc = (MsgProc)msgproc;
	m->old.name = m->spec.name; m->old.key = k; m->old.msgproc = (MsgProc)msgproc;
	if (g_nmode == g_capmode) { g_capmode = g_capmode ? g_capmode * 2 : 16; g_mode = (Mode **)realloc (g_mode, g_capmode * sizeof *g_mode); }
	g_mode[g_nmode++] = m;
	return id;
}

// the record stays: an open MFD of the mode still gets its closing message, as Orbiter's instrument keeps the msgproc
bool wb::UnregisterMode (int id, uint32_t vessel)
{
	WbWriter &w = wb::Begin (L_UnregisterMFDMode); w.i32 (id); w.u32 (vessel);
	WbReader r = wb::Call ();
	bool ok = r.b ();
	if (ok) for (uint32_t i = 0; i < g_nmode; i++) if (g_mode[i]->id == id && g_mode[i]->vessel == vessel) g_mode[i]->id = -1;
	return ok;
}

int oapiRegisterMFDMode (MFDMODESPEC &spec)
{
	return wb::RegisterMode (spec.name, spec.key, 0, (const void *)spec.msgproc, 0); // Orbiter's copy has no context
}

int oapiRegisterMFDMode (MFDMODESPECEX &spec)
{
	return wb::RegisterMode (spec.name, spec.key, spec.context, (const void *)spec.msgproc, 0);
}

bool oapiUnregisterMFDMode (int mode)
{
	return wb::UnregisterMode (mode, 0);
}

// Orbiter's spec of a mode by name: the helper's copy of a bridged mode, else one of Orbiter's
static int ModeSpec (const char *name, MFDMODESPECEX **spec, MFDMODESPEC **old)
{
	wb::Begin (L_MFDModeSpec).str (name);
	WbReader r = wb::Call ();
	int id = r.i32 ();
	bool found = r.b ();
	if (r.bad) return MFD_NONE;
	if (!found) return id; // MFD_NONE, the spec as it was
	uint32_t n;
	const char *nm = r.str (&n);
	DWORD key = r.u32 ();
	bool proc = r.b ();
	int num = (int)r.u32 ();
	if (r.bad) return MFD_NONE;
	if (Mode *m = num ? ModeOf (num) : 0) {
		if (spec) *spec = &m->spec;
		if (old) *old = &m->old;
		return id;
	}
	Foreign *f = 0;
	for (uint32_t i = 0; i < g_nforeign; i++) if (g_foreign[i]->id == id) f = g_foreign[i];
	if (!f) {
		f = (Foreign *)calloc (1, sizeof *f);
		f->id = id;
		f->spec.name = Dup (nm ? nm : "", nm ? n : 0);
		f->spec.key = key;
		f->spec.msgproc = proc ? Unbridged : 0;
		f->old.name = f->spec.name; f->old.key = key; f->old.msgproc = f->spec.msgproc;
		if (g_nforeign == g_capforeign) { g_capforeign = g_capforeign ? g_capforeign * 2 : 16; g_foreign = (Foreign **)realloc (g_foreign, g_capforeign * sizeof *g_foreign); }
		g_foreign[g_nforeign++] = f;
	}
	if (spec) *spec = &f->spec;
	if (old) *old = &f->old;
	return id;
}

int oapiGetMFDModeSpecEx (char *name, MFDMODESPECEX **spec)
{
	return ModeSpec (name, spec, 0);
}

int oapiGetMFDModeSpec (char *name, MFDMODESPEC **spec)
{
	return ModeSpec (name, 0, spec);
}

int oapiBroadcastMFDMessage (int mode, int msg, void *data)
{
	WbWriter &w = wb::Begin (L_BroadcastMFDMessage); w.i32 (mode); w.i32 (msg); w.u32 (wb::Id (data));
	WbReader r = wb::Call ();
	return r.i32 ();
}

// ---------------------------------------------------------------------
// ExternMFD (MFDAPI.cpp of the Windows source): Orbiter's object, with the instrument, is the proxy on the Linux side; the
// members a module reads go with every call between the two

namespace {
	struct Emfd { uint32_t num, proxy; ExternMFD *obj; int module; bool dying; char label[65][64]; }; // a buffer per button (the last: others)
	Emfd **g_emfd;
	uint32_t g_nemfd, g_capemfd, g_nextEmfd;

	Emfd *EmfdOf (const ExternMFD *o) { for (uint32_t i = 0; i < g_nemfd; i++) if (g_emfd[i]->obj == o) return g_emfd[i]; return 0; }
	Emfd *EmfdNum (uint32_t num) { for (uint32_t i = 0; i < g_nemfd; i++) if (g_emfd[i]->num == num) return g_emfd[i]; return 0; }
	void EmfdDrop (Emfd *e)
	{
		for (uint32_t i = 0; i < g_nemfd; i++) if (g_emfd[i] == e) { g_emfd[i] = g_emfd[--g_nemfd]; break; }
		free (e);
	}

	// the protected members, through the class's own names
	struct EmfdAccess : ExternMFD {
		static void Put (WbWriter &w, const ExternMFD *e)
		{
			w.u32 (wb::Id (e->*(&EmfdAccess::hVessel)));
			w.i32 (e->*(&EmfdAccess::DW)); w.i32 (e->*(&EmfdAccess::DH)); w.i32 (e->*(&EmfdAccess::pmode));
			w.i32 (e->*(&EmfdAccess::nbt1)); w.i32 (e->*(&EmfdAccess::nbt2)); w.i32 (e->*(&EmfdAccess::bty0));
			w.i32 (e->*(&EmfdAccess::btdy)); w.i32 (e->*(&EmfdAccess::btpressed)); w.b (e->instr != 0);
		}
		static void Get (WbReader &r, ExternMFD *e, Emfd *rec)
		{
			uint32_t hv = r.u32 ();
			int dw = r.i32 (), dh = r.i32 (), pm = r.i32 (), n1 = r.i32 (), n2 = r.i32 (), y0 = r.i32 (), dy = r.i32 (), bp = r.i32 ();
			bool in = r.b ();
			if (r.bad) return;
			e->*(&EmfdAccess::hVessel) = (OBJHANDLE)wb::Ptr (hv);
			e->*(&EmfdAccess::DW) = dw; e->*(&EmfdAccess::DH) = dh; e->*(&EmfdAccess::pmode) = pm;
			e->*(&EmfdAccess::nbt1) = n1; e->*(&EmfdAccess::nbt2) = n2; e->*(&EmfdAccess::bty0) = y0;
			e->*(&EmfdAccess::btdy) = dy; e->*(&EmfdAccess::btpressed) = bp;
			e->instr = in ? (Instrument *)rec : 0; // Orbiter's instrument is there: Active, and a module's test of instr
		}
	};

	void PutSpec (WbWriter &w, const MFDSPEC &s)
	{
		w.i32 (s.pos.left); w.i32 (s.pos.top); w.i32 (s.pos.right); w.i32 (s.pos.bottom);
		w.i32 (s.nbt_left); w.i32 (s.nbt_right); w.i32 (s.bt_yofs); w.i32 (s.bt_ydist);
	}

	// a call to Orbiter's object: its members first; the reply's come back into this one
	WbWriter &EBegin (int y, const ExternMFD *o, Emfd *e)
	{
		WbWriter &w = wb::Begin (L_Emfd); w.i32 (y); w.u32 (e->proxy);
		EmfdAccess::Put (w, o);
		return w;
	}
	WbReader ECall (const ExternMFD *o, Emfd *e)
	{
		WbReader r = wb::Call ();
		EmfdAccess::Get (r, const_cast<ExternMFD *> (o), e);
		return r;
	}
}

ExternMFD::ExternMFD (const MFDSPEC &spec)
{
	instr = 0;
	pmode = 0;
	btpressed = -1;
	hVessel = 0;
	DW = spec.pos.right - spec.pos.left;
	DH = spec.pos.bottom - spec.pos.top;
	nbt1 = spec.nbt_left;
	nbt2 = spec.nbt_right;
	bty0 = spec.bt_yofs;
	btdy = spec.bt_ydist;
	Emfd *e = (Emfd *)calloc (1, sizeof *e);
	e->num = ++g_nextEmfd;
	e->obj = this;
	e->module = wb::AddrModule (__builtin_return_address (0)); // the module's constructor
	if (g_nemfd == g_capemfd) { g_capemfd = g_capemfd ? g_capemfd * 2 : 8; g_emfd = (Emfd **)realloc (g_emfd, g_capemfd * sizeof *g_emfd); }
	g_emfd[g_nemfd++] = e;
	WbWriter &w = wb::Begin (L_Emfd); w.i32 (Y_New); w.u32 (e->num); PutSpec (w, spec);
	WbReader r = wb::Call ();
	e->proxy = r.u32 ();
	EmfdAccess::Get (r, this, e); // hVessel: the focus vessel, as Orbiter's constructor sets it
}


ExternMFD::~ExternMFD ()
{
	Emfd *e = EmfdOf (this);
	if (!e) return;
	if (!e->dying) { // the module deletes it: Orbiter's object goes too (unregistered first, if it is)
		WbWriter &w = wb::Begin (L_Emfd); w.i32 (Y_Gone); w.u32 (e->proxy);
		wb::Call ();
	}
	EmfdDrop (e);
}

UINT_PTR ExternMFD::Id () const
{
	Emfd *e = EmfdOf (this);
	if (!e) return 0;
	EBegin (Y_Id, this, e);
	WbReader r = ECall (this, e);
	return r.u32 (); // the Linux object's, as the mode's msgproc gets it
}

bool ExternMFD::Active () const
{
	Emfd *e = EmfdOf (this);
	if (!e) return false;
	EBegin (Y_Active, this, e);
	WbReader r = ECall (this, e);
	return r.b ();
}

OBJHANDLE ExternMFD::GetVessel () const
{
	Emfd *e = EmfdOf (this);
	if (!e) return hVessel;
	EBegin (Y_GetVessel, this, e);
	WbReader r = ECall (this, e);
	return (OBJHANDLE)wb::Ptr (r.u32 ());
}

SURFHANDLE ExternMFD::GetDisplaySurface () const
{
	Emfd *e = EmfdOf (this);
	if (!e) return 0;
	EBegin (Y_DisplaySurface, this, e);
	WbReader r = ECall (this, e);
	return (SURFHANDLE)wb::Ptr (r.u32 ());
}

const char *ExternMFD::GetButtonLabel (int bt) const
{
	Emfd *e = EmfdOf (this);
	if (!e) return 0;
	EBegin (Y_ButtonLabel, this, e).i32 (bt);
	WbReader r = ECall (this, e);
	uint32_t n;
	const char *s = r.str (&n);
	if (!s || r.bad) return 0;
	char *buf = e->label[bt >= 0 && bt < 64 ? bt : 64]; // the module's pointer stays valid, as a mode's own label
	if (n > 63) n = 63;
	memcpy (buf, s, n);
	buf[n] = 0;
	return buf;
}

bool ExternMFD::ProcessButton (int bt, int event)
{
	Emfd *e = EmfdOf (this);
	if (!e) return false;
	WbWriter &w = EBegin (Y_ProcessButton, this, e); w.i32 (bt); w.i32 (event);
	WbReader r = ECall (this, e);
	return r.b ();
}

bool ExternMFD::SendKey (DWORD key)
{
	Emfd *e = EmfdOf (this);
	if (!e) return false;
	EBegin (Y_SendKey, this, e).u32 (key);
	WbReader r = ECall (this, e);
	return r.b ();
}

bool ExternMFD::Resize (const MFDSPEC &spec)
{
	Emfd *e = EmfdOf (this);
	if (!e) return false;
	PutSpec (EBegin (Y_Resize, this, e), spec);
	WbReader r = ECall (this, e);
	return r.b ();
}

bool ExternMFD::SetMode (int mode)
{
	Emfd *e = EmfdOf (this);
	if (!e) return false;
	EBegin (Y_SetMode, this, e).i32 (mode);
	WbReader r = ECall (this, e);
	return r.b ();
}

bool ExternMFD::OpenModeHelp () const
{
	Emfd *e = EmfdOf (this);
	if (!e) return false;
	EBegin (Y_OpenModeHelp, this, e);
	WbReader r = ECall (this, e);
	return r.b ();
}

// the virtual methods' defaults: Orbiter's object's own
void ExternMFD::SetVessel (OBJHANDLE hV)
{
	Emfd *e = EmfdOf (this);
	if (!e) { hVessel = hV; return; }
	EBegin (Y_SetVessel, this, e).u32 (wb::Id (hV));
	ECall (this, e);
}

void ExternMFD::clbkUpdate ()
{
	Emfd *e = EmfdOf (this);
	if (!e) return;
	EBegin (Y_Update, this, e);
	ECall (this, e);
}

void ExternMFD::clbkRefreshDisplay (SURFHANDLE hSurf)
{
	Emfd *e = EmfdOf (this);
	if (!e) return;
	EBegin (Y_RefreshDisplay, this, e).u32 (wb::Id (hSurf));
	ECall (this, e);
}

void ExternMFD::clbkRefreshButtons ()
{
	Emfd *e = EmfdOf (this);
	if (!e) return;
	EBegin (Y_RefreshButtons, this, e);
	ECall (this, e);
}

void ExternMFD::clbkFocusChanged (OBJHANDLE hFocus)
{
	Emfd *e = EmfdOf (this);
	if (!e) { SetVessel (hFocus); return; }
	EBegin (Y_FocusChanged, this, e).u32 (wb::Id (hFocus));
	ECall (this, e);
}

void oapiRegisterExternMFD (ExternMFD *emfd, const MFDSPEC &spec)
{
	Emfd *e = EmfdOf (emfd);
	if (!e) return;
	PutSpec (EBegin (Y_Register, emfd, e), spec);
	ECall (emfd, e);
}

// Orbiter deletes its object, and with it the module's (X_Delete)
bool oapiUnregisterExternMFD (ExternMFD *emfd)
{
	Emfd *e = EmfdOf (emfd);
	if (!e) return false;
	WbWriter &w = wb::Begin (L_Emfd); w.i32 (Y_Unregister); w.u32 (e->proxy);
	WbReader r = wb::Call ();
	return r.b ();
}

// ---------------------------------------------------------------------
// Orbiter's calls to the modes and objects

namespace {
	bool Update2 (void *mfd2, void *skp) { return ((MFD2 *)mfd2)->Update ((oapi::Sketchpad *)skp); }

	void OpenMode (WbReader &r, WbWriter &w)
	{
		int num = (int)r.u32 ();
		UINT id = r.u32 ();
		int W = r.i32 (), H = r.i32 ();
		DWORD key = r.u32 ();
		uint32_t vid = r.u32 (), link = r.u32 ();
		Mode *m = r.bad ? 0 : ModeOf (num);
		MFD *mfd = 0;
		if (m) m->spec.key = m->old.key = key; // Orbiter's copy's: a key another mode had is changed
		if (m && !m->dead && m->spec.msgproc) { // as Instrument_User: the extended message first
			MFDMODEOPENSPEC os = {W, H, &m->spec};
			LPARAM v = (LPARAM)oapiGetVesselInterface ((OBJHANDLE)wb::Ptr (vid)); // the module's interface of the vessel
			mfd = (MFD *)m->spec.msgproc (OAPI_MSG_MFD_OPENEDEX, id, (WPARAM)&os, v);
			if (!mfd) mfd = (MFD *)m->spec.msgproc (OAPI_MSG_MFD_OPENED, id, MAKEWPARAM (W, H), v);
		}
		if (!mfd) { w.u32 (0); w.b (true); return; }
		Instrument_User *in = (Instrument_User *)calloc (1, sizeof *in);
		in->num = ++g_nextIns;
		in->link = link;
		in->mfd = mfd;
		in->mfd2 = AsMfd2 (mfd);
		const void *vt = *(const void *const *)mfd;
		in->module = wb::AddrModule (vt);
		if (!in->module) in->module = m->module;
		in->old = wb::CallerApi (vt) < wb::API_2016;
		in->IW = W; in->IH = H;
		if (g_nins == g_capins) { g_capins = g_capins ? g_capins * 2 : 16; g_ins = (Instrument_User **)realloc (g_ins, g_capins * sizeof *g_ins); }
		g_ins[g_nins++] = in;
		Instrument_User::Attach (mfd, in);
		w.u32 (in->num); w.b (in->mfd2 != 0);
	}

	// the reply (b: the object is there, then the results) is written after the module's code ran: its calls to Orbiter reuse
	// the buffer, as do the arguments (read first)
	void Mfd (WbReader &r, WbWriter &w)
	{
		uint32_t num = r.u32 ();
		int c = r.i32 ();
		DWORD cw = r.u32 (), ch = r.u32 ();
		Instrument_User *in = r.bad ? 0 : Ins (num);
		if (!in || !in->mfd) {
			if (in && c == M_Delete) InsDrop (in);
			w.b (false);
			return;
		}
		in->cw = cw; in->ch = ch;
		in->Sync ();
		MFD *mfd = in->mfd;
		switch (c) {
		case M_Update: {
			uint32_t surf = r.u32 (), skp = r.u32 ();
			bool res = !r.bad && in->mfd2 ? wb::SkpClbk (in->old, surf, skp, Update2, in->mfd2) : false;
			w.b (true); w.b (res);
			break; }
		case M_UpdateDC: { // the DC as Orbiter's BeginDrawHDC leaves it; the text in its painter's pen colour
			COLORREF text = r.u32 ();
			HDC dc = (HDC)wb::DcOpen (r);
			if (dc) {
				SetTextColor (dc, text);
				SelectObject (dc, InsFont (in, 0));
				SetBkMode (dc, TRANSPARENT);
				SelectObject (dc, GetStockObject (NULL_BRUSH));
				SelectObject (dc, DefPen (0));
				mfd->Update (dc);
			}
			w.b (true);
			if (dc) wb::DcClose (dc, w);
			else w.b (false);
			break; }
		case M_KeyBuffered: { DWORD key = r.u32 (); bool res = !r.bad && mfd->ConsumeKeyBuffered (key); w.b (true); w.b (res); break; }
		case M_KeyImmediate: {
			char ks[256];
			r.raw (ks, 256);
			bool res = !r.bad && mfd->ConsumeKeyImmediate (ks);
			w.b (true); w.b (res); w.raw (ks, 256);
			break; }
		case M_Button: { int bt = r.i32 (), ev = r.i32 (); bool res = !r.bad && mfd->ConsumeButton (bt, ev); w.b (true); w.b (res); break; }
		case M_Label: { int bt = r.i32 (); char *s = r.bad ? 0 : mfd->ButtonLabel (bt); w.b (true); w.str (s); break; }
		case M_Menu: {
			const MFDBUTTONMENU *menu = 0;
			int n = mfd->ButtonMenu (&menu);
			if (!menu || n < 0) n = 0;
			if (n > 64) n = 64; // more than any MFD has buttons
			w.b (true); w.i32 (n);
			for (int i = 0; i < n; i++) { w.str (menu[i].line1); w.str (menu[i].line2); w.raw (&menu[i].selchar, 1); }
			break; }
		case M_WriteStatus: { uint32_t f = r.u32 (); if (!r.bad) mfd->WriteStatus ((FILEHANDLE)wb::Ptr (f)); w.b (true); break; }
		case M_ReadStatus: { uint32_t f = r.u32 (); if (!r.bad) mfd->ReadStatus ((FILEHANDLE)wb::Ptr (f)); w.b (true); break; }
		case M_StoreStatus: mfd->StoreStatus (); w.b (true); break;
		case M_RecallStatus: mfd->RecallStatus (); w.b (true); break;
		case M_Delete: delete mfd; InsDrop (in); w.b (true); break; // Orbiter deletes its instrument's object
		default: w.b (true); break;
		}
	}

	void Emfd_ (WbReader &r, WbWriter &w)
	{
		uint32_t num = r.u32 ();
		int c = r.i32 ();
		Emfd *e = r.bad ? 0 : EmfdNum (num);
		if (!e) { w.b (false); return; }
		ExternMFD *o = e->obj;
		EmfdAccess::Get (r, o, e);
		switch (c) {
		case X_SetVessel: { uint32_t h = r.u32 (); if (!r.bad) o->SetVessel ((OBJHANDLE)wb::Ptr (h)); break; }
		case X_Update: o->clbkUpdate (); break;
		case X_RefreshDisplay: { uint32_t s = r.u32 (); if (!r.bad) o->clbkRefreshDisplay ((SURFHANDLE)wb::Ptr (s)); break; }
		case X_RefreshButtons: o->clbkRefreshButtons (); break;
		case X_FocusChanged: { uint32_t h = r.u32 (); if (!r.bad) o->clbkFocusChanged ((OBJHANDLE)wb::Ptr (h)); break; }
		case X_Delete: e->dying = true; delete o; w.b (true); return; // Orbiter deleted its object (oapiUnregisterExternMFD)
		default: break;
		}
		w.b (true); // after the module's code: its calls reuse the buffer
		if (EmfdNum (num)) EmfdAccess::Put (w, o); // unless the module deleted it meanwhile
	}
}

void wb::ServeMfd (uint32_t fn, WbReader &r, WbWriter &w)
{
	switch (fn) {
	case H_MFDOPEN: OpenMode (r, w); break;
	case H_MFDCLOSED: {
		int num = (int)r.u32 ();
		UINT id = r.u32 ();
		Mode *m = r.bad ? 0 : ModeOf (num);
		if (m && !m->dead && m->spec.msgproc) m->spec.msgproc (OAPI_MSG_MFD_CLOSED, id, 0, 0);
		break; }
	case H_MFD: Mfd (r, w); break;
	case H_EMFD: Emfd_ (r, w); break;
	}
}

// the module's code is gone: its modes are dead, its objects are no more called (as on Windows, nothing deletes them); their
// memory went with the module's runtime, so it isn't touched
void wb::MfdFreed (int module)
{
	for (uint32_t i = 0; i < g_nmode; i++) if (g_mode[i]->module == module) g_mode[i]->dead = true;
	for (uint32_t i = 0; i < g_nins; ) {
		Instrument_User *in = g_ins[i];
		if (in->module == module) { in->mfd = in->mfd2 = 0; InsDrop (in); }
		else i++;
	}
	for (uint32_t i = 0; i < g_nemfd; ) {
		if (g_emfd[i]->module == module) EmfdDrop (g_emfd[i]);
		else i++;
	}
}

// ---------------------------------------------------------------------
// the classes' vtables in the 2010 SDK's order (MFDAPI.h, ModuleAPI.h of 2010; 2016 appended Module's last three)

bool wb::MfdProbe ()
{
	using namespace oapi;
	return VcallSlot (static_cast<void (MFD::*) (HDC)> (&MFD::Update)) == 1 && VcallSlot (&MFD::ConsumeKeyBuffered) == 2 &&
		VcallSlot (&MFD::ConsumeKeyImmediate) == 3 && VcallSlot (&MFD::ConsumeButton) == 4 && VcallSlot (&MFD::ButtonLabel) == 5 &&
		VcallSlot (&MFD::ButtonMenu) == 6 && VcallSlot (&MFD::WriteStatus) == 7 && VcallSlot (&MFD::ReadStatus) == 8 &&
		VcallSlot (&MFD::StoreStatus) == 9 && VcallSlot (&MFD::RecallStatus) == 10 &&
		VcallSlot (static_cast<bool (MFD2::*) (Sketchpad *)> (&MFD2::Update)) == 11 &&
		VcallSlot (&ExternMFD::SetVessel) == 1 && VcallSlot (&ExternMFD::clbkUpdate) == 2 && VcallSlot (&ExternMFD::clbkRefreshDisplay) == 3 &&
		VcallSlot (&ExternMFD::clbkRefreshButtons) == 4 && VcallSlot (&ExternMFD::clbkFocusChanged) == 5 &&
		VcallSlot (&Module::clbkSimulationStart) == 1 + O_SimulationStart && VcallSlot (&Module::clbkSimulationEnd) == 1 + O_SimulationEnd &&
		VcallSlot (&Module::clbkPreStep) == 1 + O_PreStep && VcallSlot (&Module::clbkPostStep) == 1 + O_PostStep &&
		VcallSlot (&Module::clbkTimeJump) == 1 + O_TimeJump && VcallSlot (&Module::clbkFocusChanged) == 1 + O_FocusChanged &&
		VcallSlot (&Module::clbkTimeAccChanged) == 1 + O_TimeAccChanged && VcallSlot (&Module::clbkNewVessel) == 1 + O_NewVessel &&
		VcallSlot (&Module::clbkDeleteVessel) == 1 + O_DeleteVessel && VcallSlot (&Module::clbkVesselJump) == 1 + O_VesselJump &&
		VcallSlot (&Module::clbkPause) == 1 + O_Pause && VcallSlot (&Module::clbkProcessMouse) == 1 + O_ProcessMouse &&
		VcallSlot (&Module::clbkProcessKeyboardImmediate) == 1 + O_ProcessKeyboardImmediate &&
		VcallSlot (&Module::clbkProcessKeyboardBuffered) == 1 + O_ProcessKeyboardBuffered;
}

// ---------------------------------------------------------------------
// the inline and compiler-made members Orbiter.exe exports (api/inline.def): made here (this file is built without inlining)

namespace {
	struct Graph : GraphMFD { using GraphMFD::GraphMFD; void Update (HDC) {} }; // GraphMFD's copy members, through a class of it
}

__attribute__ ((used)) static void Made (MFD2 *a, Graph *g, ExternMFD *e, MFDSPEC *s)
{
	MFD2 m2 (0, 0, 0);
	MFD2 c2 (*a);
	*a = c2;
	Graph cg (*g);
	*g = cg;
	ExternMFD ce (*e);
	*e = ce;
	ExternMFD ne (*s);
	DWORD x = a->GetWidth () + a->GetHeight ();
	(void)x;
}
