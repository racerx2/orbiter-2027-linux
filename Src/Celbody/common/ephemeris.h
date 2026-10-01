#pragma once
// ---------------------------------------------------------------------------
// Analytic satellite ephemeris: a Keplerian orbit whose plane and line of apsides
// precess at constant rates about a fitted axis, plus a short series of periodic
// radial / along-track / normal terms. Position/velocity are returned relative to
// the primary in the ecliptic frame of J2000 (Orbiter axis order x, z, y), in m and m/s.
//
// Elements, rates, axis and terms are fitted to JPL Horizons over 1800-2200; J2 and Req are unused.
// ---------------------------------------------------------------------------
#include "celbody.h"
#include <cmath>

namespace eph {
  constexpr double PI   = 3.14159265358979323846;
  constexpr double DEG  = PI/180.0;
  constexpr double DAY  = 86400.0;
  constexpr double YEAR = 365.25*DAY;
  constexpr double OBLIQ= 23.43929111*PI/180.0;   // Earth mean obliquity, J2000

  struct V3 { double x,y,z; };
  inline V3 operator+(V3 a,V3 b){ return {a.x+b.x,a.y+b.y,a.z+b.z}; }
  inline V3 operator*(V3 a,double s){ return {a.x*s,a.y*s,a.z*s}; }
  inline double dot(V3 a,V3 b){ return a.x*b.x+a.y*b.y+a.z*b.z; }
  inline V3 cross(V3 a,V3 b){ return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
  inline double len(V3 a){ return std::sqrt(dot(a,a)); }
  inline V3 unit(V3 a){ double l=len(a); return {a.x/l,a.y/l,a.z/l}; }

  // Rotate v about unit axis k by angle th (Rodrigues' formula).
  inline V3 rotate(V3 v, V3 k, double th){
    double c=std::cos(th), s=std::sin(th);
    return v*c + cross(k,v)*s + k*(dot(k,v)*(1.0-c));
  }
  // Equatorial (ICRF/J2000) -> ecliptic (J2000).
  inline V3 eqToEcl(V3 v){
    double c=std::cos(OBLIQ), s=std::sin(OBLIQ);
    return { v.x, c*v.y + s*v.z, -s*v.y + c*v.z };
  }
  // Solve Kepler's equation M = E - e sinE for the eccentric anomaly E.
  inline double solveKepler(double M, double e){
    M = std::fmod(M, 2*PI); if(M> PI) M-=2*PI; if(M<-PI) M+=2*PI;
    double E = M;
    for(int k=0;k<80;k++){ double d=(E-e*std::sin(E)-M)/(1-e*std::cos(E)); E-=d; if(std::fabs(d)<1e-15) break; }
    return E;
  }

  struct Elem   { double a_km, e, inc_deg, node_deg, peri_deg, M0_deg, n_degs, nodeRate_degs, periRate_degs, epoch_mjd; double ndot_degs2 = 0; };
  // poleRA/poleDec: the fitted precession axis (equatorial J2000); it may point along or against the orbit normal
  struct Planet { double J2, Req_km, poleRA_deg, poleDec_deg; };
  // displacement c*cos(k*L + nu*dt) + s*sin(k*L + nu*dt) along R, T or N (comp 0, 1, 2); nu in rad/s, c and s in m
  struct Term   { int comp, k; double nu, c, s; };
}

// A celestial-body module driven by one element set + one primary.
class MoonModule : public CELBODY {
  eph::V3 h0, e0, q0, pole;      // orbit normal, periapsis dir, in-plane dir (ecliptic, J2000)
  double  a_m, e, n, M0, epoch;  // a [m], ecc, mean motion [rad/s], mean anomaly@epoch [rad], MJD
  double  nodeRate, periRate;    // secular rates about the axis as given (see Planet)  [rad/s]
  double  ndot = 0;              // d(mean motion)/dt [rad/s^2]
  const eph::Term* terms = nullptr; int nterms = 0;   // periodic terms, L = M + periRate*dt
public:
  MoonModule(const eph::Elem& el, const eph::Planet& pl){ init(el,pl); }
  MoonModule(const eph::Elem& el, const eph::Planet& pl, const eph::Term* t, int nt) : terms(t), nterms(nt) { init(el,pl); }

  void init(const eph::Elem& el, const eph::Planet& pl){
    using namespace eph;
    a_m   = el.a_km*1000.0;
    e     = el.e;
    n     = el.n_degs*eph::DEG;
    M0    = el.M0_deg*eph::DEG;
    epoch = el.epoch_mjd;
    double i=el.inc_deg*eph::DEG, Om=el.node_deg*eph::DEG, w=el.peri_deg*eph::DEG;

    // Orientation vectors in the ecliptic frame from the orbital angles.
    h0 = unit(V3{ std::sin(i)*std::sin(Om), -std::sin(i)*std::cos(Om), std::cos(i) });
    double co=std::cos(Om),so=std::sin(Om),ci=std::cos(i),cw=std::cos(w),sw=std::sin(w);
    e0 = unit(V3{ co*cw - so*sw*ci, so*cw + co*sw*ci, sw*std::sin(i) });
    q0 = cross(h0,e0);

    // Precession axis (RA/Dec -> unit vector -> ecliptic).
    double ra=pl.poleRA_deg*eph::DEG, dc=pl.poleDec_deg*eph::DEG;
    pole = unit(eqToEcl(V3{ std::cos(dc)*std::cos(ra), std::cos(dc)*std::sin(ra), std::sin(dc) }));

    // Secular precession rates about the axis, fitted to JPL Horizons.
    nodeRate = el.nodeRate_degs*eph::DEG;
    periRate = el.periRate_degs*eph::DEG;
    ndot     = el.ndot_degs2*eph::DEG;
    (void)pl.J2; (void)pl.Req_km;
  }

  int state(double mjd, double* ret) const {
    using namespace eph;
    double dt = (mjd - epoch)*DAY;
    double M  = M0 + n*dt + 0.5*ndot*dt*dt;
    // Precess the orbit: regress the plane about the pole, advance periapsis in-plane.
    double dNode = nodeRate*dt;
    double dPeri = (periRate - nodeRate)*dt;        // argument-of-periapsis advance
    V3 h = rotate(h0, pole, dNode);
    V3 ev= rotate( rotate(e0, pole, dNode), h, dPeri );
    V3 q = cross(h, ev);
    // Position & velocity in the (precessed) orbital plane.
    double E=solveKepler(M,e), b=std::sqrt(1.0-e*e), ce=std::cos(E), se=std::sin(E);
    double rx=a_m*(ce-e), ry=a_m*b*se;
    double f=(n + ndot*dt)*a_m/(1.0-e*ce);
    double vx=-f*se, vy=f*b*ce;
    V3 r = ev*rx + q*ry;
    V3 v = ev*vx + q*vy;
    // velocity of the precessing frame
    v = v + cross(pole, r)*nodeRate + cross(h, r)*(periRate - nodeRate);
    if (nterms) {
      // periodic terms along R, T = N x R, N = h, and their rates
      double rl = len(r);
      V3 R = r*(1.0/rl), N = h, T = cross(N, R);
      V3 Rd = (v + R*(-dot(R, v)))*(1.0/rl), Nd = cross(pole, N)*nodeRate, Td = cross(Nd, R) + cross(N, Rd);
      double L = M + periRate*dt, Ld = n + ndot*dt + periRate;
      double d[3] = {0, 0, 0}, dd[3] = {0, 0, 0};
      for (int j = 0; j < nterms; j++) {
        const Term& t = terms[j];
        double th = t.k*L + t.nu*dt, ct = std::cos(th), st = std::sin(th);
        d[t.comp]  += t.c*ct + t.s*st;
        dd[t.comp] += (t.k*Ld + t.nu)*(t.s*ct - t.c*st);
      }
      r = r + R*d[0] + T*d[1] + N*d[2];
      v = v + R*dd[0] + Rd*d[0] + T*dd[1] + Td*d[1] + N*dd[2] + Nd*d[2];
    }
    // Orbiter's axis order is x, z, y (y = ecliptic north), as in CelBodyAPI.h
    ret[0]=r.x; ret[1]=r.z; ret[2]=r.y;  ret[3]=v.x; ret[4]=v.z; ret[5]=v.y;
    ret[6]=r.x; ret[7]=r.z; ret[8]=r.y;  ret[9]=v.x; ret[10]=v.z; ret[11]=v.y;
    return 0x1f;   // true + barycentric position & velocity provided
  }

  bool bEphemeris() const override { return true; }
  int  clbkEphemeris(double mjd, int /*req*/, double* ret) override { return state(mjd, ret); }
  int  clbkFastEphemeris(double simt, int /*req*/, double* ret) override { return state(oapiTime2MJD(simt), ret); }

  // diagnostics (used by the verification harness / docs)
  double nodalPeriodYears()   const { return 2*eph::PI/std::fabs(nodeRate)/eph::YEAR; }
  double apsidalPeriodYears() const { return 2*eph::PI/std::fabs(periRate)/eph::YEAR; }
};
