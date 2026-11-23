#pragma once
#include <cmath>
#include <glm/glm.hpp>

// Mapping from physical positions (heliocentric ecliptic J2000, AU) to the
// renderer's world (y-up) and display scale. Pure and header-only.
namespace Display {

// Real semi-major axes range from a fraction of an AU to hundreds of AU
// across different systems. Compressing large distances with a log curve
// keeps compact systems from vanishing into a single pixel next to
// sprawling ones, while preserving each planet's *angle* (direction from
// the star) exactly - only the radius is remapped.
inline double Magnitude(double auMagnitude, bool logScale) {
    if (!logScale) return auMagnitude;
    return std::log10(1.0 + auMagnitude) * 3.0;
}

// KeplerOrbit works in the ecliptic frame (orbital plane = x-y, north = +z).
// The renderer's world is y-up (x-z horizontal). One rotation, used everywhere.
inline glm::dvec3 EclipticToWorld(const glm::dvec3& p) { return glm::dvec3(p.x, p.z, -p.y); }

inline glm::vec3 ToDisplay(const glm::dvec3& posEcliptic, bool logScale) {
    glm::dvec3 posAU = EclipticToWorld(posEcliptic);
    double r = glm::length(posAU);
    if (r < 1e-9) return glm::vec3(0.0f);
    double displayR = Magnitude(r, logScale);
    glm::dvec3 dir = posAU / r;
    return glm::vec3(dir * displayR);
}

// Equatorial (ICRS) RA/Dec -> unit direction in the y-up world, via the
// ecliptic (J2000 obliquity 23.4392911 deg), so stars share the planets'
// frame. (Without the rotation the starfield sat tilted ~23 deg.)
inline glm::dvec3 EquatorialToEcliptic(const glm::dvec3& eq) {
    const double eps = glm::radians(23.4392911);
    return glm::dvec3(eq.x, eq.y * std::cos(eps) + eq.z * std::sin(eps), -eq.y * std::sin(eps) + eq.z * std::cos(eps));
}

inline glm::vec3 RaDecToDirection(double raDeg, double decDeg) {
    double ra = glm::radians(raDeg), dec = glm::radians(decDeg);
    glm::dvec3 eq(std::cos(dec) * std::cos(ra), std::cos(dec) * std::sin(ra), std::sin(dec));
    return glm::normalize(glm::vec3(EclipticToWorld(EquatorialToEcliptic(eq))));
}

} // namespace Display
