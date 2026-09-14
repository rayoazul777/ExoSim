#pragma once
#include <glm/glm.hpp>

// Classical (Keplerian) orbital elements, as pulled from the
// NASA Exoplanet Archive `ps` table for a single planet.
struct OrbitalElements {
    double semiMajorAxisAU   = 1.0;   // pl_orbsmax
    double eccentricity      = 0.0;   // pl_orbeccen
    double inclinationDeg    = 0.0;   // pl_orbincl
    double argPeriapsisDeg   = 0.0;   // pl_orblper   (argument of periapsis)
    double longAscNodeDeg    = 0.0;   // not in archive for most planets - default 0
    double periodDays        = 365.25; // pl_orbper
    double meanAnomalyAtEpochDeg = 0.0; // usually unknown - default 0
};

// Solves Kepler's equation and propagates a single planet's
// position/velocity in AU over time. This is intentionally plain
// double-precision math with no dependency on the renderer, so it
// can be unit-tested or reused (e.g. for a headless data export).
class KeplerOrbit {
public:
    explicit KeplerOrbit(const OrbitalElements& elements);

    // timeDays: elapsed simulation time in days since epoch (t=0).
    // Returns position in AU in a right-handed, orbit-plane-referenced
    // frame (X toward periapsis direction after node/inclination rotation).
    glm::dvec3 positionAtTime(double timeDays) const;

private:
    OrbitalElements m_elements;

    // Solves M = E - e*sin(E) for E via Newton-Raphson.
    static double solveEccentricAnomaly(double meanAnomalyRad, double eccentricity);
};
