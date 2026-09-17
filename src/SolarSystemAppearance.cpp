#include "SolarSystemAppearance.h"

PlanetAppearance SolarSystemAppearance::Lookup(const std::string& name) {
    PlanetAppearance a;

    if (name == "Mercury") {
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.55f, 0.53f, 0.50f);   // gray, heavily cratered
        a.secondaryColor = glm::vec3(0.42f, 0.40f, 0.38f);
        a.axialTiltDeg = 0.03f;
    } else if (name == "Venus") {
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.92f, 0.85f, 0.62f);   // pale yellow-white
        a.secondaryColor = glm::vec3(0.85f, 0.75f, 0.50f);
        a.hazeIntensity = 0.85f;                            // near-total cloud cover
        a.axialTiltDeg = 177.4f;                            // retrograde, almost upside-down
    } else if (name == "Earth") {
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.12f, 0.32f, 0.62f);    // ocean blue
        a.secondaryColor = glm::vec3(0.32f, 0.42f, 0.22f);  // land green/brown
        a.hasContinents = true;
        a.hasPolarCaps = true;
        a.axialTiltDeg = 23.4f;
    } else if (name == "Mars") {
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.72f, 0.35f, 0.20f);    // rust red-orange
        a.secondaryColor = glm::vec3(0.50f, 0.22f, 0.15f);
        a.hasPolarCaps = true;                              // smaller than Earth's, shader treats uniformly
        a.axialTiltDeg = 25.2f;
    } else if (name == "Jupiter") {
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.85f, 0.68f, 0.48f);    // tan/orange bands
        a.secondaryColor = glm::vec3(0.65f, 0.48f, 0.32f);
        a.hasGreatSpot = true;
        a.spotColor = glm::vec3(0.72f, 0.30f, 0.20f);       // the Great Red Spot
        a.spotLatitude = 0.62f;                             // southern hemisphere
        a.spotLongitude = 0.35f;
        a.axialTiltDeg = 3.1f;
    } else if (name == "Saturn") {
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.85f, 0.76f, 0.56f);    // pale gold
        a.secondaryColor = glm::vec3(0.74f, 0.65f, 0.46f);
        a.hasRings = true;
        a.ringColor = glm::vec3(0.82f, 0.76f, 0.62f);
        a.ringInnerRadius = 1.35f;
        a.ringOuterRadius = 2.4f;
        a.ringOpacity = 0.8f;                               // Saturn's rings are the famous, prominent ones
        a.axialTiltDeg = 26.7f;
    } else if (name == "Uranus") {
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.62f, 0.85f, 0.86f);    // pale cyan
        a.secondaryColor = glm::vec3(0.52f, 0.76f, 0.78f);
        a.hasRings = true;                                  // real, but much fainter/darker than Saturn's
        a.ringColor = glm::vec3(0.45f, 0.55f, 0.58f);
        a.ringInnerRadius = 1.4f;
        a.ringOuterRadius = 1.9f;
        a.ringOpacity = 0.3f;
        a.axialTiltDeg = 97.8f;                             // famously rolls on its side
    } else if (name == "Neptune") {
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.16f, 0.28f, 0.75f);    // deep blue
        a.secondaryColor = glm::vec3(0.12f, 0.20f, 0.60f);
        a.hasGreatSpot = true;
        a.spotColor = glm::vec3(0.08f, 0.14f, 0.40f);       // the (historical) Great Dark Spot
        a.spotLatitude = 0.45f;
        a.spotLongitude = 0.6f;
        a.axialTiltDeg = 28.3f;
    }
    // Everything else (every real exoplanet, and the demo's own Sun) is
    // left as the default isKnown=false - PlanetClassifier's generic
    // procedural look applies instead.

    return a;
}
