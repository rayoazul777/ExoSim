#include "KeplerOrbit.h"
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
double degToRad(double deg) { return deg * kPi / 180.0; }
} // namespace

KeplerOrbit::KeplerOrbit(const OrbitalElements& elements) : m_elements(elements) {}

double KeplerOrbit::solveEccentricAnomaly(double M, double e) {
    // Normalize M into [-pi, pi] for faster/more stable convergence.
    while (M > kPi) M -= 2.0 * kPi;
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
        double f = E - e * std::sin(E) - M;
        double fp = 1.0 - e * std::cos(E);
        double dE = f / fp;
        E -= dE;
        if (std::fabs(dE) < 1e-12) {
            converged = true;
            break;
        }
    }

    // Guaranteed fallback: f(E) = E - e sin E - M is monotonic on
    // [-pi, pi] with f(-pi) <= 0 <= f(pi), so bisection always converges.
    if (!converged || std::fabs(E - e * std::sin(E) - M) > 1e-9) {
        double lo = -kPi, hi = kPi;
        for (int i = 0; i < 100; ++i) {
            double mid = 0.5 * (lo + hi);
            if (mid - e * std::sin(mid) - M < 0.0)
                lo = mid;
            else
                hi = mid;
        }
        E = 0.5 * (lo + hi);
    }
    return E;
}

OrbitalElements KeplerOrbit::elementsAt(double timeDays) const {
    if (!m_elements.hasSecularRates) return m_elements;
    const OrbitalElements& e0 = m_elements;
    const double T = timeDays / 36525.0;
    OrbitalElements e = e0;
    e.semiMajorAxisAU = e0.semiMajorAxisAU + e0.rateA * T;
    e.eccentricity = e0.eccentricity + e0.rateE * T;
    e.inclinationDeg = e0.inclinationDeg + e0.rateIncl * T;
    e.longAscNodeDeg = e0.longAscNodeDeg + e0.rateNode * T;
    const double varpi = e0.longPerihelionDeg + e0.rateLongPeri * T;
    const double L = e0.meanLongitudeDeg + e0.rateMeanLong * T;
    e.argPeriapsisDeg = varpi - e.longAscNodeDeg;
    e.meanAnomalyAtEpochDeg = std::fmod(L - varpi, 360.0); // mean anomaly AT timeDays
    e.periodDays = 360.0 / (e0.rateMeanLong / 36525.0);
    return e;
}

namespace {
// Position and velocity for elements whose meanAnomalyAtEpochDeg is the
// mean anomaly M at the instant of interest.
void stateFromElements(const OrbitalElements& el, double M, glm::dvec3* pos, glm::dvec3* vel, double (*solve)(double, double)) {
    const double a = el.semiMajorAxisAU;
    const double e = el.eccentricity;
    const double E = solve(M, e);
    const double cosE = std::cos(E), sinE = std::sin(E);
    const double sq = std::sqrt(1.0 - e * e);
    const double xOrbit = a * (cosE - e);
    const double yOrbit = a * sq * sinE;

    // 3-1-3 rotation: argument of periapsis, inclination, node.
    const double omega = degToRad(el.argPeriapsisDeg);
    const double incl = degToRad(el.inclinationDeg);
    const double node = degToRad(el.longAscNodeDeg);
    const double cosO = std::cos(omega), sinO = std::sin(omega);
    const double cosI = std::cos(incl), sinI = std::sin(incl);
    const double cosN = std::cos(node), sinN = std::sin(node);
    auto rotate = [&](double xo, double yo) {
        const double xw = cosO * xo - sinO * yo;
        const double yw = sinO * xo + cosO * yo;
        const double xi = xw, yi = yw * cosI, zi = yw * sinI;
        return glm::dvec3(cosN * xi - sinN * yi, sinN * xi + cosN * yi, zi);
    };
    if (pos) *pos = rotate(xOrbit, yOrbit);
    if (vel) {
        const double periodDays = el.periodDays > 0.0 ? el.periodDays : 365.25;
        const double n = 2.0 * kPi / periodDays; // rad/day
        const double Edot = n / (1.0 - e * cosE);
        *vel = rotate(-a * sinE * Edot, a * sq * cosE * Edot);
    }
}
} // namespace

glm::dvec3 KeplerOrbit::velocityAtTime(double timeDays) const {
    glm::dvec3 v(0.0);
    if (m_elements.hasSecularRates) {
        OrbitalElements el = elementsAt(timeDays);
        stateFromElements(el, degToRad(el.meanAnomalyAtEpochDeg), nullptr, &v, &KeplerOrbit::solveEccentricAnomaly);
    } else {
        const double periodDays = (m_elements.periodDays > 0.0) ? m_elements.periodDays : 365.25;
        const double M = degToRad(m_elements.meanAnomalyAtEpochDeg) + 2.0 * kPi / periodDays * timeDays;
        stateFromElements(m_elements, M, nullptr, &v, &KeplerOrbit::solveEccentricAnomaly);
    }
    return v;
}

glm::dvec3 KeplerOrbit::positionAtTime(double timeDays) const {
    if (m_elements.hasSecularRates) {
        OrbitalElements el = elementsAt(timeDays);
        glm::dvec3 p;
        stateFromElements(el, degToRad(el.meanAnomalyAtEpochDeg), &p, nullptr, &KeplerOrbit::solveEccentricAnomaly);
        return p;
    }
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
    const double incl = degToRad(m_elements.inclinationDeg);
    const double node = degToRad(m_elements.longAscNodeDeg);

    const double cosO = std::cos(omega), sinO = std::sin(omega);
    const double cosI = std::cos(incl), sinI = std::sin(incl);
    const double cosN = std::cos(node), sinN = std::sin(node);

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
