
// custom: forms skins; heliocentric ecliptic J2000 positions (Planetary Defense's Orbits.js in C++), the Apophis clock, date texts

#include "Orbits.h"
#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QTimeZone>
#include <algorithm>
#include <cmath>
#include <limits>

namespace forms {

namespace {

	const double PI = 3.14159265358979323846;
	const double DEG = PI / 180;

	// Standish (JPL), approximate planet elements, table 1, 1800-2050: a e I L varpi Omega, then their rates per century
	struct PlanetEl { const char *name; double el[12]; };
	const PlanetEl PLANET_EL[4] = {
		{"Mercury", {0.38709927, 0.20563593, 7.00497902, 252.25032350, 77.45779628, 48.33076593, 0.00000037, 0.00001906, -0.00594749, 149472.67411175, 0.16047689, -0.12534081}},
		{"Venus", {0.72333566, 0.00677672, 3.39467605, 181.97909950, 131.60246718, 76.67984255, 0.00000390, -0.00004107, -0.00078890, 58517.81538729, 0.00268329, -0.27769418}},
		{"Earth", {1.00000261, 0.01671123, -0.00001531, 100.46457166, 102.93768193, 0.0, 0.00000562, -0.00004392, -0.01294668, 35999.37244981, 0.32327364, 0.0}},
		{"Mars", {1.52371034, 0.09339410, 1.84969142, -4.55343205, -23.94362959, 49.55953891, 0.00001847, 0.00007882, -0.00813131, 19140.30268499, 0.44441088, -0.29257343}},
	};
	// JPL element sets (Horizons osculating elements, or SBDB API full-prec), fetched 2026-10-01; positions only inside [from, to) JD
	const double B0 = 2449718.5;
	const double B1 = 2453371.0;
	const double B2 = 2457023.5;
	const double B3 = 2460025.0;
	const double TEN_YEARS_D = 3652.5;
	const double APOPHIS_FLYBY_JD = 2462240.407091969; // JPL CAD, orbit 220: 2029-Apr-13 21:46 TDB, 0.000254090910419299 AU
	const std::vector<Neo> NEOS = {
		{"Apophis", {
			{"JPL Horizons, JPL#220", 2451544.5, 0.1913926258631164, 0.9223417394291891, 3.331244196967460, 204.6568307850113, 126.0648832968501, 231.6243952547896, 1.112669747393845, B0, B1},
			{"JPL Horizons, JPL#220", 2455197.5, 0.1912122049020198, 0.9224209173959603, 3.331518281222531, 204.4396276189811, 126.4238779576592, 336.6112378997939, 1.112526487896919, B1, B2},
			{"JPL Horizons, JPL#220", 2458849.5, 0.1914663582610371, 0.9225609080452407, 3.336782730252751, 204.0529292717555, 126.6848743835318, 80.19184213500814, 1.112273273075492, B2, B3},
			{"JPL SBDB orbit 220", 2461200.5, 0.1911492279663492, 0.9223592206975018, 3.340996879880978, 203.8936514240762, 126.6795706895841, 175.3304026592739, 1.112638115271892, B3, APOPHIS_FLYBY_JD},
			{"JPL Horizons, JPL#220, after the 2029 flyby", 2462320.5, 0.1890124082863401, 1.103037292117766, 2.221033579148814, 203.5583001910897, 71.43484156145448, 16.46552059305237, 0.8507829551907622, APOPHIS_FLYBY_JD, 2462320.5 + TEN_YEARS_D},
		}},
		{"Bennu", {
			{"JPL Horizons, ORX_merged_DE424", 2451544.5, 0.2046526054479352, 1.128924032007707, 6.025534771807846, 2.178572502802274, 65.67196720343235, 35.00708949375966, 0.8216880984884486, B0, B1},
			{"JPL Horizons, ORX_merged_DE424", 2455197.5, 0.2037780137686343, 1.126302125138441, 6.035045534848877, 2.061281162512221, 66.22699956249274, 160.7508508017112, 0.8245589662568907, B1, B2},
			{"JPL Horizons, ORX_merged_DE424", 2458849.5, 0.2036793381859808, 1.125917932416679, 6.034108877858773, 2.008417796820511, 66.33952684003408, 293.0255804840464, 0.8249810439684178, B2, B3},
			{"JPL Horizons, ORX_merged_DE424", 2461200.5, 0.2036821438660532, 1.125950726463012, 6.032966274857705, 1.966574037255910, 66.41055452273676, 72.45176654686115, 0.8249450020676293, B3, 2461200.5 + TEN_YEARS_D},
		}},
		{"Didymos", {
			{"JPL Horizons, JPL#240", 2451544.5, 0.3831879008645149, 1.642077489706229, 3.396519095582996, 73.44778599143386, 318.8861467146697, 65.51836576888675, 0.4683964581536544, B0, B1},
			{"JPL Horizons, JPL#240", 2455197.5, 0.3838277441879669, 1.644520880440456, 3.407759704053140, 73.24799659098328, 319.1897828040269, 334.1783022505095, 0.4673529472150221, B1, B2},
			{"JPL Horizons, JPL#240", 2458849.5, 0.3837939531356034, 1.644532506309142, 3.408311785548309, 73.20920222626359, 319.3057776538304, 241.0207711969688, 0.4673479913618807, B2, B3},
			{"JPL SBDB orbit 240", 2461200.5, 0.3831233242624545, 1.642709608529702, 3.413876519313629, 72.9858236207145, 319.5807001349104, 260.8612886320632, 0.4681261239466357, B3, 2461200.5 + TEN_YEARS_D},
		}},
	};

	const char *const MONTHS[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

	double Wrap360 (double d)
	{
		d = std::fmod (d, 360.0);
		return d < 0 ? d + 360 : d;
	}

	double Wrap180 (double d)
	{
		d = Wrap360 (d + 180);
		return d - 180;
	}

	double SolveKepler (double M, double e)
	{
		double E = (e < 0.8 ? M : PI);
		for (int k = 0; k < 30; k++) {
			const double dE = (E - e * std::sin (E) - M) / (1 - e * std::cos (E));
			E -= dE;
			if (std::fabs (dE) < 1e-12) break;
		}
		return E;
	}

	// orbital plane coordinates rotated into the ecliptic: u = argument of latitude (rad)
	Vec3 ToEcliptic (double r, double u, double i, double om)
	{
		const double ci = std::cos (i), co = std::cos (om), so = std::sin (om), cu = std::cos (u), su = std::sin (u);
		Vec3 v;
		v.x = r * (co * cu - so * su * ci);
		v.y = r * (so * cu + co * su * ci);
		v.z = r * su * std::sin (i);
		return v;
	}

	Vec3 FromAnomaly (double a, double e, double iDeg, double omDeg, double wDeg, double Mdeg)
	{
		const double E = SolveKepler (Wrap180 (Mdeg) * DEG, e);
		const double nu = 2 * std::atan2 (std::sqrt (1 + e) * std::sin (E / 2), std::sqrt (1 - e) * std::cos (E / 2));
		const double r = a * (1 - e * std::cos (E));
		return ToEcliptic (r, wDeg * DEG + nu, iDeg * DEG, omDeg * DEG);
	}

	struct Elements { double a, e, i, om, w, M; };

	const PlanetEl *PlanetByName (const QString &name)
	{
		for (const auto &p : PLANET_EL)
			if (name == QLatin1String (p.name)) return &p;
		return nullptr;
	}

	Elements PlanetElements (const PlanetEl &p, double mjd)
	{
		const double T = (mjd - orbits::MJD_J2000) / 36525;
		double o[6];
		for (int k = 0; k < 6; k++) o[k] = p.el[k] + p.el[k + 6] * T;
		return {o[0], o[1], o[2], o[5], o[4] - o[5], o[3] - o[4]};
	}

	// points around an orbit from its true anomaly, for drawing
	std::vector<Vec3> Ellipse (double a, double e, double iDeg, double omDeg, double wDeg, int n)
	{
		std::vector<Vec3> pts;
		const double p = a * (1 - e * e);
		n = std::max (1, n);
		for (int k = 0; k <= n; k++) {
			const double nu = double (k) / n * 2 * PI;
			pts.push_back (ToEcliptic (p / (1 + e * std::cos (nu)), wDeg * DEG + nu, iDeg * DEG, omDeg * DEG));
		}
		return pts;
	}

	// JS Math.round: halves go up
	double JsRound (double v)
	{
		return std::floor (v + 0.5);
	}

	bool DateOf (double mjd, QDateTime &dt)
	{
		const double ms = JsRound ((mjd - 40587) * 86400000.0);
		if (!std::isfinite (ms) || std::fabs (ms) > 8.64e15) return false; // JS Date's range
		dt = QDateTime::fromMSecsSinceEpoch ((qint64)ms, QTimeZone::UTC);
		return dt.isValid ();
	}

	// JS "" + number, for the integers the texts use
	QString Num (double v)
	{
		if (v == std::floor (v) && std::fabs (v) < 1e15) return QString::number ((qint64)v);
		return QString::number (v, 'g', 17);
	}

}

namespace orbits {

const char *const NEO_YEARS = "1995–2036";
const char *const PLANETS[4] = {"Mercury", "Venus", "Earth", "Mars"};

const std::vector<Neo> &Neos ()
{
	return NEOS;
}

const Neo *NeoByName (const QString &name)
{
	for (const auto &n : NEOS)
		if (name == QLatin1String (n.name)) return &n;
	return nullptr;
}

bool IsPlanet (const QString &name)
{
	return PlanetByName (name) != nullptr;
}

Vec3 Planet (const QString &name, double mjd)
{
	const PlanetEl *p = PlanetByName (name);
	if (!p) return Vec3 ();
	Elements k = PlanetElements (*p, mjd);
	return FromAnomaly (k.a, k.e, k.i, k.om, k.w, k.M);
}

std::vector<Vec3> PlanetPath (const QString &name, double mjd, int n)
{
	const PlanetEl *p = PlanetByName (name);
	if (!p) return {};
	Elements k = PlanetElements (*p, mjd);
	return Ellipse (k.a, k.e, k.i, k.om, k.w, n);
}

SetAt NeoSetAt (const QString &name, double mjd)
{
	SetAt r;
	const Neo *o = NeoByName (name);
	if (!o) return r;
	const double jd = mjd + 2400000.5;
	double gap = std::numeric_limits<double>::infinity ();
	for (const auto &s : o->sets) {
		if (jd >= s.from && jd < s.to) {
			r.set = &s;
			r.inside = true;
			return r;
		}
		const double g = (jd < s.from ? s.from - jd : jd - s.to);
		if (g < gap) {
			gap = g;
			r.set = &s;
		}
	}
	return r;
}

std::optional<Vec3> NeoAt (const QString &name, double mjd)
{
	SetAt r = NeoSetAt (name, mjd);
	if (!r.set || !r.inside) return std::nullopt;
	const NeoSet &s = *r.set;
	return FromAnomaly (s.a, s.e, s.i, s.om, s.w, Wrap360 (s.ma + s.n * (mjd + 2400000.5 - s.epoch)));
}

std::vector<Vec3> NeoPath (const QString &name, double mjd, int n)
{
	SetAt r = NeoSetAt (name, mjd);
	if (!r.set) return {};
	const NeoSet &s = *r.set;
	return Ellipse (s.a, s.e, s.i, s.om, s.w, n);
}

double MjdOfMs (double ms)
{
	return ms / 86400000 + 40587;
}

// Apophis closest approach in UTC: 21:46:12.746 TDB minus TT-UTC 69.184 s
double FlybyMs ()
{
	return (double)QDateTime (QDate (2029, 4, 13), QTime (21, 45, 3, 562), QTimeZone::UTC).toMSecsSinceEpoch ();
}

Countdown CountdownAt (double nowMs)
{
	Countdown c;
	double d = FlybyMs () - nowMs;
	c.past = d < 0;
	if (c.past) d = -d;
	c.ms = std::floor (std::fmod (d, 1000.0));
	const double s = std::floor (d / 1000);
	c.days = std::floor (s / 86400);
	c.hours = std::fmod (std::floor (s / 3600), 24.0);
	c.minutes = std::fmod (std::floor (s / 60), 60.0);
	c.seconds = std::fmod (s, 60.0);
	return c;
}

QString Pad (const QString &v, int n)
{
	return v.rightJustified (n, '0');
}

QString EpochText (double mjd)
{
	QDateTime d;
	if (!DateOf (mjd, d)) return QString ();
	return QString::number (d.date ().day ()) + " " + MONTHS[d.date ().month () - 1] + " " + QString::number (d.date ().year ()) + " "
		+ Pad (QString::number (d.time ().hour ()), 2) + ":" + Pad (QString::number (d.time ().minute ()), 2);
}

QString DayText (double mjd)
{
	QDateTime d;
	if (!DateOf (mjd, d)) return QString ();
	return QString::number (d.date ().year ()) + "-" + Pad (QString::number (d.date ().month ()), 2) + "-" + Pad (QString::number (d.date ().day ()), 2);
}

}

// ---------------------------------------------------------------------------------------------------------
// OrbitsApi

QStringList OrbitsApi::planetNames () const
{
	QStringList l;
	for (const char *p : orbits::PLANETS) l << p;
	return l;
}

QStringList OrbitsApi::neoNames () const
{
	QStringList l;
	for (const auto &n : orbits::Neos ()) l << n.name;
	return l;
}

QVariantMap OrbitsApi::countdown (double nowMs) const
{
	orbits::Countdown c = orbits::CountdownAt (nowMs);
	return {{"past", c.past}, {"days", c.days}, {"hours", c.hours}, {"minutes", c.minutes}, {"seconds", c.seconds}, {"ms", c.ms}};
}

QString OrbitsApi::pad (const QVariant &v, int n) const
{
	bool ok = false;
	double d = v.toDouble (&ok);
	return orbits::Pad (ok && v.typeId () != QMetaType::QString ? Num (d) : v.toString (), std::clamp (n, 0, 64)); // the watchdog can't stop C++
}

static QVariantMap SetMap (const NeoSet &s)
{
	return {{"src", QString::fromUtf8 (s.src)}, {"epoch", s.epoch}, {"e", s.e}, {"a", s.a}, {"i", s.i}, {"om", s.om},
		{"w", s.w}, {"ma", s.ma}, {"n", s.n}, {"from", s.from}, {"to", s.to}};
}

QVariantMap OrbitsApi::neoSet (const QString &name, double mjd) const
{
	orbits::SetAt r = orbits::NeoSetAt (name, mjd);
	if (!r.set) return QVariantMap ();
	return {{"set", SetMap (*r.set)}, {"inside", r.inside}};
}

QVariantList OrbitsApi::neoSets (const QString &name) const
{
	QVariantList l;
	if (const Neo *n = orbits::NeoByName (name))
		for (const auto &s : n->sets) l << SetMap (s);
	return l;
}

QVariantMap OrbitsApi::planet (const QString &name, double mjd) const
{
	Vec3 v = orbits::Planet (name, mjd);
	return {{"x", v.x}, {"y", v.y}, {"z", v.z}};
}

QVariant OrbitsApi::neo (const QString &name, double mjd) const
{
	auto v = orbits::NeoAt (name, mjd);
	if (!v) return QVariant ();
	return QVariantMap {{"x", v->x}, {"y", v->y}, {"z", v->z}};
}

}
