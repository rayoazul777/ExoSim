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

void DrawControlPanel(AppContext& ctx) {
    SimulationState& sim = ctx.sim;
    SystemScene& scene = ctx.systemScene;
    const StarSystem& currentSystem = sim.currentSystem();

    ImGui::Begin("ExoSim Control");
    ImGui::Text("Star systems loaded: %zu", sim.systems().size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Milky Way map")) OpenGalaxyMap(ctx);
    float speed = sim.speedDaysPerSec();
    if (ImGui::SliderFloat("Sim speed (days/sec)", &speed, 0.0f, 200.0f, "%.5f", ImGuiSliderFlags_Logarithmic))
        sim.apply(Command::Speed(speed));
    ImGui::Text("Sim date: %s", MissionPlanner::DayToCalendar(sim.day()).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Today")) sim.apply(Command::Day(MissionPlanner::NowDaysSinceJ2000()));
    ImGui::TextDisabled(currentSystem.isHome ? "(Sol: real J2000-based planet positions)"
                                             : "(exoplanet orbital phases are unknown - positions illustrative)");
    float epoch = sim.skyEpochOffsetYears();
    if (ImGui::SliderFloat("Sky epoch (years from now)", &epoch, -100000.0f, 100000.0f, "%.0f")) {
        sim.apply(Command::SkyEpoch(epoch));
        scene.syncWithState(ctx);
    }
    ImGui::TextDisabled("(real Hipparcos proper motions; linear approximation)");
    ImGui::Text("Visible stars from here: %d", scene.visibleStarCount());
    if (!currentSystem.isHome && currentSystem.distPc > 0.0) {
        ImGui::TextDisabled("Sky recomputed from %.1f pc away via real parallaxes.", currentSystem.distPc);
        if (scene.sunVisible())
            ImGui::Text("Our Sun is visible from here (mag %.1f).", scene.sunApparentVmag());
        else
            ImGui::TextDisabled("Our Sun is too faint to see from here (mag %.1f).", scene.sunApparentVmag());
    }
    if (ImGui::Checkbox("Log-scale distances", &ctx.view.logScale)) { scene.syncWithState(ctx); }
    ImGui::TextDisabled("(compresses large orbits so compact systems don't vanish)");
    if (!scene.guideRings().empty()) {
        std::string legend = "Reference rings shown:";
        for (const auto& gr : scene.guideRings()) {
            legend += " " +
                      (gr.realAU >= 1000.0 ? std::to_string((int)(gr.realAU / 1000.0)) + "k" : std::to_string((int)gr.realAU)) +
                      " AU";
        }
        ImGui::TextDisabled("%s", legend.c_str());
    }
    if (ImGui::SliderFloat("Star size exaggeration", &ctx.view.starSizeExaggeration, 1.0f, 200.0f)) { scene.syncWithState(ctx); }
    ImGui::TextDisabled("(a star's true AU-scale size is nearly invisible next to orbits -");
    ImGui::TextDisabled(" this exaggerates it uniformly, so relative sizes between");
    ImGui::TextDisabled(" different stars stay accurate even though absolute size isn't)");
    ImGui::Separator();

    ImGui::TextColored(ImVec4(scene.star().color.r, scene.star().color.g, scene.star().color.b, 1.0f), "Host star: %s%s",
                       scene.star().spectralClass.c_str(), currentSystem.isHome ? "  [SOL - HOME SYSTEM]" : "");
    if (scene.star().hasMeasuredRadius) {
        ImGui::Text("Radius: %.2f solar radii (measured)", scene.star().displayRadiusSolar);
    } else {
        ImGui::TextDisabled("Radius: not measured (assumed Sun-like, 1.0 solar radii)");
    }
    if (currentSystem.numStars > 1) {
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "Multi-star system (%d stars).", currentSystem.numStars);
        ImGui::TextDisabled("Companion position and orbital motion shown are schematic, not");
        ImGui::TextDisabled("a real separation or period - the Archive has no bulk field for");
        ImGui::TextDisabled("either. Color/type below IS real where the Archive resolved it.");
        for (const auto& comp : scene.companions()) {
            ImGui::TextColored(ImVec4(comp.color.r, comp.color.g, comp.color.b, 1.0f), "  Companion: %s", comp.label.c_str());
            if (comp.realSeparation) {
                ImGui::Text("    Projected separation: %.0f AU (real, WDS; a lower bound)", comp.separationAU);
                ImGui::Text("    Orbital period: >= ~%.0f years (Kepler's 3rd law)", comp.periodDays / 365.25);
                if (!ctx.view.logScale && comp.orbitRadius > ctx.cam.systemDistance)
                    ImGui::TextDisabled("    (off-screen at true scale - zoom out or enable log scale)");
            } else {
                ImGui::TextDisabled("    Separation unknown - position/motion schematic.");
            }
        }
    }
    ImGui::Separator();

    ImGui::Text("Find a star system:");
    ImGui::InputTextWithHint("##search", "Search host star name...", ctx.view.searchBuffer, sizeof(ctx.view.searchBuffer));
    ImGui::SliderInt("Min planets in system", &ctx.view.minPlanetsFilter, 1, 8);

    ImGui::BeginChild("SystemList", ImVec2(0, 140), true);
    std::string search(ctx.view.searchBuffer);
    for (int i = 0; i < (int)sim.systems().size(); ++i) {
        if ((int)sim.systems()[i].planetIndices.size() < ctx.view.minPlanetsFilter) continue;
        std::string searchableName = sim.systems()[i].hostName + (sim.systems()[i].isHome ? " Sol Home" : "");
        if (!Format::ContainsCaseInsensitive(searchableName, search)) continue;

        std::string displayName = sim.systems()[i].isHome ? "Sol (Home System)" : sim.systems()[i].hostName;
        std::string label = displayName + " (" + std::to_string(sim.systems()[i].planetIndices.size()) + " planet" +
                            (sim.systems()[i].planetIndices.size() != 1 ? "s" : "") + ")";
        if (ImGui::Selectable(label.c_str(), i == sim.systemIndex())) {
            if (i != sim.systemIndex()) {
                sim.apply(Command::System(i));
                scene.syncWithState(ctx);
            }
        }
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Text("Planets in %s:", currentSystem.isHome ? "Sol (Home System)" : currentSystem.hostName.c_str());
    for (int local = 0; local < (int)currentSystem.planetIndices.size(); ++local) {
        size_t pIdx = currentSystem.planetIndices[local];
        const auto& moonsForRow = sim.moonsFor(pIdx);
        std::string rowLabel = sim.planets()[pIdx].name;
        if (!moonsForRow.empty()) {
            bool rowMoonsReal = !sim.planets()[pIdx].moons.empty();
            size_t n = moonsForRow.size();
            rowLabel += " (" + std::to_string(n) + (rowMoonsReal ? " moon" : " candidate moon") + (n == 1 ? ")" : "s)");
        }
        if (ImGui::Selectable(rowLabel.c_str(), sim.selectedPlanetLocal() == local)) { sim.apply(Command::Planet(local)); }
    }

    if (sim.selectedPlanetLocal() >= 0 && sim.selectedPlanetLocal() < (int)currentSystem.planetIndices.size()) {
        size_t pIdx = currentSystem.planetIndices[sim.selectedPlanetLocal()];
        const auto& p = sim.planets()[pIdx];
        ImGui::Separator();
        ImGui::Text("Host star: %s", p.hostStar.c_str());
        ImGui::Text("Semi-major axis: %.3f AU", p.elements.semiMajorAxisAU);
        ImGui::Text("Eccentricity: %.3f", p.elements.eccentricity);
        ImGui::Text("Period: %.1f days", p.elements.periodDays);
        if (p.elements.inclinationMeasured) {
            ImGui::Text("Inclination: %.2f deg (measured)", p.elements.inclinationDeg);
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "Inclination: not measured (assumed coplanar, 0 deg)");
        }

        if (ImGui::Button("Inspect Planet")) ctx.view.enterDetail(sim, pIdx, -1);
        const auto& moonsHere = sim.moonsFor(pIdx);
        for (int mi = 0; mi < static_cast<int>(moonsHere.size()); ++mi) {
            std::string lbl =
                "Inspect " + moonsHere[mi].name + (moonsHere[mi].isReal ? "" : " (speculative)") + "##m" + std::to_string(mi);
            if (ImGui::SmallButton(lbl.c_str())) ctx.view.enterDetail(sim, pIdx, mi);
        }
    }
    ImGui::End();
}
