#include "LandingSimulator.h"
#include "BodyConstants.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kG0 = 9.80665;

struct V2 {
    double x = 0.0, y = 0.0;
    V2 operator+(const V2& o) const { return {x + o.x, y + o.y}; }
    V2 operator-(const V2& o) const { return {x - o.x, y - o.y}; }
    V2 operator*(double s) const { return {x * s, y * s}; }
    double len() const { return std::sqrt(x * x + y * y); }
    double dot(const V2& o) const { return x * o.x + y * o.y; }
};
V2 normalize(const V2& v) {
    double l = v.len();
    return l > 0.0 ? v * (1.0 / l) : V2{0.0, 1.0};
}

// Vehicle class parameters. Values are representative of the named class
// (public mission documentation), rounded; see LandingSimulator.h.
struct Profile {
    std::string name;
    bool entry = false, powered = false;
    // Entry vehicle
    double entryMassKg = 0.0, capsuleCdA = 0.0, noseRadiusM = 1.0, liftToDrag = 0.0, suttonGravesK = 1.7415e-4;
    double entryAltM = 0.0, entrySpeedMs = 0.0, gammaDeg = 0.0;
    // Parachute
    bool hasChute = false;
    double chuteDeploySpeedMs = 0.0, chuteCdA = 0.0, chuteInflateS = 2.0;
    double heatShieldMassKg = 0.0, heatShieldJettisonDelayS = 20.0, postShieldCdA = 0.0; // 0 = drag area unchanged
    double chuteJettisonAltM = -1.0, postChuteCdA = 0.0; // Venus: chute released high, drag disk below
    double drogueSwapDelayS = -1.0, drogueCdA = 0.0; // Titan: main chute swapped for a smaller drogue
    // Powered descent
    double poweredStartAltM = 0.0, backshellMassKg = 0.0, landerCdA = 0.0;
    double ispS = 0.0, propellantKg = 0.0, maxThrustN = 0.0;
    double gateAltM = 30.0, gateDescentMs = 1.5, touchdownDescentMs = 0.75;
    // Airless start state
    double startAltM = 0.0;
    // Illustrative design limits
    double gYellow = 0, gRed = 0, heatYellow = 0, heatRed = 0, tdYellow = 0, tdRed = 0;
};

Profile makeProfile(const BodyConstants& b, double vInfKms, std::string& why) {
    Profile p;
    const double mu = b.gmKm3s2 * 1e9, R = b.radiusKm * 1000.0;
    auto entryInterface = [&](double rhoTop) { return b.scaleHeightKm * 1000.0 * std::log(b.surfaceDensityKgM3 / rhoTop); };
    auto entrySpeed = [&](double alt) { return std::sqrt(vInfKms * 1000.0 * vInfKms * 1000.0 + 2.0 * mu / (R + alt)); };
    const std::string n = b.name;

    if (!b.hasSolidSurface) {
        why = n + " has no solid surface - the mission ends at orbit insertion.";
        return p;
    }

    if (n == "Mars") {
        p.name = "Mars EDL (Perseverance-class)";
        p.entry = p.powered = p.hasChute = true;
        p.entryMassKg = 3440.0;
        p.capsuleCdA = 1.68 * kPi * 2.25 * 2.25;
        p.noseRadiusM = 1.125;
        p.liftToDrag = 0.24;
        p.suttonGravesK = 1.9027e-4;
        p.entryAltM = entryInterface(1e-7);
        p.entrySpeedMs = entrySpeed(p.entryAltM);
        p.gammaDeg = -15.5;
        p.chuteDeploySpeedMs = 420.0;
        p.chuteCdA = 0.62 * kPi * 10.75 * 10.75;
        p.heatShieldMassKg = 440.0;
        p.heatShieldJettisonDelayS = 20.0;
        p.poweredStartAltM = 2100.0;
        p.backshellMassKg = 900.0;
        p.landerCdA = 1.0 * 6.0;
        p.ispS = 225.0;
        p.propellantKg = 400.0;
        p.maxThrustN = 8 * 3100.0;
        p.gateAltM = 20.0;
        p.gateDescentMs = 1.5;
        p.touchdownDescentMs = 0.75;
        p.gYellow = 15;
        p.gRed = 20;
        p.heatYellow = 180;
        p.heatRed = 240;
        p.tdYellow = 1.5;
        p.tdRed = 3.0;
    } else if (n == "Venus") {
        p.name = "Venus descent probe (Venera-class)";
        p.entry = p.hasChute = true;
        p.entryMassKg = 1560.0;
        p.capsuleCdA = 1.05 * kPi * 1.2 * 1.2;
        p.noseRadiusM = 1.2;
        p.suttonGravesK = 1.9027e-4;
        p.entryAltM = 200000.0;
        p.entrySpeedMs = entrySpeed(p.entryAltM);
        p.gammaDeg = -20.0;
        p.chuteDeploySpeedMs = 250.0;
        p.chuteCdA = 0.6 * kPi * 6.0 * 6.0;
        p.heatShieldMassKg = 900.0;
        p.heatShieldJettisonDelayS = 10.0;
        p.postShieldCdA = 0.5 * kPi; // aeroshell release leaves a ~660 kg, 2 m lander
        p.chuteJettisonAltM = 50000.0;
        p.postChuteCdA = 1.2 * kPi * 1.05 * 1.05 + 1.0; // drag-disk free fall
        p.gYellow = 300;
        p.gRed = 450;
        p.heatYellow = 4000;
        p.heatRed = 6000;
        p.tdYellow = 8.0;
        p.tdRed = 12.0;
    } else if (n == "Titan") {
        p.name = "Titan descent probe (Huygens-class)";
        p.entry = p.hasChute = true;
        p.entryMassKg = 319.0;
        p.capsuleCdA = 1.5 * kPi * 1.35 * 1.35;
        p.noseRadiusM = 1.25;
        // Huygens' measured entry state (ESA): 1270 km, ~6.0 km/s, -65 deg.
        // Used instead of an approach v-infinity: the Saturn -> Titan leg isn't modeled.
        p.entryAltM = 1270000.0;
        p.entrySpeedMs = 6000.0;
        p.gammaDeg = -65.0;
        p.chuteDeploySpeedMs = 400.0;
        p.chuteCdA = 0.6 * kPi * 4.15 * 4.15;
        p.heatShieldMassKg = 100.0;
        p.heatShieldJettisonDelayS = 30.0;
        p.postShieldCdA = 1.0 * kPi * 0.65 * 0.65;
        p.drogueSwapDelayS = 15.0 * 60.0;
        p.drogueCdA = 0.6 * kPi * 1.515 * 1.515;
        p.gYellow = 20;
        p.gRed = 25;
        p.heatYellow = 150;
        p.heatRed = 200;
        p.tdYellow = 6.0;
        p.tdRed = 10.0;
    } else if (b.hasAtmosphere()) {
        why = "No descent profile for " + n + "'s atmosphere.";
        return p;
    } else {
        // Airless body: powered descent from a low circular orbit (Apollo
        // started powered descent from ~15 km). Small bodies start at half
        // their radius so the "orbit" isn't further out than the body is wide.
        p.name = "Airless powered descent (Apollo LM-class)";
        p.powered = true;
        p.startAltM = std::min(15000.0, 0.5 * R);
        double g = b.surfaceGravityMs2();
        double vCirc = std::sqrt(mu / (R + p.startAltM));
        p.ispS = 311.0;
        double dvBudget = (vCirc + std::sqrt(2.0 * g * p.startAltM)) * 1.25 + 150.0;
        const double dryKg = 2500.0;
        p.propellantKg = dryKg * (std::exp(dvBudget / (p.ispS * kG0)) - 1.0);
        p.entryMassKg = dryKg + p.propellantKg;
        p.maxThrustN = std::max(2.5 * g, 3.0) * p.entryMassKg;
        p.gateAltM = 30.0;
        p.gateDescentMs = 1.5;
        p.touchdownDescentMs = 0.9;
        p.gYellow = 1.2;
        p.gRed = 2.0;
        p.tdYellow = 1.5;
        p.tdRed = 3.0;
    }
    return p;
}

LimitCheck limit(const std::string& name, const std::string& unit, double v, double y, double r, bool higherIsWorse = true) {
    LimitCheck c{name, unit, v, y, r, higherIsWorse, LimitStatus::Green};
    bool red = higherIsWorse ? v >= r : v <= r;
    bool yel = higherIsWorse ? v >= y : v <= y;
    c.status = red ? LimitStatus::Red : yel ? LimitStatus::Yellow : LimitStatus::Green;
    return c;
}
} // namespace

const char* DescentPhaseName(DescentPhase p) {
    switch (p) {
        case DescentPhase::Entry: return "ENTRY";
        case DescentPhase::Parachute: return "PARACHUTE DESCENT";
        case DescentPhase::PoweredDescent: return "POWERED DESCENT";
        case DescentPhase::TerminalDescent: return "TERMINAL DESCENT";
        case DescentPhase::Landed: return "TOUCHDOWN";
        case DescentPhase::Crashed: return "SURFACE IMPACT";
        case DescentPhase::Deorbit: return "DEORBIT";
    }
    return "?";
}

LandingResult LandingSimulator::Run(const LandingConfig& cfg) {
    LandingResult res;
    res.bodyName = cfg.bodyName;
    const BodyConstants* body = Bodies::Lookup(cfg.bodyName);
    if (!body) {
        res.message = "No physical data for " + cfg.bodyName + " - landing not simulated.";
        return res;
    }

    Profile pf = makeProfile(*body, cfg.approachVinfKms, res.message);
    if (pf.name.empty()) return res;

    const double mu = body->gmKm3s2 * 1e9;
    const double R = body->radiusKm * 1000.0;
    res.bodyRadiusM = R;
    res.profileName = pf.name;
    res.atmosphericEntry = pf.entry;
    res.powered = pf.powered;

    // --- Initial state (planet-centred plane; downrange angle measured from +y) ---
    auto radial = [](const V2& r) { return normalize(r); };
    auto tangential = [](const V2& r) {
        V2 u = normalize(r);
        return V2{u.y, -u.x};
    }; // direction of increasing angle

    V2 r{0.0, R + (pf.entry ? pf.entryAltM : pf.startAltM)};
    V2 v;
    if (pf.entry) {
        double gam = pf.gammaDeg * kPi / 180.0;
        v = tangential(r) * (pf.entrySpeedMs * std::cos(gam)) + radial(r) * (pf.entrySpeedMs * std::sin(gam));
        res.entrySpeedMs = pf.entrySpeedMs;
        res.entryAltitudeM = pf.entryAltM;
        res.flightPathAngleDeg = pf.gammaDeg;
    } else {
        v = tangential(r) * std::sqrt(mu / r.len());
        res.entrySpeedMs = v.len();
        res.entryAltitudeM = pf.startAltM;
    }

    double mass = pf.entryMassKg;
    double prop = pf.propellantKg;
    res.propellantInitialKg = prop;
    double capsuleCdA = pf.capsuleCdA;
    bool heatShield = pf.entry, chute = false;
    double chuteCdA = pf.chuteCdA, chuteDeployT = -1.0;
    bool drogueSwapped = false;
    DescentPhase phase = pf.entry ? DescentPhase::Entry : DescentPhase::PoweredDescent;

    // Guidance state
    double tgoEnd = 0.0; // absolute time at which the guidance gate is due
    double targetAngle = 0.0;
    V2 thrustCmd; // commanded thrust acceleration (world), held between guidance cycles
    double throttle = 0.0;

    const double dt = 1.0 / kPhysicsHz;
    const int guidanceEvery = static_cast<int>(kPhysicsHz / kGuidanceHz);
    double t = 0.0, nextSample = 0.0;
    long step = 0;
    const double kMaxT = 6.0 * 3600.0;

    auto log = [&](const std::string& s) { res.events.push_back({t, s}); };
    char buf[256];

    auto startPoweredGuidance = [&]() {
        V2 up = radial(r), hz = tangential(r);
        double vh = v.dot(hz), vv = v.dot(up), h = r.len() - R;
        double g = mu / r.dot(r);
        double aMax = pf.maxThrustN / mass;
        double speedTerm = v.len() / (0.6 * aMax);
        double altTerm = std::fabs(vv) > 1.0 ? 1.4 * h / std::fabs(vv) : std::sqrt(4.0 * h / g);
        double tgo = std::clamp(std::max(speedTerm, altTerm), 15.0, 900.0);
        tgoEnd = t + tgo;
        double ang = std::atan2(r.x, r.y);
        targetAngle = ang + (0.5 * vh * tgo) / R;
        res.targetAngleRad = targetAngle;
        std::snprintf(buf, sizeof(buf), "Guidance: ZEM/ZEV target set %.1f km downrange, tgo %.0f s", 0.5 * vh * tgo / 1000.0,
                      tgo);
        log(buf);
    };

    if (pf.entry) {
        std::snprintf(buf, sizeof(buf), "Entry interface: %.0f km altitude, %.2f km/s, flight-path angle %.1f deg",
                      pf.entryAltM / 1000.0, pf.entrySpeedMs / 1000.0, pf.gammaDeg);
        log(buf);
    } else {
        std::snprintf(buf, sizeof(buf), "Powered descent initiation: %.1f km altitude, %.2f km/s orbital speed",
                      pf.startAltM / 1000.0, v.len() / 1000.0);
        log(buf);
        startPoweredGuidance();
    }

    // Non-thrust accelerations (gravity, drag, lift) for the integrator.
    auto accel = [&](const V2& rr, const V2& vv, double m, double cda, const V2& thrust) {
        double rl = rr.len();
        V2 a = rr * (-mu / (rl * rl * rl));
        double rho = body->densityAt(rl - R);
        double sp = vv.len();
        if (rho > 0.0 && sp > 0.0 && cda > 0.0) {
            V2 drag = vv * (-0.5 * rho * sp * cda / m);
            a = a + drag;
            if (heatShield && pf.liftToDrag > 0.0) {
                // Full lift-up: lift perpendicular to velocity, toward local up.
                V2 perp{-vv.y, vv.x};
                if (perp.dot(rr) < 0.0) perp = perp * -1.0;
                a = a + normalize(perp) * (drag.len() * pf.liftToDrag);
            }
        }
        return a + thrust;
    };

    while (t < kMaxT) {
        V2 up = radial(r), hz = tangential(r);
        double h = r.len() - R;
        double speed = v.len();
        double vv = v.dot(up), vh = v.dot(hz);
        double rho = body->densityAt(h);

        // ---------------- Sequencer (phase transitions) ----------------
        if (phase == DescentPhase::Entry && pf.hasChute && speed < pf.chuteDeploySpeedMs && vv < 0.0 && t > 1.0) {
            chute = true;
            chuteDeployT = t;
            phase = DescentPhase::Parachute;
            std::snprintf(buf, sizeof(buf), "Parachute deploy at %.1f km altitude, %.0f m/s", h / 1000.0, speed);
            log(buf);
        }
        if (chute && heatShield && t >= chuteDeployT + pf.heatShieldJettisonDelayS) {
            heatShield = false;
            mass -= pf.heatShieldMassKg;
            if (pf.postShieldCdA > 0.0) capsuleCdA = pf.postShieldCdA;
            std::snprintf(buf, sizeof(buf), "Heat shield jettison at %.1f km", h / 1000.0);
            log(buf);
        }
        if (chute && pf.drogueSwapDelayS > 0.0 && !drogueSwapped && t >= chuteDeployT + pf.drogueSwapDelayS) {
            drogueSwapped = true;
            chuteCdA = pf.drogueCdA;
            std::snprintf(buf, sizeof(buf), "Main parachute released, drogue chute deployed at %.1f km", h / 1000.0);
            log(buf);
        }
        if (chute && pf.chuteJettisonAltM > 0.0 && h < pf.chuteJettisonAltM) {
            chute = false;
            capsuleCdA = pf.postChuteCdA;
            std::snprintf(buf, sizeof(buf), "Parachute released at %.1f km - free fall on drag disk", h / 1000.0);
            log(buf);
        }
        if (phase == DescentPhase::Parachute && pf.powered && h < pf.poweredStartAltM) {
            chute = false;
            heatShield = false;
            mass -= pf.backshellMassKg;
            capsuleCdA = pf.landerCdA;
            phase = DescentPhase::PoweredDescent;
            std::snprintf(buf, sizeof(buf), "Backshell separation, descent engines ignite at %.2f km, %.0f m/s", h / 1000.0,
                          speed);
            log(buf);
            startPoweredGuidance();
        }
        if (phase == DescentPhase::PoweredDescent && tgoEnd - t < 1.0) {
            phase = DescentPhase::TerminalDescent;
            std::snprintf(buf, sizeof(buf), "Low gate: %.0f m altitude, %.1f m/s - terminal descent", h, speed);
            log(buf);
        }

        // ---------------- Guidance & control (20 Hz, zero-order hold) ----------------
        if (step % guidanceEvery == 0) {
            V2 cmd;
            double aMax = prop > 0.0 && pf.maxThrustN > 0.0 ? pf.maxThrustN / mass : 0.0;
            if (phase == DescentPhase::PoweredDescent) {
                double tgo = std::max(tgoEnd - t, 1.0);
                V2 rT = V2{std::sin(targetAngle), std::cos(targetAngle)} * (R + pf.gateAltM);
                V2 vT = radial(rT) * (-pf.gateDescentMs);
                V2 g = up * (-mu / r.dot(r));
                V2 zem = rT - (r + v * tgo + g * (0.5 * tgo * tgo));
                V2 zev = vT - (v + g * tgo);
                cmd = zem * (6.0 / (tgo * tgo)) - zev * (2.0 / tgo);
            } else if (phase == DescentPhase::TerminalDescent) {
                double g = mu / r.dot(r);
                double aUp = g + 1.5 * (-pf.touchdownDescentMs - vv);
                cmd = up * aUp + hz * (-1.0 * vh);
            }
            double mag = cmd.len();
            if (mag > aMax) cmd = aMax > 0.0 ? cmd * (aMax / mag) : V2{};
            thrustCmd = cmd;
            throttle = aMax > 0.0 ? thrustCmd.len() / aMax : 0.0;
        }

        // Sensed loads, recorded before integrating.
        double cda = capsuleCdA + (chute ? chuteCdA * std::min(1.0, (t - chuteDeployT) / pf.chuteInflateS) : 0.0);
        double dragAcc = 0.5 * rho * speed * speed * cda / mass;
        double liftAcc = heatShield ? dragAcc * pf.liftToDrag : 0.0;
        double sensedG = std::sqrt(dragAcc * dragAcc + liftAcc * liftAcc) / kG0 + thrustCmd.len() / kG0;
        double q = 0.5 * rho * speed * speed;
        double heat = heatShield ? pf.suttonGravesK * std::sqrt(rho / pf.noseRadiusM) * speed * speed * speed / 1e4 : 0.0;
        res.peakG = std::max(res.peakG, sensedG);
        res.peakDynPressurePa = std::max(res.peakDynPressurePa, q);
        res.peakHeatFluxWcm2 = std::max(res.peakHeatFluxWcm2, heat);

        if (t >= nextSample) {
            LandingSample s;
            s.t = t;
            s.altitudeM = h;
            s.angleRad = std::atan2(r.x, r.y);
            s.downrangeM = s.angleRad * R;
            s.speedMs = speed;
            s.verticalMs = vv;
            s.horizontalMs = vh;
            s.accelG = sensedG;
            s.dynPressurePa = q;
            s.heatFluxWcm2 = heat;
            s.massKg = mass;
            s.propellantKg = prop;
            s.throttle = throttle;
            double tl = thrustCmd.len();
            if (tl > 0.0) {
                s.thrustDirX = thrustCmd.dot(hz) / tl;
                s.thrustDirY = thrustCmd.dot(up) / tl;
            }
            s.rangeToTargetM = pf.powered && phase != DescentPhase::Entry && phase != DescentPhase::Parachute
                                   ? std::fabs(targetAngle - s.angleRad) * R
                                   : 0.0;
            s.phase = phase;
            s.chuteDeployed = chute;
            s.heatShieldAttached = heatShield;
            res.samples.push_back(s);
            nextSample += kSampleInterval;
        }

        // ---------------- Touchdown ----------------
        if (h <= 0.0) {
            res.touchdownSpeedMs = speed;
            res.landed = speed < pf.tdRed;
            phase = res.landed ? DescentPhase::Landed : DescentPhase::Crashed;
            double ang = std::atan2(r.x, r.y);
            res.missDistanceM = pf.powered ? std::fabs(targetAngle - ang) * R : 0.0;
            std::snprintf(buf, sizeof(buf),
                          res.landed ? "TOUCHDOWN at %.2f m/s - %.0f km downrange of entry"
                                     : "SURFACE IMPACT at %.1f m/s - %.0f km downrange",
                          speed, ang * R / 1000.0);
            log(buf);
            LandingSample s = res.samples.empty() ? LandingSample{} : res.samples.back();
            s.t = t;
            s.altitudeM = 0.0;
            s.angleRad = ang;
            s.downrangeM = ang * R;
            s.speedMs = 0.0;
            s.verticalMs = s.horizontalMs = 0.0;
            s.accelG = 0.0;
            s.throttle = 0.0;
            s.dynPressurePa = 0.0;
            s.heatFluxWcm2 = 0.0;
            s.massKg = mass;
            s.propellantKg = prop;
            s.phase = phase;
            s.chuteDeployed = false;
            s.rangeToTargetM = res.missDistanceM;
            res.samples.push_back(s);
            break;
        }

        // ---------------- Integrate (RK4, thrust held constant over the step) ----------------
        auto f = [&](const V2& rr, const V2& vel) { return accel(rr, vel, mass, cda, thrustCmd); };
        V2 k1v = f(r, v), k1r = v;
        V2 k2v = f(r + k1r * (dt / 2), v + k1v * (dt / 2)), k2r = v + k1v * (dt / 2);
        V2 k3v = f(r + k2r * (dt / 2), v + k2v * (dt / 2)), k3r = v + k2v * (dt / 2);
        V2 k4v = f(r + k3r * dt, v + k3v * dt), k4r = v + k3v * dt;
        r = r + (k1r + k2r * 2.0 + k3r * 2.0 + k4r) * (dt / 6.0);
        v = v + (k1v + k2v * 2.0 + k3v * 2.0 + k4v) * (dt / 6.0);

        double thrustN = thrustCmd.len() * mass;
        if (thrustN > 0.0 && pf.ispS > 0.0) {
            double burned = std::min(prop, thrustN / (pf.ispS * kG0) * dt);
            prop -= burned;
            mass -= burned;
            if (prop <= 0.0 && burned > 0.0) log("PROPELLANT DEPLETED");
        }
        t += dt;
        ++step;
    }

    if (t >= kMaxT) {
        res.message = "Simulation timed out before touchdown.";
        return res;
    }

    res.durationS = t;
    res.propellantUsedKg = res.propellantInitialKg - prop;
    res.valid = true;

    // ---------------- Limit checks (illustrative design limits for the vehicle class) ----------------
    res.limits.push_back(limit("Peak sensed load", "g", res.peakG, pf.gYellow, pf.gRed));
    if (pf.entry)
        res.limits.push_back(limit("Peak heat flux (Sutton-Graves)", "W/cm2", res.peakHeatFluxWcm2, pf.heatYellow, pf.heatRed));
    res.limits.push_back(limit("Touchdown speed", "m/s", res.touchdownSpeedMs, pf.tdYellow, pf.tdRed));
    if (pf.powered) {
        double margin = res.propellantInitialKg > 0.0 ? 100.0 * prop / res.propellantInitialKg : 0.0;
        res.limits.push_back(limit("Propellant margin", "%", margin, 10.0, 3.0, /*higherIsWorse=*/false));
        res.limits.push_back(limit("Landing error vs. guidance target", "m", res.missDistanceM, 100.0, 1000.0));
    }
    return res;
}

LandingSample LandingSimulator::At(const LandingResult& r, double t) {
    if (r.samples.empty()) return {};
    if (t <= r.samples.front().t) return r.samples.front();
    if (t >= r.samples.back().t) return r.samples.back();
    auto it = std::upper_bound(r.samples.begin(), r.samples.end(), t, [](double tv, const LandingSample& s) { return tv < s.t; });
    const LandingSample& b = *it;
    const LandingSample& a = *(it - 1);
    double u = (b.t > a.t) ? (t - a.t) / (b.t - a.t) : 0.0;
    auto lerp = [u](double x, double y) { return x + (y - x) * u; };
    LandingSample s = a;
    s.t = t;
    s.altitudeM = lerp(a.altitudeM, b.altitudeM);
    s.downrangeM = lerp(a.downrangeM, b.downrangeM);
    s.angleRad = lerp(a.angleRad, b.angleRad);
    s.speedMs = lerp(a.speedMs, b.speedMs);
    s.verticalMs = lerp(a.verticalMs, b.verticalMs);
    s.horizontalMs = lerp(a.horizontalMs, b.horizontalMs);
    s.accelG = lerp(a.accelG, b.accelG);
    s.dynPressurePa = lerp(a.dynPressurePa, b.dynPressurePa);
    s.heatFluxWcm2 = lerp(a.heatFluxWcm2, b.heatFluxWcm2);
    s.massKg = lerp(a.massKg, b.massKg);
    s.propellantKg = lerp(a.propellantKg, b.propellantKg);
    s.throttle = lerp(a.throttle, b.throttle);
    s.rangeToTargetM = lerp(a.rangeToTargetM, b.rangeToTargetM);
    return s;
}

bool LandingSimulator::WriteCsv(const LandingResult& r, const std::string& path) {
    std::ofstream out(path);
    if (!out) return false;
    out << "# ExoSim landing telemetry - " << r.profileName << " at " << r.bodyName << "\n";
    out << "# Simulated (not flight data). Physics " << kPhysicsHz << " Hz, guidance " << kGuidanceHz << " Hz.\n";
    out << "t_s,phase,altitude_m,downrange_m,speed_ms,vertical_ms,horizontal_ms,sensed_g,dyn_pressure_pa,"
           "heat_flux_wcm2,mass_kg,propellant_kg,throttle,range_to_target_m,chute,heat_shield\n";
    char line[512];
    for (const auto& s : r.samples) {
        std::snprintf(line, sizeof(line), "%.2f,%s,%.2f,%.2f,%.3f,%.3f,%.3f,%.4f,%.2f,%.3f,%.2f,%.3f,%.4f,%.2f,%d,%d\n", s.t,
                      DescentPhaseName(s.phase), s.altitudeM, s.downrangeM, s.speedMs, s.verticalMs, s.horizontalMs, s.accelG,
                      s.dynPressurePa, s.heatFluxWcm2, s.massKg, s.propellantKg, s.throttle, s.rangeToTargetM,
                      s.chuteDeployed ? 1 : 0, s.heatShieldAttached ? 1 : 0);
        out << line;
    }
    return static_cast<bool>(out);
}
