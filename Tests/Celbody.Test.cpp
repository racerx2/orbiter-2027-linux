// not upstream: loads the ported Celbody modules (.so) the way Orbiter does and checks their ephemerides and atmospheres

#include "OrbiterAPI.h"
#include "CelBodyAPI.h"
#include <catch2/catch_test_macros.hpp>
#include <dlfcn.h>
#include <link.h>
#include <strings.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// exe side the modules bind at dlopen: test doubles of Orbiter's versions
static const double mjd0 = 51544.5; // simulation starts at J2000
static std::vector<void*> initlib_handles;

// GetProcAddress semantics: dlsym also searches dependencies, keep only the module's own symbol
static void *OwnProc (void *hModule, const char *name)
{
	void *proc = dlsym (hModule, name);
	struct link_map *lm;
	Dl_info info;
	if (proc && (dlinfo (hModule, RTLD_DI_LINKMAP, &lm) || !dladdr (proc, &info) || strcmp (info.dli_fname, lm->l_name))) proc = 0;
	return proc;
}

DLLEXPORT void InitLib (void *hModule)
{
	initlib_handles.push_back (hModule);
	void (*DLLInit)(void*) = (void(*)(void*))OwnProc (hModule, "InitModule");
	if (DLLInit) (*DLLInit)(hModule);
}

DLLEXPORT int Date2Int (char *date)
{
	return 1;
}

double oapiTime2MJD (double simt) { return mjd0 + simt/86400.0; }
double oapiGetSimMJD () { return mjd0; }

void oapiWriteLogV (const char *format, ...)
{
	va_list ap;
	va_start (ap, format);
	vprintf (format, ap);
	va_end (ap);
	printf ("\n");
}

void __writeLogError (const char *func, const char *file, int line, const char *format, ...)
{
	va_list ap;
	va_start (ap, format);
	printf ("ERROR: ");
	vprintf (format, ap);
	va_end (ap);
	printf ("\n");
}

// FILEHANDLE here is a std::string* holding the config file path
bool oapiReadItem_float (FILEHANDLE f, const char *item, double &d) // not upstream: matches the const API
{
	if (!f) return false;
	std::ifstream ifs (oapiResolvePath (((std::string*)f)->c_str()));
	std::string line;
	size_t n = strlen (item);
	while (std::getline (ifs, line)) {
		size_t p = line.find_first_not_of (" \t");
		if (p == std::string::npos || strncasecmp (line.c_str()+p, item, n)) continue;
		p = line.find ('=', p+n);
		if (p != std::string::npos) return sscanf (line.c_str()+p+1, "%lf", &d) == 1;
	}
	return false;
}

void oapiGetGlobalPos (OBJHANDLE hObj, VECTOR3 *pos)
{
	*pos = _V(AU, 0, 0);
}

void oapiGlobalToEqu (OBJHANDLE hObj, const VECTOR3 &glob, double *lng, double *lat, double *rad)
{
	*rad = length (glob);
	*lng = atan2 (glob.z, glob.x);
	*lat = asin (glob.y / *rad);
}

// CELBODY/CELBODY2/ATMOSPHERE as in Src/Orbiter/Celbody.cpp, minus the parts that need the simulation
CELBODY::CELBODY () { version = 1; }
bool CELBODY::bEphemeris () const { return false; }
void CELBODY::clbkInit (FILEHANDLE cfg) {}
int CELBODY::clbkEphemeris (double mjd, int req, double *ret) { return 0; }
int CELBODY::clbkFastEphemeris (double simt, int req, double *ret) { return 0; }
bool CELBODY::clbkAtmParam (double alt, ATMPARAM *prm) { return false; }

void CELBODY::Pol2Crt (double *pol, double *crt)
{
	double rad  = pol[2] * AU;
	double cosp = cos(pol[0]), sinp = sin(pol[0]);
	double cost = cos(pol[1]), sint = sin(pol[1]);
	double xz   = rad * cost;
	crt[0] = xz  * cosp;
	crt[2] = xz  * sinp;
	crt[1] = rad * sint;
	double vl = xz  * pol[3];
	double vb = rad * pol[4];
	double vr = pol[5] * AU;
	crt[3] = cosp*cost*vr - cosp*sint*vb - sinp*vl;
	crt[4] = sint*     vr + cost*     vb;
	crt[5] = sinp*cost*vr - sinp*sint*vb + cosp*vl;
}

CELBODY2::CELBODY2 (OBJHANDLE hCBody): CELBODY ()
{
	version++;
	hBody = hCBody;
	atm = NULL;
	hAtmModule = NULL;
}

CELBODY2::~CELBODY2 () {}
void CELBODY2::clbkInit (FILEHANDLE cfg) { CELBODY::clbkInit (cfg); } // atmosphere modules are loaded by the test itself
double CELBODY2::SidRotPeriod () const { return 86164.1; }

ATMOSPHERE::ATMOSPHERE (CELBODY2 *body) { cbody = body; }

bool ATMOSPHERE::clbkConstants (ATMCONST *atmc) const
{
	atmc->R = 286.91;
	atmc->gamma = 1.4;
	return false;
}

bool ATMOSPHERE::clbkParams (const PRM_IN *prm_in, PRM_OUT *prm_out) { return false; }

// the loader steps Orbiter takes for a Celbody module: LoadLibrary (DllMain -> InitLib), InitInstance, clbkInit
struct CelbodyModule {
	void *hDLL = 0;
	CELBODY *body = 0;
	std::string cfg;

	CelbodyModule (const char *name, const char *dir = "Modules/Celbody/")
	{
		cfg = std::string ("Config/") + name + ".cfg";
		std::string path = std::string (dir) + name + ".so";
		hDLL = dlopen (path.c_str(), RTLD_NOW);
		if (!hDLL) { printf ("%s\n", dlerror()); return; }
		CELBODY *(*init)(OBJHANDLE) = (CELBODY*(*)(OBJHANDLE))OwnProc (hDLL, "InitInstance");
		if (!init) return;
		body = init ((OBJHANDLE)this);
		body->clbkInit ((FILEHANDLE)&cfg);
	}

	~CelbodyModule ()
	{
		void (*exit)(CELBODY*) = (void(*)(CELBODY*))OwnProc (hDLL, "ExitInstance");
		if (body && exit) exit (body);
		if (hDLL) dlclose (hDLL);
	}

	// position/velocity block and its flags for the requested time
	int Ephem (double mjd, double *s)
	{
		double ret[12];
		int flg = body->clbkEphemeris (mjd, EPHEM_TRUEPOS | EPHEM_TRUEVEL, ret);
		int ofs = (flg & EPHEM_TRUEPOS ? 0 : 6);
		for (int i = 0; i < 6; i++) s[i] = ret[ofs+i];
		return flg;
	}

	int FastEphem (double simt, double *s)
	{
		double ret[12];
		int flg = body->clbkFastEphemeris (simt, EPHEM_TRUEPOS | EPHEM_TRUEVEL, ret);
		int ofs = (flg & EPHEM_TRUEPOS ? 0 : 6);
		for (int i = 0; i < 6; i++) s[i] = ret[ofs+i];
		return flg;
	}
};

static double Distance (const double *s, int flg)
{
	return (flg & EPHEM_POLAR ? s[2]*AU : sqrt (s[0]*s[0] + s[1]*s[1] + s[2]*s[2]));
}

static void PolarPos (const double *s, double *p)
{
	double rad = s[2]*AU;
	p[0] = rad*cos(s[1])*cos(s[0]);
	p[1] = rad*sin(s[1]);
	p[2] = rad*cos(s[1])*sin(s[0]);
}

struct BodyRange { const char *name; double rmin, rmax, vtol = 1e-5, itol = 10.0; int ver = 2; };

// distance from the parent (or barycentre) at J2000 must lie between periapsis and apoapsis (+-1%)
// vtol/itol: TASS17 (Satsat) returns osculating two-body velocities, not the derivative of its perturbed positions
static const BodyRange bodies[] = {
	{"Sun",       1e6,      2.0e9},
	{"Mercury",   0.303*AU, 0.472*AU},
	{"Venus",     0.711*AU, 0.736*AU},
	{"Earth",     0.973*AU, 1.027*AU},
	{"Mars",      1.367*AU, 1.683*AU},
	{"Jupiter",   4.90*AU,  5.51*AU},
	{"Saturn",    8.94*AU,  10.16*AU},
	{"Uranus",    18.1*AU,  20.3*AU},
	{"Neptune",   29.5*AU,  30.7*AU},
	{"Moon",      3.52e8,   4.11e8},
	{"Io",        4.17e8,   4.27e8},
	{"Europa",    6.57e8,   6.84e8},
	{"Ganymede",  1.058e9,  1.083e9},
	{"Callisto",  1.845e9,  1.921e9},
	{"Mimas",     1.80e8,   1.91e8, 5e-5, 10.0},
	{"Enceladus", 2.35e8,   2.41e8, 5e-5, 10.0},
	{"Tethys",    2.91e8,   2.98e8, 5e-5, 10.0},
	{"Dione",     3.73e8,   3.82e8, 5e-5, 10.0},
	{"Rhea",      5.21e8,   5.33e8, 5e-5, 10.0},
	{"Titan",     1.174e9,  1.269e9, 5e-5, 10.0},
	{"Hyperion",  1.286e9,  1.680e9, 1e-5, 10.0}, // Satsat returns the derivative of Hyperion's position
	{"Iapetus",   3.424e9,  3.700e9, 5e-5, 10.0},
	// the eight moons with new 64-bit modules (CELBODY version 1)
	{"Phobos",    9.14e6,   9.62e6,  1e-5, 10.0, 1},
	{"Deimos",    2.321e7,  2.370e7, 1e-5, 10.0, 1},
	{"Miranda",   1.283e8,  1.314e8, 1e-5, 10.0, 1},
	{"Ariel",     1.888e8,  1.931e8, 1e-5, 10.0, 1},
	{"Umbriel",   2.623e8,  2.697e8, 1e-5, 10.0, 1},
	{"Titania",   4.313e8,  4.413e8, 1e-5, 10.0, 1},
	{"Oberon",    5.768e8,  5.901e8, 1e-5, 10.0, 1},
	{"Triton",    3.511e8,  3.584e8, 1e-5, 10.0, 1},
	// modules for the bodies upstream gives only a cfg Kepler orbit: 0.99 x min and 1.01 x max over 1800-2200
	{"Proteus",   1.1641e8,  1.1888e8,  1e-5, 10.0, 1},
	{"Nereid",    1.3286e9,  9.7833e9,  1e-5, 10.0, 1},
	{"Vesta",     3.1784e11, 3.8918e11, 1e-5, 10.0, 1},
	// the Pluto system, series fitted to JPL's DE441 and plu060: 0.99 x min and 1.01 x max over 1800-2200
	{"Pluto",     29.36*AU,  49.81*AU,  1e-5, 10.0, 1},
	{"Charon",    1.9397e7,  1.9795e7,  1e-5, 10.0, 1},
	{"Styx",      3.9708e7,  4.5434e7,  1e-5, 10.0, 1},
	{"Nix",       4.5989e7,  5.1576e7,  1e-5, 10.0, 1},
	{"Kerberos",  5.4864e7,  6.0738e7,  1e-5, 10.0, 1},
	{"Hydra",     6.1607e7,  6.7913e7,  1e-5, 10.0, 1},
};

TEST_CASE("Celbody modules load through the Orbitersdk entry point", "[celbody]")
{
	for (const BodyRange &b : bodies) {
		INFO(b.name);
		initlib_handles.clear();
		CelbodyModule m (b.name);
		REQUIRE(m.hDLL);
		REQUIRE(m.body);
		CHECK(std::find (initlib_handles.begin(), initlib_handles.end(), m.hDLL) != initlib_handles.end());
		CHECK(m.body->Version() == b.ver);
		CHECK(m.body->bEphemeris());
		CHECK(OwnProc (m.hDLL, "GetModuleVersion"));
	}
}

TEST_CASE("Earth VSOP87B at J2000 matches the VSOP87 check values", "[celbody]")
{
	CelbodyModule m ("Earth");
	REQUIRE(m.body);
	double s[6];
	int flg = m.Ephem (mjd0, s);
	CHECK(flg & EPHEM_POLAR);
	CHECK(fabs (s[0] - 1.7519238681) < 1e-7);
	CHECK(fabs (s[1] - (-0.0000039656)) < 1e-7);
	CHECK(fabs (s[2] - 0.9833276819) < 1e-7);
}

TEST_CASE("Distances at J2000 lie inside each orbit", "[celbody]")
{
	for (const BodyRange &b : bodies) {
		CelbodyModule m (b.name);
		REQUIRE(m.body);
		double s[6];
		int flg = m.Ephem (mjd0, s);
		double r = Distance (s, flg);
		INFO(b.name << " r=" << r);
		CHECK(r > b.rmin);
		CHECK(r < b.rmax);
	}
}

TEST_CASE("Velocities match the derivative of the positions", "[celbody]")
{
	const double dt = 10.0; // [s]
	for (const BodyRange &b : bodies) {
		CelbodyModule m (b.name);
		REQUIRE(m.body);
		double s[6], s0[6], s1[6];
		int flg = m.Ephem (mjd0, s);
		m.Ephem (mjd0 - dt/86400.0, s0);
		m.Ephem (mjd0 + dt/86400.0, s1);
		double vmax = std::max ({fabs (s[3]), fabs (s[4]), fabs (s[5])});
		for (int i = 0; i < 3; i++) {
			double d = s1[i] - s0[i];
			if ((flg & EPHEM_POLAR) && i == 0) d = remainder (d, 2.0*PI);
			double fd = d / (2.0*dt);
			double tol = (flg & EPHEM_POLAR ? b.vtol*fabs (s[i+3]) + 1e-15 : b.vtol*vmax + 1e-6);
			INFO(b.name << " component " << i << " fd=" << fd << " v=" << s[i+3]);
			CHECK(fabs (fd - s[i+3]) <= tol);
		}
	}
}

TEST_CASE("Interpolated ephemerides follow the exact ones", "[celbody]")
{
	for (const BodyRange &b : bodies) {
		CelbodyModule m (b.name);
		REQUIRE(m.body);
		double maxerr = 0.0;
		for (double simt = 0.0; simt <= 7200.0; simt += 37.0) {
			double f[6], e[6], pf[3], pe[3];
			int flg = m.FastEphem (simt, f);
			m.Ephem (oapiTime2MJD (simt), e);
			if (flg & EPHEM_POLAR) PolarPos (f, pf), PolarPos (e, pe);
			else for (int i = 0; i < 3; i++) pf[i] = f[i], pe[i] = e[i];
			double err = sqrt ((pf[0]-pe[0])*(pf[0]-pe[0]) + (pf[1]-pe[1])*(pf[1]-pe[1]) + (pf[2]-pe[2])*(pf[2]-pe[2]));
			maxerr = std::max (maxerr, err);
		}
		INFO(b.name << " max interpolation error " << maxerr << " m");
		CHECK(maxerr < b.itol);
	}
}

struct HorizonsState { const char *name; double mjd, x, y, z, tol; };

// JPL Horizons positions [m] in Orbiter's x, z, y order (nep098_merged, JPL#36, sat441l), fetched 2026-10-01; tables at 2000, a seam, 2026, the first and last segment
static const HorizonsState horizons[] = {
	{"Proteus", 51544.5, 4.605179358222e+07, -5.371470149603e+07, -9.401116452485e+07, 20000},
	{"Proteus", 61314.5, 1.057110914431e+08, -2.834648979754e+07, 4.306630856295e+07, 20000},
	{"Nereid", -21502.0, -1.246628825615e+09, 4.956620525161e+08, 7.383220659554e+09, 10000},
	{"Nereid", 51544.5, 8.937645622070e+08, 6.795867691287e+08, 9.317777487860e+09, 10000},
	{"Nereid", 51560.001533406365, 2.906031895582e+08, 6.283270770493e+08, 9.074602873049e+09, 10000},
	{"Nereid", 61314.5, -9.307838647058e+08, 4.861848624614e+08, 8.060498231531e+09, 10000},
	{"Nereid", 124590.0, 2.993091168722e+09, 7.533574030416e+08, 8.992352147833e+09, 10000},
	{"Vesta", -94552.5, -3.053123555849e+11, 2.952365701987e+10, 1.799541444510e+11, 50000},
	{"Vesta", 51544.5, -2.024927599995e+11, 3.214885425221e+10, -2.502976747755e+11, 50000},
	{"Vesta", 51601.31357254289, -1.124252094738e+11, 2.272042055231e+10, -3.003997218336e+11, 50000},
	{"Vesta", 61314.5, 3.495935383930e+11, -4.566607556951e+10, 1.043191306148e+11, 50000},
	{"Vesta", 234165.5, -2.743866829350e+11, 2.893082480883e+10, 2.437714596967e+11, 50000},
	{"Hyperion", 51544.5, 1.710492869804e+08, -6.593858184561e+08, 1.274310893164e+09, 20000000},
	{"Hyperion", 61314.5, -6.835348535917e+08, -5.744101335684e+08, 1.269662312169e+09, 20000000},
	// the Pluto system (Horizons: plu060 and DE441) at 1800-01-03, J2000, 2026-09-27 and 2199-12-29 TDB, fetched for design G
	{"Pluto", -21501.5, 5.435080979875e+12, -1.304323205531e+12, -2.497574147439e+12, 200},
	{"Pluto", 51544.5, -1.477330922307e+12, 8.752154807950e+11, -4.182574867536e+12, 200},
	{"Pluto", 61274.16411646549, 2.970620852029e+12, -3.883236091579e+11, -4.399981576212e+12, 200},
	{"Pluto", 124589.5, -4.101437832983e+12, 8.290917317308e+11, 3.343201512513e+12, 200},
	{"Charon", -21501.5, 1.345032463528e+07, -1.251101294146e+07, 6.817678811747e+06, 200},
	{"Charon", 51544.5, -6.837721052183e+06, -1.129855593548e+07, -1.448059952788e+07, 200},
	{"Charon", 61274.16411646549, -9.859235589544e+06, 1.693491856108e+07, -1.583741203505e+05, 200},
	{"Charon", 124589.5, -1.347936762420e+07, 7.190526474074e+05, -1.420935352889e+07, 200},
	{"Styx", -21501.5, 3.200218280279e+07, -2.507894960536e+07, 1.910848386858e+07, 200},
	{"Styx", 51544.5, 3.020342037197e+07, -1.949651224662e+07, 2.064644414430e+07, 200},
	{"Styx", 61274.16411646549, -2.771235335248e+07, 3.410873034870e+07, -8.900930489688e+06, 200},
	{"Styx", 124589.5, 2.723774301087e+07, -1.119127991244e+05, 2.951349007902e+07, 200},
	{"Nix", -21501.5, 1.705052616339e+07, 2.822685126425e+07, 3.614522789656e+07, 200},
	{"Nix", 51544.5, -3.191637391155e+06, 4.111032607466e+07, 2.217735690132e+07, 200},
	{"Nix", 61274.16411646549, 9.567064266264e+06, -4.275746018972e+07, -1.623589019966e+07, 200},
	{"Nix", 124589.5, -2.770502509447e+07, 4.096143827538e+07, -4.598015269974e+06, 200},
	{"Kerberos", -21501.5, -3.985927898079e+07, 6.986835324914e+06, -3.892154783478e+07, 200},
	{"Kerberos", 51544.5, 5.356908875723e+06, -5.267263965912e+07, -2.752056447792e+07, 200},
	{"Kerberos", 61274.16411646549, -1.626313966657e+07, 5.470947829610e+07, 1.710015326460e+07, 200},
	{"Kerberos", 124589.5, 4.029201197901e+07, -9.011158963730e+06, 3.754755526255e+07, 200},
	{"Hydra", -21501.5, 4.784450498138e+07, -3.563936735872e+07, 2.949961194646e+07, 200},
	{"Hydra", 51544.5, 3.141035313591e+07, -5.700688499153e+07, -9.602320080273e+05, 200},
	{"Hydra", 61274.16411646549, 1.905308989149e+07, 4.198676651587e+07, 4.666803143914e+07, 200},
	{"Hydra", 124589.5, 1.323802668758e+07, -5.919147424379e+07, -2.301733351546e+07, 200},
};

TEST_CASE("Modules for the cfg-orbit bodies follow JPL Horizons", "[celbody]")
{
	for (const HorizonsState &h : horizons) {
		CelbodyModule m (h.name);
		REQUIRE(m.body);
		double s[6];
		m.Ephem (h.mjd, s);
		double d = sqrt ((s[0]-h.x)*(s[0]-h.x) + (s[1]-h.y)*(s[1]-h.y) + (s[2]-h.z)*(s[2]-h.z));
		INFO(h.name << " mjd " << h.mjd << " error " << d << " m");
		CHECK(d < h.tol);
	}
}

TEST_CASE("Outside its span the Pluto system is finite, continuous at the ends and near its orbit", "[celbody]")
{
	static const char *names[] = {"Pluto", "Charon", "Styx", "Nix", "Kerberos", "Hydra"};
	for (const char *name : names) {
		const BodyRange *b = std::find_if (std::begin (bodies), std::end (bodies), [&](const BodyRange &r) { return !strcmp (r.name, name); });
		REQUIRE(b != std::end (bodies));
		CelbodyModule m (name);
		REQUIRE(m.body);
		for (double mjd : {-30000.0, 130000.0, mjd0 - 1e6, mjd0 + 1e6}) {
			double s[6];
			m.Ephem (mjd, s);
			double r = sqrt (s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
			INFO(name << " mjd " << mjd << " r=" << r);
			CHECK(std::isfinite (r));
			CHECK(r > 0.95*b->rmin);
			CHECK(r < 1.05*b->rmax);
		}
		const double e = 1e-7; // [days]
		for (double te : {-21503.0, 124591.0}) {
			double a[6], c[6];
			m.Ephem (te - e, a);
			m.Ephem (te + e, c);
			for (int i = 0; i < 3; i++) {
				INFO(name << " end " << te << " component " << i);
				CHECK(fabs (c[i] - a[i] - (a[i+3] + c[i+3])*e*86400.0) < 1.0);
				CHECK(fabs (c[i+3] - a[i+3]) < 1e-3);
			}
		}
	}
}

struct ChebTable { const char *name, *file, *header; };

// the CHEB1 header each table was built with
static const ChebTable tables[] = {
	{"Vesta",  "Config/Vesta/Data/vesta.cheb",   "CHEB1 -94553 128.20553822152885 2564 20"},
	{"Nereid", "Config/Nereid/Data/nereid.cheb", "CHEB1 -21503 32.00306681270537 4565 24"},
};

TEST_CASE("Chebyshev tables have the expected header and are continuous at every segment end", "[celbody]")
{
	for (const ChebTable &t : tables) {
		INFO(t.name);
		std::ifstream ifs (t.file, std::ios::binary);
		REQUIRE(ifs);
		std::string hdr;
		std::getline (ifs, hdr);
		CHECK(hdr == t.header);
		double t0, L;
		int nseg, n;
		REQUIRE(sscanf (hdr.c_str(), "CHEB1 %lf %lf %d %d", &t0, &L, &nseg, &n) == 4);
		CelbodyModule m (t.name);
		REQUIRE(m.body);
		const double e = 1e-7; // [days]
		double dpmax = 0.0, dvmax = 0.0;
		for (int k = 1; k < nseg; k++) {
			double a[6], b[6], tk = t0 + k*L;
			m.Ephem (tk - e, a);
			m.Ephem (tk + e, b);
			for (int i = 0; i < 3; i++) {
				dpmax = std::max (dpmax, fabs (b[i] - a[i] - (a[i+3] + b[i+3])*e*86400.0));
				dvmax = std::max (dvmax, fabs (b[i+3] - a[i+3]));
			}
		}
		INFO("max position jump " << dpmax << " m, velocity jump " << dvmax << " m/s");
		CHECK(dpmax < 1.0);
		CHECK(dvmax < 1e-3);
	}
}

TEST_CASE("Outside their tables Vesta and Nereid use the fitted orbit", "[celbody]")
{
	static const struct { const char *name; double mjd; } outside[] = {{"Vesta", 240000.0}, {"Nereid", 130000.0}};
	for (const auto &t : outside) {
		const BodyRange *b = std::find_if (std::begin (bodies), std::end (bodies), [&](const BodyRange &r) { return !strcmp (r.name, t.name); });
		REQUIRE(b != std::end (bodies));
		CelbodyModule m (t.name);
		REQUIRE(m.body);
		double s[6];
		m.Ephem (t.mjd, s);
		double r = sqrt (s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
		INFO(t.name << " r=" << r);
		CHECK(std::isfinite (r));
		CHECK(r > b->rmin);
		CHECK(r < b->rmax);
	}
}

struct AtmCase { const char *body, *module; double alt, rhomin, rhomax; };

TEST_CASE("Atmosphere modules give physical densities", "[celbody]")
{
	static const AtmCase cases[] = {
		{"Earth", "EarthAtm2006",       0.0,   1.20,   1.25},
		{"Earth", "EarthAtmJ71G",       400e3, 1e-13,  1e-10},
		{"Earth", "EarthAtmNRLMSISE00", 400e3, 1e-13,  1e-10},
		{"Mars",  "MarsAtm2006",        0.0,   0.005,  0.05},
		{"Venus", "VenusAtm2006",       0.0,   40.0,   90.0},
	};
	for (const AtmCase &c : cases) {
		INFO(c.module);
		CelbodyModule m (c.body);
		REQUIRE(m.body);
		std::string dir = std::string ("Modules/Celbody/") + c.body + "/Atmosphere/";
		initlib_handles.clear();
		void *hAtm = dlopen ((dir + c.module + ".so").c_str(), RTLD_NOW);
		REQUIRE(hAtm);
		CHECK(std::find (initlib_handles.begin(), initlib_handles.end(), hAtm) != initlib_handles.end());
		ATMOSPHERE *(*create)(CELBODY2*) = (ATMOSPHERE*(*)(CELBODY2*))OwnProc (hAtm, "CreateAtmosphere");
		void (*destroy)(ATMOSPHERE*) = (void(*)(ATMOSPHERE*))OwnProc (hAtm, "DeleteAtmosphere");
		REQUIRE(create);
		REQUIRE(destroy);
		ATMOSPHERE *atm = create ((CELBODY2*)m.body);
		ATMOSPHERE::PRM_IN in;
		memset (&in, 0, sizeof(in));
		in.alt = c.alt;
		in.flag = ATMOSPHERE::PRM_ALT;
		ATMOSPHERE::PRM_OUT out;
		CHECK(atm->clbkParams (&in, &out));
		INFO("rho=" << out.rho << " T=" << out.T << " p=" << out.p);
		CHECK(out.rho > c.rhomin);
		CHECK(out.rho < c.rhomax);
		destroy (atm);
		dlclose (hAtm);
	}
}

TEST_CASE("Pluto's atmosphere follows the New Horizons profile", "[celbody]")
{
	CelbodyModule m ("Pluto");
	REQUIRE(m.body);
	ATMPARAM a;
	REQUIRE(m.body->clbkAtmParam (0.0, &a));
	CHECK(fabs (a.p - 1.15) < 1e-9);
	CHECK(fabs (a.T - 38.0) < 1e-9);
	CHECK(fabs (a.rho - 1.15/(296.8*38.0)) < 1e-12);
	double plast = a.p;
	for (double alt = 1e3; alt <= 600e3; alt += 1e3) {
		REQUIRE(m.body->clbkAtmParam (alt, &a));
		INFO("alt " << alt << " T=" << a.T << " p=" << a.p);
		CHECK(a.p < plast);
		CHECK(a.T > 37.0);
		CHECK(a.T < 109.0);
		plast = a.p;
	}
	const double rad = 1.1883e6;
	for (double z : {4e3, 14e3, 30e3, 200e3}) { // layer bases in geopotential altitude: no jump
		double h = z*rad/(rad - z);
		ATMPARAM lo, hi;
		m.body->clbkAtmParam (h - 0.01, &lo);
		m.body->clbkAtmParam (h + 0.01, &hi);
		INFO("layer base " << z);
		CHECK(fabs (hi.p - lo.p) < 1e-5*lo.p);
		CHECK(fabs (hi.T - lo.T) < 1e-3);
	}
	m.body->clbkAtmParam (30e3, &a);
	CHECK(a.T > 105.0); // stratopause
}
