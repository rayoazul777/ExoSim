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

void DrawMissionPlanner(AppContext& ctx) {
    SimulationState& sim = ctx.sim;
    const StarSystem& currentSystem = sim.currentSystem();
    if (currentSystem.isHome && !sim.missionTargets().empty()) {
        ImGui::Begin("Mission Planner");
        int vehicle = sim.vehicleChoice();
        const char* vehicles[SimulationState::kVehicleCount] = {SimulationState::VehicleName(0), SimulationState::VehicleName(1)};
        if (ImGui::Combo("Launch vehicle", &vehicle, vehicles, SimulationState::kVehicleCount))
            sim.apply(Command::Vehicle(vehicle));
        if (ImGui::BeginCombo("Destination", sim.missionTargets()[sim.targetChoice()].label.c_str())) {
            for (int ti = 0; ti < static_cast<int>(sim.missionTargets().size()); ++ti)
                if (ImGui::Selectable(sim.missionTargets()[ti].label.c_str(), ti == sim.targetChoice()))
                    sim.apply(Command::Target(ti));
            ImGui::EndCombo();
        }
        int model = sim.trajectoryModel();
        const char* models[] = {"Lambert: optimal 3-D transfer", "Hohmann: coplanar idealization"};
        if (ImGui::Combo("Trajectory model", &model, models, 2)) sim.apply(Command::Model(model));
        if (ImGui::Button("Plan next launch window & run simulation")) {
            sim.apply(Command::Plan(sim.targetChoice()));
            if (sim.missionActive()) ctx.view.telemetryStatus.clear();
        }
        if (ImGui::Button("Porkchop plot")) ctx.view.porkchop.open = true;
        ImGui::SameLine();
        if (ImGui::Button("Gravity assists")) ctx.view.flybyOpen = true;
        if (sim.missionActive()) {
            ImGui::SameLine();
            if (ImGui::Button("Jump to launch")) sim.apply(Command::Day(sim.mission().launchDay));
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {
                sim.apply(Command::Clear());
                ctx.cam.stopFollowing();
            }
        }

        if (sim.missionActive()) {
            const MissionPlan& m = sim.mission();
            ImGui::Separator();
            ImGui::Text("Target: %s", m.targetName.c_str());
            ImGui::Text("Optimal launch: %s", MissionPlanner::DayToCalendar(m.launchDay).c_str());
            ImGui::Text("Arrival:        %s", MissionPlanner::DayToCalendar(m.arrivalDay).c_str());
            if (m.transferDays < 60.0)
                ImGui::Text("Transfer time: %.1f days", m.transferDays);
            else
                ImGui::Text("Transfer time: %.0f days (%.1f years)", m.transferDays, m.transferDays / 365.25);
            if (m.synodicPeriodDays > 0.0) ImGui::Text("Windows repeat every ~%.0f days (synodic period)", m.synodicPeriodDays);
            ImGui::Text("Method: %s", m.method.c_str());
            ImGui::Text("Departure burn from 200 km LEO: %.2f km/s  (C3 %.1f km2/s2)", m.dvFromLeoKms, m.c3);
            if (m.method == "Lambert")
                ImGui::Text("Transfer angle %.0f deg, inclined %.2f deg to the ecliptic", m.transferAngleDeg, m.inclinationDeg);
            ImGui::Text("Arrival excess speed: %.2f km/s", m.vInfArrivalKms);
            if (!sim.timeline().captureModeled)
                ImGui::TextDisabled("Capture burn: not modeled for moon targets");
            else if (sim.timeline().captureDvKms > 0.0)
                ImGui::Text("Orbit insertion burn: %.2f km/s", sim.timeline().captureDvKms);
            else
                ImGui::Text("Orbit insertion burn: none (direct atmospheric entry)");

            if (sim.day() < m.launchDay)
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "T-minus %.1f days to launch", m.launchDay - sim.day());
            else if (sim.day() < m.arrivalDay)
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "In flight: %.0f%% - ETA %.1f days",
                                   100.0 * (sim.day() - m.launchDay) / m.transferDays, m.arrivalDay - sim.day());
            else
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "Arrived.");

            ImGui::Separator();
            if (sim.vehicleChoice() == 0) {
                ImGui::TextWrapped("Falcon Heavy published capacity (SpaceX, fully expendable): "
                                   "63,800 kg to LEO, 16,800 kg to Mars, 3,500 kg to Pluto. "
                                   "It launched NASA's Europa Clipper in Oct 2024.");
            } else {
                ImGui::TextWrapped("SLS Block 1 (NASA): ~27,000 kg to trans-lunar injection. "
                                   "Artemis is a LUNAR program - Artemis I (2022) reached the Moon "
                                   "in ~5 days. No SLS planetary mission is currently planned, so "
                                   "non-lunar destinations here are hypothetical.");
            }
            ImGui::TextDisabled("Published payload figures only exist for some destinations - none are");
            ImGui::TextDisabled("invented here. Trajectory time/energy don't depend on the vehicle.");
            ImGui::Separator();
            if (m.method == "Hohmann")
                ImGui::TextWrapped("Idealized minimum-energy (Hohmann) transfer: coplanar, ignores the planets' "
                                   "inclinations and eccentric positions. Switch to Lambert for the real 3-D optimum.");
            else if (m.method == "Lambert")
                ImGui::TextWrapped("Lambert transfer between the planets' real positions on these dates (3-D, "
                                   "two-body, patched conics). Outer-planet missions usually add gravity assists "
                                   "(Voyager 2: Neptune in ~12 yr) - see Gravity assists.");
            else
                ImGui::TextWrapped("Gravity-assist chain: Lambert legs joined by flybys (patched conics). See the "
                                   "Gravity assists window for each encounter.");
            if (m.earthMoonMission) ImGui::TextDisabled("Earth-Moon transfer is too small to draw at AU scale.");
            if (m.arrivalBodyName != m.targetName && !m.earthMoonMission)
                ImGui::TextDisabled("Moon targets: arrival in the parent planet's system; the final "
                                    "capture and moon transfer aren't modeled.");
        }
        ImGui::End();
    }
}

void DrawFlightDirector(AppContext& ctx) {
    SimulationState& sim = ctx.sim;
    if (sim.missionActive()) {
        const MissionPlan& m = sim.mission();
        const MissionTimelineData& tl = sim.timeline();
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(ctx.width) - 480.0f, 10.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(470, 560), ImGuiCond_FirstUseEver);
        ImGui::Begin("Flight Director");
        MissionPhase ph = MissionTimeline::PhaseAt(tl, m, sim.day());
        ImGui::Text("MET %s", MissionTimeline::FormatMET((sim.day() - m.launchDay) * 86400.0).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("UTC %s", Format::Utc(sim.day()).c_str());
        ImVec4 phCol = ph == MissionPhase::Lost                                    ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
                       : ph == MissionPhase::Landed || ph == MissionPhase::InOrbit ? ImVec4(0.3f, 1.0f, 0.5f, 1.0f)
                                                                                   : ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
        ImGui::TextColored(phCol, "PHASE: %s", MissionPhaseName(ph));
        if (const TimelineEvent* ne = MissionTimeline::NextEvent(tl, sim.day()))
            ImGui::Text("NEXT: %s in %s", ne->code.c_str(), Format::Duration((ne->day - sim.day()) * 86400.0).c_str());

        ImGui::Separator();
        if (ImGui::Button(sim.paused() ? "Resume" : "Pause")) sim.apply(Command::Paused(!sim.paused()));
        const struct {
            const char* label;
            double warp;
        } rates[] = {{"1x", 1.0}, {"60x", 60.0}, {"1 h/s", 3600.0}, {"1 d/s", 86400.0}, {"10 d/s", 864000.0}};
        for (const auto& r : rates) {
            ImGui::SameLine();
            if (ImGui::SmallButton(r.label)) {
                sim.apply(Command::Speed(r.warp / 86400.0));
                sim.apply(Command::Paused(false));
            }
        }
        ImGui::Text("Rate: %s%s", Format::Warp(sim.speedDaysPerSec() * 86400.0).c_str(), sim.paused() ? "  (PAUSED)" : "");
        bool warpToEvents = sim.warpToEvents();
        if (ImGui::Checkbox("Warp to events (stop at launch and at the landing)", &warpToEvents))
            sim.apply(Command::WarpToEvents(warpToEvents));

        if (!m.earthMoonMission) {
            if (ctx.cam.followingCraft) {
                if (ImGui::Button("Stop following")) ctx.cam.stopFollowing();
            } else if (ImGui::Button("Follow spacecraft")) {
                ctx.cam.startFollowing();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(or click the rocket)");
        }
        if (tl.hasLanding) {
            bool before = sim.day() < tl.landingStartDay;
            if (ImGui::Button(before ? "Skip ahead to the landing" : "Watch landing")) {
                if (before) sim.apply(Command::Day(tl.landingStartDay - 5.0 / 86400.0));
                sim.apply(Command::Speed(1.0 / 86400.0));
                sim.apply(Command::Paused(false));
                ctx.view.mode = ViewMode::Landing;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", tl.landing.profileName.c_str());
        } else {
            ImGui::TextDisabled("No landing: %s", tl.landingNote.c_str());
        }

        // Live telemetry from the ephemeris + transfer arc.
        ImGui::Separator();
        const auto& earthP = sim.orbits()[sim.earthIndex()];
        glm::dvec3 earthPos = earthP.positionAtTime(sim.day());
        if (!m.earthMoonMission) {
            glm::dvec3 craftPos = sim.craftPosition(sim.day());
            constexpr double kAuKm = 1.495978707e8;
            double r = glm::length(craftPos);
            bool cruising = sim.day() >= m.launchDay && sim.day() < m.arrivalDay;
            const double h = 0.01; // days
            double v = cruising
                           ? glm::length(m.positionAt(sim.day() + h) - m.positionAt(sim.day() - h)) / (2.0 * h) * kAuKm / 86400.0
                           : 0.0;
            double earthRange = glm::length(craftPos - earthPos);
            double tgtRange = glm::length(craftPos - sim.orbits()[sim.activeTarget().planetIdx].positionAtTime(sim.day()));
            ImGui::Text("Sun distance     %.4f AU", r);
            if (cruising) ImGui::Text("Heliocentric v   %.3f km/s", v);
            ImGui::Text("Earth range      %.2f million km", earthRange * kAuKm / 1e6);
            ImGui::Text("Target range     %.2f million km", tgtRange * kAuKm / 1e6);
            ImGui::Text("One-way light    %s", Format::Duration(MissionTimeline::LightTimeSec(craftPos, earthPos)).c_str());
        } else {
            ImGui::TextDisabled("Earth-Moon transfer: geocentric, light time ~1.3 s");
        }
        ImGui::TextWrapped("%s", tl.arrivalNote.c_str());

        ImGui::Separator();
        ImGui::Text("Sequence of events");
        ImGui::BeginChild("soe", ImVec2(0, 190), true);
        const TimelineEvent* nextEv = MissionTimeline::NextEvent(tl, sim.day());
        for (const auto& e : tl.events) {
            bool past = e.day <= sim.day();
            ImVec4 c = past ? ImVec4(0.3f, 1.0f, 0.5f, 1.0f)
                            : (&e == nextEv ? ImVec4(1.0f, 0.85f, 0.3f, 1.0f) : ImVec4(0.55f, 0.6f, 0.6f, 1.0f));
            ImGui::TextColored(c, "%s  %-6s %s", MissionTimeline::FormatMET((e.day - m.launchDay) * 86400.0).c_str(),
                               e.code.c_str(), e.text.c_str());
        }
        ImGui::EndChild();

        if (ImGui::Button("Export trajectory CSV") && !m.earthMoonMission) {
            std::filesystem::create_directories(ctx.telemetryDir);
            std::string path = ctx.telemetryDir + "/" + Format::SafeFileName(sim.activeTarget().body) + "_cruise.csv";
            ctx.view.telemetryStatus = MissionTimeline::WriteCruiseCsv(m, path)
                                           ? "Wrote " + std::filesystem::absolute(path).string()
                                           : "Export failed: " + path;
        }
        ImGui::SameLine();
        if (ImGui::Button("Export landing CSV") && tl.hasLanding) {
            std::filesystem::create_directories(ctx.telemetryDir);
            std::string path = ctx.telemetryDir + "/" + Format::SafeFileName(sim.activeTarget().body) + "_landing.csv";
            ctx.view.telemetryStatus = LandingSimulator::WriteCsv(tl.landing, path)
                                           ? "Wrote " + std::filesystem::absolute(path).string()
                                           : "Export failed: " + path;
        }
        if (!ctx.view.telemetryStatus.empty()) ImGui::TextWrapped("%s", ctx.view.telemetryStatus.c_str());
        ImGui::TextDisabled("Idealized two-body trajectory; correction slots are nominal (0 m/s).");
        ImGui::End();
    }
}
