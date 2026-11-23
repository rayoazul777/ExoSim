#include "app/ViewState.h"
#include "sim/Formatting.h"
#include "sim/SimulationState.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

void ViewState::enterDetail(const SimulationState& sim, size_t planetIdx, int moonIndex) {
    const PlanetRecord& p = sim.planets()[planetIdx];
    detail.planetIdx = planetIdx;
    detail.classification = PlanetClassifier::Classify(p);
    detail.appearance = SolarSystemAppearance::Lookup(p.name);
    detail.surfaceSeed = Format::HashSeed(p.name);
    detail.moons = sim.moonsFor(planetIdx);
    detail.moonsReal = !p.moons.empty();
    detail.moonIndex = moonIndex < static_cast<int>(detail.moons.size()) ? moonIndex : -1;
    mode = ViewMode::PlanetDetail;
}

glm::vec3 CameraState::orbitEye(float distance) const {
    return glm::vec3(distance * std::cos(pitch) * std::cos(yaw), distance * std::sin(pitch),
                     distance * std::cos(pitch) * std::sin(yaw));
}

void CameraState::startFollowing() {
    followingCraft = true;
    systemDistanceTarget = systemDistance = std::min(systemDistance, 0.6f);
}

void CameraState::stopFollowing() {
    followingCraft = false;
    systemDistanceTarget = fitDistance;
}

glm::dmat4 GalaxyView::viewProj(double aspect) const {
    if (topDown) {
        const double halfH = distance * kTanHalfFov, halfW = halfH * aspect;
        // From the NGP, Galactic Center (+X) up: l = 90 deg falls to the left, as in standard maps.
        glm::dmat4 view = glm::lookAt(target + glm::dvec3(0.0, 0.0, distance), target, glm::dvec3(1.0, 0.0, 0.0));
        return glm::ortho(-halfW, halfW, -halfH, halfH, -100000.0, distance + 100000.0) * view;
    }
    glm::dvec3 eye =
        target + distance * glm::dvec3(std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch));
    glm::dmat4 view = glm::lookAt(eye, target, glm::dvec3(0.0, 0.0, 1.0));
    return glm::perspective(2.0 * std::atan(kTanHalfFov), aspect, distance * 0.001, distance * 10.0 + 100000.0) * view;
}

void GalaxyView::screenBasis(int winH, glm::dvec3& rightPerPx, glm::dvec3& upPerPx) const {
    const double perPx = 2.0 * distance * kTanHalfFov / std::max(winH, 1);
    if (topDown) {
        rightPerPx = glm::dvec3(0.0, -perPx, 0.0);
        upPerPx = glm::dvec3(perPx, 0.0, 0.0);
        return;
    }
    glm::dvec3 fwd = -glm::dvec3(std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch));
    glm::dvec3 right = glm::normalize(glm::cross(fwd, glm::dvec3(0.0, 0.0, 1.0)));
    glm::dvec3 up = glm::cross(right, fwd);
    rightPerPx = right * perPx;
    upPerPx = up * perPx;
}

void GalaxyView::pan(double dxPx, double dyPx, int winH) {
    glm::dvec3 r, u;
    screenBasis(winH, r, u);
    target += -r * dxPx + u * dyPx;
    targetGoal = target;
}

void GalaxyView::zoomAt(double factor, double sx, double sy, int winW, int winH) {
    const double newDist = std::clamp(distance * factor, kMinDistancePc, kMaxDistancePc);
    const double f = newDist / distance;
    if (topDown) {
        glm::dvec3 r, u;
        screenBasis(winH, r, u);
        glm::dvec3 under = target + r * (sx - winW * 0.5) + u * (winH * 0.5 - sy);
        target = under - (under - target) * f;
    }
    distance = distanceGoal = newDist;
    targetGoal = target;
}

void GalaxyView::rotate(double dxPx, double dyPx) {
    yaw -= dxPx * 0.006;
    pitch = std::clamp(pitch + dyPx * 0.006, 0.02, 1.55);
}

void GalaxyView::flyTo(const glm::dvec3& pc, double distancePc) {
    targetGoal = pc;
    distanceGoal = std::clamp(distancePc, kMinDistancePc, kMaxDistancePc);
}

void GalaxyView::update(float dt) {
    const double k = 1.0 - std::exp(-static_cast<double>(dt) * 4.0);
    target += (targetGoal - target) * k;
    // Ease zoom in log space so a 40 kpc -> 20 pc flight reads as a smooth dive.
    distance = std::exp(std::log(distance) + (std::log(distanceGoal) - std::log(distance)) * k);
}
