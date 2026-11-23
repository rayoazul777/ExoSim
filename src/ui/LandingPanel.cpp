#include "ui/Panels.h"
#include "app/AppContext.h"
#include "scene/SystemScene.h"
#include "sim/Formatting.h"
#include "KeplerOrbit.h"
#include "LandingSimulator.h"
#include "MissionPlanner.h"
#include "MissionTimeline.h"
#include "PlanetClassifier.h"
#include "SolarSystemAppearance.h"

#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

void DrawLandingTelemetry(AppContext& ctx) {
    SimulationState& sim = ctx.sim;
    const LandingResult& L = sim.timeline().landing;
    const MissionPlan& m = sim.mission();
    const double tLand = (sim.day() - sim.timeline().landingStartDay) * 86400.0;
    const LandingSample cur = LandingSimulator::At(L, tLand);
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(ctx.width) - 450.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(440, static_cast<float>(std::min(ctx.height - 20, 780))), ImGuiCond_FirstUseEver);
    ImGui::Begin("Landing Telemetry");
    ImGui::Text("%s", L.profileName.c_str());
    ImGui::TextDisabled("Target body: %s", L.bodyName.c_str());
    ImVec4 phCol = cur.phase == DescentPhase::Landed    ? ImVec4(0.3f, 1.0f, 0.5f, 1.0f)
                   : cur.phase == DescentPhase::Crashed ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
                                                        : ImVec4(1.0f, 0.75f, 0.25f, 1.0f);
    if (tLand < 0.0)
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "AWAITING %s  T-%s",
                           L.atmosphericEntry ? "ENTRY INTERFACE" : "DESCENT START", Format::Duration(-tLand).c_str());
    else
        ImGui::TextColored(phCol, "%s", DescentPhaseName(cur.phase));
    ImGui::Text("MET %s   EDL clock %s", MissionTimeline::FormatMET((sim.day() - m.launchDay) * 86400.0).c_str(),
                MissionTimeline::FormatMET(tLand).c_str());
    if (ImGui::Button(sim.paused() ? "Resume" : "Pause")) sim.apply(Command::Paused(!sim.paused()));
    for (double w : {1.0, 2.0, 5.0, 10.0, 50.0}) {
        ImGui::SameLine();
        char lb[16];
        std::snprintf(lb, sizeof(lb), "%gx", w);
        if (ImGui::SmallButton(lb)) {
            sim.apply(Command::Speed(w / 86400.0));
            sim.apply(Command::Paused(false));
        }
    }
    ImGui::Text("Playback: %s%s", Format::Warp(sim.speedDaysPerSec() * 86400.0).c_str(), sim.paused() ? "  (PAUSED)" : "");
    if (ImGui::Button("Restart descent")) sim.apply(Command::Day(sim.timeline().landingStartDay - 5.0 / 86400.0));
    ImGui::SameLine();
    if (ImGui::Button("Skip to final 60 s"))
        sim.apply(Command::Day(sim.timeline().landingStartDay + std::max(0.0, L.durationS - 60.0) / 86400.0));
    ImGui::SameLine();
    if (ImGui::Button("Back to system view")) ctx.view.mode = ViewMode::System;
    ImGui::TextDisabled("Drag to orbit the camera, scroll to zoom.");

    // Signal delay: everything here reaches Earth one light-time later.
    {
        glm::dvec3 earthPos = sim.orbits()[sim.earthIndex()].positionAtTime(sim.day());
        double lt =
            sim.activeTarget().isEarthMoon
                ? 384400.0 / 299792.458
                : MissionTimeline::LightTimeSec(sim.orbits()[sim.activeTarget().planetIdx].positionAtTime(sim.day()), earthPos);
        ImGui::TextWrapped("Signal delay to Earth: %s. Mission control sees each event that much later - "
                           "the vehicle must fly itself.",
                           Format::Duration(lt).c_str());
    }
    ImGui::Separator();

    auto altText = [](double mtr) {
        char b[48];
        if (mtr >= 10000.0)
            std::snprintf(b, sizeof(b), "%.2f km", mtr / 1000.0);
        else
            std::snprintf(b, sizeof(b), "%.1f m", mtr);
        return std::string(b);
    };
    ImGui::Text("Altitude        %s", altText(cur.altitudeM).c_str());
    ImGui::Text("Speed           %.1f m/s", cur.speedMs);
    ImGui::Text("  vertical      %+.1f m/s", cur.verticalMs);
    ImGui::Text("  horizontal    %.1f m/s", cur.horizontalMs);
    ImGui::Text("Sensed load     %.2f g", cur.accelG);
    if (L.atmosphericEntry) {
        ImGui::Text("Dyn. pressure   %.0f Pa", cur.dynPressurePa);
        ImGui::Text("Heat flux       %.1f W/cm2", cur.heatFluxWcm2);
    }
    ImGui::Text("Mass            %.0f kg", cur.massKg);
    if (L.powered) {
        float propFrac = L.propellantInitialKg > 0.0 ? static_cast<float>(cur.propellantKg / L.propellantInitialKg) : 0.0f;
        char pb[48];
        std::snprintf(pb, sizeof(pb), "%.0f kg", cur.propellantKg);
        ImGui::ProgressBar(propFrac, ImVec2(-1, 0), pb);
        ImGui::TextDisabled("Propellant");
        char tb[32];
        std::snprintf(tb, sizeof(tb), "%.0f%%", cur.throttle * 100.0);
        ImGui::ProgressBar(static_cast<float>(cur.throttle), ImVec2(-1, 0), tb);
        ImGui::TextDisabled("Throttle");
        if (cur.phase == DescentPhase::PoweredDescent || cur.phase == DescentPhase::TerminalDescent)
            ImGui::Text("Range to target %s", altText(cur.rangeToTargetM).c_str());
    }

    // Strip charts of the flown profile so far.
    {
        std::vector<float> alt, spd;
        size_t stride = std::max<size_t>(1, L.samples.size() / 400);
        for (size_t k = 0; k < L.samples.size() && L.samples[k].t <= std::max(tLand, 0.0); k += stride) {
            alt.push_back(static_cast<float>(L.samples[k].altitudeM / 1000.0));
            spd.push_back(static_cast<float>(L.samples[k].speedMs));
        }
        if (alt.size() > 1) {
            ImGui::PlotLines("Altitude km", alt.data(), static_cast<int>(alt.size()), 0, nullptr, 0.0f, FLT_MAX, ImVec2(0, 60));
            ImGui::PlotLines("Speed m/s", spd.data(), static_cast<int>(spd.size()), 0, nullptr, 0.0f, FLT_MAX, ImVec2(0, 60));
        }
    }

    // Limit monitors: values so far against the vehicle class's illustrative limits.
    ImGui::Separator();
    ImGui::Text("Limit monitors");
    {
        double peakG = 0.0, peakHeat = 0.0;
        for (const auto& sm : L.samples) {
            if (sm.t > tLand) break;
            peakG = std::max(peakG, sm.accelG);
            peakHeat = std::max(peakHeat, sm.heatFluxWcm2);
        }
        bool done = tLand >= L.durationS;
        for (const auto& lim : L.limits) {
            double v = lim.value;
            bool known = true;
            if (lim.name.rfind("Peak sensed", 0) == 0)
                v = peakG;
            else if (lim.name.rfind("Peak heat", 0) == 0)
                v = peakHeat;
            else if (lim.name.rfind("Propellant", 0) == 0)
                v = L.propellantInitialKg > 0.0 ? 100.0 * cur.propellantKg / L.propellantInitialKg : 0.0;
            else
                known = done; // touchdown speed, landing error: only once landed
            bool red = lim.higherIsWorse ? v >= lim.red : v <= lim.red;
            bool yel = lim.higherIsWorse ? v >= lim.yellow : v <= lim.yellow;
            ImVec4 c = !known ? ImVec4(0.55f, 0.6f, 0.6f, 1.0f)
                       : red  ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
                       : yel  ? ImVec4(1.0f, 0.85f, 0.3f, 1.0f)
                              : ImVec4(0.3f, 1.0f, 0.5f, 1.0f);
            if (known)
                ImGui::TextColored(c, "[%s] %s: %.2f %s",
                                   red   ? "RED"
                                   : yel ? "YEL"
                                         : " OK",
                                   lim.name.c_str(), v, lim.unit.c_str());
            else
                ImGui::TextColored(c, "[ --] %s: pending", lim.name.c_str());
        }
        ImGui::TextDisabled("Limits are illustrative for the vehicle class, not flight values.");
    }

    ImGui::Separator();
    ImGui::BeginChild("edl_events", ImVec2(0, 130), true);
    for (const auto& e : L.events) {
        bool past = e.t <= tLand;
        ImGui::TextColored(past ? ImVec4(0.3f, 1.0f, 0.5f, 1.0f) : ImVec4(0.55f, 0.6f, 0.6f, 1.0f), "T+%02d:%02d:%02d  %s",
                           static_cast<int>(e.t) / 3600, (static_cast<int>(e.t) / 60) % 60, static_cast<int>(e.t) % 60,
                           e.text.c_str());
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextWrapped("Simulated, not flight data: 2-D point-mass physics at 100 Hz with %s; "
                       "%s guidance at 20 Hz. No winds, rotation or terrain relief; vehicle glyph not to scale "
                       "when zoomed out.",
                       L.atmosphericEntry ? "drag, lift and parachutes in a tabulated/exponential atmosphere"
                                          : "inverse-square gravity",
                       L.powered ? "ZEM/ZEV powered-descent" : "no powered");
    ImGui::End();
}
