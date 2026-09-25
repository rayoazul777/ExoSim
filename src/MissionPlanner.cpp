#include "MissionPlanner.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kMuSun = 1.32712440018e11;   // km^3/s^2
    constexpr double kMuEarth = 398600.4418;      // km^3/s^2
    constexpr double kAuKm = 1.495978707e8;
    constexpr double kLeoRadiusKm = 6378.137 + 200.0;
    constexpr double kMoonDistanceKm = 384400.0;

    double wrapPi(double x) {
        x = std::fmod(x + kPi, 2.0 * kPi);
        if (x < 0.0) x += 2.0 * kPi;
        return x - kPi;
    }

    double angleXY(const glm::dvec3& p) { return std::atan2(p.y, p.x); }
    double radiusXY(const glm::dvec3& p) { return std::sqrt(p.x * p.x + p.y * p.y); }

    // Half the period of an ellipse with semi-major axis (r1+r2)/2, in days.
    double hohmannDays(double r1AU, double r2AU) {
        double aKm = 0.5 * (r1AU + r2AU) * kAuKm;
        return kPi * std::sqrt(aKm * aKm * aKm / kMuSun) / 86400.0;
    }

    struct WindowEval { double error; double transferDays; double r1, r2; double earthAngle; };

    // Phase error for launching on `day`: how far the target will be from
    // the transfer's arrival point (180 deg from Earth's launch position)
    // when the spacecraft gets there. A launch window is where this is 0.
    // r2 is refined iteratively from the target's actual position at
    // arrival, so eccentric orbits (Mars, Mercury) are handled properly.
    WindowEval evaluate(const KeplerOrbit& earth, const KeplerOrbit& target, double targetA, double day) {
        glm::dvec3 e = earth.positionAtTime(day);
        double r1 = radiusXY(e);
        double r2 = targetA;
        double T = hohmannDays(r1, r2);
        for (int i = 0; i < 4; ++i) {
            r2 = radiusXY(target.positionAtTime(day + T));
            T = hohmannDays(r1, r2);
        }
        glm::dvec3 t = target.positionAtTime(day + T);
        double err = wrapPi(angleXY(t) - angleXY(e) - kPi);
        return {err, T, r1, r2, angleXY(e)};
    }
}

MissionPlan MissionPlanner::PlanHohmann(const OrbitalElements& earthEl, const OrbitalElements& targetEl,
                                        double fromDay, const std::string& targetName) {
    MissionPlan plan;
    plan.targetName = targetName;
    plan.arrivalBodyName = targetName;

    KeplerOrbit earth(earthEl), target(targetEl);
    double pE = earthEl.periodDays, pT = targetEl.periodDays;
    plan.synodicPeriodDays = (std::fabs(1.0 / pE - 1.0 / pT) > 1e-12) ? 1.0 / std::fabs(1.0 / pE - 1.0 / pT) : 0.0;

    // Scan day by day for a sign change of the phase error, then bisect.
    // |error| < 1.5 rad filters out the +-pi wraparound discontinuity,
    // which is also a sign change but not a real window.
    double scanLimit = std::max(2.0 * plan.synodicPeriodDays, 800.0);
    WindowEval prev = evaluate(earth, target, targetEl.semiMajorAxisAU, fromDay);
    for (double d = fromDay + 1.0; d <= fromDay + scanLimit; d += 1.0) {
        WindowEval cur = evaluate(earth, target, targetEl.semiMajorAxisAU, d);
        bool signChange = (prev.error <= 0.0 && cur.error > 0.0) || (prev.error >= 0.0 && cur.error < 0.0);
        if (signChange && std::fabs(cur.error) < 1.5 && std::fabs(prev.error) < 1.5) {
            double lo = d - 1.0, hi = d;
            double loErr = prev.error;
            for (int it = 0; it < 30; ++it) {
                double mid = 0.5 * (lo + hi);
                double midErr = evaluate(earth, target, targetEl.semiMajorAxisAU, mid).error;
                if ((loErr <= 0.0) == (midErr <= 0.0)) { lo = mid; loErr = midErr; } else { hi = mid; }
            }
            double launch = 0.5 * (lo + hi);
            WindowEval w = evaluate(earth, target, targetEl.semiMajorAxisAU, launch);

            plan.valid = true;
            plan.launchDay = launch;
            plan.transferDays = w.transferDays;
            plan.arrivalDay = launch + w.transferDays;
            plan.r1AU = w.r1;
            plan.r2AU = w.r2;
            plan.outbound = w.r2 > w.r1;

            // Patched-conic energetics (standard textbook formulas).
            double r1 = w.r1 * kAuKm, r2 = w.r2 * kAuKm, a = 0.5 * (r1 + r2);
            double vCirc1 = std::sqrt(kMuSun / r1), vCirc2 = std::sqrt(kMuSun / r2);
            double vT1 = std::sqrt(kMuSun * (2.0 / r1 - 1.0 / a));
            double vT2 = std::sqrt(kMuSun * (2.0 / r2 - 1.0 / a));
            plan.vInfDepartureKms = std::fabs(vT1 - vCirc1);
            plan.vInfArrivalKms = std::fabs(vCirc2 - vT2);
            plan.dvFromLeoKms = std::sqrt(plan.vInfDepartureKms * plan.vInfDepartureKms + 2.0 * kMuEarth / kLeoRadiusKm)
                                - std::sqrt(kMuEarth / kLeoRadiusKm);

            // Transfer ellipse in the ecliptic: periapsis at Earth's launch
            // direction when heading outward; apoapsis there when inward.
            OrbitalElements t;
            t.semiMajorAxisAU = 0.5 * (w.r1 + w.r2);
            t.eccentricity = std::fabs(w.r2 - w.r1) / (w.r1 + w.r2);
            t.inclinationDeg = 0.0;
            t.longAscNodeDeg = 0.0;
            double depDeg = w.earthAngle * 180.0 / kPi;
            t.argPeriapsisDeg = plan.outbound ? depDeg : depDeg + 180.0;
            t.meanAnomalyAtEpochDeg = plan.outbound ? 0.0 : 180.0;
            t.periodDays = 2.0 * w.transferDays;
            plan.transferElements = t;
            return plan;
        }
        prev = cur;
    }
    return plan; // valid = false: no window found in the scan range
}

MissionPlan MissionPlanner::PlanEarthMoon(double fromDay) {
    MissionPlan plan;
    plan.valid = true;
    plan.earthMoonMission = true;
    plan.targetName = "Moon";
    plan.arrivalBodyName = "Moon";

    double r1 = kLeoRadiusKm, r2 = kMoonDistanceKm, a = 0.5 * (r1 + r2);
    plan.transferDays = kPi * std::sqrt(a * a * a / kMuEarth) / 86400.0;
    plan.launchDay = fromDay;
    plan.arrivalDay = fromDay + plan.transferDays;
    plan.dvFromLeoKms = std::sqrt(kMuEarth * (2.0 / r1 - 1.0 / a)) - std::sqrt(kMuEarth / r1); // trans-lunar injection
    plan.vInfArrivalKms = std::fabs(std::sqrt(kMuEarth * (2.0 / r2 - 1.0 / a)) - std::sqrt(kMuEarth / r2));
    plan.r1AU = r1 / kAuKm;
    plan.r2AU = r2 / kAuKm;
    return plan;
}

std::string MissionPlanner::DayToCalendar(double daysSinceJ2000) {
    // J2000 = 2000-01-01 12:00 = Unix day 10957.5. Civil-from-days
    // conversion (Howard Hinnant's public-domain algorithm).
    long long z = static_cast<long long>(std::floor(daysSinceJ2000 + 10957.5)) + 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    long long doe = z - era * 146097;
    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = yoe + era * 400;
    long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long long mp = (5 * doy + 2) / 153;
    long long d = doy - (153 * mp + 2) / 5 + 1;
    long long m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) ++y;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04lld-%02lld-%02lld", y, m, d);
    return buf;
}

double MissionPlanner::NowDaysSinceJ2000() {
    using namespace std::chrono;
    double unixSeconds = static_cast<double>(duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
    return unixSeconds / 86400.0 - 10957.5;
}
