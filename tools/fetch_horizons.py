#!/usr/bin/env python3
"""Regenerate tests/data/horizons_planets_1950_2050.csv from JPL Horizons.

Heliocentric geometric positions (ecliptic and mean equinox of J2000, km,
TDB) of the eight planets, every 730 days from 1950-01-01 to 2050. The
ExoSimTests EPH-H checks compare ExoSim's ephemeris against this file.

    python tools/fetch_horizons.py            # writes the CSV in place
    python tools/fetch_horizons.py --step 365d --out my.csv

Uses only the standard library. Horizons API docs:
https://ssd-api.jpl.nasa.gov/doc/horizons.html
"""
import argparse
import datetime
import json
import ssl
import sys
import urllib.parse
import urllib.request
from pathlib import Path

PLANETS = [("Mercury", "199"), ("Venus", "299"), ("Earth", "399"), ("Mars", "499"),
           ("Jupiter", "599"), ("Saturn", "699"), ("Uranus", "799"), ("Neptune", "899")]
API = "https://ssd.jpl.nasa.gov/api/horizons.api"


def ssl_context():
    try:
        import certifi  # optional: fixes CERTIFICATE_VERIFY_FAILED on some Windows Pythons
        return ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        return ssl.create_default_context()


def fetch(command, start, stop, step):
    params = {
        "format": "json", "COMMAND": f"'{command}'", "OBJ_DATA": "'NO'", "MAKE_EPHEM": "'YES'",
        "EPHEM_TYPE": "'VECTORS'", "CENTER": "'500@10'", "START_TIME": f"'{start}'",
        "STOP_TIME": f"'{stop}'", "STEP_SIZE": f"'{step}'", "REF_PLANE": "'ECLIPTIC'",
        "REF_SYSTEM": "'J2000'", "VEC_TABLE": "'1'", "OUT_UNITS": "'KM-S'", "CSV_FORMAT": "'YES'",
        "VEC_LABELS": "'NO'",
    }
    url = API + "?" + urllib.parse.urlencode(params)
    with urllib.request.urlopen(url, context=ssl_context(), timeout=60) as r:
        result = json.load(r)["result"]
    rows = result.split("$$SOE")[1].split("$$EOE")[0].strip().splitlines()
    out = []
    for row in rows:
        f = [x.strip() for x in row.split(",")]
        out.append((f[0], f[2], f[3], f[4]))  # JD, X, Y, Z (column 1 is the calendar date)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--start", default="1950-01-01")
    ap.add_argument("--stop", default="2050-01-02")
    ap.add_argument("--step", default="730d")
    ap.add_argument("--out", default=str(Path(__file__).resolve().parent.parent / "tests" / "data" / "horizons_planets_1950_2050.csv"))
    a = ap.parse_args()

    lines = [
        "# JPL Horizons (DE441) heliocentric geometric positions, ecliptic and mean equinox of J2000.0, km.",
        f"# Query: CENTER=500@10, EPHEM_TYPE=VECTORS, REF_PLANE=ECLIPTIC, START {a.start}, STEP {a.step}, time scale TDB.",
        f"# Regenerate with: python tools/fetch_horizons.py   (retrieved {datetime.date.today().isoformat()})",
        "body,jd_tdb,x_km,y_km,z_km",
    ]
    for name, cmd in PLANETS:
        print(f"Fetching {name} ...", file=sys.stderr)
        for jd, x, y, z in fetch(cmd, a.start, a.stop, a.step):
            lines.append(f"{name},{float(jd)},{x},{y},{z}")
    Path(a.out).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote {a.out} ({len(lines) - 4} rows)", file=sys.stderr)


if __name__ == "__main__":
    main()
