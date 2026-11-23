#pragma once
#include <glm/glm.hpp>

// Classical (Keplerian) orbital elements, as pulled from the
// NASA Exoplanet Archive `ps` table for a single planet.
struct OrbitalElements {
    double semiMajorAxisAU = 1.0; // pl_orbsmax
    double eccentricity = 0.0; // pl_orbeccen
    double inclinationDeg = 0.0; // pl_orbincl
    bool inclinationMeasured = false; // false = archive had no value, defaulted to coplanar (0 deg)
    double argPeriapsisDeg = 0.0; // pl_orblper   (argument of periapsis)
    double longAscNodeDeg = 0.0; // not in archive for most planets - default 0
    double periodDays = 365.25; // pl_orbper
    double meanAnomalyAtEpochDeg = 0.0; // usually unknown - default 0

    // Optional secular rates (per Julian century of 36525 days from the
    // epoch), as in JPL's "Keplerian Elements for Approximate Positions of
    // the Major Planets" (Standish), valid 1800-2050. With rates, the mean
    // anomaly comes from the mean longitude L(t) = L0 + Ldot*T rather than
    // from periodDays, and a, e, i, node and perihelion drift linearly.
    bool hasSecularRates = false;
    double meanLongitudeDeg = 0.0; // L0 at epoch
    double longPerihelionDeg = 0.0; // varpi0 at epoch (= node + argPeriapsis)
    double rateA = 0.0, rateE = 0.0, rateIncl = 0.0; // AU/cy, 1/cy, deg/cy
    double rateMeanLong = 0.0, rateLongPeri = 0.0, rateNode = 0.0; // deg/cy
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

    // Velocity in AU/day (analytic two-body velocity for the osculating
    // elements at that time).
    glm::dvec3 velocityAtTime(double timeDays) const;

    // Elements in effect at timeDays (applies secular rates if present).
    OrbitalElements elementsAt(double timeDays) const;

    const OrbitalElements& elements() const { return m_elements; }

private:
    OrbitalElements m_elements;

    // Solves M = E - e*sin(E) for E via Newton-Raphson.
    static double solveEccentricAnomaly(double meanAnomalyRad, double eccentricity);
};
