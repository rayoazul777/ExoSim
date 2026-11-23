#pragma once
#include <vector>
#include <glm/glm.hpp>
#include "Dataset.h"

// The visible sky from any star system: Hipparcos stars re-projected from
// the observer's real position (parallax), moved by real proper motion to
// the chosen epoch, and dimmed or brightened by the inverse-square law.
// Pure (no GL): the renderer receives the result via setStarCatalog().
//
//  - Observer: from an exoplanet system, stars with a real parallax are
//    placed in 3-D (distance = 1000/parallax pc) and re-projected from the
//    system's position (host RA/Dec + distance); magnitude changes by
//    5*log10(newDist/oldDist). Stars without usable parallax stay fixed.
//    Our Sun is added when bright enough to see.
//  - Epoch: proper motion is applied linearly from J1991.25 (ignores radial
//    velocity and perspective acceleration, which matter over long spans).
namespace Sky {

// Real B-V color index -> approximate RGB (piecewise-linear anchors from hot
// blue-white to cool red). The input is measured; the mapping is a standard
// visualization approximation, not a blackbody calculation.
glm::vec3 BvToColor(double bv);
// Point size / brightness from visual magnitude: linear in magnitude, tuned
// by eye for a ~1280x800 window.
float VmagToPointSize(double vmag);
float VmagToBrightness(double vmag);

struct SkyView {
    std::vector<glm::vec3> directions, colors;
    std::vector<float> sizes;
    int visibleCount = 0;
    bool sunVisible = false;
    double sunApparentVmag = 0.0;
};

constexpr double kVisibleLimitVmag = 7.5;

// nowDaysSinceJ2000: the date proper motion is applied to (plus the offset).
SkyView Compute(const std::vector<StarCatalogEntry>& catalog, const StarSystem& observerSystem, double nowDaysSinceJ2000,
                double epochOffsetYears);

} // namespace Sky
