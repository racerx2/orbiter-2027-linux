#!/usr/bin/env python3
"""Build Orbiter Neptune-system barycenter Chebyshev ephemeris from JPL Horizons.

Offline development tool: not linked into the simulator. Requires Python 3 + numpy.
The generated CHEB1 table is loaded by Orbiter using its existing table format.
JPL Horizons target 8 (Neptune system barycenter) relative to Sun 10,
geometric ICRF J2000 ecliptic, TDB, km and km/s. No spacecraft physics changes.

Usage:
  python3 Tools/ephemeris/build_neptune_cheb.py
"""
import argparse
import json
import math
import os
from pathlib import Path
import struct
import tempfile
import time
from urllib.parse import urlencode
from urllib.request import Request, urlopen

import numpy as np
from numpy.polynomial import chebyshev as ch

MJD0 = -21504.0                # 1800-01-01 00:00 TDB
DAYS_PER_SEGMENT = 32.0
N_SEGMENTS = 4566               # inclusive through 2200-01-16 TDB
COEFFS = 12                    # degree 11; smooth barycentric orbit
SAMPLE_SPACING_DAYS = 2.0      # 17 measured points for every 32-day fit
SEGMENTS_PER_QUERY = 48        # 769 states per JPL request
SECONDS_PER_DAY = 86400.0
J2000_MJD_TO_JD = 2400000.5
HORIZONS_URL = "https://ssd.jpl.nasa.gov/api/horizons.api"
DEFAULT_OUTPUT = (Path(__file__).resolve().parents[2] /
                  "Src/Celbody/Vsop87/Neptune/Config/Neptune/Data/neptune.cheb")


def fetch(first_mjd, last_mjd, sample_step=2.0):
    """Return contiguous numpy rows [JD, XYZ(km), VXYZ(km/s)]."""
    params = dict(
        format="json", COMMAND="8", EPHEM_TYPE="VECTORS",
        CENTER="500@10", REF_PLANE="ECLIPTIC", REF_SYSTEM="ICRF",
        VEC_TABLE="2", OUT_UNITS="KM-S", CSV_FORMAT="YES",
        VEC_CORR="NONE", TIME_TYPE="TDB",
        START_TIME=f"JD{first_mjd + J2000_MJD_TO_JD:.8f}",
        STOP_TIME=f"JD{last_mjd + J2000_MJD_TO_JD:.8f}",
        STEP_SIZE=f"{sample_step:g}d",
    )
    url = HORIZONS_URL + "?" + urlencode(params)
    error = None
    for attempt in range(5):
        try:
            req = Request(url, headers={"User-Agent": "Orbiter2027-Linux-ephemeris-builder/1.0"})
            with urlopen(req, timeout=75) as f:
                payload = json.load(f)
            source = payload.get("result", "")
            if payload.get("error") or "$$SOE" not in source or "$$EOE" not in source:
                raise RuntimeError(str(payload.get("error") or source[-800:]))
            rows = []
            for line in source.split("$$SOE", 1)[1].split("$$EOE", 1)[0].splitlines():
                if not line.strip():
                    continue
                parts = [col.strip() for col in line.split(",")]
                if len(parts) < 8:
                    raise ValueError("unexpected Horizons vector row: " + line[:150])
                rows.append([float(parts[0]), *[float(parts[i]) for i in range(2, 8)]])
            out = np.asarray(rows, dtype=np.float64)
            expected_count = int(round((last_mjd-first_mjd)/sample_step)) + 1
            if out.shape != (expected_count, 7):
                raise ValueError(f"Horizons returned {out.shape}, expected {(expected_count,7)}")
            jd_expect = (first_mjd + J2000_MJD_TO_JD
                         + np.arange(expected_count)*sample_step)
            if np.max(np.abs(out[:, 0] - jd_expect)) > 1e-7:
                raise ValueError("Horizons epoch mismatch")
            if not np.isfinite(out).all():
                raise ValueError("non-finite Horizons vectors")
            return out
        except Exception as exc:
            error = exc
            time.sleep(min(5.0, 0.5 * (attempt+1)))
    raise RuntimeError(f"NASA/JPL Horizons retrieval failed at {first_mjd}: {error}") from error


def make_table(path):
    first = MJD0
    total_days = int(N_SEGMENTS * DAYS_PER_SEGMENT)
    print(f"Target: Neptune system barycenter 8 vs Sun 10, ICRF ecliptic J2000/TDB", flush=True)
    print(f"Range: MJD {first:.1f} to {first+total_days:.1f}, {N_SEGMENTS} x {int(DAYS_PER_SEGMENT)}-day segments", flush=True)
    print(f"Format: CHEB1, {COEFFS} coefficients per axis (km); derivative gives km/s", flush=True)
    path.parent.mkdir(parents=True, exist_ok=True)

    max_pos_residual_km, max_vel_residual_kms = 0.0, 0.0
    total_samples = 0
    file_fd, temporary_name = tempfile.mkstemp(prefix=".neptune-cheb-", suffix=".part", dir=Path(__file__).parent)
    try:
        with os.fdopen(file_fd, "wb") as dst:
            dst.write(f"CHEB1 {MJD0:.9f} {DAYS_PER_SEGMENT:.9f} {N_SEGMENTS} {COEFFS}\n".encode("ascii"))
            for begin_seg in range(0, N_SEGMENTS, SEGMENTS_PER_QUERY):
                amount = min(SEGMENTS_PER_QUERY, N_SEGMENTS - begin_seg)
                begin = MJD0 + begin_seg * DAYS_PER_SEGMENT
                end = begin + amount * DAYS_PER_SEGMENT
                rows = fetch(begin, end, SAMPLE_SPACING_DAYS)
                npts = int(DAYS_PER_SEGMENT / SAMPLE_SPACING_DAYS) + 1
                x = np.linspace(-1.0, 1.0, npts)
                for seg in range(amount):
                    lo = seg * (npts - 1)
                    samples = rows[lo:lo+npts]
                    position = samples[:, 1:4]
                    velocity = samples[:, 4:7]
                    coefs = ch.chebfit(x, position, COEFFS-1)
                    # Cubic Hermite correction enforces C1 continuity.
                    end_value = np.vstack([
                        np.array([ch.chebval(z, coefs[:, a]) for a in range(3)])
                        for z in (-1.0, 1.0)
                    ])
                    end_derivative = np.vstack([
                        np.array([ch.chebval(z, ch.chebder(coefs[:, a]))
                                  for a in range(3)])
                        for z in (-1.0, 1.0)
                    ])
                    target_derivative = velocity[[0,-1], :] * (
                        DAYS_PER_SEGMENT * SECONDS_PER_DAY / 2.0
                    )
                    # Hermite rows: endpoint positions, then derivatives.
                    hermite = np.array([
                        [1., -1.,  1., -1.],
                        [1.,  1.,  1.,  1.],
                        [0.,  1., -2.,  3.],
                        [0.,  1.,  2.,  3.],
                    ])
                    residuals = np.vstack([
                        position[0, :] - end_value[0, :],
                        position[-1, :] - end_value[1, :],
                        target_derivative[0, :] - end_derivative[0, :],
                        target_derivative[1, :] - end_derivative[1, :],
                    ])
                    correction = np.linalg.solve(hermite, residuals)
                    for axis in range(3):
                        coefs[:4, axis] += ch.poly2cheb(correction[:, axis])
                    # Coefficient layout [segment][axis][coefficient] matches CHEB1.
                    for axis in range(3):
                        dst.write(struct.pack("<" + "d"*COEFFS, *coefs[:, axis]))
                    # Cross-check the actual JPL velocities, not merely fitted positions.
                    checkp = np.column_stack([ch.chebval(x, coefs[:, axis]) for axis in range(3)])
                    checkv = np.column_stack([
                        ch.chebval(x, ch.chebder(coefs[:, axis])) * 2.0 /
                        (DAYS_PER_SEGMENT*SECONDS_PER_DAY) for axis in range(3)
                    ])
                    max_pos_residual_km = max(max_pos_residual_km,
                                              float(np.max(np.linalg.norm(checkp-position, axis=1))))
                    max_vel_residual_kms = max(max_vel_residual_kms,
                                               float(np.max(np.linalg.norm(checkv-velocity, axis=1))))
                total_samples += len(rows)
                print(f"  {begin_seg+amount:4d}/{N_SEGMENTS} segments; "
                      f"max node error {max_pos_residual_km*1000:.3f} m / "
                      f"{max_vel_residual_kms*1000:.6f} m/s", flush=True)
        if max_pos_residual_km > 0.05 or max_vel_residual_kms > 0.005:
            raise ValueError("fit accuracy exceeded 50 m / 5 mm/s, refusing to install")
        os.replace(temporary_name, path)
        os.chmod(path, 0o644)
    finally:
        if os.path.exists(temporary_name):
            os.remove(temporary_name)
    print(f"SUCCESS: {path} ({path.stat().st_size:,} bytes)", flush=True)
    print(f"Max sampled-node position discrepancy: {max_pos_residual_km*1000:.4f} m", flush=True)
    print(f"Max sampled-node velocity discrepancy: {max_vel_residual_kms*1000:.8f} m/s", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    make_table(args.output)


if __name__ == "__main__":
    main()
