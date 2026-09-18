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
import csv
import io
import json
import ssl
import sys
import urllib.error
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
    "sy_snum",       # number of stars in the system - flags binaries/multiples
    "sy_dist", "ra", "dec",
    "glat", "glon",  # galactic coordinates - used to orient the background skybox per system
]

# `stellarhosts` carries stellar parameters per star, including
# gravitationally-bound companion stars that don't host any known planet
# themselves. It does NOT include a physical/angular separation column -
# that data mostly lives in individual discovery papers, not a queryable
# bulk field - so this only gives us real per-star temperature/mass/radius
# where measured; companion *position* has to stay schematic in the app.
STELLARHOSTS_COLUMNS = ["hostname", "st_teff", "st_rad", "st_mass", "st_spectype", "sy_snum"]

# NASA/JPL's own Small-Body DataBase API (ssd-api.jpl.nasa.gov) - a
# DIFFERENT, official NASA data source from the Exoplanet Archive above.
# No key needed. Used here to pull REAL orbital + physical data for our
# Solar System's 5 IAU-recognized dwarf planets, rather than hand-typed
# constants. Docs: https://ssd-api.jpl.nasa.gov/doc/sbdb.html
SBDB_BASE = "https://ssd-api.jpl.nasa.gov/sbdb.api"
DWARF_PLANET_NAMES = ["Ceres", "Pluto", "Eris", "Haumea", "Makemake"]

# CDS's VizieR TAP service (Strasbourg astronomical data center) - a
# THIRD distinct data source, different from both NASA services above.
# Used to pull the real Hipparcos star catalog (ESA mission, table
# I/239/hip_main) for the skybox's starfield, replacing the earlier
# purely-stylized hash-noise stars with real measured positions.
# No key needed. Docs: https://tapvizier.cds.unistra.fr/adql/
VIZIER_TAP_BASE = "https://tapvizier.cds.unistra.fr/TAPVizieR/tap/sync"
# Vmag <= 6.5 is the traditional naked-eye visibility limit - about 9,000
# stars, a well-understood scope match for "the visible night sky" rather
# than the full ~118,000-star catalog (which goes much fainter than any
# eye could see, and would be gratuitous data volume for a background).
STAR_CATALOG_VMAG_LIMIT = 6.5


def build_query(limit: int | None) -> str:
    cols = ",".join(COLUMNS)
    query = f"SELECT {cols} FROM ps WHERE default_flag=1"
    if limit:
        query = f"SELECT TOP {limit} {cols} FROM ps WHERE default_flag=1"
    return query


def build_stellarhosts_query(limit: int | None) -> str:
    cols = ",".join(STELLARHOSTS_COLUMNS)
    query = f"SELECT {cols} FROM stellarhosts"
    if limit:
        query = f"SELECT TOP {limit} {cols} FROM stellarhosts"
    return query


def run_query(query: str, timeout: int = 60):
    params = {"query": query, "format": "json"}
    url = f"{TAP_BASE}?{urllib.parse.urlencode(params)}"
    print(f"Requesting: {url}")

    with urllib.request.urlopen(url, timeout=timeout) as resp:
        if resp.status != 200:
            raise RuntimeError(f"TAP request failed: HTTP {resp.status}")
        return json.loads(resp.read().decode("utf-8"))


def _to_float(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def fetch_dwarf_planets():
    """
    Queries NASA/JPL's SBDB API (a separate, official NASA data source from
    the Exoplanet Archive) for each of the 5 IAU-recognized dwarf planets,
    returning a list of records shaped to match what Dataset::LoadDwarfPlanets
    expects on the C++ side: real orbital elements (a, e, i, argument of
    periapsis, longitude of ascending node, period) plus diameter and GM
    (standard gravitational parameter) where SBDB has them measured.
    """
    results = []
    for name in DWARF_PLANET_NAMES:
        url = f"{SBDB_BASE}?{urllib.parse.urlencode({'sstr': name, 'phys-par': '1'})}"
        print(f"Requesting: {url}")
        try:
            with urllib.request.urlopen(url, timeout=30) as resp:
                data = json.loads(resp.read().decode("utf-8"))
        except Exception as e:
            print(f"  WARNING: couldn't fetch {name} from SBDB: {e}", file=sys.stderr)
            continue

        if "object" not in data or "orbit" not in data:
            print(f"  WARNING: unexpected SBDB response for {name}, skipping", file=sys.stderr)
            continue

        elements = {el["name"]: el.get("value") for el in data["orbit"].get("elements", [])}
        phys = {p["name"]: p.get("value") for p in data.get("phys_par", [])}

        results.append({
            "pl_name": name,  # plain query name, not SBDB's numbered "134340 Pluto" style fullname
            "hostname": "Sun",
            "pl_orbsmax": _to_float(elements.get("a")),
            "pl_orbeccen": _to_float(elements.get("e")),
            "pl_orbincl": _to_float(elements.get("i")),
            "pl_orblper": _to_float(elements.get("w")),
            "pl_orbper": _to_float(elements.get("per")),
            "long_asc_node": _to_float(elements.get("om")),
            "diameter_km": _to_float(phys.get("diameter")),
            "gm": _to_float(phys.get("GM")),
            "source": "NASA/JPL SBDB",
        })
    return results


def fetch_star_catalog():
    """
    Queries CDS's VizieR TAP service for the Hipparcos catalog (ESA
    mission, table I/239/hip_main): real right ascension, declination,
    visual magnitude, and B-V color index for every star down to the
    naked-eye limit. Returns a list of {"ra_deg","dec_deg","vmag","bv"}
    records for ExoSim's skybox to render as real, positioned points
    instead of procedural noise.

    VizieR's TAP sync endpoint returns CSV here (confirmed to work in
    published examples) rather than JSON (unconfirmed for this specific
    service), so this parses CSV defensively: any row that doesn't parse
    cleanly is skipped rather than aborting the whole fetch, since a
    background starfield missing a few odd rows is harmless, while
    crashing on one bad row would lose the whole catalog.
    """
    adql = (
        'SELECT HIP, RAICRS, DEICRS, Vmag, "B-V" '
        f'FROM "I/239/hip_main" WHERE Vmag<={STAR_CATALOG_VMAG_LIMIT}'
    )
    params = {"REQUEST": "doQuery", "LANG": "ADQL", "FORMAT": "CSV", "QUERY": adql}
    url = f"{VIZIER_TAP_BASE}?{urllib.parse.urlencode(params)}"
    print(f"Requesting: {url}")

    try:
        with urllib.request.urlopen(url, timeout=90) as resp:
            if resp.status != 200:
                raise RuntimeError(f"VizieR TAP request failed: HTTP {resp.status}")
            text = resp.read().decode("utf-8", errors="replace")
    except urllib.error.URLError as e:
        # Some Windows Python installs can't verify tapvizier.cds.unistra.fr's
        # certificate chain even though other HTTPS endpoints in this script
        # work fine (a local trust-store gap, not a problem with the query or
        # endpoint). Retry once without verification, ONLY after a genuine
        # verification failure, and ONLY for this one request - this catalog
        # is public, non-sensitive astronomical data, so that's a low-stakes
        # trade for a background starfield. The proper fix is `pip install
        # certifi` (Python often picks it up automatically) or updating your
        # OS's root certificates - this fallback exists so the fetch still
        # works today without adding that as a required dependency.
        is_cert_error = isinstance(getattr(e, "reason", None), ssl.SSLCertVerificationError) \
            or "CERTIFICATE_VERIFY_FAILED" in str(e)
        if not is_cert_error:
            print(f"WARNING: couldn't fetch the Hipparcos star catalog ({e}) - "
                  f"the skybox will fall back to its procedural stars, but ExoSim still runs fine.",
                  file=sys.stderr)
            return []

        print(f"WARNING: TLS certificate verification failed for VizieR ({e}) - retrying this "
              f"one request without verification (see comments in fetch_star_catalog for why "
              f"this is a reasonable fallback here, and how to fix it properly instead).",
              file=sys.stderr)
        try:
            insecure_ctx = ssl.create_default_context()
            insecure_ctx.check_hostname = False
            insecure_ctx.verify_mode = ssl.CERT_NONE
            with urllib.request.urlopen(url, timeout=90, context=insecure_ctx) as resp:
                if resp.status != 200:
                    raise RuntimeError(f"VizieR TAP request failed: HTTP {resp.status}")
                text = resp.read().decode("utf-8", errors="replace")
        except Exception as e2:
            print(f"WARNING: retry without certificate verification also failed ({e2}) - "
                  f"the skybox will fall back to its procedural stars, but ExoSim still runs fine.",
                  file=sys.stderr)
            return []
    except Exception as e:
        print(f"WARNING: couldn't fetch the Hipparcos star catalog ({e}) - "
              f"the skybox will fall back to its procedural stars, but ExoSim still runs fine.",
              file=sys.stderr)
        return []

    reader = csv.reader(io.StringIO(text))
    rows = [r for r in reader if r and not r[0].startswith("#")]
    if not rows:
        print("WARNING: Hipparcos catalog response had no usable rows.", file=sys.stderr)
        return []

    header = [h.strip().lstrip("_").upper() for h in rows[0]]

    def col_index(*names):
        for name in names:
            if name in header:
                return header.index(name)
        return None

    ra_i = col_index("RAICRS", "RA_ICRS", "RA(ICRS)")
    de_i = col_index("DEICRS", "DE_ICRS", "DE(ICRS)")
    vmag_i = col_index("VMAG")
    bv_i = col_index("B-V", "B_V", "BV")

    if ra_i is None or de_i is None or vmag_i is None:
        print(f"WARNING: unexpected Hipparcos CSV header {header!r} - "
              f"couldn't find RA/Dec/Vmag columns, skipping star catalog.", file=sys.stderr)
        return []

    results = []
    for row in rows[1:]:
        if len(row) <= max(ra_i, de_i, vmag_i):
            continue
        ra = _to_float(row[ra_i])
        dec = _to_float(row[de_i])
        vmag = _to_float(row[vmag_i])
        if ra is None or dec is None or vmag is None:
            continue
        bv = _to_float(row[bv_i]) if (bv_i is not None and bv_i < len(row)) else None
        results.append({"ra_deg": ra, "dec_deg": dec, "vmag": vmag, "bv": bv})

    return results


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--limit", type=int, default=None,
                         help="Only fetch the first N rows of each table (useful while testing)")
    parser.add_argument("--out", type=str, default="exoplanets.json",
                         help="Output path for the planet table, relative to this script's directory")
    parser.add_argument("--stellarhosts-out", type=str, default="stellar_hosts.json",
                         help="Output path for the stellar hosts table, relative to this script's directory")
    parser.add_argument("--dwarf-planets-out", type=str, default="dwarf_planets.json",
                         help="Output path for NASA/JPL SBDB dwarf-planet data, relative to this script's directory")
    parser.add_argument("--skip-dwarf-planets", action="store_true",
                         help="Skip the NASA/JPL SBDB dwarf-planet fetch (5 quick extra requests)")
    parser.add_argument("--star-catalog-out", type=str, default="star_catalog.json",
                         help="Output path for the Hipparcos star catalog, relative to this script's directory")
    parser.add_argument("--skip-star-catalog", action="store_true",
                         help="Skip the VizieR/Hipparcos star catalog fetch (~9,000-row CSV request)")
    args = parser.parse_args()

    try:
        rows = run_query(build_query(args.limit))
    except Exception as e:
        print(f"ERROR fetching planet table: {e}", file=sys.stderr)
        sys.exit(1)

    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(rows, f)

    usable = sum(1 for r in rows if r.get("pl_orbsmax") and r.get("pl_orbper"))
    print(f"Wrote {len(rows)} planet records to {args.out}")
    print(f"({usable} have both semi-major axis and period, so are drawable)")

    try:
        # This table is large (tens of thousands of rows, unfiltered) and
        # slower than the planet query above, hence the longer timeout.
        star_rows = run_query(build_stellarhosts_query(args.limit), timeout=120)
        with open(args.stellarhosts_out, "w", encoding="utf-8") as f:
            json.dump(star_rows, f)
        print(f"Wrote {len(star_rows)} stellar host records to {args.stellarhosts_out}")
        print("(used to render real companion-star temperature/mass/radius in multi-star systems -")
        print(" note the Archive has no bulk separation field, so companion POSITION is schematic)")
    except Exception as e:
        print(f"WARNING: couldn't fetch stellar hosts table ({e}) - "
              f"companion stars won't be modeled, but the app still runs fine without it.",
              file=sys.stderr)

    if not args.skip_dwarf_planets:
        dwarf_rows = fetch_dwarf_planets()
        if dwarf_rows:
            with open(args.dwarf_planets_out, "w", encoding="utf-8") as f:
                json.dump(dwarf_rows, f)
            print(f"Wrote {len(dwarf_rows)} dwarf planet records to {args.dwarf_planets_out} "
                  f"(NASA/JPL SBDB - a different NASA source from the Exoplanet Archive above)")

    if not args.skip_star_catalog:
        star_catalog_rows = fetch_star_catalog()
        if star_catalog_rows:
            with open(args.star_catalog_out, "w", encoding="utf-8") as f:
                json.dump(star_catalog_rows, f)
            print(f"Wrote {len(star_catalog_rows)} real star positions to {args.star_catalog_out} "
                  f"(Hipparcos catalog via CDS VizieR TAP - a third, separate data source)")


if __name__ == "__main__":
    main()
