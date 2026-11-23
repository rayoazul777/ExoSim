#include "ui/Panels.h"
#include "app/AppContext.h"
#include "scene/GalaxyScene.h"
#include "sim/Formatting.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
std::string widthLabel(double pc) {
    char buf[48];
    if (pc >= 1000.0)
        std::snprintf(buf, sizeof buf, "%.1f kpc (%.0fk ly)", pc / 1000.0, pc * 3.26156 / 1000.0);
    else
        std::snprintf(buf, sizeof buf, "%.1f pc (%.0f ly)", pc, pc * 3.26156);
    return buf;
}
} // namespace

// Opens the map. The first time it frames the whole Galaxy; the current star
// system's host (if it has a measured distance) is selected either way.
void OpenGalaxyMap(AppContext& ctx) {
    GalaxyScene& scene = *ctx.galaxyScene;
    scene.ensureBuilt(ctx);
    GalaxyView& g = ctx.view.galaxy;
    ctx.view.mode = ViewMode::Galaxy;
    g.selected = scene.hostForSystem(ctx.sim.currentSystem().hostName);
    g.opened = true;
}

void DrawGalaxyPanel(AppContext& ctx) {
    GalaxyScene& scene = *ctx.galaxyScene;
    GalaxyView& g = ctx.view.galaxy;
    const auto& hosts = scene.hosts();
    const double aspect = static_cast<double>(ctx.width) / std::max(ctx.height, 1);

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Milky Way");
    if (ImGui::Button("Back to system view")) ctx.view.mode = ViewMode::System;
    ImGui::SameLine();
    if (ImGui::RadioButton("2-D map", g.topDown)) g.topDown = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("3-D", !g.topDown)) g.topDown = false;

    ImGui::Text("Field width: %s", widthLabel(g.fieldWidthPc(aspect)).c_str());
    if (ImGui::SmallButton("Whole Galaxy")) g.flyTo(glm::dvec3(Galaxy::kSunToCenterPc, 0.0, 0.0), 42000.0);
    ImGui::SameLine();
    if (ImGui::SmallButton("Solar neighborhood")) g.flyTo(glm::dvec3(0.0), 60.0);
    ImGui::SameLine();
    if (ImGui::SmallButton("Kepler field")) {
        // Kepler's field (Cygnus-Lyra, l ~ 76 deg, b ~ +13.5 deg): most hosts at 0.3-1.5 kpc.
        g.flyTo(Galaxy::GalacticToCartesian(76.3, 13.5, 700.0), 2500.0);
    }
    ImGui::TextDisabled("Scroll: zoom at cursor. Drag: %s. Right-drag: pan.", g.topDown ? "pan" : "orbit");

    ImGui::Separator();
    ImGui::Checkbox("Exoplanet hosts", &g.showHosts);
    ImGui::SameLine();
    ImGui::Checkbox("Hipparcos stars", &g.showLocal);
    ImGui::Checkbox("Galaxy model", &g.showModel);
    ImGui::SameLine();
    ImGui::Checkbox("Arms", &g.showArms);
    ImGui::SameLine();
    ImGui::Checkbox("Rings", &g.showRings);
    ImGui::SameLine();
    ImGui::Checkbox("Labels", &g.showLabels);

    ImGui::Separator();
    ImGui::Text("%zu exoplanet host stars plotted", hosts.size());
    if (scene.hostStats().withoutDistance > 0)
        ImGui::TextDisabled("(%d more hosts have no measured distance)", scene.hostStats().withoutDistance);
    if (scene.localStarCount() > 0)
        ImGui::Text("%d Hipparcos stars within 1 kpc", scene.localStarCount());
    else
        ImGui::TextDisabled("(star_catalog.json has no parallaxes - re-run data/fetch_exoplanets.py)");
    ImGui::InputTextWithHint("##find", "Find a host star...", g.search, sizeof g.search);
    if (g.search[0] != '\0') {
        int shown = 0;
        for (size_t i = 0; i < hosts.size() && shown < 10; ++i) {
            if (!Format::ContainsCaseInsensitive(hosts[i].name, g.search)) continue;
            ++shown;
            char lbl[160];
            std::snprintf(lbl, sizeof lbl, "%s  (%.0f pc)##h%zu", hosts[i].name.c_str(), hosts[i].distPc, i);
            if (ImGui::Selectable(lbl, g.selected == static_cast<int>(i))) {
                g.selected = static_cast<int>(i);
                g.flyTo(hosts[i].posPc, std::clamp(hosts[i].distPc * 0.25, 15.0, 3000.0));
            }
        }
        if (shown == 0) ImGui::TextDisabled("(no host with a measured distance matches)");
    }

    if (g.selected >= 0 && g.selected < static_cast<int>(hosts.size())) {
        const Galaxy::Host& h = hosts[static_cast<size_t>(g.selected)];
        ImGui::Separator();
        ImGui::Text("%s", h.name.c_str());
        ImGui::Text("Distance: %.2f pc (%.1f ly)", h.distPc, h.distPc * 3.26156);
        ImGui::Text("Galactic l = %.2f deg, b = %+.2f deg", h.lDeg, h.bDeg);
        ImGui::Text("Planets: %d%s", h.planets, h.teffK > 0.0 ? "" : "   Teff: unknown");
        if (h.teffK > 0.0) {
            ImGui::SameLine();
            ImGui::Text("   Teff: %.0f K", h.teffK);
        }
        ImGui::Text("Galactocentric radius: %.2f kpc", Galaxy::GalactocentricRadiusPc(h.posPc) / 1000.0);
        if (ImGui::SmallButton("Center")) g.flyTo(h.posPc, std::min(g.distance, std::max(15.0, h.distPc * 0.25)));
        ImGui::SameLine();
        if (h.systemIndex >= 0) {
            if (ImGui::SmallButton("Open system")) {
                ctx.sim.apply(Command::System(h.systemIndex));
                ctx.view.mode = ViewMode::System;
            }
        } else {
            ImGui::TextDisabled("(no planet with orbital elements to simulate)");
        }
    }

    ImGui::Separator();
    ImGui::TextWrapped("Real: host positions (Archive RA/Dec + distance) and Hipparcos stars (parallax). "
                       "Illustrative model: the disk, bar, bulge and arms - a 4-arm log spiral fitted to "
                       "maser-parallax arm crossings (Reid et al. 2019); R0 = 8.178 kpc. We see the Galaxy "
                       "from inside it, so its exact shape - especially the far side - is uncertain.");
    ImGui::End();
}
