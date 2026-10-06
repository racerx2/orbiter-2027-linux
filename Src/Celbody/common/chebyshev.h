// not upstream: Chebyshev table of JPL Horizons positions; outside it, or without the file, the fitted orbit of ephemeris.h is used
#pragma once
// File: a text line "CHEB1 <mjd0> <segment days> <segments> <coefficients>", then little-endian doubles [segment][x, y, z][coefficient], km, ecliptic J2000.
#include "ephemeris.h"
#include <cstdio>
#include <string>
#include <vector>

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "the .cheb tables are little-endian");

class ChebModule : public MoonModule {
  const char* path;                // table file, relative to the Orbiter root
  double t0 = 0, L = 0;            // table start [MJD] and segment length [days]
  int nseg = 0, n = 0;             // segments, coefficients per axis
  std::vector<double> c;           // the coefficients
  bool tried = false;

  void load() {
    tried = true;
    FILE* f = std::fopen(oapiResolvePath(path).c_str(), "rb");
    if (!f) { oapiWriteLogError("%s: file not found, using the fitted orbit", path); return; }
    char hdr[128] = {0};
    bool ok = std::fgets(hdr, sizeof(hdr), f) && std::sscanf(hdr, "CHEB1 %lf %lf %d %d", &t0, &L, &nseg, &n) == 4
              && std::isfinite(t0) && std::isfinite(L) && L >= 1e-3 && nseg > 0 && n > 1 && n <= 64;
    if (ok) {
      long at = std::ftell(f);
      ok = at > 0 && std::fseek(f, 0, SEEK_END) == 0 && std::ftell(f) - at == (long)((size_t)nseg*3*n*sizeof(double)) && std::fseek(f, at, SEEK_SET) == 0;
    }
    if (ok) {
      c.resize((size_t)nseg*3*n);
      ok = std::fread(c.data(), sizeof(double), c.size(), f) == c.size();
    }
    std::fclose(f);
    if (!ok) { c.clear(); nseg = 0; oapiWriteLogError("%s: bad file, using the fitted orbit", path); }
  }

  // position [m] and velocity [m/s] in Orbiter's x, z, y order; false outside the table
  bool table(double mjd, double* ret) {
    if (!tried) load();
    if (!nseg) return false;
    double u = (mjd - t0)/L;
    if (!(u >= 0 && u < nseg)) return false;
    int s = (int)u;
    double x = 2.0*(u - s) - 1.0, p[3], v[3];
    for (int a = 0; a < 3; a++) {
      const double* k = c.data() + ((size_t)s*3 + a)*n;
      double T0 = 1, T1 = x, D0 = 0, D1 = 1, r = k[0] + k[1]*x, d = k[1];
      for (int j = 2; j < n; j++) {
        double T2 = 2*x*T1 - T0, D2 = 2*T1 + 2*x*D1 - D0;
        r += k[j]*T2; d += k[j]*D2;
        T0 = T1; T1 = T2; D0 = D1; D1 = D2;
      }
      p[a] = r*1000.0;
      v[a] = d*(2.0/L)*1000.0/eph::DAY;
    }
    ret[0]=p[0]; ret[1]=p[2]; ret[2]=p[1];  ret[3]=v[0]; ret[4]=v[2]; ret[5]=v[1];
    for (int i = 0; i < 6; i++) ret[i+6] = ret[i];
    return true;
  }
public:
  ChebModule(const char* file, const eph::Elem& el, const eph::Planet& pl, const eph::Term* t, int nt) : MoonModule(el, pl, t, nt), path(file) {}
  ChebModule(const char* file, const eph::Elem& el, const eph::Planet& pl) : MoonModule(el, pl), path(file) {}

  void clbkInit(FILEHANDLE cfg) override { CELBODY::clbkInit(cfg); if (!tried) load(); }
  int  clbkEphemeris(double mjd, int req, double* ret) override { return table(mjd, ret) ? 0x1f : MoonModule::clbkEphemeris(mjd, req, ret); }
  int  clbkFastEphemeris(double simt, int req, double* ret) override { return clbkEphemeris(oapiTime2MJD(simt), req, ret); }
};
