#pragma once
#include <glm/glm.hpp>
#include <string>
#include "Dataset.h"

// The NASA Exoplanet Archive does NOT include measured atmospheric
// composition or surface data for the vast majority of planets - that
// requires spectroscopy (JWST, etc.) done for only a small, specific
// set of targets. What we CAN do honestly is bucket a planet by its
// measured (or estimated) radius/mass into the broad categories the
// exoplanet science community uses (rocky / super-Earth / sub-Neptune /
// ice giant / gas giant), based on well-known radius-gap research.
// Everything here is a statistical estimate, never a measurement,
// and the UI must say so.
enum class PlanetType {
    Unknown,
    Rocky,
    SuperEarth,
    MiniNeptune,
    IceGiant,
    GasGiant
};

struct PlanetClassification {
    PlanetType type = PlanetType::Unknown;
    std::string typeLabel;            // e.g. "Gas giant"
    std::string surfaceGuess;         // one line, hedged
    std::string atmosphereGuess;      // one line, hedged
    glm::vec3 displayColor{0.6f, 0.6f, 0.6f};
    bool basedOnRadius = false;       // true = radius/density-based (more reliable), false = mass-only fallback
    bool basedOnDensity = false;      // true = the higher-confidence density-based path was used
    bool hasAnyPhysicalData = false;  // false = neither radius, density nor mass known

    // A small, hand-curated set of exoplanets DO have real published
    // atmospheric detections (JWST/Hubble spectroscopy). When the planet
    // matches one of those, this holds the real finding instead of - or
    // alongside - the statistical estimate above, and the UI must make
    // clear this one is an actual measurement, not a guess.
    bool hasRealAtmosphereFindings = false;
    std::string realAtmosphereFindings;
};

class PlanetClassifier {
public:
    static PlanetClassification Classify(const PlanetRecord& planet);
};
