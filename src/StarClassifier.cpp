#include "StarClassifier.h"

StarClassification StarClassifier::Classify(double teffK, double radiusSolar, const std::string& archiveSpectralType) {
    StarClassification c;

    c.hasMeasuredRadius = radiusSolar > 0.0;
    c.displayRadiusSolar = c.hasMeasuredRadius ? radiusSolar : 1.0;

    if (teffK <= 0.0) {
        c.hasMeasuredTeff = false;
        c.spectralClass = archiveSpectralType.empty()
            ? "Unknown spectral type"
            : ("Unclassified (archive spectral type: " + archiveSpectralType + ")");
        c.color = glm::vec3(0.75f, 0.75f, 0.8f); // neutral pale color when we genuinely don't know
        return c;
    }

    c.hasMeasuredTeff = true;

    // Boundaries and colors approximate the standard Morgan-Keenan
    // sequence / blackbody-tinted outreach palette.
    if (teffK >= 30000.0) {
        c.spectralClass = "O-type (blue, extremely hot and massive)";
        c.color = glm::vec3(0.60f, 0.70f, 1.0f);
    } else if (teffK >= 10000.0) {
        c.spectralClass = "B-type (blue-white, very hot)";
        c.color = glm::vec3(0.70f, 0.80f, 1.0f);
    } else if (teffK >= 7500.0) {
        c.spectralClass = "A-type (white)";
        c.color = glm::vec3(0.90f, 0.92f, 1.0f);
    } else if (teffK >= 6000.0) {
        c.spectralClass = "F-type (yellow-white)";
        c.color = glm::vec3(1.0f, 0.96f, 0.85f);
    } else if (teffK >= 5200.0) {
        c.spectralClass = "G-type (yellow, Sun-like)";
        c.color = glm::vec3(1.0f, 0.92f, 0.6f);
    } else if (teffK >= 3700.0) {
        c.spectralClass = "K-type (orange)";
        c.color = glm::vec3(1.0f, 0.72f, 0.42f);
    } else if (teffK >= 2400.0) {
        c.spectralClass = "M-type (red dwarf)";
        c.color = glm::vec3(1.0f, 0.45f, 0.35f);
    } else {
        c.spectralClass = "Ultra-cool dwarf (below standard M-type range)";
        c.color = glm::vec3(0.85f, 0.35f, 0.30f);
    }

    return c;
}
