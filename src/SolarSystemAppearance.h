#pragma once
#include <glm/glm.hpp>
#include <string>

// Unlike PlanetClassifier (which estimates unknown exoplanets from
// indirect data), this describes what our own Solar System's planets
// actually, visibly look like - real, well-photographed facts, not
// estimates. It only covers the 8 named planets below; anything else
// returns isKnown=false and the generic procedural classifier shader
// is used instead.
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
    float ringInnerRadius = 1.4f; // relative to the planet's own display radius
    float ringOuterRadius = 2.3f;
    float ringOpacity = 0.6f;

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
