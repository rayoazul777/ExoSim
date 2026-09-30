#!/usr/bin/env python3
"""
Fetches ExoSim's real data from four public sources (no API keys needed)
and writes JSON files next to this script:

  exoplanets.json     NASA Exoplanet Archive, `ps` table (TAP)
  stellar_hosts.json  NASA Exoplanet Archive, `stellarhosts` table (TAP)
  dwarf_planets.json  NASA/JPL Small-Body Database API
  star_catalog.json   Hipparcos catalogue via CDS VizieR (TAP)
  double_stars.json   Washington Double Star catalog via CDS VizieR (TAP)

Any source that fails is skipped with a warning; ExoSim disables only the
feature that file feeds. See docs/DATA_SOURCES.md for citations and terms.

Usage:
    python fetch_exoplanets.py [--limit N] [--star-vmag-limit M]
                               [--skip-dwarf-planets] [--skip-star-catalog]
                               [--skip-double-stars]

Tip: `pip install certifi` fixes TLS verification failures against
VizieR on some Windows Python installs (used automatically when present).
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
            "mean_anomaly_deg": _to_float(elements.get("ma")),
            "epoch_jd": _to_float(data["orbit"].get("epoch")),
            "diameter_km": _to_float(phys.get("diameter")),
            "gm": _to_float(phys.get("GM")),
            "source": "NASA/JPL SBDB",
        })
    return results


def _https_context():
    """
    Preferred TLS setup: use certifi's CA bundle when it's installed - the
    proper fix for Windows Python installs whose local trust store can't
    verify some servers (e.g. VizieR). `pip install certifi` enables it.
    Falls back to the system default context otherwise.
    """
    try:
        import certifi  # optional dependency
        return ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        return ssl.create_default_context()


def _vizier_csv_rows(adql, label, timeout=120):
    """
    Runs one ADQL query against CDS VizieR's TAP service and returns parsed
    CSV rows (header first), or [] on failure. Certificate handling, in order:
    certifi bundle if installed -> system store -> ONE retry without
    verification, only after a genuine CERTIFICATE_VERIFY_FAILED, only for
    this public, non-sensitive astronomical data - and always logged.
    """
    params = {"REQUEST": "doQuery", "LANG": "ADQL", "FORMAT": "CSV", "QUERY": adql}
    url = f"{VIZIER_TAP_BASE}?{urllib.parse.urlencode(params)}"
    print(f"Requesting: {url}")

    def _get(ctx):
        with urllib.request.urlopen(url, timeout=timeout, context=ctx) as resp:
            if resp.status != 200:
                raise RuntimeError(f"VizieR TAP request failed: HTTP {resp.status}")
            return resp.read().decode("utf-8", errors="replace")

    try:
        text = _get(_https_context())
    except urllib.error.URLError as e:
        is_cert_error = isinstance(getattr(e, "reason", None), ssl.SSLCertVerificationError) \
            or "CERTIFICATE_VERIFY_FAILED" in str(e)
        if not is_cert_error:
            print(f"WARNING: couldn't fetch {label} ({e}).", file=sys.stderr)
            return []
        print(f"WARNING: TLS certificate verification failed for VizieR ({e}) - retrying this "
              f"one public-data request without verification. For the proper fix, run "
              f"`pip install certifi` (this script uses it automatically when present).",
              file=sys.stderr)
        insecure = ssl.create_default_context()
        insecure.check_hostname = False
        insecure.verify_mode = ssl.CERT_NONE
        try:
            text = _get(insecure)
        except Exception as e2:
            print(f"WARNING: retry for {label} also failed ({e2}).", file=sys.stderr)
            return []
    except Exception as e:
        print(f"WARNING: couldn't fetch {label} ({e}).", file=sys.stderr)
        return []

    rows = [r for r in csv.reader(io.StringIO(text)) if r and not r[0].startswith("#")]
    if len(rows) < 2:
        print(f"WARNING: {label} response had no data rows. First 300 chars of response:\n{text[:300]}",
              file=sys.stderr)
        return []
    return rows


def _col(header, *names):
    for n in names:
        if n in header:
            return header.index(n)
    return None


def _norm_header(row):
    return [h.strip().lstrip("_").upper() for h in row]


def fetch_star_catalog(vmag_limit):
    """
    Real Hipparcos catalog (ESA; VizieR table I/239/hip_main): position,
    magnitude, B-V color, proper motion (pmRA includes cos(dec), per
    Hipparcos convention) and parallax, for every star down to vmag_limit.
    Rows that don't parse cleanly are skipped rather than aborting.
    """
    adql = ('SELECT HIP, RAICRS, DEICRS, Vmag, "B-V", pmRA, pmDE, Plx '
            f'FROM "I/239/hip_main" WHERE Vmag<={vmag_limit}')
    rows = _vizier_csv_rows(adql, "the Hipparcos star catalog")
    if not rows:
        return []
    header = _norm_header(rows[0])
    ra_i = _col(header, "RAICRS", "RA_ICRS", "RA(ICRS)")
    de_i = _col(header, "DEICRS", "DE_ICRS", "DE(ICRS)")
    vm_i = _col(header, "VMAG")
    bv_i = _col(header, "B-V", "B_V", "BV")
    pr_i = _col(header, "PMRA")
    pd_i = _col(header, "PMDE")
    px_i = _col(header, "PLX")
    if ra_i is None or de_i is None or vm_i is None:
        print(f"WARNING: unexpected Hipparcos CSV header {header!r} - skipping star catalog.", file=sys.stderr)
        return []

    def cell(row, i):
        return _to_float(row[i]) if (i is not None and i < len(row)) else None

    out = []
    for row in rows[1:]:
        ra, dec, vmag = cell(row, ra_i), cell(row, de_i), cell(row, vm_i)
        if ra is None or dec is None or vmag is None:
            continue
        out.append({"ra_deg": ra, "dec_deg": dec, "vmag": vmag, "bv": cell(row, bv_i),
                    "pm_ra_mas_yr": cell(row, pr_i), "pm_dec_mas_yr": cell(row, pd_i),
                    "parallax_mas": cell(row, px_i)})
    return out


def _parse_angle(text, is_ra):
    """Decimal degrees, or sexagesimal ('hh mm ss.s' / 'dd:mm:ss') if needed."""
    v = _to_float(text)
    if v is not None:
        return v
    parts = text.replace(":", " ").split()
    if len(parts) < 2:
        return None
    try:
        sign = -1.0 if parts[0].strip().startswith("-") else 1.0
        a, b = abs(float(parts[0])), float(parts[1])
        c = float(parts[2]) if len(parts) > 2 else 0.0
        val = a + b / 60.0 + c / 3600.0
        return val * 15.0 if is_ra else sign * val
    except ValueError:
        return None


def fetch_double_stars():
    """
    Washington Double Star catalog (USNO; VizieR table B/wds/wds): position
    and most recent measured angular separation (arcsec) for each pair.
    ExoSim cross-matches these to multi-star exoplanet hosts by position,
    and converts separation x distance -> real PROJECTED separation in AU.
    """
    rows = _vizier_csv_rows('SELECT RAJ2000, DEJ2000, sep1, sep2 FROM "B/wds/wds"',
                            "the Washington Double Star catalog", timeout=180)
    if not rows:
        return []
    header = _norm_header(rows[0])
    ra_i = _col(header, "RAJ2000", "RA_J2000")
    de_i = _col(header, "DEJ2000", "DE_J2000")
    s1_i = _col(header, "SEP1")
    s2_i = _col(header, "SEP2")
    if ra_i is None or de_i is None or (s1_i is None and s2_i is None):
        print(f"WARNING: unexpected WDS CSV header {header!r} - skipping double stars.", file=sys.stderr)
        return []
    out = []
    for row in rows[1:]:
        if len(row) <= max(i for i in (ra_i, de_i, s1_i, s2_i) if i is not None):
            continue
        ra = _parse_angle(row[ra_i], True)
        dec = _parse_angle(row[de_i], False)
        sep = _to_float(row[s2_i]) if s2_i is not None else None  # sep2 = most recent measurement
        if sep is None and s1_i is not None:
            sep = _to_float(row[s1_i])
        if ra is None or dec is None or sep is None or sep <= 0:
            continue
        out.append({"ra_deg": ra, "dec_deg": dec, "sep_arcsec": sep})
    return out


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
    parser.add_argument("--star-vmag-limit", type=float, default=STAR_CATALOG_VMAG_LIMIT,
                         help="Faintest star magnitude to fetch (default 6.5 = naked eye, ~9k stars; "
                              "8.0 ~ 40k stars; 12.4 = full Hipparcos, ~118k)")
    parser.add_argument("--skip-double-stars", action="store_true",
                         help="Skip the Washington Double Star catalog fetch (real companion separations)")
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
        star_catalog_rows = fetch_star_catalog(args.star_vmag_limit)
        if star_catalog_rows:
            with open(args.star_catalog_out, "w", encoding="utf-8") as f:
                json.dump(star_catalog_rows, f)
            print(f"Wrote {len(star_catalog_rows)} real star positions to {args.star_catalog_out} "
                  f"(Hipparcos catalog via CDS VizieR TAP - a third, separate data source)")



    if not args.skip_double_stars:
        pairs = fetch_double_stars()
        if pairs:
            with open("double_stars.json", "w", encoding="utf-8") as f:
                json.dump(pairs, f)
            print(f"Wrote {len(pairs)} double-star separations to double_stars.json "
                  f"(Washington Double Star catalog via CDS VizieR)")


if __name__ == "__main__":
    main()
