#pragma once
#include <glm/glm.hpp>
#include <string>

// Classifies a host star by effective temperature into the standard
// O/B/A/F/G/K/M spectral sequence, with an approximate real color for
// each (the same blue-to-red mapping used in astronomy outreach material).
// Unlike PlanetClassifier, this is NOT a guess from indirect proxies -
// st_teff is a directly measured (or well-modeled) stellar property, so
// this classification is on much firmer ground when the value is present.
struct StarClassification {
    std::string spectralClass;      // "G-type (Sun-like)", etc.
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    bool hasMeasuredTeff = false;
    bool hasMeasuredRadius = false;
    double displayRadiusSolar = 1.0; // radiusSolar if measured, else an assumed 1.0 (Sun-like)
};

class StarClassifier {
public:
    static StarClassification Classify(double teffK, double radiusSolar, const std::string& archiveSpectralType);
};
