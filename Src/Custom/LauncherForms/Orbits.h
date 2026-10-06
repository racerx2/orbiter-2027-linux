
// custom: forms skins; heliocentric ecliptic J2000 positions (Planetary Defense's Orbits.js in C++), the Apophis clock, date texts

#ifndef __FORMS_ORBITS_H
#define __FORMS_ORBITS_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <optional>
#include <vector>

namespace forms {

	struct Vec3 { double x = 0, y = 0, z = 0; };

	struct NeoSet {
		const char *src;
		double epoch, e, a, i, om, w, ma, n, from, to; // degrees, degrees per day, JD (TDB)
	};

	struct Neo {
		const char *name;
		std::vector<NeoSet> sets;
	};

	namespace orbits {

		const double MJD_J2000 = 51544.5;
		const double MJD_MIN = -21504;  // 1800-01-01, start of the planet table's range
		const double MJD_MAX = 69807;   // 2050-01-01, end of the planet table's range and of time flow
		extern const char *const NEO_YEARS;
		extern const char *const PLANETS[4];

		const std::vector<Neo> &Neos ();
		const Neo *NeoByName (const QString &name);
		bool IsPlanet (const QString &name);
		Vec3 Planet (const QString &name, double mjd);
		std::vector<Vec3> PlanetPath (const QString &name, double mjd, int n);

		struct SetAt { const NeoSet *set = nullptr; bool inside = false; };
		SetAt NeoSetAt (const QString &name, double mjd); // the set in force, or else the nearest one
		std::optional<Vec3> NeoAt (const QString &name, double mjd);
		std::vector<Vec3> NeoPath (const QString &name, double mjd, int n);

		double MjdOfMs (double ms);
		double FlybyMs ();

		struct Countdown { bool past = false; double days = 0, hours = 0, minutes = 0, seconds = 0, ms = 0; };
		Countdown CountdownAt (double nowMs);

		QString Pad (const QString &v, int n);
		QString EpochText (double mjd); // "7 APR 2001 17:58"
		QString DayText (double mjd);   // "2001-04-07"
	}

	// the JS object Orbits
	class OrbitsApi: public QObject {
		Q_OBJECT
		Q_PROPERTY(double MJD_MIN READ mjdMin CONSTANT)
		Q_PROPERTY(double MJD_MAX READ mjdMax CONSTANT)
		Q_PROPERTY(QString NEO_YEARS READ neoYears CONSTANT)
		Q_PROPERTY(QStringList planetNames READ planetNames CONSTANT)
		Q_PROPERTY(QStringList neoNames READ neoNames CONSTANT)
	public:
		explicit OrbitsApi (QObject *parent = nullptr): QObject (parent) {}
		double mjdMin () const { return orbits::MJD_MIN; }
		double mjdMax () const { return orbits::MJD_MAX; }
		QString neoYears () const { return QString::fromUtf8 (orbits::NEO_YEARS); }
		QStringList planetNames () const;
		QStringList neoNames () const;

		Q_INVOKABLE QVariantMap countdown (double nowMs) const;
		Q_INVOKABLE QString epochText (double mjd) const { return orbits::EpochText (mjd); }
		Q_INVOKABLE QString dayText (double mjd) const { return orbits::DayText (mjd); }
		Q_INVOKABLE double mjdOfMs (double ms) const { return orbits::MjdOfMs (ms); }
		Q_INVOKABLE QString pad (const QVariant &v, int n) const;
		Q_INVOKABLE QVariantMap neoSet (const QString &name, double mjd) const;
		Q_INVOKABLE QVariantList neoSets (const QString &name) const;
		Q_INVOKABLE QVariantMap planet (const QString &name, double mjd) const;
		Q_INVOKABLE QVariant neo (const QString &name, double mjd) const;
	};

}

#endif // !__FORMS_ORBITS_H
