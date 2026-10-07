// not upstream: test-only vessel module of the collision addon scenario tests: a parent and a child animation (Design CA E4 7.4; E2-H5, E2-H8)
#define ORBITER_MODULE
#include <cstdlib>
#include <cstring>
#include "Orbitersdk.h"

namespace {

const VECTOR3 PB_PMI = {2.28, 2.31, 0.79};
TOUCHDOWNVTX tdvtx[3] = {{{0, -1.5, 2}, 2e4, 1e3, 1.6, 1}, {{-1, -1.5, -1.5}, 2e4, 1e3, 3.0, 1}, {{1, -1.5, -1.5}, 2e4, 1e3, 3.0, 1}};
UINT GRP_LWING = 2, GRP_RWING = 3; // ShuttlePB mesh groups

class CollTestAnim : public VESSEL4 {
public:
	CollTestAnim (OBJHANDLE h, int fm) : VESSEL4 (h, fm), rot (0, &GRP_LWING, 1, _V (-1.3, -0.725, -1.5), _V (-0.9619, -0.2735, 0), (float)(30 * RAD)),
		tr (0, &GRP_RWING, 1, _V (0, 0.5, 0)) {}
	void clbkSetClassCaps (FILEHANDLE cfg) override
	{
		oapiReadItem_bool (cfg, (char *)"AnimInVisual", inVisual);
		SetSize (3.5);
		SetEmptyMass (500.0);
		SetPMI (PB_PMI);
		SetTouchdownPoints (tdvtx, 3);
		AddMesh ("ShuttlePB");
		if (!inVisual) Create ();
	}
	void clbkVisualCreated (VISHANDLE, int) override // CollTestAnimVC: after the client's constructor step (review CA-v2-B 11)
	{
		if (inVisual && !made) Create ();
	}
	void clbkLoadStateEx (FILEHANDLE scn, void *status) override
	{
		char *line;
		while (oapiReadScenario_nextline (scn, line)) {
			if (!strncmp (line, "TESTANIMCYCLE", 13)) period = atoi (line + 13);
			else ParseScenarioLineEx (line, status);
		}
	}
	void clbkPreStep (double simt, double, double) override // TESTANIMCYCLE: both states 0 -> 1 -> 0 over period frames
	{
		k++;
		if (!made || period <= 0) return;
		double ph = (k % period) / (double)period, s = ph < 0.5 ? 2 * ph : 2 - 2 * ph;
		SetAnimation (parent, s);
		SetAnimation (child, s);
		if (k % period == 0) oapiWriteLogV ("CollTestAnim act k=%d simt=%.17g TESTANIMCYCLE '%s' %d", k, simt, GetName (), period);
	}
private:
	void Create () // a parent and a child animation at state 1 with defstate 0
	{
		parent = CreateAnimation (0.0);
		child = CreateAnimation (0.0);
		ANIMATIONCOMPONENT_HANDLE hp = AddAnimationComponent (parent, 0, 1, &rot);
		AddAnimationComponent (child, 0, 1, &tr, hp);
		SetAnimation (parent, 1.0);
		SetAnimation (child, 1.0);
		made = true;
		oapiWriteLogV ("CollTestAnim '%s' animations parent=%u child=%u%s", GetName (), parent, child, inVisual ? " (visual)" : "");
	}
	MGROUP_ROTATE rot;
	MGROUP_TRANSLATE tr;
	bool inVisual = false, made = false;
	UINT parent = 0, child = 0;
	int period = 0, k = 0;
};

}

DLLCLBK VESSEL *ovcInit (OBJHANDLE h, int fm) { return new CollTestAnim (h, fm); }

DLLCLBK void ovcExit (VESSEL *v) { delete (CollTestAnim *)v; }
