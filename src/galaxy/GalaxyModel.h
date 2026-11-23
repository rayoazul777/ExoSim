#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "Dataset.h"

// The Milky Way map's data and model. Pure (no GL), unit-tested.
//
// Frame: heliocentric Galactic Cartesian, parsecs. X toward the Galactic
// Center (l = 0, b = 0), Y toward l = 90 deg (the direction of Galactic
// rotation), Z toward the North Galactic Pole. Viewed from the NGP the
// Galaxy rotates clockwise, so spiral arms trail counter-clockwise.
//
// REAL: exoplanet host positions (Archive RA/Dec + distance) and Hipparcos
// stars (parallax distances).
// MODEL (illustrative, labeled in the UI): the disk, bar, bulge and spiral
// arms - a four-arm logarithmic spiral (pitch 12.5 deg) whose arms cross the
// Sun's azimuth at the radii found by trigonometric-parallax maser surveys
// (Reid et al. 2019) - Norma-Outer, Scutum-Centaurus, Sagittarius-Carina,
// Perseus - plus the Local Arm (Orion Spur); disk scale length 2.6 kpc,
// scale height 300 pc, bar half-length 4.4 kpc at 27 deg to the Sun-center
// line (Bland-Hawthorn & Gerhard 2016). Galactic Center distance 8.178 kpc
// (GRAVITY Collaboration 2019); Sun 20.8 pc above the plane (Bennett & Bovy 2019).
namespace Galaxy {

constexpr double kSunToCenterPc = 8178.0;
constexpr double kSunHeightPc = 20.8;
constexpr double kPitchDeg = 12.5;

// IAU 1958/J2000 equatorial -> Galactic. Returns (l, b) in degrees, l in [0, 360).
glm::dvec2 EquatorialToGalactic(double raDeg, double decDeg);

// Heliocentric Galactic Cartesian (pc) from longitude, latitude and distance.
glm::dvec3 GalacticToCartesian(double lDeg, double bDeg, double distPc);

// Galactocentric radius (pc) of a heliocentric position, in the plane.
double GalactocentricRadiusPc(const glm::dvec3& helioPc);

struct Arm {
    const char* name;
    double radiusAtSunAzimuthPc; // galactocentric radius where the arm crosses the Sun-center line (Sun's side)
    bool spur = false; // Local Arm: short segment only
};
const std::vector<Arm>& Arms();

// Points along an arm's centerline (heliocentric, pc), from r = 2.5 kpc out to 17 kpc.
std::vector<glm::dvec3> ArmCenterline(const Arm& arm, int samples);

struct ModelPoint {
    glm::vec3 posPc;
    glm::vec4 rgba;
    float sizePx;
};
// Deterministic synthetic star field tracing the model (disk + arms + bar + bulge).
std::vector<ModelPoint> GenerateModel(int count, std::uint32_t seed);

// An exoplanet host star with a measured distance.
struct Host {
    std::string name;
    double raDeg = 0.0, decDeg = 0.0, distPc = 0.0;
    double lDeg = 0.0, bDeg = 0.0;
    double teffK = 0.0;
    int planets = 0;
    glm::dvec3 posPc{0.0};
    int systemIndex = -1; // index into the simulation's systems when it has orbital elements
};

// All hosts in the Archive export with a distance, one per hostname. The
// `ps` default parameter set sometimes lacks sy_dist; then the first
// distance any `stellarhosts` reference gives for that host is used (pass
// "" to skip). Returns an empty list if the planet file is missing or malformed.
struct HostLoadStats {
    int fromStellarHosts = 0; // distances filled in from stellar_hosts.json
    int withoutDistance = 0; // hosts still without any distance (not plotted)
};
std::vector<Host> LoadHosts(const std::string& exoplanetsJsonPath, const std::string& stellarHostsJsonPath = "",
                            HostLoadStats* stats = nullptr);

// Display color for a stellar effective temperature (via the Ballesteros
// B-V relation and Sky::BvToColor). Unknown (<= 0) -> Sun-like.
glm::vec3 TeffToColor(double teffK);

// Hipparcos stars with a usable parallax (> 1 mas, i.e. within 1 kpc),
// placed at 1000/parallax pc in the Galactic frame.
struct LocalStar {
    glm::dvec3 posPc{0.0};
    glm::vec3 color{1.0f};
    double vmag = 0.0;
};
std::vector<LocalStar> LocalStars(const std::vector<StarCatalogEntry>& stars);

} // namespace Galaxy
