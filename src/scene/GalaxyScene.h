#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "galaxy/GalaxyModel.h"

struct AppContext;

// The Milky Way map. Three point clouds - the illustrative disk/arm/bar
// model, the real exoplanet hosts (Archive RA/Dec + distance) and real
// Hipparcos stars within 1 kpc - plus arm centerlines, distance rings around
// the Sun and labels. Layers cross-fade with zoom so the model never stands
// in for real stars at close range.
class GalaxyScene {
public:
    // Loads hosts and uploads the point clouds (first use only).
    void ensureBuilt(AppContext& ctx);
    // GL pass (inside the CRT pass).
    void render(AppContext& ctx);
    // ImGui pass: labels, hover/click picking on hosts, the selection marker.
    void drawOverlay(AppContext& ctx);

    const std::vector<Galaxy::Host>& hosts() const { return m_hosts; }
    int localStarCount() const { return m_localCount; }
    const Galaxy::HostLoadStats& hostStats() const { return m_hostStats; }
    // Index of the host for a star system (by host name), or -1.
    int hostForSystem(const std::string& hostName) const;

    // Layer opacity as a function of camera distance (pc) - exposed for tests and the panel.
    static float ModelAlpha(double distancePc);
    static float LocalStarAlpha(double distancePc);

private:
    bool m_built = false;
    std::vector<Galaxy::Host> m_hosts;
    std::vector<std::vector<glm::vec3>> m_arms;
    int m_localCount = 0;
    Galaxy::HostLoadStats m_hostStats;
    glm::dmat4 m_viewProj{1.0};
};
