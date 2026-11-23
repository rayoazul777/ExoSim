#include "Lambert.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kRadToDeg = 180.0 / kPi;
} // namespace

namespace Lambert {

double StumpffC(double z) {
    if (z > 1e-6) return (1.0 - std::cos(std::sqrt(z))) / z;
    if (z < -1e-6) return (std::cosh(std::sqrt(-z)) - 1.0) / (-z);
    return 0.5 - z / 24.0 + z * z / 720.0;
}

double StumpffS(double z) {
    if (z > 1e-6) {
        double s = std::sqrt(z);
        return (s - std::sin(s)) / (s * s * s);
    }
    if (z < -1e-6) {
        double s = std::sqrt(-z);
        return (std::sinh(s) - s) / (s * s * s);
    }
    return 1.0 / 6.0 - z / 120.0 + z * z / 5040.0;
}

Solution Solve(const glm::dvec3& r1v, const glm::dvec3& r2v, double tof, double mu, bool prograde) {
    Solution sol;
    const double r1 = glm::length(r1v), r2 = glm::length(r2v);
    if (r1 <= 0.0 || r2 <= 0.0 || tof <= 0.0 || mu <= 0.0) return sol;

    // Transfer angle, prograde sense.
    double cosDnu = std::clamp(glm::dot(r1v, r2v) / (r1 * r2), -1.0, 1.0);
    double dnu = std::acos(cosDnu);
    double crossZ = glm::cross(r1v, r2v).z;
    if ((prograde && crossZ < 0.0) || (!prograde && crossZ >= 0.0)) dnu = 2.0 * kPi - dnu;
    sol.transferAngleRad = dnu;

    const double A = std::sin(dnu) * std::sqrt(r1 * r2 / (1.0 - cosDnu));
    if (std::fabs(A) < 1e-12 || !std::isfinite(A)) return sol; // 0 or 180 deg: transfer plane undefined

    auto yOf = [&](double z) { return r1 + r2 + A * (z * StumpffS(z) - 1.0) / std::sqrt(StumpffC(z)); };
    auto tofOf = [&](double z, double y) {
        double C = StumpffC(z), S = StumpffS(z);
        double chi = std::sqrt(y / C);
        return (chi * chi * chi * S + A * std::sqrt(y)) / std::sqrt(mu);
    };

    // Bisection on z in (zLow, (2 pi)^2): single-revolution range.
    double zLow = -4.0 * kPi * kPi, zHigh = 4.0 * kPi * kPi - 1e-9;
    double z = 0.0;
    double y = 0.0;
    int it = 0;
    for (; it < 200; ++it) {
        z = 0.5 * (zLow + zHigh);
        y = yOf(z);
        if (y < 0.0) {
            zLow = z;
            continue;
        } // y grows with z: need a larger z
        double t = tofOf(z, y);
        if (std::fabs(t - tof) <= 1e-12 * std::max(1.0, tof)) break;
        if (t < tof)
            zLow = z;
        else
            zHigh = z;
        if (zHigh - zLow < 1e-14) break;
    }
    if (y <= 0.0) return sol;

    // Lagrange coefficients.
    const double f = 1.0 - y / r1;
    const double g = A * std::sqrt(y / mu);
    const double gdot = 1.0 - y / r2;
    if (std::fabs(g) < 1e-300) return sol;
    sol.v1 = (r2v - f * r1v) / g;
    sol.v2 = (gdot * r2v - r1v) / g;
    sol.iterations = it;
    sol.ok = std::isfinite(sol.v1.x) && std::isfinite(sol.v2.x);
    return sol;
}

} // namespace Lambert

namespace Orbit {

OrbitalElements ElementsFromState(const glm::dvec3& r, const glm::dvec3& v, double mu) {
    OrbitalElements el;
    const double rm = glm::length(r);
    const double v2 = glm::dot(v, v);
    const glm::dvec3 h = glm::cross(r, v);
    const double hm = glm::length(h);
    const glm::dvec3 evec = ((v2 - mu / rm) * r - glm::dot(r, v) * v) / mu;
    const double e = glm::length(evec);
    const double energy = v2 / 2.0 - mu / rm;
    el.eccentricity = e;
    el.inclinationMeasured = true;
    if (e >= 1.0 || energy >= 0.0) {
        el.semiMajorAxisAU = -1.0;
        return el;
    }
    const double a = -mu / (2.0 * energy);
    el.semiMajorAxisAU = a;
    el.periodDays = 2.0 * kPi * std::sqrt(a * a * a / mu);

    const double incl = std::acos(std::clamp(h.z / hm, -1.0, 1.0));
    el.inclinationDeg = incl * kRadToDeg;
    const glm::dvec3 nvec(-h.y, h.x, 0.0); // k x h
    const double nm = glm::length(nvec);
    const bool equatorial = nm < 1e-11 * hm;
    const bool circular = e < 1e-11;

    double node = equatorial ? 0.0 : std::atan2(nvec.y, nvec.x);
    // Argument of periapsis, measured from the node (or the x-axis if equatorial).
    double argp = 0.0;
    if (!circular) {
        if (equatorial) {
            argp = std::atan2(evec.y, evec.x);
            if (h.z < 0.0) argp = -argp;
        } else {
            argp = std::acos(std::clamp(glm::dot(nvec, evec) / (nm * e), -1.0, 1.0));
            if (evec.z < 0.0) argp = 2.0 * kPi - argp;
        }
    }
    // True anomaly (from periapsis; from the node/x-axis when circular).
    double nu;
    if (!circular) {
        nu = std::acos(std::clamp(glm::dot(evec, r) / (e * rm), -1.0, 1.0));
        if (glm::dot(r, v) < 0.0) nu = 2.0 * kPi - nu;
    } else if (!equatorial) {
        nu = std::acos(std::clamp(glm::dot(nvec, r) / (nm * rm), -1.0, 1.0));
        if (r.z < 0.0) nu = 2.0 * kPi - nu;
    } else {
        nu = std::atan2(r.y, r.x);
        if (h.z < 0.0) nu = -nu;
    }
    const double E = 2.0 * std::atan2(std::sqrt(1.0 - e) * std::sin(nu / 2.0), std::sqrt(1.0 + e) * std::cos(nu / 2.0));
    double M = E - e * std::sin(E);

    el.longAscNodeDeg = node * kRadToDeg;
    el.argPeriapsisDeg = argp * kRadToDeg;
    el.meanAnomalyAtEpochDeg = M * kRadToDeg;
    return el;
}

glm::dvec3 Propagate(const glm::dvec3& r0v, const glm::dvec3& v0v, double dt, double mu, glm::dvec3* vOut) {
    if (dt == 0.0) {
        if (vOut) *vOut = v0v;
        return r0v;
    }
    const double r0 = glm::length(r0v);
    const double vr0 = glm::dot(r0v, v0v) / r0;
    const double sqmu = std::sqrt(mu);
    const double alpha = 2.0 / r0 - glm::dot(v0v, v0v) / mu; // 1/a
    // Universal anomaly by Newton; elliptic orbits are first reduced to
    // less than one period so Newton starts close.
    double t = dt;
    if (alpha > 1e-12) {
        const double period = 2.0 * kPi / (sqmu * std::pow(alpha, 1.5));
        t = std::fmod(dt, period);
    }
    double chi = sqmu * std::fabs(alpha) * t;
    if (alpha <= 1e-12) chi = (t >= 0 ? 1.0 : -1.0) * std::sqrt(std::fabs(2.0 * r0 * sqmu * std::fabs(t)) + 1e-30);
    for (int i = 0; i < 100; ++i) {
        const double z = alpha * chi * chi;
        const double C = Lambert::StumpffC(z), S = Lambert::StumpffS(z);
        const double F = r0 * vr0 / sqmu * chi * chi * C + (1.0 - alpha * r0) * chi * chi * chi * S + r0 * chi - sqmu * t;
        const double dF = r0 * vr0 / sqmu * chi * (1.0 - z * S) + (1.0 - alpha * r0) * chi * chi * C + r0;
        const double step = F / dF;
        chi -= step;
        if (std::fabs(step) < 1e-13 * std::max(1.0, std::fabs(chi))) break;
    }
    const double z = alpha * chi * chi;
    const double C = Lambert::StumpffC(z), S = Lambert::StumpffS(z);
    const double f = 1.0 - chi * chi / r0 * C;
    const double g = t - chi * chi * chi / sqmu * S;
    const glm::dvec3 r = f * r0v + g * v0v;
    if (vOut) {
        const double rm = glm::length(r);
        const double fdot = sqmu / (rm * r0) * (alpha * chi * chi * chi * S - chi);
        const double gdot = 1.0 - chi * chi / rm * C;
        *vOut = fdot * r0v + gdot * v0v;
    }
    return r;
}

} // namespace Orbit
