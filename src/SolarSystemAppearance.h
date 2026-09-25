#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Unlike PlanetClassifier (which estimates unknown exoplanets from
// indirect data), this describes what our own Solar System's planets
// actually, visibly look like - real, well-photographed facts, not
// estimates. It only covers the 8 named planets below; anything else
// returns isKnown=false and the generic procedural classifier shader
// is used instead.
// One real ring (or gap-bounded ring region), radii in units of the
// planet's own equatorial radius (1.0 = cloud tops / surface).
struct RingBand {
    float innerRadius;
    float outerRadius;
    float opacity; // relative visual prominence, 0..1 - a qualitative scale, not measured optical depth
};

struct PlanetAppearance {
    bool isKnown = false;

    glm::vec3 primaryColor{0.6f, 0.6f, 0.6f};
    glm::vec3 secondaryColor{0.5f, 0.5f, 0.5f}; // band/land/secondary tone

    bool hasContinents = false; // Earth: land-vs-ocean mask instead of bands
    bool hasPolarCaps = false;  // Earth, Mars: bright poles

    bool hasGreatSpot = false;  // Jupiter's Great Red Spot, Neptune's Great Dark Spot
    glm::vec3 spotColor{0.0f, 0.0f, 0.0f};
    float spotLatitude = 0.5f;  // UV.y, 0=north pole, 1=south pole
    float spotLongitude = 0.3f; // UV.x, 0..1 wraps around

    float hazeIntensity = 0.0f; // Venus/Titan-style thick obscuring cloud

    bool hasRings = false;
    glm::vec3 ringColor{0.7f, 0.65f, 0.55f};
    // Real ring radii from NASA's planetary ring fact sheets - gaps (like
    // Saturn's Cassini Division) are simply the space BETWEEN bands, so
    // they fall at their real measured radii rather than stylized ones.
    std::vector<RingBand> ringBands;

    // Europa-style lineae: long crack features drawn as great-circle-like
    // arcs across the sphere (the real geometry type), with procedurally
    // chosen orientations (NOT a map of Europa's actual individual cracks).
    bool hasLineae = false;

    // True for the 4 giants: render cloud BANDS. Everything else known
    // (rocky planets, moons, dwarf planets) renders cratered/bump-shaded
    // TERRAIN in its real colors instead.
    bool isBanded = false;

    // Real axial tilt (obliquity), degrees. Applied as a visual rotation
    // of the whole planet+rings so e.g. Uranus's famous "rolling on its
    // side" tilt actually shows.
    float axialTiltDeg = 0.0f;
};

class SolarSystemAppearance {
public:
    // Matches by exact planet name (as used in Dataset::DemoSolarSystem).
    // Returns a default (isKnown=false) PlanetAppearance for anything else,
    // including every real exoplanet - those keep using PlanetClassifier's
    // generic procedural look, since we have no real imagery to go on there.
    static PlanetAppearance Lookup(const std::string& planetName);
};
