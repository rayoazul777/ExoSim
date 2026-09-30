#include "KeplerOrbit.h"
#include <cmath>

namespace {
    constexpr double kPi = 3.14159265358979323846;
    double degToRad(double deg) { return deg * kPi / 180.0; }
}

KeplerOrbit::KeplerOrbit(const OrbitalElements& elements)
    : m_elements(elements) {}

double KeplerOrbit::solveEccentricAnomaly(double M, double e) {
    // Normalize M into [-pi, pi] for faster/more stable convergence.
    while (M > kPi)  M -= 2.0 * kPi;
    while (M < -kPi) M += 2.0 * kPi;

    // Initial guess. For high eccentricity, start at pi with the SAME SIGN
    // as M: Newton's method then converges monotonically for any e < 1.
    // (An earlier version always started at +pi, which for negative M -
    // just past apoapsis - threw Newton toward E ~ 0, where the derivative
    // 1 - e*cos(E) nearly vanishes, and returned a wrong root: positions
    // jumped by several AU on e >= 0.9 orbits. Caught by the KEP-4/KEP-5
    // conservation-law tests in tests/test_physics.cpp.)
    double E = (e < 0.8) ? M : (M >= 0.0 ? kPi : -kPi);

    bool converged = false;
    for (int i = 0; i < 50; ++i) {
        double f  = E - e * std::sin(E) - M;
        double fp = 1.0 - e * std::cos(E);
        double dE = f / fp;
        E -= dE;
        if (std::fabs(dE) < 1e-12) { converged = true; break; }
    }

    // Guaranteed fallback: f(E) = E - e sin E - M is monotonic on
    // [-pi, pi] with f(-pi) <= 0 <= f(pi), so bisection always converges.
    if (!converged || std::fabs(E - e * std::sin(E) - M) > 1e-9) {
        double lo = -kPi, hi = kPi;
        for (int i = 0; i < 100; ++i) {
            double mid = 0.5 * (lo + hi);
            if (mid - e * std::sin(mid) - M < 0.0) lo = mid; else hi = mid;
        }
        E = 0.5 * (lo + hi);
    }
    return E;
}

glm::dvec3 KeplerOrbit::positionAtTime(double timeDays) const {
    const double a = m_elements.semiMajorAxisAU;
    const double e = m_elements.eccentricity;
    const double periodDays = (m_elements.periodDays > 0.0) ? m_elements.periodDays : 365.25;

    // Mean motion (rad/day) and mean anomaly at requested time.
    const double n = 2.0 * kPi / periodDays;
    const double M0 = degToRad(m_elements.meanAnomalyAtEpochDeg);
    const double M = M0 + n * timeDays;

    // Solve for eccentric anomaly, then true anomaly + radius.
    const double E = solveEccentricAnomaly(M, e);
    const double xOrbit = a * (std::cos(E) - e);
    const double yOrbit = a * std::sqrt(1.0 - e * e) * std::sin(E);

    // Rotate from the orbital plane into the reference frame using
    // argument of periapsis (omega), inclination (i), and longitude
    // of ascending node (Omega). Standard 3-1-3 Euler rotation.
    const double omega = degToRad(m_elements.argPeriapsisDeg);
    const double incl   = degToRad(m_elements.inclinationDeg);
    const double node   = degToRad(m_elements.longAscNodeDeg);

    const double cosO = std::cos(omega), sinO = std::sin(omega);
    const double cosI = std::cos(incl),  sinI = std::sin(incl);
    const double cosN = std::cos(node),  sinN = std::sin(node);

    // Rotate by argument of periapsis in the orbital plane first.
    const double xw = cosO * xOrbit - sinO * yOrbit;
    const double yw = sinO * xOrbit + cosO * yOrbit;

    // Tilt by inclination.
    const double xi = xw;
    const double yi = yw * cosI;
    const double zi = yw * sinI;

    // Rotate by longitude of ascending node into the final frame.
    const double x = cosN * xi - sinN * yi;
    const double y = sinN * xi + cosN * yi;
    const double z = zi;

    return glm::dvec3(x, y, z);
}
