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
    double numOr(const json& row, const char* key, double defaultVal) {
        if (row.contains(key) && !row[key].is_null()) {
            return row[key].get<double>();
        }
        return defaultVal;
    }

    std::string strOr(const json& row, const char* key, const std::string& defaultVal) {
        if (row.contains(key) && !row[key].is_null()) {
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
        p.name = row.value("pl_name", "Unnamed");
        p.hostStar = row.value("hostname", "Unknown");
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

        planets.push_back(std::move(p));
    }

    outPlanets = std::move(planets);
    std::cout << "[Dataset] Loaded " << outPlanets.size() << " planets with usable orbital elements.\n";
    return true;
}

std::vector<PlanetRecord> Dataset::DemoSolarSystem() {
    // Real values (approximate, J2000-epoch orbital angles) so the demo
    // system is as physically detailed as the real planets we actually
    // know well - not just placeholder numbers to make the app run.
    // eqTempK follows the same "airless blackbody equilibrium" definition
    // the Archive uses for pl_eqt, so it's directly comparable to
    // exoplanet values elsewhere in the app (this is why it reads far
    // colder than each planet's real surface temperature, especially for
    // Venus, whose greenhouse effect this number deliberately excludes).
    struct Seed {
        const char* name;
        double a, e, i, per, density;
        double eqTempK, radiusJup, massJup;
        double argPeriapsisDeg, longAscNodeDeg;
    };
    static const Seed seeds[] = {
        // name        a       e      i     per       density  eqTempK  radiusJup  massJup   argPeri  longAscNode
        {"Mercury",   0.387, 0.206, 7.00,   88.0,   5.43,  440.0, 0.0349, 0.000174,  29.1,  48.3},
        {"Venus",     0.723, 0.007, 3.39,  224.7,   5.24,  227.0, 0.0865, 0.00257,   54.9,  76.7},
        {"Earth",     1.000, 0.017, 0.00,  365.25,  5.51,  255.0, 0.0892, 0.00315,  114.2,   0.0},
        {"Mars",      1.524, 0.093, 1.85,  687.0,   3.93,  210.0, 0.0474, 0.000338, 286.5,  49.6},
        {"Jupiter",   5.203, 0.049, 1.30, 4331.0,   1.33,  110.0, 1.0,    1.0,      273.9, 100.5},
        {"Saturn",    9.537, 0.056, 2.49,10747.0,   0.69,   81.0, 0.843,  0.299,    339.4, 113.7},
        {"Uranus",   19.191, 0.046, 0.77,30589.0,   1.27,   58.0, 0.365,  0.0457,    96.9,  74.0},
        {"Neptune",  30.07,  0.010, 1.77,59800.0,   1.64,   46.0, 0.354,  0.0536,   276.3, 131.8},
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
        {"Moon", 60.3, 27.32, 5.14, 0.0549, 318.15, 125.08, true},
    };
    findPlanet("Mars").moons = {
        {"Phobos", 2.76, 0.319, 1.08, 0.0151, 0.0, 0.0, true},
        {"Deimos", 6.92, 1.263, 1.79, 0.00033, 0.0, 0.0, true},
    };
    findPlanet("Jupiter").moons = {
        {"Io", 5.9, 1.77, 0.04, 0.0041, 0.0, 0.0, true},
        {"Europa", 9.4, 3.55, 0.47, 0.009, 0.0, 0.0, true},
        {"Ganymede", 15.0, 7.15, 0.20, 0.0013, 0.0, 0.0, true},
        {"Callisto", 26.3, 16.69, 0.19, 0.0074, 0.0, 0.0, true},
    };
    findPlanet("Saturn").moons = {
        {"Titan", 20.3, 15.95, 0.35, 0.0288, 0.0, 0.0, true},
        {"Rhea", 8.75, 4.52, 0.345, 0.001258, 0.0, 0.0, true},
    };
    findPlanet("Uranus").moons = {
        {"Titania", 17.6, 8.71, 0.34, 0.0011, 0.0, 0.0, true},
        {"Oberon", 23.6, 13.46, 0.058, 0.0014, 0.0, 0.0, true},
    };
    findPlanet("Neptune").moons = {
        {"Triton", 14.3, 5.877, 157.0, 0.000016, 0.0, 0.0, true},
    };

    return out;
}

std::vector<StarSystem> Dataset::GroupByHost(const std::vector<PlanetRecord>& planets) {
    std::vector<StarSystem> systems;

    // Preserve first-seen order while grouping, using a simple linear
    // lookup into `systems` by name (fine at this data scale - a few
    // thousand rows, not tens of millions).
    for (size_t i = 0; i < planets.size(); ++i) {
        const std::string& host = planets[i].hostStar;
        auto it = std::find_if(systems.begin(), systems.end(),
            [&host](const StarSystem& s) { return s.hostName == host; });
        if (it == systems.end()) {
            StarSystem sys;
            sys.hostName = host;
            sys.planetIndices = {i};
            sys.starTeffK = planets[i].starTeffK;
            sys.starRadiusSolar = planets[i].starRadiusSolar;
            sys.starMassSolar = planets[i].starMassSolar;
            sys.starSpectralType = planets[i].starSpectralType;
            sys.numStars = planets[i].numStarsInSystem;
            sys.galacticLatDeg = planets[i].galacticLatDeg;
            sys.galacticLonDeg = planets[i].galacticLonDeg;
            systems.push_back(std::move(sys));
        } else {
            it->planetIndices.push_back(i);
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
        stars.push_back(s);
    }

    std::cout << "[Dataset] Loaded " << stars.size() << " real stars from the Hipparcos catalog.\n";
    return stars;
}
