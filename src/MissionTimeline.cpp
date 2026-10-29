#include "MissionTimeline.h"
#include "BodyConstants.h"
#include "KeplerOrbit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace {
constexpr double kAuKm = 1.495978707e8;
constexpr double kCKms = 299792.458;
constexpr double kMuSun = 1.32712440018e11;
constexpr double kPi = 3.14159265358979323846;

std::string fmt(const char* f, double a, double b = 0.0) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), f, a, b);
    return buf;
}
}

const char* MissionPhaseName(MissionPhase p) {
    switch (p) {
        case MissionPhase::Prelaunch: return "PRELAUNCH";
        case MissionPhase::Launch: return "LAUNCH & INJECTION";
        case MissionPhase::Cruise: return "CRUISE";
        case MissionPhase::Approach: return "APPROACH";
        case MissionPhase::Arrival: return "ARRIVAL OPS";
        case MissionPhase::Descent: return "ENTRY / DESCENT / LANDING";
        case MissionPhase::Landed: return "SURFACE OPS";
        case MissionPhase::InOrbit: return "ORBITAL OPS";
        case MissionPhase::Lost: return "LOSS OF SIGNAL";
    }
    return "?";
}

double MissionTimeline::CaptureDvKms(double mu, double vInf, double rp, double ra) {
    double vPeriHyper = std::sqrt(vInf * vInf + 2.0 * mu / rp);
    double a = 0.5 * (rp + ra);
    double vPeriOrbit = std::sqrt(mu * (2.0 / rp - 1.0 / a));
    return vPeriHyper - vPeriOrbit;
}

MissionTimelineData MissionTimeline::Build(const MissionPlan& plan, const std::string& landingBody,
                                           bool arrivalIsParentSystem, const std::string& vehicleName) {
    MissionTimelineData tl;
    if (!plan.valid) return tl;
    tl.valid = true;
    tl.landingBody = landingBody;
    const double L = plan.launchDay, A = plan.arrivalDay;
    auto add = [&](double day, const std::string& code, const std::string& text, bool landing = false) {
        tl.events.push_back({day, code, text, landing});
    };

    add(L, "LAUNCH", "Liftoff - " + vehicleName);
    const double tenMin = 10.0 / 1440.0;
    if (plan.earthMoonMission)
        add(L + tenMin, "TLI", fmt("Trans-lunar injection burn complete: %.2f km/s from 200 km LEO", plan.dvFromLeoKms));
    else
        add(L + tenMin, "TMI", fmt("Interplanetary injection burn complete: %.2f km/s from 200 km LEO (C3 %.1f km2/s2)",
                                   plan.dvFromLeoKms, plan.vInfDepartureKms * plan.vInfDepartureKms));
    if (plan.transferDays > 40.0) {
        add(L + 15.0, "TCM-1", "Trajectory correction slot 1 (nominal trajectory: 0 m/s required)");
        add(A - 10.0, "TCM-F", "Final trajectory correction slot (nominal: 0 m/s)");
    }
    double approachLead = plan.earthMoonMission ? 0.5 : std::min(45.0, plan.transferDays * 0.15);
    add(A - approachLead, "APPR", "Approach phase: optical navigation, science instruments on");

    const BodyConstants* body = Bodies::Lookup(landingBody);
    const BodyConstants* arrivalBody = Bodies::Lookup(plan.earthMoonMission ? "Moon" : plan.arrivalBodyName);

    // ---- Arrival: direct entry, orbit insertion, or (moon targets) not modeled ----
    double landingStart = A;
    LandingConfig lc{landingBody, plan.vInfArrivalKms};
    if (arrivalIsParentSystem) {
        tl.captureModeled = false;
        tl.arrivalNote = "Capture at " + plan.arrivalBodyName + " and the transfer to " + landingBody +
                         " are not modeled (they need multi-body / tour design).";
        add(A, "ARR", "Arrival in the " + plan.arrivalBodyName + " system");
        landingStart = A + 1.0;
        if (body) add(landingStart - 0.01, "DOI", landingBody + " deorbit (moon tour not modeled: +1 day placeholder)");
    } else if (arrivalBody && !arrivalBody->hasSolidSurface) {
        double rp = arrivalBody->radiusKm * 1.05, ra = arrivalBody->radiusKm * 100.0;
        tl.captureDvKms = CaptureDvKms(arrivalBody->gmKm3s2, plan.vInfArrivalKms, rp, ra);
        tl.arrivalNote = fmt("Capture into a 1.05 x 100 planet-radius orbit (Juno-style; Juno's real JOI was 0.54 km/s).", 0);
        add(A, "OI", fmt("Orbit insertion burn: %.2f km/s - capture into elliptical orbit", tl.captureDvKms));
    } else if (body && body->hasAtmosphere() && landingBody != "Titan") {
        tl.captureDvKms = 0.0;
        tl.arrivalNote = "Direct atmospheric entry from the approach hyperbola - the aeroshell, not a burn, removes the speed.";
    } else if (arrivalBody) {
        double r = arrivalBody->radiusKm + 100.0;
        tl.captureDvKms = CaptureDvKms(arrivalBody->gmKm3s2, plan.vInfArrivalKms, r, r);
        tl.arrivalNote = fmt("Insertion into a 100 km circular orbit (Apollo lunar orbit insertion was ~0.9 km/s).", 0);
        add(A, "OI", fmt("Orbit insertion burn: %.2f km/s into 100 km circular orbit", tl.captureDvKms));
        double period = 2.0 * kPi * std::sqrt(r * r * r / arrivalBody->gmKm3s2) / 86400.0;
        landingStart = A + period;
        add(landingStart - 0.002, "DOI", "Deorbit burn after one orbit (real missions spend days in orbit first)");
    }

    // ---- Landing sequence ----
    if (!body) {
        tl.landingNote = "No physical data for " + landingBody + " - landing not simulated.";
    } else {
        tl.landing = LandingSimulator::Run(lc);
        if (tl.landing.valid) {
            tl.hasLanding = true;
            tl.landingStartDay = landingStart;
            for (const auto& e : tl.landing.events)
                add(landingStart + e.t / 86400.0, "EDL", e.text, true);
        } else {
            tl.landingNote = tl.landing.message;
        }
    }

    std::stable_sort(tl.events.begin(), tl.events.end(),
                     [](const TimelineEvent& a, const TimelineEvent& b) { return a.day < b.day; });
    return tl;
}

MissionPhase MissionTimeline::PhaseAt(const MissionTimelineData& tl, const MissionPlan& plan, double day) {
    if (day < plan.launchDay) return MissionPhase::Prelaunch;
    if (day < plan.launchDay + 10.0 / 1440.0) return MissionPhase::Launch;
    double approachLead = plan.earthMoonMission ? 0.5 : std::min(45.0, plan.transferDays * 0.15);
    if (day < plan.arrivalDay - approachLead) return MissionPhase::Cruise;
    if (day < plan.arrivalDay) return MissionPhase::Approach;
    if (tl.hasLanding) {
        double end = tl.landingStartDay + tl.landing.durationS / 86400.0;
        if (day < tl.landingStartDay) return MissionPhase::Arrival;
        if (day < end) return MissionPhase::Descent;
        return tl.landing.landed ? MissionPhase::Landed : MissionPhase::Lost;
    }
    return tl.captureModeled ? MissionPhase::InOrbit : MissionPhase::Arrival;
}

const TimelineEvent* MissionTimeline::NextEvent(const MissionTimelineData& tl, double day) {
    for (const auto& e : tl.events) if (e.day > day + 1e-9) return &e;
    return nullptr;
}

std::string MissionTimeline::FormatMET(double s) {
    char sign = s < 0.0 ? '-' : '+';
    double a = std::fabs(s);
    long long total = static_cast<long long>(std::floor(a));
    long long d = total / 86400, h = (total / 3600) % 24, m = (total / 60) % 60, sec = total % 60;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "T%c%03lldd %02lld:%02lld:%02lld", sign, d, h, m, sec);
    return buf;
}

double MissionTimeline::LightTimeSec(const glm::dvec3& a, const glm::dvec3& b) {
    return glm::length(a - b) * kAuKm / kCKms;
}

bool MissionTimeline::WriteCruiseCsv(const MissionPlan& plan, const std::string& path, double stepDays) {
    if (!plan.valid || plan.earthMoonMission || stepDays <= 0.0) return false;
    std::ofstream out(path);
    if (!out) return false;
    KeplerOrbit transfer(plan.transferElements);
    out << "# ExoSim cruise trajectory - " << plan.targetName << " (idealized Hohmann transfer, two-body)\n";
    out << "# Heliocentric ecliptic J2000. Position AU, velocity km/s (central difference).\n";
    out << "day_since_j2000,utc_date,met_days,x_au,y_au,z_au,vx_kms,vy_kms,vz_kms,sun_range_au,speed_kms,visviva_speed_kms\n";
    const double aKm = plan.transferElements.semiMajorAxisAU * kAuKm;
    char line[512];
    for (double t = 0.0; t <= plan.transferDays + 1e-9; t += stepDays) {
        double tt = std::min(t, plan.transferDays);
        glm::dvec3 p = transfer.positionAtTime(tt);
        const double h = 1e-3; // days
        glm::dvec3 v = (transfer.positionAtTime(tt + h) - transfer.positionAtTime(tt - h)) * (kAuKm / (2.0 * h * 86400.0));
        double r = glm::length(p);
        double vv = std::sqrt(kMuSun * (2.0 / (r * kAuKm) - 1.0 / aKm));
        std::snprintf(line, sizeof(line), "%.4f,%s,%.4f,%.9f,%.9f,%.9f,%.6f,%.6f,%.6f,%.9f,%.6f,%.6f\n",
                      plan.launchDay + tt, MissionPlanner::DayToCalendar(plan.launchDay + tt).c_str(), tt,
                      p.x, p.y, p.z, v.x, v.y, v.z, r, glm::length(v), vv);
        out << line;
    }
    return static_cast<bool>(out);
}
