#!/usr/bin/env python3
"""
Pulls confirmed-planet orbital elements from the NASA Exoplanet Archive's
TAP service and writes them to data/exoplanets.json for ExoSim to load.

No API key needed - this is a public, unauthenticated endpoint.
Docs: https://exoplanetarchive.ipac.caltech.edu/docs/TAP/usingTAP.html

Usage:
    python fetch_exoplanets.py [--limit N] [--out PATH]
"""

import argparse
import json
import sys
import urllib.parse
import urllib.request

TAP_BASE = "https://exoplanetarchive.ipac.caltech.edu/TAP/sync"

# `ps` = Planetary Systems table. default_flag=1 keeps just the one
# "best" parameter set per planet instead of every historical entry.
COLUMNS = [
    "pl_name", "hostname",
    "pl_orbper", "pl_orbsmax", "pl_orbeccen", "pl_orbincl", "pl_orblper",
    "pl_bmassj", "pl_radj",
    "pl_dens",       # bulk density, g/cm^3 - much more diagnostic of composition than radius alone
    "pl_eqt",        # equilibrium temperature, K - informs whether an atmosphere could survive
    "pl_insol",      # insolation flux, relative to Earth
    "st_mass", "st_rad", "st_teff", "st_spectype",
    "sy_dist", "ra", "dec",
]


def build_query(limit: int | None) -> str:
    cols = ",".join(COLUMNS)
    query = f"SELECT {cols} FROM ps WHERE default_flag=1"
    if limit:
        query = f"SELECT TOP {limit} {cols} FROM ps WHERE default_flag=1"
    return query


def fetch(limit: int | None):
    query = build_query(limit)
    params = {"query": query, "format": "json"}
    url = f"{TAP_BASE}?{urllib.parse.urlencode(params)}"
    print(f"Requesting: {url}")

    with urllib.request.urlopen(url, timeout=60) as resp:
        if resp.status != 200:
            raise RuntimeError(f"TAP request failed: HTTP {resp.status}")
        data = json.loads(resp.read().decode("utf-8"))

    return data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--limit", type=int, default=None,
                         help="Only fetch the first N rows (useful while testing)")
    parser.add_argument("--out", type=str, default="exoplanets.json",
                         help="Output path, relative to this script's directory")
    args = parser.parse_args()

    try:
        rows = fetch(args.limit)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)

    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(rows, f)

    usable = sum(1 for r in rows if r.get("pl_orbsmax") and r.get("pl_orbper"))
    print(f"Wrote {len(rows)} planet records to {args.out}")
    print(f"({usable} have both semi-major axis and period, so are drawable)")


if __name__ == "__main__":
    main()
