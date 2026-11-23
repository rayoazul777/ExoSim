#include "scene/SystemScene.h"
#include "app/AppContext.h"
#include "MissionPlanner.h"
#include "Renderer.h"
#include "GravityAssist.h"
#include "sim/DisplaySpace.h"
#include "sim/Formatting.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kSolarRadiusAU = 0.00465047;
} // namespace

void SystemScene::rebuildSky(AppContext& ctx) {
    const SimulationState& sim = ctx.sim;
    Sky::SkyView sky =
        Sky::Compute(sim.catalog().stars, sim.currentSystem(), MissionPlanner::NowDaysSinceJ2000(), sim.skyEpochOffsetYears());
    ctx.renderer.setStarCatalog(sky.directions, sky.colors, sky.sizes);
    m_sky.visibleCount = sky.visibleCount;
    m_sky.sunVisible = sky.sunVisible;
    m_sky.sunApparentVmag = sky.sunApparentVmag;
    m_builtEpoch = sim.skyEpochOffsetYears();
}

void SystemScene::syncWithState(AppContext& ctx) {
    if (ctx.sim.systemIndex() != m_builtSystem || ctx.view.logScale != m_builtLog ||
        ctx.view.starSizeExaggeration != m_builtExaggeration) {
        rebuild(ctx, false);
    } else if (ctx.sim.skyEpochOffsetYears() != m_builtEpoch) {
        rebuildSky(ctx);
    }
}

// Rebuilds the orbit-ring geometry, star classification/size, companions,
// guide rings, camera fit and sky for the current system.
void SystemScene::rebuild(AppContext& ctx, bool snap) {
    const SimulationState& sim = ctx.sim;
    const bool logScale = ctx.view.logScale;
    const float exaggeration = ctx.view.starSizeExaggeration;
    Renderer& renderer = ctx.renderer;
    renderer.clearOrbits();
    const StarSystem& sys = sim.currentSystem();
    const auto& planets = sim.planets();
    double maxA = 0.0, minA = 1e18;

    for (size_t local = 0; local < sys.planetIndices.size(); ++local) {
        size_t pIdx = sys.planetIndices[local];
        double a = planets[pIdx].elements.semiMajorAxisAU;
        maxA = std::max(maxA, a);
        minA = std::min(minA, a);
        std::vector<glm::vec3> ring;
        constexpr int kSegments = 180;
        for (int s = 0; s <= kSegments; ++s) {
            double t = planets[pIdx].elements.periodDays * static_cast<double>(s) / kSegments;
            ring.push_back(Display::ToDisplay(sim.orbits()[pIdx].positionAtTime(t), logScale));
        }
        renderer.setOrbitPath(local, ring);
    }

    double maxDisplay = Display::Magnitude(std::max(maxA, 0.05), logScale);
    double minDisplay = sys.planetIndices.empty() ? maxDisplay : Display::Magnitude(std::max(minA, 0.01), logScale);

    m_star = StarClassifier::Classify(sys.starTeffK, sys.starRadiusSolar, sys.starSpectralType);
    double starRealAU = m_star.displayRadiusSolar * kSolarRadiusAU;
    // Exaggerated uniformly, but never so large it swallows the innermost orbit.
    m_starDisplayRadius = static_cast<float>(std::min(starRealAU * exaggeration, minDisplay * 0.3));
    m_starDisplayRadius = std::max(m_starDisplayRadius, 0.01f);

    // Companion stars: real temperature/mass/radius where resolved, but a
    // schematic position unless a real WDS projected separation exists.
    m_companions.clear();
    int numCompanions = static_cast<int>(sys.companions.size());
    if (numCompanions > 0) {
        double offsetRadius = std::min(minDisplay * 0.5, static_cast<double>(m_starDisplayRadius) * 8.0);
        offsetRadius = std::max(offsetRadius, static_cast<double>(m_starDisplayRadius) * 2.5);
        for (int i = 0; i < numCompanions; ++i) {
            CompanionVisual cv;
            cv.orbitRadius = static_cast<float>(offsetRadius);
            cv.baseAngle = static_cast<float>(2.0 * kPi * i / numCompanions);
            const StarComponent& comp = sys.companions[i];
            if (comp.projectedSepAU > 0.0) {
                // P[yr] = sqrt(a^3 / (M1+M2)), with the projected separation as a
                // lower bound on a - so the period is a lower bound too.
                cv.realSeparation = true;
                cv.separationAU = comp.projectedSepAU;
                cv.orbitRadius = static_cast<float>(Display::Magnitude(comp.projectedSepAU, logScale));
                double m1 = sys.starMassSolar > 0.0 ? sys.starMassSolar : 1.0;
                double m2 = comp.massSolar > 0.0 ? comp.massSolar : 0.0;
                double a = comp.projectedSepAU;
                cv.periodDays = std::sqrt(a * a * a / (m1 + m2)) * 365.25;
            }
            if (comp.hasData) {
                StarClassification compCls = StarClassifier::Classify(comp.teffK, comp.radiusSolar, comp.spectralType);
                cv.color = compCls.color;
                double compRealAU = compCls.displayRadiusSolar * kSolarRadiusAU;
                cv.displayRadius = static_cast<float>(std::min(compRealAU * exaggeration, minDisplay * 0.3));
                cv.displayRadius = std::max(cv.displayRadius, m_starDisplayRadius * 0.3f);
                cv.hasData = true;
                cv.label = compCls.spectralClass;
            } else {
                cv.color = glm::vec3(0.55f, 0.55f, 0.6f);
                cv.displayRadius = m_starDisplayRadius * 0.6f;
                cv.hasData = false;
                cv.label = "Unknown companion (no resolved stellar data)";
            }
            m_companions.push_back(cv);
        }
    }

    m_guideRings.clear();
    if (logScale) {
        for (double refAU : {1.0, 10.0, 100.0, 1000.0}) {
            if (refAU < maxA * 0.05 || refAU > maxA * 20.0) continue;
            GuideRing gr;
            gr.realAU = refAU;
            double r = Display::Magnitude(refAU, true);
            constexpr int kRingSegs = 96;
            for (int s = 0; s <= kRingSegs; ++s) {
                double phi = 2.0 * kPi * s / kRingSegs;
                gr.points.emplace_back(static_cast<float>(r * std::cos(phi)), 0.0f, static_cast<float>(r * std::sin(phi)));
            }
            m_guideRings.push_back(std::move(gr));
            if (m_guideRings.size() >= 3) break;
        }
    }

    float fitDistance = static_cast<float>(std::max(maxDisplay, 0.5)) * 2.5f;
    ctx.cam.systemDistanceTarget = fitDistance;
    ctx.cam.fitDistance = fitDistance;
    ctx.cam.followingCraft = false;
    if (snap) ctx.cam.systemDistance = fitDistance;

    m_builtSystem = sim.systemIndex();
    m_builtLog = logScale;
    m_builtExaggeration = exaggeration;
    rebuildSky(ctx); // the sky as seen from THIS system
}

void SystemScene::updateCraft(AppContext& ctx) {
    const SimulationState& sim = ctx.sim;
    const bool craftOnArc = sim.missionActive() && !sim.mission().earthMoonMission && sim.currentSystem().isHome;
    if (craftOnArc) {
        m_craftDisplayPos = Display::ToDisplay(sim.mission().positionAt(sim.day()), ctx.view.logScale);
    } else {
        ctx.cam.followingCraft = false;
    }
}

void SystemScene::render(AppContext& ctx) {
    const SimulationState& sim = ctx.sim;
    const StarSystem& currentSystem = sim.currentSystem();
    const bool logScale = ctx.view.logScale;
    const double simTimeDays = sim.day();
    const float now = static_cast<float>(ctx.now);
    CameraState& cam = ctx.cam;
    Renderer& renderer = ctx.renderer;
    const float aspect = static_cast<float>(ctx.width) / static_cast<float>(ctx.height);

    glm::vec3 camTarget = cam.followingCraft ? m_craftDisplayPos : glm::vec3(0.0f);
    glm::vec3 eye = camTarget + cam.orbitEye(cam.systemDistance);
    glm::mat4 view = glm::lookAt(eye, camTarget, glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, std::min(0.01f, cam.systemDistance * 0.01f),
                                      cam.systemDistance * 50.0f + 50.0f);
    glm::mat4 viewProj = proj * view;

    glm::mat4 viewRotOnly = glm::mat4(glm::mat3(view));
    glm::mat4 viewProjRotFwd = glm::perspective(glm::radians(45.0f), aspect, 0.01f, 100.0f) * viewRotOnly;
    renderer.renderSkybox(glm::inverse(viewProjRotFwd), now, static_cast<float>(currentSystem.galacticLatDeg),
                          static_cast<float>(currentSystem.galacticLonDeg), Format::HashSeed(currentSystem.hostName));
    renderer.renderStarCatalog(viewProjRotFwd);

    std::vector<glm::vec3> positions(currentSystem.planetIndices.size());
    for (size_t local = 0; local < currentSystem.planetIndices.size(); ++local) {
        size_t pIdx = currentSystem.planetIndices[local];
        positions[local] = Display::ToDisplay(sim.orbits()[pIdx].positionAtTime(simTimeDays), logScale);
    }
    renderer.render(viewProj, positions);

    renderer.renderStar(viewProj, glm::scale(glm::mat4(1.0f), glm::vec3(m_starDisplayRadius)), m_star.color);

    for (const auto& comp : m_companions) {
        // Real period when the separation is real; otherwise a stylized speed
        // (slower for larger schematic orbits).
        float angle = comp.realSeparation && comp.periodDays > 0.0
                          ? comp.baseAngle + static_cast<float>(2.0 * kPi * std::fmod(simTimeDays / comp.periodDays, 1.0))
                          : comp.baseAngle + now * (0.15f / comp.orbitRadius);
        glm::vec3 compPos(comp.orbitRadius * std::cos(angle), 0.0f, comp.orbitRadius * std::sin(angle));
        glm::mat4 compModel =
            glm::translate(glm::mat4(1.0f), compPos) * glm::scale(glm::mat4(1.0f), glm::vec3(comp.displayRadius));
        renderer.renderStar(viewProj, compModel, comp.color);
    }

    for (const auto& gr : m_guideRings) renderer.renderAuxiliaryRing(viewProj, gr.points, glm::vec3(0.5f, 0.5f, 0.55f), 0.25f);

    // Mission trajectory (Sol only; Earth-Moon transfers are too small to see at AU scale).
    if (!(sim.missionActive() && currentSystem.isHome && !sim.mission().earthMoonMission)) return;
    const MissionPlan& m = sim.mission();
    std::vector<glm::vec3> arc;
    const int kArcSegs = 128 * std::max<int>(1, static_cast<int>(m.legs.size()));
    for (int k = 0; k <= kArcSegs; ++k)
        arc.push_back(Display::ToDisplay(m.positionAt(m.launchDay + m.transferDays * k / kArcSegs), logScale));
    for (int k = kArcSegs; k >= 0; --k) arc.push_back(arc[k]); // retrace: no closing chord on the line loop
    renderer.renderAuxiliaryRing(viewProj, arc, glm::vec3(1.0f, 0.65f, 0.15f), 0.45f);

    // Flyby markers: a diamond at each closest approach.
    if (const FlybyPlan* fp = sim.flyby()) {
        std::vector<glm::vec3> marks;
        float d = cam.systemDistance * 0.012f;
        for (const FlybyEncounter& f : fp->flybys) {
            glm::vec3 c = Display::ToDisplay(m.positionAt(f.day), logScale);
            marks.insert(marks.end(),
                         {c + glm::vec3(d, 0, 0), c + glm::vec3(0, 0, d), c + glm::vec3(0, 0, d), c - glm::vec3(d, 0, 0),
                          c - glm::vec3(d, 0, 0), c - glm::vec3(0, 0, d), c - glm::vec3(0, 0, d), c + glm::vec3(d, 0, 0)});
        }
        renderer.renderLineSegments(viewProj, marks, glm::vec3(0.4f, 0.9f, 1.0f), 0.9f);
    }

    double tRel = std::clamp(simTimeDays - m.launchDay, 0.0, m.transferDays);
    glm::vec3 craft = m_craftDisplayPos;
    glm::vec3 ahead = Display::ToDisplay(m.positionAt(m.launchDay + std::min(tRel + 1.0, m.transferDays)), logScale);
    glm::vec3 dir = glm::length(ahead - craft) > 1e-6f ? glm::normalize(ahead - craft) : glm::vec3(1, 0, 0);
    glm::vec3 side = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)) + glm::vec3(1e-6f));
    float sz = cam.systemDistance * 0.018f; // constant on-screen size
    std::vector<glm::vec3> glyph = {
        craft + dir * sz * 1.2f,
        craft + dir * sz * 0.5f + side * sz * 0.25f,
        craft - dir * sz * 0.8f + side * sz * 0.25f,
        craft - dir * sz * 1.2f + side * sz * 0.6f,
        craft - dir * sz * 1.0f,
        craft - dir * sz * 1.2f - side * sz * 0.6f,
        craft - dir * sz * 0.8f - side * sz * 0.25f,
        craft + dir * sz * 0.5f - side * sz * 0.25f,
    };
    renderer.renderAuxiliaryRing(viewProj, glyph, glm::vec3(1.0f, 0.9f, 0.5f), 1.0f);
    if (cam.followingCraft) {
        float b = sz * 2.2f;
        glm::vec3 camRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
        glm::vec3 camUp = glm::vec3(view[0][1], view[1][1], view[2][1]);
        std::vector<glm::vec3> br;
        for (int cx : {-1, 1})
            for (int cy : {-1, 1}) {
                glm::vec3 c = craft + camRight * (b * cx) + camUp * (b * cy);
                br.push_back(c);
                br.push_back(c - camRight * (b * 0.5f * cx));
                br.push_back(c);
                br.push_back(c - camUp * (b * 0.5f * cy));
            }
        renderer.renderLineSegments(viewProj, br, glm::vec3(0.3f, 1.0f, 0.5f), 0.9f);
    }

    // Click-to-follow: project the craft to window coordinates (which may
    // differ from framebuffer pixels on high-DPI displays).
    if (ctx.click.pending && ctx.window) {
        glm::vec4 clip = viewProj * glm::vec4(craft, 1.0f);
        int winW = 1, winH = 1;
        glfwGetWindowSize(ctx.window, &winW, &winH);
        if (clip.w > 0.0f) {
            glm::vec2 ndc(clip.x / clip.w, clip.y / clip.w);
            double sx = (ndc.x * 0.5 + 0.5) * winW, sy = (1.0 - (ndc.y * 0.5 + 0.5)) * winH;
            if (std::hypot(sx - ctx.click.x, sy - ctx.click.y) < 22.0) cam.startFollowing();
        }
    }
}
