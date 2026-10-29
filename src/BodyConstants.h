#pragma once
#include <string>

// Physical constants for Solar System bodies a mission can arrive at.
//
// GM and mean radius: NASA/JPL planetary & satellite physical parameters
// (ssd.jpl.nasa.gov) and the NSSDCA planetary fact sheets. Atmospheres are
// a single-scale-height exponential fit, rho(h) = rho0 * exp(-h / H), using
// the fact-sheet surface density and scale height - a deliberately simple
// model: real atmospheres vary with temperature, latitude and season, and
// Venus/Titan in particular deviate from one exponential. Good enough for
// a representative entry profile; not a flight-grade atmosphere model.
// Venus and Titan use tabulated profiles instead (see BodyConstants.cpp).
struct BodyConstants {
    const char* name = "";
    double gmKm3s2 = 0.0;          // gravitational parameter
    double radiusKm = 0.0;         // mean radius
    bool hasSolidSurface = true;   // false for the giant planets
    double surfaceDensityKgM3 = 0.0; // 0 = airless (exosphere ignored)
    double scaleHeightKm = 0.0;

    bool hasAtmosphere() const { return surfaceDensityKgM3 > 0.0; }
    double surfaceGravityMs2() const { return gmKm3s2 / (radiusKm * radiusKm) * 1000.0; }
    double densityAt(double altitudeM) const;   // kg/m^3
};

namespace Bodies {
    // nullptr when the body isn't in the table (e.g. an exoplanet).
    const BodyConstants* Lookup(const std::string& name);
}
