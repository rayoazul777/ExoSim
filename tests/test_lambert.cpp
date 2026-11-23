// LAM group: Lambert solver, state <-> elements, porkchop optimization, and
// Lambert mission planning against real mission dates.
#include "Harness.h"

#include "Lambert.h"
#include "MissionPlanner.h"
#include "Porkchop.h"
#include "GravityAssist.h"
#include "BodyConstants.h"

#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;

void testSolver() {
    // Curtis, "Orbital Mechanics for Engineering Students", Example 5.2
    // (geocentric): r1, r2 in km, 1 hour apart.
    const double mu = 398600.0;
    Lambert::Solution s = Lambert::Solve({5000, 10000, 2100}, {-14600, 2500, 7000}, 3600.0, mu);
    double e1 = glm::length(s.v1 - glm::dvec3(-5.9925, 1.9254, 3.2456));
    double e2 = glm::length(s.v2 - glm::dvec3(-3.3125, -4.1966, -0.38529));
    check("LAM-1", "Curtis Example 5.2: departure velocity v1", 0.0, s.ok ? e1 : 1e9, 1e-3, "km/s", "Curtis (2014) Ex. 5.2");
    check("LAM-2", "Curtis Example 5.2: arrival velocity v2", 0.0, s.ok ? e2 : 1e9, 1e-3, "km/s", "Curtis (2014) Ex. 5.2");

    // Analytic: a point on a Hohmann-like ellipse. From perihelion at 1 AU,
    // reach true anomaly 150 deg; Lambert must return the perihelion speed,
    // purely tangential.
    const double muS = Orbit::kMuSunAU3Day2;
    const double rp = 1.0, ra = 1.524, a = 0.5 * (rp + ra), e = (ra - rp) / (ra + rp);
    const double nu = 150.0 * kPi / 180.0;
    const double r = a * (1 - e * e) / (1 + e * std::cos(nu));
    const double E = 2.0 * std::atan(std::sqrt((1 - e) / (1 + e)) * std::tan(nu / 2.0));
    const double t = (E - e * std::sin(E)) * std::sqrt(a * a * a / muS);
    Lambert::Solution h = Lambert::Solve({rp, 0, 0}, {r * std::cos(nu), r * std::sin(nu), 0}, t, muS);
    const double vPeri = std::sqrt(muS * (2.0 / rp - 1.0 / a));
    check("LAM-3", "Analytic ellipse: perihelion speed recovered", vPeri * Orbit::kAuPerDayToKms,
          glm::length(h.v1) * Orbit::kAuPerDayToKms, 1e-6, "km/s", "Vis-viva");
    check("LAM-4", "Analytic ellipse: departure is tangential (radial component)", 0.0, std::fabs(h.v1.x) * Orbit::kAuPerDayToKms,
          1e-6, "km/s", "Periapsis geometry");

    check("LAM-5", "Stumpff C(z) continuous through z=0", 0.5, Lambert::StumpffC(1e-7), 1e-8, "", "C(0) = 1/2");
    check("LAM-6", "Stumpff S(z) continuous through z=0", 1.0 / 6.0, Lambert::StumpffS(-1e-7), 1e-8, "", "S(0) = 1/6");
}

void testStateConversion() {
    auto sol = Dataset::DemoSolarSystem();
    KeplerOrbit mars(solPlanet(sol, "Mars").elements);
    const double day = 9800.0;
    // velocityAtTime vs a central difference of positionAtTime.
    const double h = 1e-3;
    glm::dvec3 num = (mars.positionAtTime(day + h) - mars.positionAtTime(day - h)) / (2 * h);
    check("LAM-7", "Analytic planet velocity vs numerical derivative", 0.0,
          glm::length(mars.velocityAtTime(day) - num) * Orbit::kAuPerDayToKms, 1e-4, "km/s", "Finite difference");

    // Round trip: state -> elements -> propagate one week -> compare.
    OrbitalElements el = Orbit::ElementsFromState(mars.positionAtTime(day), mars.velocityAtTime(day), Orbit::kMuSunAU3Day2);
    KeplerOrbit fit(el);
    double drift = glm::length(fit.positionAtTime(7.0) - mars.positionAtTime(day + 7.0)) * Orbit::kAuKm;
    check("LAM-8", "State->elements round trip (7-day propagation vs ephemeris)", 0.0, drift, 2000.0, "km",
          "Two-body consistency (residual = secular drift + GM rounding)");
    // Exact round trip on a pure two-body orbit (period consistent with GM):
    // Standish's mean elements fold planetary perturbations into the mean
    // motion, so their osculating elements legitimately differ slightly.
    OrbitalElements truth;
    truth.semiMajorAxisAU = 2.7;
    truth.eccentricity = 0.23;
    truth.inclinationDeg = 12.0;
    truth.longAscNodeDeg = 80.0;
    truth.argPeriapsisDeg = 73.0;
    truth.meanAnomalyAtEpochDeg = 0.0;
    truth.periodDays = 2.0 * kPi * std::sqrt(std::pow(truth.semiMajorAxisAU, 3) / Orbit::kMuSunAU3Day2);
    KeplerOrbit body(truth);
    OrbitalElements rec = Orbit::ElementsFromState(body.positionAtTime(500.0), body.velocityAtTime(500.0), Orbit::kMuSunAU3Day2);
    check("LAM-9", "State->elements: eccentricity recovered (two-body orbit)", truth.eccentricity, rec.eccentricity, 1e-10, "",
          "Exact inverse");
    check("LAM-10", "State->elements: semi-major axis, inclination, node, perihelion recovered", 0.0,
          std::fabs(rec.semiMajorAxisAU - truth.semiMajorAxisAU) + std::fabs(rec.inclinationDeg - truth.inclinationDeg) +
              std::fabs(rec.longAscNodeDeg - truth.longAscNodeDeg) + std::fabs(rec.argPeriapsisDeg - truth.argPeriapsisDeg),
          1e-8, "sum |error|", "Exact inverse");
}

void testPlanning() {
    auto sol = Dataset::DemoSolarSystem();
    const auto& earth = solPlanet(sol, "Earth").elements;
    const auto& mars = solPlanet(sol, "Mars").elements;

    // Mars 2020 (Perseverance): launched 2020-07-30, landed 2021-02-18
    // (203 days). Plan from 2020-05-01 (day 7425.5).
    MissionPlan m20 = MissionPlanner::PlanOptimal(earth, mars, 7425.5, "Mars");
    check("LAM-11", "2020 Mars window: optimal launch date (got " + MissionPlanner::DayToCalendar(m20.launchDay) + ")", 7515.5,
          m20.launchDay, 45.0, "days since J2000", "Perseverance launch 2020-07-30 (NASA)");
    check("LAM-12", "2020 Mars window: time of flight", 203.0, m20.transferDays, 60.0, "days",
          "Perseverance cruise 203 days (NASA)");
    check("LAM-13", "2020 Mars window: launch energy C3 (plausibility)", 14.0, m20.c3, 6.0, "km^2/s^2",
          "Mars 2020 launch period C3 ~ 8-20");
    check("LAM-14", "Lambert transfer is 3-D (inclined to the ecliptic)", 1.0, m20.inclinationDeg > 0.05 ? 1.0 : 0.0, 0.0, "bool",
          "Mars orbit inclination 1.85 deg");

    // End to end: the plan's transfer orbit, propagated from launch, reaches the target.
    KeplerOrbit transfer(m20.transferElements), marsOrbit(mars);
    double miss =
        glm::length(transfer.positionAtTime(m20.transferDays) - marsOrbit.positionAtTime(m20.arrivalDay)) * Orbit::kAuKm;
    check("LAM-15", "Planned transfer arrives at Mars (propagated miss distance)", 0.0, miss, 1.0, "km",
          "Lambert boundary condition");

    // Optimal Lambert beats (or matches) the Hohmann idealization's total, both from the same date.
    MissionPlan hoh = MissionPlanner::PlanHohmann(earth, mars, 9800.0, "Mars");
    MissionPlan lam = MissionPlanner::PlanOptimal(earth, mars, 9800.0, "Mars");
    check("LAM-16", "2026 window: Lambert launch near the Hohmann window", hoh.launchDay, lam.launchDay, 45.0, "days",
          "Same synodic window");
    check("LAM-17", "2026 window: departure burn within 0.6 km/s of Hohmann", hoh.dvFromLeoKms, lam.dvFromLeoKms, 0.6, "km/s",
          "Hohmann is the coplanar circular limit");

    // Outer planet: Jupiter, full pipeline.
    MissionPlan jup = MissionPlanner::PlanOptimal(earth, solPlanet(sol, "Jupiter").elements, 9800.0, "Jupiter");
    KeplerOrbit jt(jup.transferElements), jo(solPlanet(sol, "Jupiter").elements);
    double jmiss = glm::length(jt.positionAtTime(jup.transferDays) - jo.positionAtTime(jup.arrivalDay)) * Orbit::kAuKm;
    check("LAM-18", "Jupiter Lambert transfer arrives (propagated miss distance)", 0.0, jup.valid ? jmiss : 1e12, 10.0, "km",
          "Lambert boundary condition");
    check("LAM-19", "Jupiter direct transfer C3 (plausibility)", 80.0, jup.c3, 15.0, "km^2/s^2",
          "Direct Earth-Jupiter C3 ~ 75-90 (Juno used an Earth flyby instead)");
}
// Days since J2000 (noon) for a calendar date at 00:00 UTC.
double dayOf(int y, int m, int d) {
    // Days from civil (H. Hinnant's algorithm), relative to 2000-01-01.
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long days = era * 146097L + static_cast<long>(doe) - 719468L; // since 1970-01-01
    return static_cast<double>(days - 10957) - 0.5;
}

void testGravityAssist() {
    const BodyConstants* earthB = Bodies::Lookup("Earth");
    const BodyConstants* jupB = Bodies::Lookup("Jupiter");
    check("GA-1", "Earth's sphere of influence (Laplace)", 0.925e6, GravityAssist::SoiRadiusKm(earthB->gmKm3s2, 1.00000261), 5e3,
          "km", "Textbook value ~925,000 km (Curtis Table 8.1)");
    check("GA-2", "Jupiter's sphere of influence", 48.2e6, GravityAssist::SoiRadiusKm(jupB->gmKm3s2, 5.2028870), 0.5e6, "km",
          "Textbook value 48.2 million km (Curtis Table 8.1)");

    // Flyby physics: equal speeds and a turn below the limit -> ballistic (no burn).
    glm::dvec3 vin(8.0, 0, 0), vout(8.0 * std::cos(0.5), 8.0 * std::sin(0.5), 0);
    FlybyEncounter b = GravityAssist::EvaluateFlyby("Jupiter", vin, vout);
    check("GA-3", "Ballistic flyby (|v_in| = |v_out|, turn < max) needs no burn", 0.0, b.poweredDvKms, 1e-6, "km/s",
          "Patched conic: a flyby conserves |v_inf|");
    double e = 1.0 + (jupB->radiusKm + b.periapsisAltitudeKm) * 64.0 / jupB->gmKm3s2;
    check("GA-4", "Flyby periapsis gives exactly the required turn: 2 asin(1/e)", 0.5, 2.0 * std::asin(1.0 / e), 1e-9, "rad",
          "Hyperbola asymptote geometry");
    FlybyEncounter hard = GravityAssist::EvaluateFlyby("Mars", vin, -vin * 0.999 + glm::dvec3(0, 0.3, 0));
    check("GA-5", "Turn beyond what Mars can provide is charged as a burn", 1.0, hard.poweredDvKms > 1.0 ? 1.0 : 0.0, 0.0, "bool",
          "Turn-deficit model");

    // Universal propagation: matches Kepler for an ellipse; conserves energy on a hyperbola.
    OrbitalElements el;
    el.semiMajorAxisAU = 2.0;
    el.eccentricity = 0.4;
    el.inclinationDeg = 5.0;
    el.argPeriapsisDeg = 30.0;
    el.longAscNodeDeg = 60.0;
    el.periodDays = 2.0 * kPi * std::sqrt(8.0 / Orbit::kMuSunAU3Day2);
    KeplerOrbit k(el);
    glm::dvec3 rU = Orbit::Propagate(k.positionAtTime(0.0), k.velocityAtTime(0.0), 400.0, Orbit::kMuSunAU3Day2);
    check("GA-6", "Universal-variable propagation vs Kepler (ellipse, 400 days)", 0.0,
          glm::length(rU - k.positionAtTime(400.0)) * Orbit::kAuKm, 1e-3, "km", "Two-body identity");
    glm::dvec3 r0(1.0, 0, 0), v0(0, 0.03, 0.002), v1;
    glm::dvec3 r1 = Orbit::Propagate(r0, v0, 900.0, Orbit::kMuSunAU3Day2, &v1);
    auto energy = [](const glm::dvec3& r, const glm::dvec3& v) {
        return glm::dot(v, v) / 2 - Orbit::kMuSunAU3Day2 / glm::length(r);
    };
    check("GA-7", "Hyperbolic propagation conserves energy (relative)", 0.0, std::fabs(energy(r1, v1) / energy(r0, v0) - 1.0),
          1e-9, "", "Two-body invariant");

    auto sol = Dataset::DemoSolarSystem();
    auto elems = [&](std::initializer_list<const char*> names) {
        std::vector<OrbitalElements> v;
        for (const char* n : names) v.push_back(solPlanet(sol, n).elements);
        return v;
    };
    // Voyager: Earth-Jupiter-Saturn found from 1977-01-01 (searching 1.5 years).
    GravityAssist::Sequence ejs = GravityAssist::Sequences()[0];
    ejs.searchYears = 1.5;
    FlybyPlan vg = GravityAssist::Optimize(ejs, elems({"Earth", "Jupiter", "Saturn"}), dayOf(1977, 1, 1));
    check("GA-8",
          "Voyager-era EJS: launch date (got " +
              MissionPlanner::DayToCalendar(vg.encounterDays.empty() ? 0 : vg.encounterDays[0]) + ")",
          dayOf(1977, 8, 28), vg.valid ? vg.encounterDays[0] : 0.0, 75.0, "days since J2000",
          "Voyager 2 / 1 launched 1977-08-20 / 09-05 (NASA)");
    check("GA-9", "Voyager-era EJS: Jupiter flyby date", dayOf(1979, 5, 7),
          vg.valid && vg.flybys.size() == 1 ? vg.flybys[0].day : 0.0, 180.0, "days since J2000",
          "Voyager 1 / 2 Jupiter flybys 1979-03-05 / 07-09");
    check("GA-10", "Gravity assist reaches Saturn faster than a direct Hohmann transfer", 1.0,
          vg.valid && vg.totalDays < 0.75 * vg.directHohmannDays ? 1.0 : 0.0, 0.0, "bool", "Voyager: ~3.2-4 yr vs ~6 yr Hohmann");
    check("GA-11", "Voyager-era EJS: Jupiter flyby is (nearly) ballistic", 0.0, vg.valid ? vg.totalPoweredDvKms : 99.0, 0.5,
          "km/s", "Voyager made no large burn at Jupiter");
    check("GA-12", "Voyager-era EJS: launch energy C3 (plausibility)", 105.0, vg.c3, 25.0, "km^2/s^2",
          "Voyager C3 ~100-110 (Titan IIIE-Centaur)");
    // New Horizons: Earth-Jupiter-Pluto from 2005-07-01.
    GravityAssist::Sequence ejp = GravityAssist::Sequences()[3];
    ejp.searchYears = 1.5;
    // Pluto isn't in the built-in Sol list (the app loads it from JPL SBDB);
    // use Standish Table 1's J2000 elements.
    OrbitalElements pluto;
    pluto.semiMajorAxisAU = 39.48211675;
    pluto.eccentricity = 0.24882730;
    pluto.inclinationDeg = 17.14001206;
    pluto.longAscNodeDeg = 110.30393684;
    pluto.argPeriapsisDeg = 113.76497945;
    pluto.meanAnomalyAtEpochDeg = 14.86012204;
    pluto.periodDays = 2.0 * kPi * std::sqrt(std::pow(pluto.semiMajorAxisAU, 3) / Orbit::kMuSunAU3Day2);
    std::vector<OrbitalElements> ejpBodies = elems({"Earth", "Jupiter"});
    ejpBodies.push_back(pluto);
    FlybyPlan nh = GravityAssist::Optimize(ejp, ejpBodies, dayOf(2005, 7, 1));
    check("GA-13", "New Horizons-era EJP: Jupiter flyby date", dayOf(2007, 2, 28),
          nh.valid && nh.flybys.size() == 1 ? nh.flybys[0].day : 0.0, 200.0, "days since J2000",
          "New Horizons at Jupiter 2007-02-28");
    check("GA-14", "New Horizons-era EJP: launch date", dayOf(2006, 1, 19), nh.valid ? nh.encounterDays[0] : 0.0, 150.0,
          "days since J2000", "New Horizons launched 2006-01-19");
    check("GA-15", "New Horizons-era EJP: launch energy C3 (plausibility)", 158.0, nh.c3, 35.0, "km^2/s^2",
          "New Horizons C3 157.7 (record at the time)");
}
} // namespace

void testLambert() {
    testSolver();
    testStateConversion();
    testPlanning();
    testGravityAssist();
}
