#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "StarClassifier.h"
#include "sky/SkyModel.h"

struct AppContext;

// The star-system view: orbit rings, planets, the host star and companions,
// log-scale guide rings, the visible sky, and the mission trajectory with
// the clickable spacecraft. Derived geometry is rebuilt automatically when
// what it depends on changes (system, log scale, star size, sky epoch) -
// whether the change came from the UI or from a replay.
class SystemScene {
public:
    // Companion star visual. Position is schematic unless a real WDS
    // projected separation exists (then period comes from Kepler's 3rd law).
    struct CompanionVisual {
        float orbitRadius = 0.0f;
        float baseAngle = 0.0f;
        bool realSeparation = false;
        double separationAU = 0.0;
        double periodDays = 0.0;
        glm::vec3 color{0.0f};
        float displayRadius = 0.0f;
        bool hasData = false;
        std::string label;
    };
    // Reference-distance rings drawn only with log scale on, so compression
    // doesn't silently imply orbits are that close together.
    struct GuideRing {
        double realAU;
        std::vector<glm::vec3> points;
    };

    // Full rebuild for the current system (geometry, star, sky, camera fit).
    // `snap` jumps the camera instead of easing (first build only).
    void rebuild(AppContext& ctx, bool snap);
    // Rebuild if the system, log scale or star size changed; recompute only
    // the sky if just the sky epoch changed.
    void syncWithState(AppContext& ctx);
    // Spacecraft display position for this frame (also ends follow mode when
    // there's no spacecraft to follow).
    void updateCraft(AppContext& ctx);
    void render(AppContext& ctx);

    const StarClassification& star() const { return m_star; }
    const std::vector<CompanionVisual>& companions() const { return m_companions; }
    const std::vector<GuideRing>& guideRings() const { return m_guideRings; }
    int visibleStarCount() const { return m_sky.visibleCount; }
    bool sunVisible() const { return m_sky.sunVisible; }
    double sunApparentVmag() const { return m_sky.sunApparentVmag; }

private:
    void rebuildSky(AppContext& ctx);

    StarClassification m_star;
    float m_starDisplayRadius = 0.05f;
    std::vector<CompanionVisual> m_companions;
    std::vector<GuideRing> m_guideRings;
    Sky::SkyView m_sky; // counts only (vectors handed to the renderer)
    glm::vec3 m_craftDisplayPos{0.0f};

    int m_builtSystem = -1;
    bool m_builtLog = false;
    float m_builtExaggeration = -1.0f;
    float m_builtEpoch = 0.0f;
};
