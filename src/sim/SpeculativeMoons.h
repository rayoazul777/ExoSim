#pragma once
#include <vector>
#include "Dataset.h"
#include "PlanetClassifier.h"

// NASA's Exoplanet Archive contains no confirmed exomoon detections. For an
// Archive planet this generates a deterministic set of purely illustrative
// "candidate" moons - never real data, always labeled as such where shown.
// Count and placement are seeded from the planet's name, biased loosely by
// composition (giants get more, following the Solar System pattern).
std::vector<MoonRecord> GenerateSpeculativeMoons(PlanetType type, float seed);
