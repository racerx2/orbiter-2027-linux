#!/usr/bin/env python3
# GPSNav: OurAirports' data (public domain, https://ourairports.com/data) as the add-on's Earth files
# usage: ourairports.py <folder with airports.csv runways.csv navaids.csv> <out folder>   (the files from github.com/davidmegginson/ourairports-data)
import csv, math, os, sys

FT = 0.3048
R = 6371010.0


def num(s):
    try:
        return float(s)
    except (TypeError, ValueError):
        return None


def dest(lat, lng, brg, d):  # degrees, metres
    la, lo, b, a = math.radians(lat), math.radians(lng), math.radians(brg), d / R
    la2 = math.asin(math.sin(la) * math.cos(a) + math.cos(la) * math.sin(a) * math.cos(b))
    lo2 = lo + math.atan2(math.sin(b) * math.sin(a) * math.cos(la), math.cos(a) - math.sin(la) * math.sin(la2))
    return math.degrees(la2), (math.degrees(lo2) + 540.0) % 360.0 - 180.0


def main(src, out):
    os.makedirs(out, exist_ok=True)
    types = {"large_airport", "medium_airport", "small_airport", "heliport", "seaplane_base"}
    apt = {}
    with open(os.path.join(src, "airports.csv"), newline="", encoding="utf-8") as f, \
         open(os.path.join(out, "airports.csv"), "w", newline="", encoding="utf-8") as o:
        w = csv.writer(o, lineterminator="\n")
        w.writerow(["ident", "name", "type", "lat", "lon", "elev_m"])
        for r in csv.DictReader(f):
            lat, lon = num(r["latitude_deg"]), num(r["longitude_deg"])
            if r["type"] not in types or lat is None or lon is None:
                continue
            el = num(r["elevation_ft"])
            el = el * FT if el is not None else 0.0
            apt[r["ident"]] = (lat, lon, el)
            w.writerow([r["ident"], r["name"], r["type"], f"{lat:.6f}", f"{lon:.6f}", f"{el:.0f}"])
    n = 0
    with open(os.path.join(src, "runways.csv"), newline="", encoding="utf-8") as f, \
         open(os.path.join(out, "runways.csv"), "w", newline="", encoding="utf-8") as o:
        w = csv.writer(o, lineterminator="\n")
        w.writerow(["airport", "id1", "lat1", "lon1", "elev1_m", "id2", "lat2", "lon2", "elev2_m", "width_m", "length_m", "surface"])
        for r in csv.DictReader(f):
            a = r["airport_ident"]
            if a not in apt or r["closed"] == "1":
                continue
            ln, wd = num(r["length_ft"]), num(r["width_ft"])
            la1, lo1, la2, lo2 = num(r["le_latitude_deg"]), num(r["le_longitude_deg"]), num(r["he_latitude_deg"]), num(r["he_longitude_deg"])
            hdg = num(r["le_heading_degT"])
            if None in (la1, lo1, la2, lo2):  # no ends given: about the airport, on the heading
                if hdg is None:
                    try:
                        hdg = int(r["le_ident"].rstrip("LRCW")[:2]) * 10.0
                    except ValueError:
                        continue
                if not ln:
                    continue
                c = apt[a]
                la1, lo1 = dest(c[0], c[1], hdg + 180.0, 0.5 * ln * FT)
                la2, lo2 = dest(c[0], c[1], hdg, 0.5 * ln * FT)
            e1, e2 = num(r["le_elevation_ft"]), num(r["he_elevation_ft"])
            el = apt[a][2]
            e1 = e1 * FT if e1 is not None else el
            e2 = e2 * FT if e2 is not None else el
            if not ln:
                d = math.radians
                h = math.sin(0.5 * d(la2 - la1)) ** 2 + math.cos(d(la1)) * math.cos(d(la2)) * math.sin(0.5 * d(lo2 - lo1)) ** 2
                ln = 2.0 * R * math.asin(math.sqrt(min(1.0, h))) / FT
            w.writerow([a, r["le_ident"], f"{la1:.6f}", f"{lo1:.6f}", f"{e1:.0f}", r["he_ident"], f"{la2:.6f}", f"{lo2:.6f}", f"{e2:.0f}",
                        f"{(wd or 0) * FT:.0f}", f"{ln * FT:.0f}", r["surface"]])
            n += 1
    m = 0
    with open(os.path.join(src, "navaids.csv"), newline="", encoding="utf-8") as f, \
         open(os.path.join(out, "navaids.csv"), "w", newline="", encoding="utf-8") as o:
        w = csv.writer(o, lineterminator="\n")
        w.writerow(["ident", "name", "type", "freq", "lat", "lon", "elev_m"])
        for r in csv.DictReader(f):
            lat, lon, khz = num(r["latitude_deg"]), num(r["longitude_deg"]), num(r["frequency_khz"])
            if lat is None or lon is None or not r["ident"]:
                continue
            t = r["type"]
            freq = "" if khz is None else f"{khz:.0f}" if t.startswith("NDB") else f"{khz / 1000.0:.2f}"
            el = num(r["elevation_ft"])
            w.writerow([r["ident"], r["name"], t, freq, f"{lat:.6f}", f"{lon:.6f}", f"{(el or 0) * FT:.0f}"])
            m += 1
    print(f"{len(apt)} airports, {n} runways, {m} navaids")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__ or "usage: ourairports.py <source folder> <out folder>")
    main(sys.argv[1], sys.argv[2])
