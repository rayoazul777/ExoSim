#include "scene/Scenes.h"
#include "app/AppContext.h"
#include "KeplerOrbit.h"
#include "LandingSimulator.h"
#include "Renderer.h"
#include "SolarSystemAppearance.h"
#include "sim/DisplaySpace.h"
#include "sim/Formatting.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

void RenderDetailScene(AppContext& ctx) {
    const SimulationState& sim = ctx.sim;
    const DetailView& det = ctx.view.detail;
    CameraState& cam = ctx.cam;
    Renderer& renderer = ctx.renderer;
    const double now = ctx.now;
    const float aspect = static_cast<float>(ctx.width) / static_cast<float>(ctx.height);
    glm::mat4 proj, view;
    glm::vec3 eye;
    eye = cam.orbitEye(cam.planetDistance);
    view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0, 1, 0));
    proj = glm::perspective(glm::radians(45.0f), aspect, 0.01f, 100.0f);
    glm::mat4 viewProj = proj * view;

    glm::mat4 viewRotOnly = glm::mat4(glm::mat3(view));
    glm::mat4 viewProjRotFwd = proj * viewRotOnly;
    glm::mat4 invViewProjRot = glm::inverse(viewProjRotFwd);
    const StarSystem& detailSystem = sim.currentSystem();
    renderer.renderSkybox(invViewProjRot, static_cast<float>(now), static_cast<float>(detailSystem.galacticLatDeg),
                          static_cast<float>(detailSystem.galacticLonDeg), Format::HashSeed(detailSystem.hostName));
    renderer.renderStarCatalog(viewProjRotFwd);

    // Real axial tilt (e.g. Uranus rolling on its side) for known
    // Solar System bodies; untilted for generic exoplanets, since
    // we have no real obliquity data for those.
    glm::mat4 tilt = det.appearance.isKnown
                         ? glm::rotate(glm::mat4(1.0f), glm::radians(det.appearance.axialTiltDeg), glm::vec3(0.0f, 0.0f, 1.0f))
                         : glm::mat4(1.0f);
    glm::mat4 spin = glm::rotate(glm::mat4(1.0f), ctx.view.planetSpinAngle, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 model = tilt * spin;
    const glm::vec3 phosphorGreen(0.15f, 0.95f, 0.55f); // matches the orbit-ring accent color

    // Zoom-driven detail: each halving of the distance to the surface
    // adds one fractal octave (5 at a comfortable distance, up to 11
    // right at the surface) - this is what keeps close-ups sharp.
    float surfaceGap = std::max(cam.planetDistance - 1.0f, 0.01f);
    renderer.setSurfaceDetail(std::clamp(5.0f + std::log2(2.0f / surfaceGap), 5.0f, 11.0f));

    bool inspectingMoon = det.moonIndex >= 0 && det.moonIndex < static_cast<int>(det.moons.size());
    if (inspectingMoon) {
        const MoonRecord& moon = det.moons[det.moonIndex];
        PlanetAppearance moonLook = moon.isReal ? SolarSystemAppearance::Lookup(moon.name) : PlanetAppearance{};
        glm::mat4 moonTilt = moonLook.isKnown
                                 ? glm::rotate(glm::mat4(1.0f), glm::radians(moonLook.axialTiltDeg), glm::vec3(0.0f, 0.0f, 1.0f))
                                 : glm::mat4(1.0f);
        glm::mat4 moonModelMain = moonTilt * spin;
        renderer.setSurfaceEqTemp(0.0f);
        // Rocky/icy generic look for moons without a real appearance
        // (speculative exomoons): illustrative only, labeled in the HUD.
        renderer.renderPlanetSurface(viewProj, moonModelMain, eye, 1, glm::vec3(0.62f, 0.60f, 0.58f), phosphorGreen,
                                     static_cast<float>(now), det.surfaceSeed + static_cast<float>(det.moonIndex) * 7.0f,
                                     moonLook.isKnown ? &moonLook : nullptr);
        renderer.renderPlanetDetail(viewProj, glm::scale(moonModelMain, glm::vec3(1.01f)), phosphorGreen * 0.8f);
    } else {
        renderer.setSurfaceEqTemp(static_cast<float>(sim.planets()[det.planetIdx].eqTempK));
        renderer.renderPlanetSurface(viewProj, model, eye, static_cast<int>(det.classification.type),
                                     det.classification.displayColor, phosphorGreen, static_cast<float>(now), det.surfaceSeed,
                                     det.appearance.isKnown ? &det.appearance : nullptr);

        // Wireframe targeting-grid overlay, very slightly inflated so
        // it doesn't z-fight with the solid surface underneath.
        glm::mat4 gridModel = glm::scale(model, glm::vec3(1.01f));
        renderer.renderPlanetDetail(viewProj, gridModel, phosphorGreen * 0.8f);

        // Rings (Saturn, Uranus) - drawn in the SAME tilted plane as
        // the planet (not spinning with it, since rings are
        // azimuthally uniform and spin wouldn't be visible anyway).
        if (det.appearance.isKnown && det.appearance.hasRings) {
            renderer.renderPlanetRings(viewProj, tilt, det.appearance.ringBands, det.appearance.ringColor, det.surfaceSeed);
        }

        // Moons: real ones for the demo solar system, otherwise
        // speculative/illustrative ones (see generateSpeculativeMoons).
        // Distances are compressed with a log curve into a viewable
        // range - real moon distances (tens of planet-radii) would
        // otherwise put them far outside the camera's usual zoom range.
        // Positions are propagated with the same KeplerOrbit math used
        // for planets, so inclination and eccentricity genuinely tilt
        // and distort each moon's path rather than a flat circle.
        renderer.setSurfaceDetail(5.0f); // moons here are small on screen
        renderer.setSurfaceEqTemp(0.0f);
        for (size_t mi = 0; mi < det.moons.size(); ++mi) {
            const MoonRecord& moon = det.moons[mi];
            float dispR = 1.3f + 0.9f * std::log10(1.0f + static_cast<float>(moon.orbitRadiusPlanetRadii));

            OrbitalElements moonElements;
            moonElements.semiMajorAxisAU = dispR; // repurposed as a display-space distance, not real AU
            moonElements.eccentricity = moon.eccentricity;
            moonElements.inclinationDeg = moon.inclinationDeg;
            moonElements.argPeriapsisDeg = moon.argPeriapsisDeg;
            moonElements.longAscNodeDeg = moon.longAscNodeDeg;
            moonElements.periodDays = std::max(moon.orbitPeriodDays, 0.1);
            moonElements.meanAnomalyAtEpochDeg = static_cast<double>(mi) * 61.0 + det.surfaceSeed;
            KeplerOrbit moonOrbit(moonElements);

            double fastDays = static_cast<double>(now) * 15.0; // stylized playback speed, not real-time
            glm::vec3 moonPos(Display::EclipticToWorld(moonOrbit.positionAtTime(fastDays)));

            glm::mat4 moonModel = glm::translate(glm::mat4(1.0f), moonPos) * glm::scale(glm::mat4(1.0f), glm::vec3(0.12f));

            PlanetAppearance moonAppearance = moon.isReal ? SolarSystemAppearance::Lookup(moon.name) : PlanetAppearance{};
            if (moonAppearance.isKnown) {
                // Real, well-photographed moon (Io, Europa, Titan, etc.)
                // - same solid-surface + wireframe-grid pipeline as planets.
                float moonSeed = det.surfaceSeed + static_cast<float>(mi) * 7.0f;
                renderer.renderPlanetSurface(viewProj, moonModel, eye, 0, moonAppearance.primaryColor, phosphorGreen,
                                             static_cast<float>(now), moonSeed, &moonAppearance);
                glm::mat4 moonGridModel = glm::scale(moonModel, glm::vec3(1.03f));
                renderer.renderPlanetDetail(viewProj, moonGridModel, phosphorGreen * 0.7f, 0.4f);
            } else {
                // Speculative moon, or a real one we haven't hand-modeled
                // yet - plain wireframe dot, dimmer to signal lower certainty.
                glm::vec3 moonColor = moon.isReal ? glm::vec3(0.8f, 0.85f, 0.95f) : glm::vec3(0.6f, 0.6f, 0.65f);
                float moonAlpha = moon.isReal ? 0.7f : 0.4f;
                renderer.renderPlanetDetail(viewProj, moonModel, moonColor, moonAlpha);
            }

            // Orbit path guide, sampled across a full period so it
            // traces the real tilted/eccentric ellipse, not a flat circle.
            glm::vec3 guideColor = moon.isReal ? glm::vec3(0.8f, 0.85f, 0.95f) : glm::vec3(0.6f, 0.6f, 0.65f);
            float guideAlpha = moon.isReal ? 0.7f : 0.4f;
            std::vector<glm::vec3> orbitPts;
            constexpr int kOrbitSegs = 64;
            for (int s = 0; s <= kOrbitSegs; ++s) {
                double t = moonElements.periodDays * s / kOrbitSegs;
                orbitPts.emplace_back(Display::EclipticToWorld(moonOrbit.positionAtTime(t)));
            }
            renderer.renderAuxiliaryRing(viewProj, orbitPts, guideColor, guideAlpha * 0.4f);
        }
    } // end: planet (not moon) inspection
}

void RenderLandingScene(AppContext& ctx) {
    const SimulationState& sim = ctx.sim;
    CameraState& cam = ctx.cam;
    Renderer& renderer = ctx.renderer;
    const double now = ctx.now;
    const float aspect = static_cast<float>(ctx.width) / static_cast<float>(ctx.height);
    glm::mat4 proj, view;
    glm::vec3 eye;
    // ---------------- Landing view ----------------
    // Units: 1 = the landing body's radius. Everything is positioned
    // relative to the craft in double precision before converting to
    // float ("relative-to-eye"), so metre-scale motion stays smooth
    // on a body thousands of km across. The descent's great-circle
    // plane is laid out with the touchdown site at +Z, travel toward +X.
    const LandingResult& L = sim.timeline().landing;
    const double R = std::max(L.bodyRadiusM, 1.0);
    const double tLand = (sim.day() - sim.timeline().landingStartDay) * 86400.0;
    const LandingSample cur = LandingSimulator::At(L, tLand);
    const double thetaF = L.samples.empty() ? 0.0 : L.samples.back().angleRad;
    auto dirAt = [&](double ang) {
        double d = ang - thetaF;
        return glm::dvec3(std::sin(d), 0.0, std::cos(d));
    };
    auto fwdAt = [&](double ang) {
        double d = ang - thetaF;
        return glm::dvec3(std::cos(d), 0.0, -std::sin(d));
    };
    auto posAt = [&](double ang, double altM) { return dirAt(ang) * (1.0 + altM / R); };
    const glm::dvec3 craftP = posAt(cur.angleRad, cur.altitudeM);
    const glm::dvec3 U = dirAt(cur.angleRad), F = fwdAt(cur.angleRad), S = glm::cross(F, U);
    auto rel = [&](const glm::dvec3& p) { return glm::vec3(p - craftP); };

    // Chase camera: distance follows altitude (zooming in as it
    // descends), scaled by the user's scroll zoom; drag orbits it.
    const double camDistM = std::max(30.0, 0.6 * cur.altitudeM) * cam.landingZoom;
    const double camDist = camDistM / R;
    const double cp = std::cos(static_cast<double>(cam.pitch)), sp = std::sin(static_cast<double>(cam.pitch));
    const double cy = std::cos(static_cast<double>(cam.yaw)), sy = std::sin(static_cast<double>(cam.yaw));
    glm::dvec3 off = (F * (cp * cy) + U * sp + S * (cp * sy)) * camDist;
    glm::dvec3 eyeD = craftP + off;
    if ((glm::length(eyeD) - 1.0) * R < 2.0) eyeD = glm::normalize(eyeD) * (1.0 + 2.0 / R); // stay above ground
    eye = glm::vec3(eyeD - craftP);
    view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(U));
    double eyeAltR = std::max(glm::length(eyeD) - 1.0, 1.0 / R);
    double horizon = std::sqrt(2.0 * eyeAltR + eyeAltR * eyeAltR);
    float farP = static_cast<float>(std::min(camDist + horizon * 1.3 + 4.0 / R, 4.0));
    proj = glm::perspective(glm::radians(50.0f), aspect, static_cast<float>(camDist * 0.02), farP);
    glm::mat4 viewProj = proj * view;

    glm::mat4 viewRotOnly = glm::mat4(glm::mat3(view));
    glm::mat4 viewProjRotFwd = glm::perspective(glm::radians(50.0f), aspect, 0.01f, 100.0f) * viewRotOnly;
    renderer.renderSkybox(glm::inverse(viewProjRotFwd), static_cast<float>(now), 0.0f, 0.0f, 7.0f);
    renderer.renderStarCatalog(viewProjRotFwd);

    // Surface: whole body slightly sunk (its facets sag below the true
    // sphere), then a dense cap mesh around the site at the true radius.
    PlanetAppearance look = SolarSystemAppearance::Lookup(L.bodyName);
    glm::mat4 bodyModel = glm::mat4(glm::translate(glm::dmat4(1.0), -craftP));
    renderer.setSurfaceEqTemp(0.0f);
    renderer.setSurfaceDetail(std::clamp(5.0f + static_cast<float>(std::log2(0.5 / std::max(camDist, 1e-9))), 5.0f, 11.0f));
    renderer.renderPlanetSurface(viewProj, glm::scale(bodyModel, glm::vec3(0.9995f)), eye, 1, glm::vec3(0.6f, 0.58f, 0.55f),
                                 glm::vec3(0.15f, 0.95f, 0.55f), static_cast<float>(now), 3.0f, look.isKnown ? &look : nullptr);
    renderer.setSurfaceCapMode(true);
    renderer.renderPlanetSurface(viewProj, bodyModel, eye, 1, glm::vec3(0.6f, 0.58f, 0.55f), glm::vec3(0.15f, 0.95f, 0.55f),
                                 static_cast<float>(now), 3.0f, look.isKnown ? &look : nullptr);
    renderer.setSurfaceCapMode(false);

    // Terrain grid (vector-arcade style), spacing adapts to zoom: a
    // power of ten metres. Lines follow the sphere, lifted slightly.
    {
        const double spacingM = std::pow(10.0, std::floor(std::log10(std::max(camDistM * 1.5, 5.0))));
        const double lift = 1.0 + std::max(0.3, 0.003 * camDistM) / R;
        const double step = spacingM / R;
        const int N = 14;
        double lamC = std::round((cur.angleRad - thetaF) / step) * step;
        auto gridPt = [&](double lam, double phi) {
            return rel(glm::dvec3(std::cos(phi) * std::sin(lam), std::sin(phi), std::cos(phi) * std::cos(lam)) * lift);
        };
        std::vector<glm::vec3> seg;
        for (int i = -N; i <= N; ++i) {
            for (int j = -N; j < N; ++j) {
                seg.push_back(gridPt(lamC + i * step, j * step));
                seg.push_back(gridPt(lamC + i * step, (j + 1) * step));
                seg.push_back(gridPt(lamC + j * step, i * step));
                seg.push_back(gridPt(lamC + (j + 1) * step, i * step));
            }
        }
        renderer.renderLineSegments(viewProj, seg, glm::vec3(0.15f, 0.95f, 0.55f), 0.28f);

        // Landing target (guidance target for powered landers; the
        // simulated touchdown point for parachute probes).
        double lamT = L.powered ? L.targetAngleRad - thetaF : 0.0;
        double rM = std::max(8.0, camDistM * 0.06);
        std::vector<glm::vec3> ring;
        for (int k = 0; k < 48; ++k) {
            double a = 2.0 * 3.14159265358979323846 * k / 48.0;
            ring.push_back(gridPt(lamT + rM / R * std::cos(a), rM / R * std::sin(a)));
        }
        glm::vec3 tgtCol = L.powered ? glm::vec3(0.3f, 1.0f, 0.4f) : glm::vec3(0.6f, 0.7f, 0.7f);
        renderer.renderAuxiliaryRing(viewProj, ring, tgtCol, 0.9f);
        renderer.renderLineSegments(
            viewProj, {gridPt(lamT - rM / R, 0.0), gridPt(lamT + rM / R, 0.0), gridPt(lamT, -rM / R), gridPt(lamT, rM / R)},
            tgtCol, 0.9f);
    }

    // Trajectory: flown (orange) and still to come (dim) - the whole
    // descent was simulated up front, so the future path is known.
    {
        std::vector<glm::vec3> past, future;
        size_t stride = std::max<size_t>(1, L.samples.size() / 3000);
        for (size_t k = 0; k < L.samples.size(); k += stride) {
            const auto& sm = L.samples[k];
            (sm.t <= tLand ? past : future).push_back(rel(posAt(sm.angleRad, sm.altitudeM)));
        }
        past.push_back(glm::vec3(0.0f));
        if (!future.empty()) future.insert(future.begin(), glm::vec3(0.0f));
        renderer.renderLineStrip(viewProj, past, glm::vec3(1.0f, 0.6f, 0.15f), 0.85f);
        renderer.renderLineStrip(viewProj, future, glm::vec3(0.6f, 0.65f, 0.7f), 0.25f);
    }

    // Vehicle glyph, in metres along a body axis `ax` (+ = aft/up) and
    // a radial direction; enlarged when far away so it stays visible.
    {
        const double k = std::max(1.0, camDistM * 0.012) / R;
        const glm::vec3 col(1.0f, 0.9f, 0.5f);
        auto P = [&](const glm::dvec3& ax, const glm::dvec3& rad, double a, double r) {
            return glm::vec3((ax * a + rad * r) * k);
        };
        glm::dvec3 vel = F * cur.horizontalMs + U * cur.verticalMs;
        bool capsule = cur.phase == DescentPhase::Entry || cur.phase == DescentPhase::Parachute;
        if (capsule || (!L.powered && cur.phase != DescentPhase::Deorbit)) {
            glm::dvec3 ax = glm::length(vel) > 0.1 ? -glm::normalize(vel) : U; // heat shield faces the flow
            if (cur.phase == DescentPhase::Landed || cur.phase == DescentPhase::Crashed) ax = U;
            glm::dvec3 n = glm::normalize(glm::cross(ax, S));
            if (glm::length(glm::cross(ax, S)) < 1e-6) n = F;
            const double shape[][2] = {{0.0, 0.0},  {0.15, 1.2}, {0.5, 2.25},  {2.6, 1.0},   {3.0, 0.6},
                                       {3.0, -0.6}, {2.6, -1.0}, {0.5, -2.25}, {0.15, -1.2}, {0.0, 0.0}};
            for (const glm::dvec3& rd : {n, S}) {
                std::vector<glm::vec3> o;
                for (const auto& sp : shape) o.push_back(P(ax, rd, sp[0], sp[1]));
                renderer.renderLineStrip(viewProj, o, col, 1.0f);
            }
            if (cur.heatShieldAttached && cur.heatFluxWcm2 > 1.0 && L.peakHeatFluxWcm2 > 0.0) {
                float glow = static_cast<float>(std::min(1.0, 1.5 * cur.heatFluxWcm2 / L.peakHeatFluxWcm2));
                for (int shell = 0; shell < 3; ++shell) {
                    std::vector<glm::vec3> arc;
                    for (int q = -10; q <= 10; ++q) {
                        double r = q * 0.4 * (1.0 + 0.3 * shell);
                        arc.push_back(P(ax, n, -0.8 - 0.6 * shell + 0.12 * r * r / (1.0 + 0.3 * shell), r));
                    }
                    renderer.renderLineStrip(viewProj, arc, glm::vec3(1.0f, 0.45f - 0.1f * shell, 0.15f),
                                             glow * (1.0f - 0.25f * shell));
                }
            }
            if (cur.chuteDeployed) {
                const double c = 30.0, rc = 9.0; // canopy distance & radius (schematic)
                for (const glm::dvec3& rd : {n, S}) {
                    std::vector<glm::vec3> dome, lines;
                    for (int q = 0; q <= 16; ++q) {
                        double tt = 3.14159265358979323846 * q / 16.0;
                        dome.push_back(P(ax, rd, c + 0.4 * rc * std::sin(tt), rc * std::cos(tt)));
                    }
                    renderer.renderLineStrip(viewProj, dome, glm::vec3(0.95f, 0.95f, 1.0f), 0.9f);
                    for (double sr : {-1.0, 1.0}) {
                        lines.push_back(P(ax, rd, 3.0, 0.6 * sr));
                        lines.push_back(P(ax, rd, c, rc * sr));
                    }
                    renderer.renderLineSegments(viewProj, lines, glm::vec3(0.8f, 0.8f, 0.85f), 0.6f);
                }
            }
        } else {
            // Powered lander: body, legs, engine plume; tilted along the thrust vector.
            glm::dvec3 ax = U;
            if (cur.throttle > 0.02) ax = glm::normalize(F * cur.thrustDirX + U * cur.thrustDirY);
            glm::dvec3 n = glm::normalize(glm::cross(ax, S));
            for (const glm::dvec3& rd : {n, S}) {
                std::vector<glm::vec3> body = {P(ax, rd, 1.0, -1.2), P(ax, rd, 1.0, 1.2), P(ax, rd, 2.6, 1.2),
                                               P(ax, rd, 2.6, -1.2), P(ax, rd, 1.0, -1.2)};
                renderer.renderLineStrip(viewProj, body, col, 1.0f);
                std::vector<glm::vec3> legs;
                for (double sr : {-1.0, 1.0}) {
                    legs.push_back(P(ax, rd, 1.1, 1.0 * sr));
                    legs.push_back(P(ax, rd, 0.0, 2.0 * sr));
                    legs.push_back(P(ax, rd, 0.0, 1.7 * sr));
                    legs.push_back(P(ax, rd, 0.0, 2.3 * sr));
                }
                renderer.renderLineSegments(viewProj, legs, col, 1.0f);
                if (cur.throttle > 0.02) {
                    double len = (2.0 + 9.0 * cur.throttle) * (0.85 + 0.15 * std::sin(now * 47.0));
                    std::vector<glm::vec3> plume = {P(ax, rd, 0.95, -0.45), P(ax, rd, 0.95 - len, 0.0), P(ax, rd, 0.95, 0.45)};
                    renderer.renderLineStrip(viewProj, plume, glm::vec3(1.0f, 0.55f, 0.15f), 0.95f);
                }
            }
        }
    }
}
