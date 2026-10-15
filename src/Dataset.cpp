#include "Dataset.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {
    // Pulls a numeric field out of a JSON row, returning defaultVal
    // if the field is missing or null (NASA archive data has a LOT
    // of nulls - not every planet has every parameter measured).
    // Type-checked field access: a field that's present but has the wrong
    // JSON type (e.g. a number arriving as a string, or a row that isn't an
    // object at all) falls back to the default instead of throwing. Before
    // this, one malformed catalog row crashed the whole app - found by the
    // DAT-1 robustness test in tests/test_physics.cpp.
    double numOr(const json& row, const char* key, double defaultVal) {
        if (!row.is_object() || !row.contains(key)) return defaultVal;
        const json& v = row[key];
        if (v.is_number()) return v.get<double>();
        if (v.is_string()) {
            try { size_t used = 0; double d = std::stod(v.get<std::string>(), &used);
                  if (used == v.get<std::string>().size()) return d; } catch (...) {}
        }
        return defaultVal;
    }

    std::string strOr(const json& row, const char* key, const std::string& defaultVal) {
        if (row.is_object() && row.contains(key) && row[key].is_string()) {
            return row[key].get<std::string>();
        }
        return defaultVal;
    }
}

bool Dataset::LoadFromFile(const std::string& path, std::vector<PlanetRecord>& outPlanets) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[Dataset] Could not open " << path
                  << " - run data/fetch_exoplanets.py first, or fall back to DemoSolarSystem().\n";
        return false;
    }

    json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        std::cerr << "[Dataset] JSON parse error: " << e.what() << "\n";
        return false;
    }

    std::vector<PlanetRecord> planets;
    planets.reserve(root.size());

    for (const auto& row : root) {
        double semiMajorAxis = numOr(row, "pl_orbsmax", -1.0);
        double period = numOr(row, "pl_orbper", -1.0);

        // Both are required to propagate a position - skip if missing.
        if (semiMajorAxis <= 0.0 || period <= 0.0) continue;

        PlanetRecord p;
        p.name = strOr(row, "pl_name", "Unnamed");
        p.hostStar = strOr(row, "hostname", "Unknown");
        p.elements.semiMajorAxisAU = semiMajorAxis;
        p.elements.periodDays = period;
        p.elements.eccentricity = numOr(row, "pl_orbeccen", 0.0);
        p.elements.inclinationDeg = numOr(row, "pl_orbincl", 0.0);
        p.elements.inclinationMeasured = row.contains("pl_orbincl") && !row["pl_orbincl"].is_null();
        p.elements.argPeriapsisDeg = numOr(row, "pl_orblper", 0.0);
        p.massJupiter = numOr(row, "pl_bmassj", 0.0);
        p.radiusJupiter = numOr(row, "pl_radj", 0.0);
        p.densityGcm3 = numOr(row, "pl_dens", 0.0);
        p.eqTempK = numOr(row, "pl_eqt", 0.0);
        p.insolationEarth = numOr(row, "pl_insol", 0.0);

        p.starTeffK = numOr(row, "st_teff", 0.0);
        p.starRadiusSolar = numOr(row, "st_rad", 0.0);
        p.starMassSolar = numOr(row, "st_mass", 0.0);
        p.starSpectralType = strOr(row, "st_spectype", "");
        p.numStarsInSystem = static_cast<int>(numOr(row, "sy_snum", 1.0));
        p.galacticLatDeg = numOr(row, "glat", 0.0);
        p.galacticLonDeg = numOr(row, "glon", 0.0);
        p.hostRaDeg = numOr(row, "ra", 0.0);
        p.hostDecDeg = numOr(row, "dec", 0.0);
        p.distPc = numOr(row, "sy_dist", 0.0);

        planets.push_back(std::move(p));
    }

    outPlanets = std::move(planets);
    std::cout << "[Dataset] Loaded " << outPlanets.size() << " planets with usable orbital elements.\n";
    return true;
}

std::vector<PlanetRecord> Dataset::DemoSolarSystem() {
    // Real J2000 orbital elements from JPL's "Keplerian Elements for
    // Approximate Positions of the Major Planets" (Standish), converted to
    // this app's convention: argPeri = (longitude of perihelion - node),
    // meanAnomalyAtEpoch = (mean longitude - longitude of perihelion). With
    // these, simulation day 0 IS the J2000 epoch (2000-01-01 12:00 TT), so
    // planet positions - and therefore the mission planner's launch windows
    // - line up with real calendar dates (to within this simple two-body
    // model's accuracy, ~a degree or so for the inner planets over decades).
    //
    // eqTempK follows the same "airless blackbody equilibrium" definition
    // the Archive uses for pl_eqt, so it's directly comparable to exoplanet
    // values elsewhere in the app (this is why it reads far colder than each
    // planet's real surface temperature, especially Venus, whose greenhouse
    // effect this number deliberately excludes).
    struct Seed {
        const char* name;
        double a, e, i, per, density;
        double eqTempK, radiusJup, massJup;
        double argPeriapsisDeg, longAscNodeDeg, meanAnomalyJ2000Deg;
    };
    static const Seed seeds[] = {
        // name        a            e           i      per       dens  eqT    rJup    mJup      argPeri  node     M0(J2000)
        {"Mercury",  0.38709927, 0.20563593, 7.005,   87.969,  5.43, 440.0, 0.0349, 0.000174,  29.127,  48.331, 174.793},
        {"Venus",    0.72333566, 0.00677672, 3.395,  224.701,  5.24, 227.0, 0.0865, 0.00257,   54.923,  76.680,  50.377},
        {"Earth",    1.00000261, 0.01671123, 0.000,  365.256,  5.51, 255.0, 0.0892, 0.00315,  102.938,   0.000, 357.527},
        {"Mars",     1.52371034, 0.09339410, 1.850,  686.980,  3.93, 210.0, 0.0474, 0.000338, 286.497,  49.560,  19.390},
        {"Jupiter",  5.20288700, 0.04838624, 1.304, 4332.59,   1.33, 110.0, 1.0,    1.0,      274.255, 100.474,  19.668},
        {"Saturn",   9.53667594, 0.05386179, 2.486,10759.22,   0.69,  81.0, 0.843,  0.299,    338.937, 113.662, 317.355},
        {"Uranus",  19.18916464, 0.04725744, 0.773,30688.5,    1.27,  58.0, 0.365,  0.0457,    96.937,  74.017, 142.284},
        {"Neptune", 30.06992276, 0.00859048, 1.770,60182.0,    1.64,  46.0, 0.354,  0.0536,   273.181, 131.784, 259.915},
    };

    std::vector<PlanetRecord> out;
    for (const auto& s : seeds) {
        PlanetRecord p;
        p.name = s.name;
        p.hostStar = "Sun";
        p.elements.semiMajorAxisAU = s.a;
        p.elements.eccentricity = s.e;
        p.elements.inclinationDeg = s.i;
        p.elements.inclinationMeasured = true; // hand-coded real values, not archive fallbacks
        p.elements.argPeriapsisDeg = s.argPeriapsisDeg;
        p.elements.longAscNodeDeg = s.longAscNodeDeg;
        p.elements.meanAnomalyAtEpochDeg = s.meanAnomalyJ2000Deg;
        p.elements.periodDays = s.per;
        p.densityGcm3 = s.density;
        p.eqTempK = s.eqTempK;
        p.radiusJupiter = s.radiusJup;
        p.massJupiter = s.massJup;
        p.starTeffK = 5778.0;
        p.starRadiusSolar = 1.0;
        p.starMassSolar = 1.0;
        p.starSpectralType = "G2 V";
        out.push_back(std::move(p));
    }

    // Real, well-documented moons (approximate orbital distance in units
    // of the parent planet's own radius, orbital period in days,
    // inclination relative to the relevant reference plane, and
    // eccentricity). Mercury and Venus genuinely have none, so they're
    // simply omitted. Triton's real orbit is retrograde; direction isn't
    // modeled here, only its unusually high inclination is.
    //
    // Argument of periapsis / longitude of ascending node are left at 0
    // for everything except the Moon, for two honest reasons: (1) most of
    // these moons have inclinations near zero, where the ascending node
    // is mathematically ill-defined (a real quirk of orbital elements,
    // not a gap in our data), and (2) the ones that DO have a meaningful
    // node (Phobos, Triton) precess fast enough that a single fixed
    // number would misrepresent them as static. The Moon's values below
    // are an approximate J2000-epoch snapshot - its node precesses a full
    // circle roughly every 18.6 years and its periapsis every 8.85 years,
    // so treat these as illustrative of typical magnitude, not live figures.
    auto findPlanet = [&out](const char* name) -> PlanetRecord& {
        for (auto& p : out) if (p.name == name) return p;
        return out[0]; // unreachable given the seeds above
    };
    findPlanet("Earth").moons = {
        {"Moon", 60.3, 27.32, 5.14, 0.0549, 318.15, 125.08, true, 1737.4},
    };
    findPlanet("Mars").moons = {
        {"Phobos", 2.76, 0.319, 1.08, 0.0151, 0.0, 0.0, true, 11.3},
        {"Deimos", 6.92, 1.263, 1.79, 0.00033, 0.0, 0.0, true, 6.2},
    };
    findPlanet("Jupiter").moons = {
        {"Io", 5.9, 1.77, 0.04, 0.0041, 0.0, 0.0, true, 1821.6},
        {"Europa", 9.4, 3.55, 0.47, 0.009, 0.0, 0.0, true, 1560.8},
        {"Ganymede", 15.0, 7.15, 0.20, 0.0013, 0.0, 0.0, true, 2634.1},
        {"Callisto", 26.3, 16.69, 0.19, 0.0074, 0.0, 0.0, true, 2410.3},
    };
    findPlanet("Saturn").moons = {
        {"Titan", 20.3, 15.95, 0.35, 0.0288, 0.0, 0.0, true, 2574.7},
        {"Rhea", 8.75, 4.52, 0.345, 0.001258, 0.0, 0.0, true, 763.8},
    };
    findPlanet("Uranus").moons = {
        {"Titania", 17.6, 8.71, 0.34, 0.0011, 0.0, 0.0, true, 788.4},
        {"Oberon", 23.6, 13.46, 0.058, 0.0014, 0.0, 0.0, true, 761.4},
    };
    findPlanet("Neptune").moons = {
        {"Triton", 14.3, 5.877, 157.0, 0.000016, 0.0, 0.0, true, 1353.4},
    };

    return out;
}

std::vector<StarSystem> Dataset::GroupByHost(const std::vector<PlanetRecord>& planets) {
    std::vector<StarSystem> systems;

    // Preserve first-seen order while grouping (the sort below reorders by
    // planet count; std::sort isn't stable, so ties may reorder).
    std::unordered_map<std::string, size_t> indexByHost;
    for (size_t i = 0; i < planets.size(); ++i) {
        const std::string& host = planets[i].hostStar;
        // Hash lookup (host name -> index into `systems`) instead of a linear
        // scan per row: O(n) overall rather than O(n x systems).
        auto found = indexByHost.find(host);
        if (found == indexByHost.end()) {
            indexByHost.emplace(host, systems.size());
            StarSystem sys;
            sys.hostName = host;
            sys.planetIndices.push_back(i);
            sys.starTeffK = planets[i].starTeffK;
            sys.starRadiusSolar = planets[i].starRadiusSolar;
            sys.starMassSolar = planets[i].starMassSolar;
            sys.starSpectralType = planets[i].starSpectralType;
            sys.numStars = planets[i].numStarsInSystem;
            sys.galacticLatDeg = planets[i].galacticLatDeg;
            sys.galacticLonDeg = planets[i].galacticLonDeg;
            sys.hostRaDeg = planets[i].hostRaDeg;
            sys.hostDecDeg = planets[i].hostDecDeg;
            sys.distPc = planets[i].distPc;
            systems.push_back(std::move(sys));
        } else {
            systems[found->second].planetIndices.push_back(i);
        }
    }

    // Multi-planet systems are more interesting to land on by default,
    // so sort those first.
    std::sort(systems.begin(), systems.end(),
        [](const StarSystem& a, const StarSystem& b) {
            return a.planetIndices.size() > b.planetIndices.size();
        });

    return systems;
}

void Dataset::LoadStellarHosts(const std::string& path, std::vector<StarSystem>& systems) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[Dataset] No stellar_hosts.json found - companion stars won't be modeled "
                  << "(run fetch_exoplanets.py to get it; the app works fine without it).\n";
        return;
    }

    json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        std::cerr << "[Dataset] stellar_hosts.json parse error: " << e.what() << "\n";
        return;
    }

    // Group raw rows by hostname, deduplicating near-identical entries -
    // the Archive often has several published parameter sets for the
    // SAME star, and treating each as a separate "star" would fabricate
    // companions that don't exist. Two rows within a loose tolerance of
    // each other in temperature and radius are treated as the same star.
    std::unordered_map<std::string, std::vector<StarComponent>> byHost;
    for (const auto& row : root) {
        std::string host = strOr(row, "hostname", "");
        if (host.empty()) continue;

        StarComponent sc;
        sc.teffK = numOr(row, "st_teff", 0.0);
        sc.radiusSolar = numOr(row, "st_rad", 0.0);
        sc.massSolar = numOr(row, "st_mass", 0.0);
        sc.spectralType = strOr(row, "st_spectype", "");
        sc.hasData = true;

        auto& list = byHost[host];
        bool isDuplicate = false;
        for (const auto& existing : list) {
            bool teffClose = std::fabs(existing.teffK - sc.teffK) < 300.0;
            bool radClose = std::fabs(existing.radiusSolar - sc.radiusSolar) < 0.1;
            if (teffClose && radClose) { isDuplicate = true; break; }
        }
        if (!isDuplicate) list.push_back(sc);
    }

    int systemsWithRealCompanionData = 0;
    int systemsWithUnresolvedCompanions = 0;

    for (auto& sys : systems) {
        if (sys.numStars <= 1) continue;

        auto it = byHost.find(sys.hostName);
        if (it != byHost.end()) {
            // it->second[0] is presumed to correspond to the primary
            // (already known from the ps-table planet data) - any
            // additional distinct entries become companions.
            for (size_t i = 1; i < it->second.size() && (int)sys.companions.size() < sys.numStars - 1; ++i) {
                sys.companions.push_back(it->second[i]);
            }
        }

        bool hadRealCompanion = !sys.companions.empty();

        // Pad with unresolved placeholders up to the real star count, so
        // the rendered star count always matches sy_snum even when we
        // couldn't distinguish real per-component data for it.
        while ((int)sys.companions.size() < sys.numStars - 1) {
            sys.companions.push_back(StarComponent{}); // hasData = false
        }

        if (hadRealCompanion) ++systemsWithRealCompanionData;
        else ++systemsWithUnresolvedCompanions;
    }

    std::cout << "[Dataset] Multi-star systems with resolved companion data: "
              << systemsWithRealCompanionData << "; with unresolved companion(s): "
              << systemsWithUnresolvedCompanions << ".\n";
}

void Dataset::LoadDwarfPlanets(const std::string& path, std::vector<PlanetRecord>& planets) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[Dataset] No dwarf_planets.json found - Sol will only show the 8 main "
                  << "planets (run fetch_exoplanets.py to pull real dwarf-planet data from "
                  << "NASA/JPL's SBDB, a different NASA source from the Exoplanet Archive).\n";
        return;
    }

    json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        std::cerr << "[Dataset] dwarf_planets.json parse error: " << e.what() << "\n";
        return;
    }

    constexpr double kJupiterRadiusKm = 69911.0;
    constexpr double kJupiterMassKg = 1.898e27;
    constexpr double kGravitationalConstantKm3 = 6.674e-20; // km^3 kg^-1 s^-2

    int added = 0;
    for (const auto& row : root) {
        double a = numOr(row, "pl_orbsmax", -1.0);
        double per = numOr(row, "pl_orbper", -1.0);
        if (a <= 0.0 || per <= 0.0) continue; // need both to propagate a position

        PlanetRecord p;
        p.name = strOr(row, "pl_name", "Unnamed dwarf planet");
        p.hostStar = "Sun";
        p.elements.semiMajorAxisAU = a;
        p.elements.periodDays = per;
        p.elements.eccentricity = numOr(row, "pl_orbeccen", 0.0);
        p.elements.inclinationDeg = numOr(row, "pl_orbincl", 0.0);
        p.elements.inclinationMeasured = row.contains("pl_orbincl") && !row["pl_orbincl"].is_null();
        p.elements.argPeriapsisDeg = numOr(row, "pl_orblper", 0.0);
        p.elements.longAscNodeDeg = numOr(row, "long_asc_node", 0.0);

        // SBDB gives the mean anomaly at ITS OWN osculating epoch (a recent
        // Julian Date) - propagate it back to J2000 so dwarf planets share
        // the same day-0 = J2000 convention as the 8 main planets above.
        double maDeg = numOr(row, "mean_anomaly_deg", -1.0);
        double epochJd = numOr(row, "epoch_jd", 0.0);
        if (maDeg >= 0.0 && epochJd > 0.0) {
            double daysFromJ2000 = epochJd - 2451545.0;
            double m0 = maDeg - (360.0 / per) * daysFromJ2000;
            m0 = std::fmod(m0, 360.0);
            if (m0 < 0.0) m0 += 360.0;
            p.elements.meanAnomalyAtEpochDeg = m0;
        }

        double diameterKm = numOr(row, "diameter_km", 0.0);
        if (diameterKm > 0.0) {
            p.radiusJupiter = (diameterKm / 2.0) / kJupiterRadiusKm;
        }

        double gm = numOr(row, "gm", 0.0); // km^3/s^2, standard gravitational parameter
        double massKg = 0.0;
        if (gm > 0.0) {
            massKg = gm / kGravitationalConstantKm3;
            p.massJupiter = massKg / kJupiterMassKg;
        }

        if (diameterKm > 0.0 && massKg > 0.0) {
            double radiusKm = diameterKm / 2.0;
            double volumeKm3 = (4.0 / 3.0) * 3.14159265358979323846 * radiusKm * radiusKm * radiusKm;
            double massGrams = massKg * 1000.0;
            double volumeCm3 = volumeKm3 * 1.0e15; // 1 km^3 = 1e15 cm^3
            p.densityGcm3 = massGrams / volumeCm3;
        }

        // Same star as the 8 main planets - joins the "Sun" system in GroupByHost.
        p.starTeffK = 5778.0;
        p.starRadiusSolar = 1.0;
        p.starMassSolar = 1.0;
        p.starSpectralType = "G2 V";

        planets.push_back(std::move(p));
        ++added;
    }

    std::cout << "[Dataset] Added " << added << " dwarf planet(s) from NASA/JPL SBDB.\n";
}

std::vector<StarCatalogEntry> Dataset::LoadStarCatalog(const std::string& path) {
    std::vector<StarCatalogEntry> stars;

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[Dataset] No star_catalog.json found - the skybox will use its procedural "
                  << "nebula/band only, with no real starfield (run fetch_exoplanets.py to pull "
                  << "the real Hipparcos catalog via CDS's VizieR TAP service).\n";
        return stars;
    }

    json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        std::cerr << "[Dataset] star_catalog.json parse error: " << e.what() << "\n";
        return stars;
    }

    stars.reserve(root.size());
    for (const auto& row : root) {
        double ra = numOr(row, "ra_deg", -1.0);
        double dec = numOr(row, "dec_deg", -999.0);
        double vmag = numOr(row, "vmag", 999.0);
        if (ra < 0.0 || dec < -90.0 - 1e-6 || vmag > 900.0) continue; // malformed row, skip

        StarCatalogEntry s;
        s.raDeg = ra;
        s.decDeg = dec;
        s.vmag = vmag;
        s.hasBv = row.contains("bv") && !row["bv"].is_null();
        if (s.hasBv) s.bv = numOr(row, "bv", 0.6);
        s.pmRaMasYr = numOr(row, "pm_ra_mas_yr", 0.0);
        s.pmDecMasYr = numOr(row, "pm_dec_mas_yr", 0.0);
        s.parallaxMas = numOr(row, "parallax_mas", 0.0);
        stars.push_back(s);
    }

    std::cout << "[Dataset] Loaded " << stars.size() << " real stars from the Hipparcos catalog.\n";
    return stars;
}

void Dataset::LoadDoubleStars(const std::string& path, std::vector<StarSystem>& systems) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[Dataset] No double_stars.json found - companion stars keep a schematic "
                  << "separation (run fetch_exoplanets.py to pull the Washington Double Star catalog).\n";
        return;
    }
    json root;
    try { file >> root; } catch (const std::exception& e) {
        std::cerr << "[Dataset] double_stars.json parse error: " << e.what() << "\n";
        return;
    }

    struct Pair { double ra, dec, sepArcsec; };
    std::vector<Pair> pairs;
    pairs.reserve(root.size());
    for (const auto& row : root) {
        double ra = numOr(row, "ra_deg", -1.0), dec = numOr(row, "dec_deg", -999.0);
        double sep = numOr(row, "sep_arcsec", -1.0);
        if (ra < 0.0 || dec < -90.0 || sep <= 0.0) continue;
        pairs.push_back({ra, dec, sep});
    }
    // Sort by declination so each lookup only scans a thin dec band
    // instead of every WDS entry for every system.
    std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.dec < b.dec; });

    constexpr double kMatchRadiusArcsec = 10.0; // WDS positions are ~arcsec-precise at best
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    int matched = 0;

    for (auto& sys : systems) {
        if (sys.numStars <= 1 || sys.companions.empty() || sys.distPc <= 0.0) continue;
        double tol = kMatchRadiusArcsec / 3600.0;
        auto lo = std::lower_bound(pairs.begin(), pairs.end(), sys.hostDecDeg - tol,
            [](const Pair& p, double d) { return p.dec < d; });
        double bestDist = 1e9; const Pair* best = nullptr;
        for (auto it = lo; it != pairs.end() && it->dec <= sys.hostDecDeg + tol; ++it) {
            double dRa = (it->ra - sys.hostRaDeg) * std::cos(sys.hostDecDeg * kDegToRad);
            if (dRa > 180.0) dRa -= 360.0; else if (dRa < -180.0) dRa += 360.0;
            double dist = std::sqrt(dRa * dRa + (it->dec - sys.hostDecDeg) * (it->dec - sys.hostDecDeg)) * 3600.0;
            if (dist < kMatchRadiusArcsec && dist < bestDist) { bestDist = dist; best = &*it; }
        }
        if (best) {
            // 1 arcsec at 1 parsec subtends exactly 1 AU (that's the parsec's
            // definition), so arcsec x pc = projected AU. Applied to the first
            // companion only - WDS rows describe one specific pair each, and
            // this simple positional match can't tell which component of a
            // triple-plus system a given row refers to.
            sys.companions[0].projectedSepAU = best->sepArcsec * sys.distPc;
            ++matched;
        }
    }
    std::cout << "[Dataset] Matched " << matched
              << " multi-star system(s) to real WDS separations (projected, i.e. lower bounds).\n";
}
