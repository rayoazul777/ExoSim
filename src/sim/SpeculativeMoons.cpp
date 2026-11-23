#include "sim/SpeculativeMoons.h"
#include <cmath>
#include <string>

std::vector<MoonRecord> GenerateSpeculativeMoons(PlanetType type, float seed) {
    int count = 0;
    switch (type) {
        case PlanetType::GasGiant: count = 2 + (static_cast<int>(seed) % 3); break; // 2-4
        case PlanetType::IceGiant: count = 1 + (static_cast<int>(seed) % 3); break; // 1-3
        case PlanetType::MiniNeptune: count = 1 + (static_cast<int>(seed) % 2); break; // 1-2
        case PlanetType::SuperEarth: count = static_cast<int>(seed) % 2; break; // 0-1
        case PlanetType::Rocky: count = static_cast<int>(seed) % 2; break; // 0-1
        default: count = 0; break;
    }

    std::vector<MoonRecord> moons;
    for (int i = 0; i < count; ++i) {
        MoonRecord m;
        m.name = "Candidate " + std::string(1, static_cast<char>('I' + i));
        float variety = std::fmod(seed * (static_cast<float>(i) + 1.7f), 23.0f);
        m.orbitRadiusPlanetRadii = 3.0 + variety; // purely illustrative spacing
        m.orbitPeriodDays = 2.0 + variety * 1.5; // purely illustrative, not physically derived
        m.inclinationDeg = std::fmod(seed * (static_cast<float>(i) + 3.3f), 25.0f);
        m.eccentricity = std::fmod(seed * (static_cast<float>(i) + 1.1f), 28.0f) / 100.0;
        m.argPeriapsisDeg = std::fmod(seed * (static_cast<float>(i) + 5.7f), 360.0f);
        m.longAscNodeDeg = std::fmod(seed * (static_cast<float>(i) + 7.9f), 360.0f);
        m.isReal = false;
        moons.push_back(m);
    }
    return moons;
}
