.pragma library

// heliocentric ecliptic J2000 positions in AU: planets from JPL's approximate elements, NEOs two-body from JPL element sets

var DEG = Math.PI / 180;
var MJD_J2000 = 51544.5;
var MJD_MIN = -21504;   // 1800-01-01, start of the planet table's range
var MJD_MAX = 69807;    // 2050-01-01, end of the planet table's range and of time flow
var TEN_YEARS_D = 3652.5; // how long the last element sets stay in use

// Standish (JPL), approximate planet elements, table 1, 1800-2050: a e I L varpi Omega, then their rates per century
var planetEl = {
    Mercury: [0.38709927, 0.20563593, 7.00497902, 252.25032350, 77.45779628, 48.33076593, 0.00000037, 0.00001906, -0.00594749, 149472.67411175, 0.16047689, -0.12534081],
    Venus:   [0.72333566, 0.00677672, 3.39467605, 181.97909950, 131.60246718, 76.67984255, 0.00000390, -0.00004107, -0.00078890, 58517.81538729, 0.00268329, -0.27769418],
    Earth:   [1.00000261, 0.01671123, -0.00001531, 100.46457166, 102.93768193, 0.0, 0.00000562, -0.00004392, -0.01294668, 35999.37244981, 0.32327364, 0.0],
    Mars:    [1.52371034, 0.09339410, 1.84969142, -4.55343205, -23.94362959, 49.55953891, 0.00001847, 0.00007882, -0.00813131, 19140.30268499, 0.44441088, -0.29257343]
};
var planetNames = ["Mercury", "Venus", "Earth", "Mars"];

// JPL element sets (Horizons osculating elements, or SBDB API full-prec), fetched 2026-10-01; positions only inside [from, to) JD
var APOPHIS_FLYBY_JD = 2462240.407091969; // JPL CAD, orbit 220: 2029-Apr-13 21:46 TDB, 0.000254090910419299 AU
var B0 = 2449718.5, B1 = 2453371.0, B2 = 2457023.5, B3 = 2460025.0; // 1995, then midpoints between the epochs
function el(src, epoch, e, a, i, om, w, ma, n, from, to) { return { src: src, epoch: epoch, e: e, a: a, i: i, om: om, w: w, ma: ma, n: n, from: from, to: to }; }
var neos = [
    { name: "Apophis", sets: [
        el("JPL Horizons, JPL#220", 2451544.5, 0.1913926258631164, 0.9223417394291891, 3.331244196967460, 204.6568307850113, 126.0648832968501, 231.6243952547896, 1.112669747393845, B0, B1),
        el("JPL Horizons, JPL#220", 2455197.5, 0.1912122049020198, 0.9224209173959603, 3.331518281222531, 204.4396276189811, 126.4238779576592, 336.6112378997939, 1.112526487896919, B1, B2),
        el("JPL Horizons, JPL#220", 2458849.5, 0.1914663582610371, 0.9225609080452407, 3.336782730252751, 204.0529292717555, 126.6848743835318, 80.19184213500814, 1.112273273075492, B2, B3),
        el("JPL SBDB orbit 220", 2461200.5, 0.1911492279663492, 0.9223592206975018, 3.340996879880978, 203.8936514240762, 126.6795706895841, 175.3304026592739, 1.112638115271892, B3, APOPHIS_FLYBY_JD),
        el("JPL Horizons, JPL#220, after the 2029 flyby", 2462320.5, 0.1890124082863401, 1.103037292117766, 2.221033579148814, 203.5583001910897, 71.43484156145448, 16.46552059305237, 0.8507829551907622, APOPHIS_FLYBY_JD, 2462320.5 + TEN_YEARS_D) ] },
    { name: "Bennu", sets: [
        el("JPL Horizons, ORX_merged_DE424", 2451544.5, 0.2046526054479352, 1.128924032007707, 6.025534771807846, 2.178572502802274, 65.67196720343235, 35.00708949375966, 0.8216880984884486, B0, B1),
        el("JPL Horizons, ORX_merged_DE424", 2455197.5, 0.2037780137686343, 1.126302125138441, 6.035045534848877, 2.061281162512221, 66.22699956249274, 160.7508508017112, 0.8245589662568907, B1, B2),
        el("JPL Horizons, ORX_merged_DE424", 2458849.5, 0.2036793381859808, 1.125917932416679, 6.034108877858773, 2.008417796820511, 66.33952684003408, 293.0255804840464, 0.8249810439684178, B2, B3),
        el("JPL Horizons, ORX_merged_DE424", 2461200.5, 0.2036821438660532, 1.125950726463012, 6.032966274857705, 1.966574037255910, 66.41055452273676, 72.45176654686115, 0.8249450020676293, B3, 2461200.5 + TEN_YEARS_D) ] },
    { name: "Didymos", sets: [
        el("JPL Horizons, JPL#240", 2451544.5, 0.3831879008645149, 1.642077489706229, 3.396519095582996, 73.44778599143386, 318.8861467146697, 65.51836576888675, 0.4683964581536544, B0, B1),
        el("JPL Horizons, JPL#240", 2455197.5, 0.3838277441879669, 1.644520880440456, 3.407759704053140, 73.24799659098328, 319.1897828040269, 334.1783022505095, 0.4673529472150221, B1, B2),
        el("JPL Horizons, JPL#240", 2458849.5, 0.3837939531356034, 1.644532506309142, 3.408311785548309, 73.20920222626359, 319.3057776538304, 241.0207711969688, 0.4673479913618807, B2, B3),
        el("JPL SBDB orbit 240", 2461200.5, 0.3831233242624545, 1.642709608529702, 3.413876519313629, 72.9858236207145, 319.5807001349104, 260.8612886320632, 0.4681261239466357, B3, 2461200.5 + TEN_YEARS_D) ] }
];
// years the element sets cover, for the diagram's note
var NEO_YEARS = "1995–2036";

// Apophis closest approach in UTC: 21:46:12.746 TDB minus TT-UTC 69.184 s
var APOPHIS_FLYBY_MS = Date.UTC(2029, 3, 13, 21, 45, 3, 562);

function wrap360(d) { d = d % 360; return d < 0 ? d + 360 : d; }
function wrap180(d) { d = wrap360(d + 180); return d - 180; }

function solveKepler(M, e) {
    var E = e < 0.8 ? M : Math.PI;
    for (var k = 0; k < 30; k++) {
        var dE = (E - e * Math.sin(E) - M) / (1 - e * Math.cos(E));
        E -= dE;
        if (Math.abs(dE) < 1e-12) break;
    }
    return E;
}

// orbital plane coordinates rotated into the ecliptic: u = argument of latitude (rad)
function toEcliptic(r, u, i, om) {
    var ci = Math.cos(i), co = Math.cos(om), so = Math.sin(om), cu = Math.cos(u), su = Math.sin(u);
    return { x: r * (co * cu - so * su * ci), y: r * (so * cu + co * su * ci), z: r * su * Math.sin(i) };
}

function fromAnomaly(a, e, iDeg, omDeg, wDeg, Mdeg) {
    var E = solveKepler(wrap180(Mdeg) * DEG, e);
    var nu = 2 * Math.atan2(Math.sqrt(1 + e) * Math.sin(E / 2), Math.sqrt(1 - e) * Math.cos(E / 2));
    var r = a * (1 - e * Math.cos(E));
    return toEcliptic(r, wDeg * DEG + nu, iDeg * DEG, omDeg * DEG);
}

function planetElements(name, mjd) {
    var el = planetEl[name], T = (mjd - MJD_J2000) / 36525, o = [];
    for (var k = 0; k < 6; k++) o.push(el[k] + el[k + 6] * T);
    return { a: o[0], e: o[1], i: o[2], om: o[5], w: o[4] - o[5], M: o[3] - o[4] };
}

function planet(name, mjd) {
    var k = planetElements(name, mjd);
    return fromAnomaly(k.a, k.e, k.i, k.om, k.w, k.M);
}

// points around an orbit from its true anomaly, for drawing
function ellipse(a, e, iDeg, omDeg, wDeg, n) {
    var pts = [], p = a * (1 - e * e);
    for (var k = 0; k <= n; k++) {
        var nu = k / n * 2 * Math.PI;
        pts.push(toEcliptic(p / (1 + e * Math.cos(nu)), wDeg * DEG + nu, iDeg * DEG, omDeg * DEG));
    }
    return pts;
}

function planetPath(name, mjd, n) {
    var k = planetElements(name, mjd);
    return ellipse(k.a, k.e, k.i, k.om, k.w, n);
}

function neoByName(name) {
    for (var k = 0; k < neos.length; k++) if (neos[k].name === name) return neos[k];
    return null;
}

// the element set for an epoch: inside its window, or else the nearest one with inside = false
function neoSet(name, mjd) {
    var o = neoByName(name), jd = mjd + 2400000.5, best = null, gap = Infinity;
    if (!o) return null;
    for (var k = 0; k < o.sets.length; k++) {
        var s = o.sets[k];
        if (jd >= s.from && jd < s.to) return { set: s, inside: true };
        var g = jd < s.from ? s.from - jd : jd - s.to;
        if (g < gap) { gap = g; best = s; }
    }
    return { set: best, inside: false };
}

// position, or null outside the element sets' windows
function neo(name, mjd) {
    var r = neoSet(name, mjd);
    if (!r || !r.inside) return null;
    var s = r.set;
    return fromAnomaly(s.a, s.e, s.i, s.om, s.w, wrap360(s.ma + s.n * (mjd + 2400000.5 - s.epoch)));
}

function neoPath(name, mjd, n) {
    var r = neoSet(name, mjd);
    if (!r) return [];
    var s = r.set;
    return ellipse(s.a, s.e, s.i, s.om, s.w, n);
}

function mjdOfMs(ms) { return ms / 86400000 + 40587; }

// time to (or since) Apophis's closest approach
function countdown(nowMs) {
    var d = APOPHIS_FLYBY_MS - nowMs, past = d < 0;
    if (past) d = -d;
    var ms = Math.floor(d % 1000), s = Math.floor(d / 1000);
    return { past: past, days: Math.floor(s / 86400), hours: Math.floor(s / 3600) % 24, minutes: Math.floor(s / 60) % 60, seconds: s % 60, ms: ms };
}

var months = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"];
function pad(v, n) { var t = "" + v; while (t.length < n) t = "0" + t; return t; }

// "7 APR 2001 17:58"
function epochText(mjd) {
    var d = new Date(Math.round((mjd - 40587) * 86400000)); // to the millisecond, so float error does not drop a minute
    if (isNaN(d.getTime())) return "";
    return d.getUTCDate() + " " + months[d.getUTCMonth()] + " " + d.getUTCFullYear() + " " + pad(d.getUTCHours(), 2) + ":" + pad(d.getUTCMinutes(), 2);
}

// "2001-04-07"
function dayText(mjd) {
    var d = new Date(Math.round((mjd - 40587) * 86400000));
    if (isNaN(d.getTime())) return "";
    return d.getUTCFullYear() + "-" + pad(d.getUTCMonth() + 1, 2) + "-" + pad(d.getUTCDate(), 2);
}
