// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// =======================================================================
// Class VObject
// Base class for all visuals

#ifndef __VOBJECT_H
#define __VOBJECT_H

// d3d.h left out: D3DMATRIX -> oapi::FMATRIX4, D3DCOLORVALUE -> COLOUR4
#include "Vecmat.h"
#include "GraphicsAPI.h"

class Body;
class Scene;
class OrbiterGraphics;

const unsigned long VOCAPS_HASENGINES = 1;

class VObject {
	friend class Scene;

public:
	VObject (const Body *_body);
	// Create a new visual for logical object _body

	virtual ~VObject() {}

	static void CreateDeviceObjects (OrbiterGraphics *gclient);
	static void DestroyDeviceObjects ();

	static COLOUR4 ColorToD3D(Vector4 col) { return { (float)col.x, (float)col.y, (float)col.z, (float)col.w }; };

	virtual unsigned long GetCaps () const
	{ return 0; }

	const Body *GetBody() const { return body; }
	// the visual's logical interface

	virtual void Update (bool moving, bool force);
	// Update visual to current simulation time

	virtual void UpdateRenderVectors();
	// render any auxiliary vectors

	virtual void Timejump (bool moving) { Update (moving, true); }
	// Discontinuous update

	virtual void CheckResolution (double iar) = 0;
	// adjust resolution for given inverse apparent radius

	virtual bool bRenderInternal () const { return false; }

	virtual bool RenderDistRange (float &dmin, float &dmax) const { return false; }
	// objects return the near and far distance limits of the rendered object
	// (including any applied scaling)

	// Render, RenderExhaust, RenderBeacons left out: Direct3D 7 inline render path

	inline double CDist () const { return cdist; }
	inline const Vector &CPos () const { return cpos; }
	inline double ScaleFactor () const { return apprad_factor; }
	inline double AppRad () const { return 1.0/iapprad; }

	inline const oapi::FMATRIX4 &MWorld() const { return mWorld; }

	virtual void clbkEvent (DWORD msg, DWORD_PTR content) {}
	// Notification of visual event (e.g. mesh addition/deletion)

	struct BodyVectorRec {
		Vector v;
		Vector orig;
		double rad;
		double dist;
		Vector col;
		float alpha;
		std::string label;
		DWORD lcol;
		float lsize;
	};

	// RenderVectors, RenderVectorLabels left out: Direct3D 7 inline render path

protected:
	static OrbiterGraphics *gc;
	// inline graphics client instance

	// RenderAsPixel, RenderAsDisc, RenderSpot, RenderAsSpot left out: Direct3D 7 inline render path

	void AddVector(const Vector& v, const Vector& orig, double rad, const std::string& label, const Vector& col, float alpha = 1.0f, DWORD lcol = 0, float lsize = -1.0);

	// DrawVector left out: Direct3D 7 inline render path

	const Body *body;    // reference to logical object
	static Scene *scene; // reference to scene
	oapi::FMATRIX4 mWorld; // world transform matrix
	MATRIX4 dmWorld;     // world transformation matrix in double precision

	Vector cpos;         // object position relative to camera in global frame
	Vector campos;       // camera position in object frame
	double cdist;        // current distance from camera
	double iapprad;      // inverse of apparent radius
	std::vector<BodyVectorRec> veclist; // list of body vectors to be rendered

private:
	double apprad_factor; // auxiliary variable for apprad calculation
	double isdist_old;    // old inverse distance camera -> object surface
	// blobtex left out: DirectDraw billboard textures for distant objects
};

// =======================================================================
// nonmember definitions

// add a vector to the render list
// cam: camera position in local object frame
// v: vector, rad: line radius, col: colour, alpha: opacity, label: label, lcol: label colour (0=same as vector), lsize: label size (-1=default)

#endif // !__VOBJECT_H
