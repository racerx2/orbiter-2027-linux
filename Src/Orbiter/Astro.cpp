// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <fstream>
#include <cmath>
#include <limits>
#include "Astro.h"
#include "Vecmat.h"

// Historical TAI-UTC values, 1960-1971 drift and later leap-second steps.
// Source: SOFA/ERFA eraDat historical table; IERS leap-second bulletins.
// Updated through the 2017-01-01 step (IERS Bulletin C 72: no leap in 2026).
// If a new leap second is announced, update the entries below.
struct UtcTaiOffset {
    double startMjd, baseSeconds, referenceMjd, rateSecondsPerDay;
};

double TaiMinusUtc (double utcMjd)
{
    static constexpr UtcTaiOffset changes[] = {
        {36934, 1.417818, 37300, .001296},
        {37300, 1.422818, 37300, .001296},
        {37512, 1.372818, 37300, .001296},
        {37665, 1.845858, 37665, .0011232},
        {38334, 1.945858, 37665, .0011232},
        {38395, 3.240130, 38761, .001296},
        {38486, 3.340130, 38761, .001296},
        {38639, 3.440130, 38761, .001296},
        {38761, 3.540130, 38761, .001296},
        {38820, 3.640130, 38761, .001296},
        {38942, 3.740130, 38761, .001296},
        {39004, 3.840130, 38761, .001296},
        {39126, 4.313170, 39126, .002592},
        {39887, 4.213170, 39126, .002592},
        {41317, 10, 0, 0},
        {41499, 11, 0, 0},
        {41683, 12, 0, 0},
        {42048, 13, 0, 0},
        {42413, 14, 0, 0},
        {42778, 15, 0, 0},
        {43144, 16, 0, 0},
        {43509, 17, 0, 0},
        {43874, 18, 0, 0},
        {44239, 19, 0, 0},
        {44786, 20, 0, 0},
        {45151, 21, 0, 0},
        {45516, 22, 0, 0},
        {46247, 23, 0, 0},
        {47161, 24, 0, 0},
        {47892, 25, 0, 0},
        {48257, 26, 0, 0},
        {48804, 27, 0, 0},
        {49169, 28, 0, 0},
        {49534, 29, 0, 0},
        {50083, 30, 0, 0},
        {50630, 31, 0, 0},
        {51179, 32, 0, 0},
        {53736, 33, 0, 0},
        {54832, 34, 0, 0},
        {56109, 35, 0, 0},
        {57204, 36, 0, 0},
        {57754, 37, 0, 0},
    };
    // UTC did not exist before 1960. Do not fabricate a historic offset.
    if (!std::isfinite(utcMjd) || utcMjd < changes[0].startMjd)
        return std::numeric_limits<double>::quiet_NaN();
    for (int i = int(sizeof(changes)/sizeof(changes[0]))-1; i >= 0; --i) {
        const UtcTaiOffset &c = changes[i];
        if (utcMjd >= c.startMjd)
            return c.baseSeconds + (utcMjd - c.referenceMjd)*c.rateSecondsPerDay;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double UtcToTtMjd (double utcMjd)
{
    return utcMjd + (TaiMinusUtc(utcMjd) + 32.184)*day; // TT-TAI is 32.184 s
}

// transforms a date and time (UT) into MJD format
double date2mjd (struct tm *date)
{
  int b;
  int y = date->tm_year+1900;
  int m = date->tm_mon;
  int d = date->tm_mday;
  double a = 10000.0*y + 100.0*m + d;
  if (m <= 2) m += 12, y--;
  if (a <= 15821004.1) b = (y+4716)/4 - 1181;
  else                 b = y/400 - y/100 + y/4;
  return 365.0*y - 679004.0 + b + (int)(30.6001*(m+1)) + d +
	  date->tm_hour/24.0 + date->tm_min/1440.0 + date->tm_sec/86400.0;
}

// transforms a MJD value into date and time structures (GMT, if MJD had no offsets)
struct tm *mjddate (double mjd)
{
	static struct tm date;
	double ijd, c, e, h;
	int a, f;

	h = 24.0 * modf (mjd, &ijd);
	if (ijd < -100840) {
		c = ijd + 2401525.0;
	} else {
		int b = (int)((ijd + 532784.75) / 36524.25);
		c = ijd + 2401526.0 + (b - b/4);
	}
	a = (int)((c-122.1)/365.25);
	e = 365.0 * a + a/4;
	f = (int)((c-e)/30.6001);
	date.tm_wday  = ((int)mjd + 3) % 7;
	date.tm_mday  = (int)(c-e+0.5) - (int)(30.6001*f);
	date.tm_mon   = f-1 - 12 * (f/14);
	date.tm_year  = a-4715 - ((7 + date.tm_mon)/10) - 1900;
	date.tm_hour  = (int)h;
	date.tm_min   = (int)(h = 60.0 * (h - date.tm_hour));
	date.tm_sec   = (int)(h = 60.0 * (h - date.tm_min));
	date.tm_isdst = 0;
	return &date;
}

char *DateStr (double mjd)
{
	static char datestr[256];
	static char wdaystr[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
	static char monstr[13][4] = {"","Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

	double ijd, c, e, h;
	int a, f, wday, mday, mon, year, hour, min, sec;

	h = 24.0 * modf (mjd, &ijd);
	if (ijd < -100840) {
		c = ijd + 2401525.0;
	} else {
		int b = (int)((ijd + 532784.75) / 36524.25);
		c = ijd + 2401526.0 + (b - b/4);
	}
	a = (int)((c-122.1)/365.25);
	e = 365.0 * a + a/4;
	f = (int)((c-e)/30.6001);
	wday  = ((int)mjd + 3) % 7;
	mday  = (int)(c-e+0.5) - (int)(30.6001*f);
	mon   = f-1 - 12 * (f/14);
	year  = a-4715 - ((7 + mon)/10);
	hour  = (int)h;
	min   = (int)(h = 60.0 * (h - hour));
	sec   = (int)(h = 60.0 * (h - min));
	
	sprintf (datestr, "%s %s %02d %02d:%02d:%02d %d", wdaystr[wday], monstr[mon], mday,
		hour, min, sec, year);
	return datestr;
}

double Obliquity (double jc)
{
	return 0.4090928042 - (2.269655248e-4 + (2.860400719e-9 - 8.789672039e-9*jc)*jc)*jc;
}

void Equ2Ecl (double cosob, double sinob, double ra, double dc, double &l, double &b)
{
	double sinra = sin (ra), cosra = cos (ra);
	double sindc = sin (dc), cosdc = cos (dc);
	l = atan2 (sinra*cosdc*cosob + sindc*sinob, cosra*cosdc);
	b = asin  (sindc*cosob - sinra*cosdc*sinob);
}

void Ecl2Equ (double cosob, double sinob, double l, double b, double &ra, double &dc)
{
	double sinl = sin (l), cosl = cos (l);
	double sinb = sin (b), cosb = cos (b);
	ra = atan2 (sinl*cosb*cosob - sinb*sinob, cosb*cosl);
	dc = asin  (sinb*cosob + cosb*sinl*sinob);
}

void Orthodome (double lng1, double lat1, double lng2, double lat2,
				double &dist, double &dir)
{
	double A = lng2-lng1;
	double dlng = fabs(A);
	double dlat = fabs(lat2-lat1);
	if (dlat < 1e-14) {
		dist = dlng;
		dir = (lng2 > lng1 ? PI05 : 3*PI05);
		return;
	} else if (dlng < 1e-14) {
		dist = dlat;
		dir = (lat2 > lat1 ? 0.0 : PI);
	} else {
		double sinA  = sin(A),    cosA  = cos(A);
		double slat1 = sin(lat1), clat1 = cos(lat1);
		double slat2 = sin(lat2), clat2 = cos(lat2);
		double cosa  = slat2*slat1 + clat2*clat1*cosA;
		dist = acos (cosa);
		dir = atan2 (sinA*clat2, clat1*slat2 - slat1*clat2*cosA);
		if (dir < 0.0) dir += Pi2;     // 0 <= dir < 2pi
	}
}

double Orthodome (double lng1, double lat1, double lng2, double lat2)
{
	double cosA  = cos(lng2-lng1);
	double slat1 = sin(lat1), clat1 = cos(lat1);
	double slat2 = sin(lat2), clat2 = cos(lat2);
	double cosa  = slat2*slat1 + clat2*clat1*cosA;
	return acos (cosa);
}

static char strbuf[256];

char *DistStr (double dist, int precision)
{
	double absd = fabs (dist);
	if (absd < 1e4) {
		if		(absd < 1e2)  sprintf (strbuf, "% 0.*f", precision-2, dist);
		else if (absd < 1e3)  sprintf (strbuf, "% 0.*f", precision-3, dist);
		else                  sprintf (strbuf, "% 0.*fk", precision-1, dist*1e-3);
	} else if (absd < 1e7) {
		if      (absd < 1e5)  sprintf (strbuf, "% 0.*fk", precision-2, dist*1e-3);
		else if (absd < 1e6)  sprintf (strbuf, "% 0.*fk", precision-3, dist*1e-3);
		else                  sprintf (strbuf, "% 0.*fM", precision-1, dist*1e-6);
	} else if (absd < 1e10) {
		if      (absd < 1e8)  sprintf (strbuf, "% 0.*fM", precision-2, dist*1e-6);
		else if (absd < 1e9)  sprintf (strbuf, "% 0.*fM", precision-3, dist*1e-6);
		else                  sprintf (strbuf, "% 0.*fG", precision-1, dist*1e-9);
	} else if (absd < 1e2*AU) {
		if      (absd < 1e11) sprintf (strbuf, "% 0.*fG", precision-2, dist*1e-9);
		else if (absd < 1e12) sprintf (strbuf, "% 0.*fG", precision-3, dist*1e-9);
		else                  sprintf (strbuf, "% 0.*fAU",precision-2, dist*iAU);
	} else {
		sprintf (strbuf, "% 0.0fAU", dist*iAU);
		//FloatStr (dist*iparsec, precision+1);
		//strcat (strbuf, "pc");
	}
	return strbuf;
}

char *FloatStr (double f, int precision)
{
	double absf = fabs (f);
	if (absf < 1e4) {
		if		(absf < 1e2)  sprintf (strbuf, "% 0.*f", precision-2, f);
		else if (absf < 1e3)  sprintf (strbuf, "% 0.*f", precision-3, f);
		else                  sprintf (strbuf, "% 0.*fk", precision-1, f*1e-3);
	} else if (absf < 1e7) {
		if      (absf < 1e5)  sprintf (strbuf, "% 0.*fk", precision-2, f*1e-3);
		else if (absf < 1e6)  sprintf (strbuf, "% 0.*fk", precision-3, f*1e-3);
		else                  sprintf (strbuf, "% 0.*fM", precision-1, f*1e-6);
	} else if (absf < 1e10) {
		if      (absf < 1e8)  sprintf (strbuf, "% 0.*fM", precision-2, f*1e-6);
		else if (absf < 1e9)  sprintf (strbuf, "% 0.*fM", precision-3, f*1e-6);
		else                  sprintf (strbuf, "% 0.*fG", precision-1, f*1e-9);
	} else if (absf < 1e13) {
		if      (absf < 1e11) sprintf (strbuf, "% 0.*fG", precision-2, f*1e-9);
		else if (absf < 1e12) sprintf (strbuf, "% 0.*fG", precision-3, f*1e-9);
		else                  sprintf (strbuf, "% 0.*fT", precision-1, f*1e-12);
	} else {
		if      (absf < 1e14) sprintf (strbuf, "% 0.*fT", precision-2, f*1e-12);
		else if (absf < 1e15) sprintf (strbuf, "% 0.*fT", precision-3, f*1e-12);
		else                  sprintf (strbuf, "% 0.0e", f);
	}
	return strbuf;
}

char *SciStr (double f, int precision, char prefix)
{
	const char *fmtstr[3] = {
		"%0.*g", "% 0.*g", "%+0.*g"
	};
	const char *fstr = fmtstr[0];
	if (prefix == ' ') fstr = fmtstr[1];
	else if (prefix == '+') fstr = fmtstr[2];

	int i, len, e;
	sprintf (strbuf, fstr, precision, f);
	len = strlen (strbuf);
	for (i = 0; i < len; i++) {
		if (strbuf[i] == 'e') {
			sscanf(strbuf+i+1, "%d", &e);
			sprintf (strbuf+i, "·10^%d", e);
		}
	}
	return strbuf;
}
