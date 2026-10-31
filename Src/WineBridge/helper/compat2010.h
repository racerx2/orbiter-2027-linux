// WineBridge helper: the classes whose layout a module built with the 2010 SDK sees differently from the 2024 headers the helper is built with
#pragma once
#include "Orbitersdk.h"

namespace wb {
	// a module's SDK, from its GetModuleVersion (Orbitersdk.lib's date): before the 2016 release it has the 2010 layouts
	const int API_2016 = 160828;

	// oapi::Sketchpad of the 2010 SDK: its virtual methods in their 2010 order (2016 put GetOrigin after SetOrigin), its one member
	class Sketchpad2010 {
	public:
		Sketchpad2010 (SURFHANDLE s) : surf (s) {}
		virtual ~Sketchpad2010 () {}
		virtual oapi::Font *SetFont (oapi::Font *font) const = 0;
		virtual oapi::Pen *SetPen (oapi::Pen *pen) const = 0;
		virtual oapi::Brush *SetBrush (oapi::Brush *brush) const = 0;
		virtual void SetTextAlign (oapi::Sketchpad::TAlign_horizontal tah, oapi::Sketchpad::TAlign_vertical tav) = 0;
		virtual DWORD SetTextColor (DWORD col) = 0;
		virtual DWORD SetBackgroundColor (DWORD col) = 0;
		virtual void SetBackgroundMode (oapi::Sketchpad::BkgMode mode) = 0;
		virtual DWORD GetCharSize () = 0;
		virtual DWORD GetTextWidth (const char *str, int len) = 0;
		virtual void SetOrigin (int x, int y) = 0;
		virtual bool Text (int x, int y, const char *str, int len) = 0;
		virtual bool TextBox (int x1, int y1, int x2, int y2, const char *str, int len) = 0;
		virtual void Pixel (int x, int y, DWORD col) = 0;
		virtual void MoveTo (int x, int y) = 0;
		virtual void LineTo (int x, int y) = 0;
		virtual void Line (int x0, int y0, int x1, int y1) = 0;
		virtual void Rectangle (int x0, int y0, int x1, int y1) = 0;
		virtual void Ellipse (int x0, int y0, int x1, int y1) = 0;
		virtual void Polygon (const oapi::IVECTOR2 *pt, int npt) = 0;
		virtual void Polyline (const oapi::IVECTOR2 *pt, int npt) = 0;
		virtual void PolyPolygon (const oapi::IVECTOR2 *pt, const int *npt, const int nline) = 0;
		virtual void PolyPolyline (const oapi::IVECTOR2 *pt, const int *npt, const int nline) = 0;
		virtual HDC GetDC () = 0;
		SURFHANDLE GetSurface () const { return surf; }
	private:
		SURFHANDLE surf;
	};

	// LightEmitter, PointLight and SpotLight of the 2010 SDK (2016 put visibility after ltype); their inline getters read these offsets
	struct Light2010 {
		LightEmitter::TYPE ltype;
		OBJHANDLE hRef;
		bool active;
		COLOUR4 col_diff, col_spec, col_ambi;
		double *intens, lintens;
		const VECTOR3 *pos, *dir;
		VECTOR3 lpos, ldir;
		double range, att[3];   // PointLight
		double umbra, penumbra; // SpotLight
	};
}
