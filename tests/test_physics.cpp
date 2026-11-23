// ExoSimTests - validation suite for the non-rendering core (orbit
// propagation, mission planning, time conversion, data loading, classifiers).
//
// Every check records: what was tested, the expected value and where it
// comes from, the computed value, and the tolerance. Run with --markdown
// to print the results table used in docs/VALIDATION.md, so the documented
// numbers are always regenerated from the code rather than typed by hand.
//
// Exit code: 0 if every check passes, 1 otherwise (CTest-compatible).

#include "KeplerOrbit.h"
#include "MissionPlanner.h"
#include "Dataset.h"
#include "PlanetClassifier.h"
#include "LandingSimulator.h"
#include "MissionTimeline.h"
#include "BodyConstants.h"
#include "SimClock.h"
#include "Harness.h"
extern std::vector<std::string> g_horizonsTable;

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>

namespace {
constexpr double kPi = 3.14159265358979323846;

double lonDeg(const glm::dvec3& p) {
    double l = std::atan2(p.y, p.x) * 180.0 / kPi;
    return l < 0.0 ? l + 360.0 : l;
}
double wrap180(double d) {
    d = std::fmod(d + 180.0, 360.0);
    if (d < 0.0) d += 360.0;
    return d - 180.0;
}
// Bisection root of f on [lo, hi] (f must change sign).
double root(const std::function<double(double)>& f, double lo, double hi) {
    double flo = f(lo);
    for (int i = 0; i < 80; ++i) {
        double mid = 0.5 * (lo + hi), fm = f(mid);
        if ((flo <= 0.0) == (fm <= 0.0)) {
            lo = mid;
            flo = fm;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

// ---------------- Kepler propagation (analytic invariants) ----------------
void testKepler() {
    for (double e : {0.0, 0.1, 0.5, 0.9, 0.99}) {
        OrbitalElements el;
        el.semiMajorAxisAU = 2.0;
        el.eccentricity = e;
        el.inclinationDeg = 0.0;
        el.periodDays = 1000.0;
        el.meanAnomalyAtEpochDeg = 0.0;
        KeplerOrbit k(el);
        char tag[32];
        std::snprintf(tag, sizeof(tag), "e=%.2f", e);

        check(std::string("KEP-1 ") + tag, "Periapsis distance a(1-e) at t=0", 2.0 * (1.0 - e),
              glm::length(k.positionAtTime(0.0)), 1e-9, "AU", "Analytic (conic geometry)");
        check(std::string("KEP-2 ") + tag, "Apoapsis distance a(1+e) at t=P/2", 2.0 * (1.0 + e),
              glm::length(k.positionAtTime(500.0)), 1e-9, "AU", "Analytic (conic geometry)");
        check(std::string("KEP-3 ") + tag, "Period closure |r(P) - r(0)|", 0.0,
              glm::length(k.positionAtTime(1000.0) - k.positionAtTime(0.0)), 1e-9, "AU", "Analytic (periodicity)");

        // Kepler's 2nd law + vis-viva, from central differences. Both would
        // fail visibly if Kepler's equation were solved incorrectly anywhere
        // along the orbit (the solver itself is private, so it's tested
        // through these invariants).
        double mu = std::pow(2.0 * kPi / 1000.0, 2) * 8.0; // GM from P and a (AU^3/day^2)
        double hMin = 1e30, hMax = 0.0, visVivaWorst = 0.0;
        const double dt = 1e-4;
        for (int s = 0; s < 720; ++s) {
            double t = 1000.0 * s / 720.0;
            glm::dvec3 r = k.positionAtTime(t);
            glm::dvec3 v = (k.positionAtTime(t + dt) - k.positionAtTime(t - dt)) / (2.0 * dt);
            double h = glm::length(glm::cross(r, v));
            hMin = std::min(hMin, h);
            hMax = std::max(hMax, h);
            double vv = mu * (2.0 / glm::length(r) - 1.0 / 2.0);
            visVivaWorst = std::max(visVivaWorst, std::fabs(glm::dot(v, v) - vv) / vv);
        }
        check(std::string("KEP-4 ") + tag, "Angular momentum spread (Kepler's 2nd law), relative", 0.0, (hMax - hMin) / hMax,
              1e-5, "-", "Analytic (conservation law)");
        check(std::string("KEP-5 ") + tag, "Worst vis-viva energy error, relative", 0.0, visVivaWorst, 1e-5, "-",
              "Analytic (vis-viva equation)");
    }
    // Inclination: max |z|/r over an orbit must equal sin(i).
    OrbitalElements el;
    el.semiMajorAxisAU = 1.0;
    el.eccentricity = 0.3;
    el.inclinationDeg = 30.0;
    el.argPeriapsisDeg = 40.0;
    el.longAscNodeDeg = 75.0;
    el.periodDays = 100.0;
    KeplerOrbit k(el);
    double zMax = 0.0;
    for (int s = 0; s < 20000; ++s) {
        glm::dvec3 r = k.positionAtTime(100.0 * s / 20000.0);
        zMax = std::max(zMax, std::fabs(r.z) / glm::length(r));
    }
    check("KEP-6 i=30", "Max |z|/r equals sin(i) for an inclined orbit", 0.5, zMax, 1e-4, "-", "Analytic (rotation geometry)");
}

// ---------------- Sol ephemeris vs published astronomical events ----------------
// Sol uses JPL's approximate J2000 Keplerian elements (Standish) as fixed
// two-body orbits. These checks compare against published event times.
// Note: Standish's Earth elements describe the Earth-Moon BARYCENTER, while
// published perihelion/equinox times refer to Earth's centre - the Moon
// shifts Earth's perihelion by up to ~1-2 days, hence the looser tolerance.
void testEphemeris() {
    auto sol = Dataset::DemoSolarSystem();
    KeplerOrbit earth(solPlanet(sol, "Earth").elements);
    auto relLon = [&](const char* name) {
        KeplerOrbit other(solPlanet(sol, name).elements);
        return
            [&, other](double d) mutable { return wrap180(lonDeg(other.positionAtTime(d)) - lonDeg(earth.positionAtTime(d))); };
    };

    double eq = root([&](double d) { return wrap180(lonDeg(earth.positionAtTime(d)) - 180.0); }, 9565.0, 9585.0);
    check("EPH-1", "2026 March equinox (Earth helio. longitude = 180 deg)", 9575.115, eq, 0.5, "days since J2000",
          "Published: 2026-03-20 14:46 UTC");
    // The model's frame is the FIXED J2000 ecliptic/equinox, but published
    // equinoxes use the equinox OF DATE, which precesses at ~50.29"/yr. Over
    // 26.22 yr that's 0.3663 deg, which Earth covers in 0.3716 days - so the
    // J2000-frame crossing is expected ~0.37 d AFTER the published time.
    const double precessionDays = (50.29 * 26.22 / 3600.0) / 0.9856;
    check("EPH-1b", "2026 March equinox, corrected for precession of the equinoxes", 9575.115 + precessionDays, eq, 0.1,
          "days since J2000", "Published time + 50.29\"/yr general precession");

    double best = 1e9, bestDay = 0.0;
    for (int i = 0; i <= 4000; ++i) {
        const double d = 9480.0 + i * 0.01;
        double r = glm::length(earth.positionAtTime(d));
        if (r < best) {
            best = r;
            bestDay = d;
        }
    }
    check("EPH-2", "2026 perihelion date", 9499.219, bestDay, 2.0, "days since J2000",
          "Published: 2026-01-03 17:16 UTC (Earth centre; model is EM barycentre)");
    check("EPH-3", "Perihelion distance a(1-e)", 0.98329, best, 1e-4, "AU", "Published: 0.98329 AU (Standish a, e)");

    check("EPH-4", "Mars opposition (Jan 2025)", 9146.606, root(relLon("Mars"), 9130.0, 9165.0), 2.0, "days since J2000",
          "Published: 2025-01-16 02:32 UTC");
    check("EPH-5", "Jupiter opposition (Dec 2024)", 9107.375, root(relLon("Jupiter"), 9090.0, 9125.0), 2.0, "days since J2000",
          "Published: 2024-12-07 ~21:00 UTC");
    check("EPH-6", "Saturn opposition (Sep 2025)", 9394.708, root(relLon("Saturn"), 9375.0, 9415.0), 2.0, "days since J2000",
          "Published: 2025-09-21 ~05:00 UTC");
}

// ---------------- Mission planner vs closed-form Hohmann ----------------
void testMission() {
    const double muSun = 1.32712440018e11, muE = 398600.4418, au = 1.495978707e8, rLeo = 6578.137;
    auto hohmannDays = [&](double r1, double r2) {
        double a = 0.5 * (r1 + r2) * au;
        return kPi * std::sqrt(a * a * a / muSun) / 86400.0;
    };
    auto dvLeo = [&](double r1, double r2) {
        double a = 0.5 * (r1 + r2) * au, r = r1 * au;
        double vinf = std::fabs(std::sqrt(muSun * (2.0 / r - 1.0 / a)) - std::sqrt(muSun / r));
        return std::sqrt(vinf * vinf + 2.0 * muE / rLeo) - std::sqrt(muE / rLeo);
    };
    auto circular = [](double a, double periodDays) {
        OrbitalElements e;
        e.semiMajorAxisAU = a;
        e.eccentricity = 0.0;
        e.periodDays = periodDays;
        return e;
    };
    OrbitalElements earthC = circular(1.0, 365.25);
    struct T {
        const char* name;
        double a, period;
    };
    for (T t : {T{"Mars", 1.524, 686.98}, T{"Venus", 0.723, 224.70}, T{"Jupiter", 5.203, 4332.59}}) {
        MissionPlan p = MissionPlanner::PlanHohmann(earthC, circular(t.a, t.period), 9500.0, t.name);
        std::string n = t.name;
        check("MIS-1 " + n, "Transfer time, circular orbits", hohmannDays(1.0, t.a), p.transferDays, 1e-3, "days",
              "Closed-form Hohmann: pi*sqrt(a^3/mu)");
        check("MIS-2 " + n, "Departure dv from 200 km LEO", dvLeo(1.0, t.a), p.dvFromLeoKms, 1e-4, "km/s",
              "Closed-form patched conic");
        double syn = 1.0 / std::fabs(1.0 / 365.25 - 1.0 / t.period);
        check("MIS-3 " + n, "Synodic period", syn, p.synodicPeriodDays, 1e-6, "days", "Closed-form 1/|1/P1-1/P2|");
        // The planned trajectory must actually connect the two bodies.
        KeplerOrbit tr(p.transferElements), tg(circular(t.a, t.period));
        glm::dvec3 end = tr.positionAtTime(p.transferDays), tgt = tg.positionAtTime(p.arrivalDay);
        check("MIS-4 " + n, "Arrival miss distance (trajectory hits target)", 0.0, std::hypot(end.x - tgt.x, end.y - tgt.y), 1e-4,
              "AU", "Consistency (window solver)");
    }
    MissionPlan moon = MissionPlanner::PlanEarthMoon(0.0);
    check("MIS-5", "Earth->Moon Hohmann time", 4.98, moon.transferDays, 0.01, "days",
          "Closed-form; cf. Artemis I ~5-day outbound coast");
    check("MIS-6", "Trans-lunar injection dv from 200 km LEO", 3.13, moon.dvFromLeoKms, 0.01, "km/s",
          "Closed-form; typical TLI ~3.1 km/s");

    // Plausibility (not a precision check): with Sol's real elements, the
    // first Mars window after 2026-01-01 should fall in late 2026.
    auto sol = Dataset::DemoSolarSystem();
    MissionPlan mars =
        MissionPlanner::PlanHohmann(solPlanet(sol, "Earth").elements, solPlanet(sol, "Mars").elements, 9497.0, "Mars");
    check("MIS-7", "First Mars window after 2026-01-01 (plausibility)", 9800.0, mars.launchDay, 60.0, "days since J2000",
          "Real-world 2026 Mars window: late 2026 (approx. Nov)");
}

// ---------------- Time conversion ----------------
void testTime() {
    auto dateCheck = [](const char* id, double day, const char* expected) {
        std::string got = MissionPlanner::DayToCalendar(day);
        check(id, std::string("Calendar date for day ") + std::to_string(day) + " -> " + expected + " (got " + got + ")", 1.0,
              got == expected ? 1.0 : 0.0, 0.0, "match", "Python datetime reference");
    };
    dateCheck("TIM-1", 0.0, "2000-01-01");
    dateCheck("TIM-2", 8825.0, "2024-02-29");
    dateCheck("TIM-3", 9497.0, "2026-01-01");
    dateCheck("TIM-4", -0.6, "1999-12-31");
}

// ---------------- Data loading robustness ----------------
std::string writeTemp(const std::string& name, const std::string& content) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path) << content;
    return path.string();
}
void testData() {
    std::vector<StarSystem> sys(2);
    sys[0].numStars = 2;
    sys[0].companions.resize(1);
    sys[0].hostRaDeg = 100.0;
    sys[0].hostDecDeg = 20.0;
    sys[0].distPc = 50.0;
    sys[1].numStars = 2;
    sys[1].companions.resize(1);
    sys[1].hostRaDeg = 10.0;
    sys[1].hostDecDeg = 5.0;
    sys[1].distPc = 20.0;
    Dataset::LoadDoubleStars(
        writeTemp(
            "exosim_ds.json",
            R"([{"ra_deg":100.0,"dec_deg":20.0,"sep_arcsec":2.0},{"ra_deg":"bad"},{"dec_deg":1},{"ra_deg":250,"dec_deg":-30,"sep_arcsec":1}])"),
        sys);
    check("DAT-1", "WDS projected separation = arcsec x pc", 100.0, sys[0].companions[0].projectedSepAU, 1e-9, "AU",
          "Definition of the parsec");
    check("DAT-2", "No WDS match -> separation left unknown (0)", 0.0, sys[1].companions[0].projectedSepAU, 0.0, "AU",
          "Expected behaviour");

    std::vector<PlanetRecord> planets;
    bool ok = Dataset::LoadFromFile(writeTemp("exosim_bad.json", "{ this is not json"), planets);
    check("DAT-3", "Malformed JSON file rejected without crashing", 0.0, ok ? 1.0 : 0.0, 0.0, "loaded", "Expected behaviour");
    ok = Dataset::LoadFromFile("/nonexistent/exoplanets.json", planets);
    check("DAT-4", "Missing file rejected without crashing", 0.0, ok ? 1.0 : 0.0, 0.0, "loaded", "Expected behaviour");
    planets.clear();
    Dataset::LoadFromFile(
        writeTemp(
            "exosim_rows.json",
            R"([{"pl_name":"ok b","hostname":"H","pl_orbsmax":0.05,"pl_orbper":3.2},{"pl_name":"no period","hostname":"H","pl_orbsmax":1.0},{"pl_name":"nulls","hostname":"H","pl_orbsmax":null,"pl_orbper":null}])"),
        planets);
    check("DAT-5", "Rows missing orbital elements are skipped", 1.0, static_cast<double>(planets.size()), 0.0, "rows",
          "Expected behaviour");
}

// ---------------- Classifier sanity on known bodies ----------------
void testClassifier() {
    auto sol = Dataset::DemoSolarSystem();
    auto typeOf = [&](const char* n) { return static_cast<double>(PlanetClassifier::Classify(solPlanet(sol, n)).type); };
    const double rocky = static_cast<double>(PlanetType::Rocky), gas = static_cast<double>(PlanetType::GasGiant);
    for (const char* n : {"Mercury", "Venus", "Earth", "Mars"})
        check(std::string("CLS-1 ") + n, "Classified as rocky (from real density)", rocky, typeOf(n), 0.0, "enum",
              "Known composition");
    for (const char* n : {"Jupiter", "Saturn"})
        check(std::string("CLS-2 ") + n, "Classified as gas giant", gas, typeOf(n), 0.0, "enum", "Known composition");
}

// ---------------- Flight software: EDL, guidance, sequencing, clock ----------------
void testFlightSoftware() {
    // Entry speed = vis-viva from the approach hyperbola: sqrt(vInf^2 + 2 mu / r_EI).
    LandingResult mars = LandingSimulator::Run({"Mars", 2.65});
    const BodyConstants* mb = Bodies::Lookup("Mars");
    double rEI = mb->radiusKm * 1000.0 + mars.entryAltitudeM;
    double vEI = std::sqrt(2650.0 * 2650.0 + 2.0 * mb->gmKm3s2 * 1e9 / rEI);
    check("FSW-1", "Mars entry speed = sqrt(vInf^2 + 2mu/r) at entry interface", vEI, mars.entrySpeedMs, 1e-6, "m/s",
          "Analytic (vis-viva, hyperbolic approach)");
    check("FSW-2", "Mars EDL lands (powered descent, ZEM/ZEV guidance)", 1.0, mars.landed ? 1.0 : 0.0, 0.0, "bool",
          "Expected behaviour");
    check("FSW-3", "Mars touchdown speed", 0.75, mars.touchdownSpeedMs, 0.1, "m/s",
          "Perseverance sky-crane touchdown ~0.75 m/s (NASA/JPL)");
    double chuteAlt = -1.0;
    for (const auto& e : mars.events)
        if (e.text.rfind("Parachute deploy", 0) == 0) std::sscanf(e.text.c_str(), "Parachute deploy at %lf", &chuteAlt);
    check("FSW-4", "Mars parachute deploy altitude (plausibility)", 11.0, chuteAlt, 4.0, "km",
          "Perseverance: ~11 km (NASA/JPL); simplified lift-up entry");
    check("FSW-5", "Mars landing error vs. guidance target", 0.0, mars.missDistanceM, 10.0, "m", "Closed-loop guidance accuracy");

    LandingResult moon = LandingSimulator::Run({"Moon", 0.8});
    const BodyConstants* lb = Bodies::Lookup("Moon");
    double vCirc = std::sqrt(lb->gmKm3s2 * 1e9 / (lb->radiusKm * 1000.0 + moon.entryAltitudeM));
    check("FSW-6", "Lunar descent starts at circular orbital speed", vCirc, moon.entrySpeedMs, 1e-6, "m/s",
          "Analytic sqrt(mu/r)");
    check("FSW-7", "Lunar powered descent duration (PDI to touchdown)", 756.0, moon.durationS, 120.0, "s",
          "Apollo 11: PDI to landing 12 min 36 s");
    check("FSW-8", "Lunar touchdown speed", 0.9, moon.touchdownSpeedMs, 0.2, "m/s", "Apollo LM: ~0.9 m/s contact");
    check("FSW-9", "Lunar landing error vs. guidance target", 0.0, moon.missDistanceM, 10.0, "m",
          "Closed-loop guidance accuracy");

    LandingResult titan = LandingSimulator::Run({"Titan", 0.0});
    check("FSW-10", "Titan touchdown speed under drogue chute", 4.5, titan.touchdownSpeedMs, 0.7, "m/s",
          "Huygens 2005: ~4.5 m/s (ESA)");
    check("FSW-11", "Titan descent duration (entry to touchdown)", 2.45 * 3600.0, titan.durationS, 0.6 * 3600.0, "s",
          "Huygens: ~2 h 27 min under parachutes (ESA)");
    LandingResult venus = LandingSimulator::Run({"Venus", 2.7});
    check("FSW-12", "Venus probe lands below 8 m/s on its drag disk", 1.0,
          venus.landed && venus.touchdownSpeedMs < 8.0 ? 1.0 : 0.0, 0.0, "bool", "Venera landers: ~7-8 m/s");
    LandingResult jup = LandingSimulator::Run({"Jupiter", 5.6});
    check("FSW-13", "No landing on a body without a solid surface", 0.0, jup.valid ? 1.0 : 0.0, 0.0, "bool",
          "Expected behaviour");

    // Determinism: the same inputs must reproduce the same telemetry bit-for-bit.
    LandingResult mars2 = LandingSimulator::Run({"Mars", 2.65});
    double diffs = mars.samples.size() == mars2.samples.size() ? 0.0 : 1e9;
    for (size_t i = 0; diffs == 0.0 && i < mars.samples.size(); ++i) {
        const auto &a = mars.samples[i], &b = mars2.samples[i];
        if (a.t != b.t || a.altitudeM != b.altitudeM || a.speedMs != b.speedMs || a.propellantKg != b.propellantKg) diffs += 1.0;
    }
    check("FSW-14", "Landing simulation is deterministic (re-run, bitwise)", 0.0, diffs, 0.0, "differing samples", "ENG-4");

    // Capture burn: with vInf = 0 into a circular orbit, dv = (sqrt2 - 1) * v_circ.
    double mu = 4902.8, r = 1837.4;
    check("FSW-15", "Capture dv identity (vInf=0, circular): (sqrt2-1) v_circ", (std::sqrt(2.0) - 1.0) * std::sqrt(mu / r),
          MissionTimeline::CaptureDvKms(mu, 0.0, r, r), 1e-12, "km/s", "Analytic");
    const BodyConstants* jb = Bodies::Lookup("Jupiter");
    check("FSW-16", "Jupiter capture into 1.05 x 100 R orbit (vInf 5.6 km/s)", 0.54,
          MissionTimeline::CaptureDvKms(jb->gmKm3s2, 5.6, jb->radiusKm * 1.05, jb->radiusKm * 100.0), 0.15, "km/s",
          "Juno JOI 2016: 0.54 km/s (similar orbit)");

    check("FSW-17", "One-way light time over 1 AU", 499.004784, MissionTimeline::LightTimeSec({1, 0, 0}, {0, 0, 0}), 1e-5, "s",
          "IAU: 1 AU / c = 499.004784 s");
    std::string met = MissionTimeline::FormatMET(3.0 * 86400.0 + 4 * 3600 + 5 * 60 + 6);
    check("FSW-18", "MET formatting T+003d 04:05:06 (got " + met + ")", 1.0, met == "T+003d 04:05:06" ? 1.0 : 0.0, 0.0, "bool",
          "Format spec");

    // Clock: identical sim time whatever the frame pacing; exact stop at events.
    SimClock a, b;
    a.timeDays = b.timeDays = 0.0;
    a.warp = b.warp = 86400.0;
    for (int i = 0; i < 120; ++i) a.advance(1.0 / 120.0 + 1e-12);
    double uneven[] = {0.3, 0.05, 0.2, 0.15, 0.3 + 1e-9};
    for (double d : uneven) b.advance(d);
    check("FSW-19", "Clock: sim time independent of frame pacing", a.timeDays, b.timeDays, 1e-12, "days", "Fixed-tick design");
    SimClock c;
    c.timeDays = 0.0;
    c.warp = 86400.0 * 10.0;
    auto adv = c.advance(1.0, 0.123456);
    check("FSW-20", "Clock: lands exactly on the next event and drops to real time", 0.123456,
          adv.hitStop && c.warp == 1.0 ? c.timeDays : -1.0, 0.0, "days", "Warp-to-event behaviour");

    // Sequencing on a real Mars plan.
    auto sol = Dataset::DemoSolarSystem();
    MissionPlan plan =
        MissionPlanner::PlanHohmann(solPlanet(sol, "Earth").elements, solPlanet(sol, "Mars").elements, 9800.0, "Mars");
    MissionTimelineData tl = MissionTimeline::Build(plan, "Mars", false, "Falcon Heavy");
    bool sorted = std::is_sorted(tl.events.begin(), tl.events.end(),
                                 [](const TimelineEvent& x, const TimelineEvent& y) { return x.day < y.day; });
    check("FSW-21", "Mission timeline events are in time order", 1.0, sorted ? 1.0 : 0.0, 0.0, "bool", "Expected behaviour");
    double ph = static_cast<double>(MissionTimeline::PhaseAt(tl, plan, plan.launchDay + 0.5 * plan.transferDays));
    check("FSW-22", "Phase at mid-transfer is CRUISE", static_cast<double>(MissionPhase::Cruise), ph, 0.0, "enum",
          "Expected behaviour");
    double phEnd = static_cast<double>(MissionTimeline::PhaseAt(tl, plan, plan.arrivalDay + 1.0));
    check("FSW-23", "Phase after the landing sequence is SURFACE OPS", static_cast<double>(MissionPhase::Landed), phEnd, 0.0,
          "enum", "Expected behaviour");
    check("FSW-24", "Mars direct entry needs no capture burn", 0.0, tl.captureDvKms, 0.0, "km/s", "Perseverance: direct entry");
}
} // namespace

int main(int argc, char** argv) {
    bool markdown = argc > 1 && std::string(argv[1]) == "--markdown";
    testKepler();
    testEphemeris();
    testMission();
    testTime();
    testData();
    testClassifier();
    testFlightSoftware();
    testEngineering();
    testHorizons();
    testLambert();
    testGalaxy();

    int failed = 0;
    if (markdown) {
        std::printf("| ID | Check | Expected | Computed | Tolerance | Unit | Reference | Result |\n");
        std::printf("|---|---|---|---|---|---|---|---|\n");
    }
    for (const auto& r : g_results) {
        if (!r.pass) ++failed;
        if (markdown)
            std::printf("| %s | %s | %.8g | %.8g | %.3g | %s | %s | %s |\n", r.id.c_str(), r.what.c_str(), r.expected, r.actual,
                        r.tolerance, r.unit.c_str(), r.source.c_str(), r.pass ? "PASS" : "**FAIL**");
        else
            std::printf("[%s] %-14s %-58s expected %.6g got %.6g (tol %.3g %s)\n", r.pass ? "PASS" : "FAIL", r.id.c_str(),
                        r.what.substr(0, 58).c_str(), r.expected, r.actual, r.tolerance, r.unit.c_str());
    }
    std::printf("\n%zu checks, %d failed\n", g_results.size(), failed);
    if (argc > 1 && std::string(argv[1]) == "--horizons-table")
        for (const auto& row : g_horizonsTable) std::printf("%s\n", row.c_str());
    return failed == 0 ? 0 : 1;
}
