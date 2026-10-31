// WineBridge helper: the inline and compiler-made members of the drawing tools, the sketchpad and the vessel classes that Orbiter.exe
// exports (api/inline.def): made here (this file is built without inlining)
#define OAPI_IMPLEMENTATION
#include "Orbitersdk.h"
#include "DrawAPI.h"
#include "wb_helper.h"

using namespace oapi;

// the 2024 header's second Font constructor has no body in Orbiter: a drawing tool's base does nothing
Font::Font (int height, const char *face, int width, int weight, FontStyle style, float spacing) {}

namespace {
	struct FontMade : Font {
		FontMade () : Font (0, false, "") {}
		FontMade (int) : Font (0, "") {}
	};
	struct PenMade : Pen { PenMade () : Pen (0, 0, 0) {} };
	struct BrushMade : Brush { BrushMade () : Brush (0) {} };
}

// what the optimiser leaves no function of: the tools' empty destructors, VESSEL's member-wise assignment
namespace wb {
	struct ToolMade { void Gone (); };
	struct VesselMade : VESSEL { VesselMade &Assign (const VesselMade &o); };
}
void wb::ToolMade::Gone () {}
wb::VesselMade &wb::VesselMade::Assign (const VesselMade &o)
{
	vessel = o.vessel;
	flightmodel = o.flightmodel;
	version = o.version;
	return *this;
}

__attribute__ ((used)) static void Made (DrawingTool *d, Font *f, Pen *p, Brush *b, Sketchpad *s, VESSEL *v, VESSEL2 *v2, VESSEL3 *v3, VESSEL4 *v4)
{
	DrawingTool dt;
	DrawingTool dc (*d);
	*d = dc;
	FontMade fm, fm2 (0);
	Font fc (*f);
	*f = fc;
	HFONT hf = f->Font::GetGDIFont ();
	PenMade pm;
	Pen pc (*p);
	*p = pc;
	BrushMade bm;
	Brush bc (*b);
	*b = bc;
	Sketchpad sc (*s);
	*s = sc;
	SURFHANDLE h = s->GetSurface ();
	int ver = v->Version ();
	VESSEL2 c2 (*v2);
	*v2 = c2;
	VESSEL3 c3 (*v3);
	*v3 = c3;
	VESSEL4 c4 (*v4);
	*v4 = c4;
	(void)hf; (void)h; (void)ver;
}
