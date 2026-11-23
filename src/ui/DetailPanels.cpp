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

void DrawMoonDetail(AppContext& ctx) {
    SimulationState& sim = ctx.sim;
    const DetailView& det = ctx.view.detail;
    const MoonRecord& moon = det.moons[det.moonIndex];
    const auto& parent = sim.planets()[det.planetIdx];
    PlanetAppearance moonLook = moon.isReal ? SolarSystemAppearance::Lookup(moon.name) : PlanetAppearance{};
    ImGui::Begin("Moon Detail");
    ImGui::Text("%s  (moon of %s)", moon.name.c_str(), parent.name.c_str());
    ImGui::TextDisabled("Scroll to zoom in - surface detail increases as you get closer.");
    ImGui::Separator();
    if (moon.isReal) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "Real moon - orbital data from published values.");
        if (moon.radiusKm > 0.0) ImGui::Text("Mean radius: %.1f km", moon.radiusKm);
        ImGui::Text("Orbital period: %.3f days", moon.orbitPeriodDays);
        ImGui::Text("Orbit radius: %.1f %s radii", moon.orbitRadiusPlanetRadii, parent.name.c_str());
        ImGui::Text("Inclination: %.2f deg   Eccentricity: %.4f", moon.inclinationDeg, moon.eccentricity);
        if (moonLook.isKnown)
            ImGui::TextWrapped("Colors and feature types reflect this moon's real, spacecraft-imaged "
                               "appearance; fine surface detail is procedurally generated.");
        else
            ImGui::TextDisabled("No hand-modeled appearance for this moon yet - generic rocky look.");
    } else {
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "SPECULATIVE - not a real detection.");
        ImGui::TextWrapped("No exomoon has been confirmed around this planet. This body, its orbit, "
                           "and its surface are illustrative placeholders only.");
    }
    ImGui::Separator();
    std::string backLbl = "Back to " + parent.name;
    if (ImGui::Button(backLbl.c_str())) ctx.view.detail.moonIndex = -1;
    ImGui::SameLine();
    if (ImGui::Button("Back to System View")) {
        ctx.view.detail.moonIndex = -1;
        ctx.view.mode = ViewMode::System;
    }
    ImGui::End();
}

void DrawPlanetDetail(AppContext& ctx) {
    SimulationState& sim = ctx.sim;
    const DetailView& det = ctx.view.detail;
    const auto& p = sim.planets()[det.planetIdx];
    ImGui::Begin("Planet Detail");
    ImGui::Text("%s", p.name.c_str());
    ImGui::Text("Host star: %s", p.hostStar.c_str());
    ImGui::TextDisabled("Scroll to zoom in on the surface pattern below.");
    ImGui::Separator();

    if (det.appearance.isKnown) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "KNOWN APPEARANCE (real, well-photographed):");
        ImGui::TextWrapped("Colors and features (bands, continents, polar caps, spots, "
                           "haze, rings, axial tilt) below reflect this planet's actual "
                           "documented look, not a statistical guess.");
        ImGui::Separator();
    } else if (det.classification.hasRealAtmosphereFindings) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "MEASURED (published spectroscopy):");
        ImGui::TextWrapped("%s", det.classification.realAtmosphereFindings.c_str());
        ImGui::Separator();
    }

    ImGui::TextColored(
        ImVec4(det.classification.displayColor.r, det.classification.displayColor.g, det.classification.displayColor.b, 1.0f),
        "%s", det.classification.typeLabel.c_str());
    ImGui::TextWrapped("Surface: %s", det.classification.surfaceGuess.c_str());
    ImGui::TextWrapped("Atmosphere: %s", det.classification.atmosphereGuess.c_str());

    ImGui::Separator();
    if (p.densityGcm3 > 0.0) { ImGui::Text("Bulk density: %.2f g/cm^3", p.densityGcm3); }
    if (p.radiusJupiter > 0.0) {
        ImGui::Text("Radius: %.3f Jupiter radii (%.2f Earth radii)", p.radiusJupiter, p.radiusJupiter * 11.2);
    }
    if (p.massJupiter > 0.0) {
        ImGui::Text("Mass: %.3f Jupiter masses (%.2f Earth masses)", p.massJupiter, p.massJupiter * 317.8);
    }
    if (p.eqTempK > 0.0) { ImGui::Text("Equilibrium temperature: %.0f K", p.eqTempK); }
    if (!det.classification.hasAnyPhysicalData) {
        ImGui::TextDisabled("No radius, density, or mass on record for this planet.");
    } else if (det.classification.basedOnDensity) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.6f, 1.0f),
                           "Classified from measured bulk density - the most reliable signal available.");
    } else if (!det.classification.basedOnRadius) {
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f),
                           "Estimated from mass only (no measured radius or density) - lower confidence.");
    }

    if (det.appearance.isKnown) {
        ImGui::TextDisabled("The classification/atmosphere lines above are still generated");
        ImGui::TextDisabled("by the same density-based estimator used for exoplanets (it");
        ImGui::TextDisabled("happens to check out here) - but the VISUAL above is real,");
        ImGui::TextDisabled("not estimated, unlike every exoplanet in this app.");
    } else {
        ImGui::TextDisabled("Composition/atmosphere are statistical estimates from");
        ImGui::TextDisabled("radius/mass, not direct measurements - the Archive rarely");
        ImGui::TextDisabled("has spectroscopic data outside a handful of flagship targets.");
        ImGui::TextDisabled("The surface/cloud pattern above is a procedural stylization");
        ImGui::TextDisabled("driven by that classification - not a real image. No exoplanet");
        ImGui::TextDisabled("has ever been directly imaged in this kind of visual detail.");
    }

    ImGui::Separator();
    if (det.moons.empty()) {
        if (p.name == "Mercury" || p.name == "Venus" || p.name == "Ceres")
            ImGui::TextDisabled("%s has no moons.", p.name.c_str());
        else if (p.hostStar == "Sun")
            ImGui::TextDisabled("%s's real moons aren't modeled yet.", p.name.c_str());
        else
            ImGui::TextDisabled("No moons shown for this planet.");
    } else if (det.moonsReal) {
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Moons (real, approximate known orbits):");
        for (int mi = 0; mi < static_cast<int>(det.moons.size()); ++mi) {
            const auto& m = det.moons[mi];
            ImGui::BulletText("%s - period ~%.2f days, inclination ~%.1f deg", m.name.c_str(), m.orbitPeriodDays,
                              m.inclinationDeg);
            ImGui::SameLine();
            if (ImGui::SmallButton(("Inspect##dm" + std::to_string(mi)).c_str())) ctx.view.detail.moonIndex = mi;
        }
    } else {
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "No confirmed moons on record.");
        ImGui::TextDisabled("The Archive doesn't include exomoon detections (only a couple");
        ImGui::TextDisabled("of disputed candidates exist in the literature, system-wide).");
        ImGui::TextDisabled("The moons below are illustrative placeholders only:");
        for (int mi = 0; mi < static_cast<int>(det.moons.size()); ++mi) {
            ImGui::BulletText("%s (speculative, not a real detection)", det.moons[mi].name.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(("Inspect##ds" + std::to_string(mi)).c_str())) ctx.view.detail.moonIndex = mi;
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Back to System View")) { ctx.view.mode = ViewMode::System; }
    ImGui::End();
}
