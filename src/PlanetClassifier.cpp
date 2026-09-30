#include "PlanetClassifier.h"
#include <vector>
#include <utility>

namespace {
    constexpr double kJupiterRadiiToEarth = 11.2;
    constexpr double kJupiterMassToEarth = 317.8;

    // Appends a hedged, temperature-aware note to an atmosphere guess when
    // equilibrium temperature is on record. This uses a real Archive field
    // (pl_eqt) to add real physical reasoning without claiming a measurement
    // that doesn't exist - we're reasoning about survivability, not
    // asserting composition.
    std::string temperatureNote(double eqTempK, bool isRockySized) {
        if (eqTempK <= 0.0) return "";
        if (isRockySized && eqTempK > 1200.0) {
            return " At this equilibrium temperature, any primordial atmosphere was likely stripped or baked off long ago.";
        }
        if (isRockySized && eqTempK < 700.0 && eqTempK > 0.0) {
            return " This temperature range doesn't rule out a retained atmosphere, though retention also depends on the planet's mass and its star's activity.";
        }
        if (!isRockySized && eqTempK > 1500.0) {
            return " This is hot enough to count as a \"hot\" gas/ice giant - expect an inflated radius and a highly active, possibly cloud-free upper atmosphere.";
        }
        return "";
    }

    // --- Density-based (highest confidence): bulk density in g/cm^3 is
    // far more diagnostic of composition than radius or mass alone, since
    // it directly distinguishes a compact rocky/iron world from a puffy
    // gas-dominated one. Rough boundaries follow solar-system anchors:
    // rocky planets cluster 3.9-5.5 g/cm^3 (Mars-Earth), ice/gas giants
    // cluster 0.7-1.7 g/cm^3 (Saturn-Neptune).
    PlanetClassification classifyByDensity(double densityGcm3, double eqTempK) {
        PlanetClassification c;
        c.basedOnDensity = true;
        c.basedOnRadius = true;
        c.hasAnyPhysicalData = true;

        bool isRockySized = densityGcm3 > 3.0;

        if (densityGcm3 > 3.5) {
            c.type = PlanetType::Rocky;
            c.typeLabel = "Rocky / terrestrial (density-confirmed)";
            c.surfaceGuess = "Bulk density matches a rocky/metallic composition, similar to Earth or Mars - very likely a solid surface.";
            c.atmosphereGuess = "Composition not directly measured." + temperatureNote(eqTempK, isRockySized);
        } else if (densityGcm3 > 2.0) {
            c.type = PlanetType::SuperEarth;
            c.typeLabel = "Likely rocky, possibly with a thick envelope";
            c.surfaceGuess = "Density suggests a rock/iron-dominated body, though it may carry a modest volatile layer.";
            c.atmosphereGuess = "Composition not directly measured." + temperatureNote(eqTempK, isRockySized);
        } else if (densityGcm3 > 1.0) {
            c.type = PlanetType::MiniNeptune;
            c.typeLabel = "Volatile/ice-rich (mini-Neptune-like)";
            c.surfaceGuess = "Density is too low for a purely rocky body - likely a substantial ice/volatile fraction, no clear solid surface.";
            c.atmosphereGuess = "Composition not directly measured." + temperatureNote(eqTempK, isRockySized);
        } else {
            c.type = PlanetType::GasGiant;
            c.typeLabel = "Gas-dominated (density-confirmed)";
            c.surfaceGuess = "Density this low is only possible for a hydrogen/helium-dominated planet - no solid surface.";
            c.atmosphereGuess = "Predominantly hydrogen/helium is very likely given the density, though exact composition is unmeasured." + temperatureNote(eqTempK, isRockySized);
        }

        // Reuse the radius-based color bands for visual consistency.
        if (densityGcm3 > 3.5)      c.displayColor = glm::vec3(0.75f, 0.55f, 0.35f);
        else if (densityGcm3 > 2.0) c.displayColor = glm::vec3(0.35f, 0.8f, 0.55f);
        else if (densityGcm3 > 1.0) c.displayColor = glm::vec3(0.3f, 0.6f, 0.9f);
        else                        c.displayColor = glm::vec3(1.0f, 0.65f, 0.15f);

        return c;
    }

    // --- Radius-based (medium confidence): follows the well-known
    // exoplanet "radius gap" research (Fulton et al. and similar) - most
    // planets cluster below ~1.5 Earth radii (rocky) or above ~2 Earth
    // radii (volatile-rich), with a sparse gap between.
    PlanetClassification classifyByRadiusEarth(double rEarth, double eqTempK) {
        PlanetClassification c;
        c.basedOnRadius = true;
        c.hasAnyPhysicalData = true;
        bool isRockySized = rEarth < 2.0;

        if (rEarth < 1.5) {
            c.type = PlanetType::Rocky;
            c.typeLabel = "Rocky / terrestrial";
            c.surfaceGuess = "Likely has a solid surface, similar in scale to Earth or Mars.";
            c.atmosphereGuess = "Composition not directly measured." + temperatureNote(eqTempK, isRockySized);
            c.displayColor = glm::vec3(0.75f, 0.55f, 0.35f);
        } else if (rEarth < 2.0) {
            c.type = PlanetType::SuperEarth;
            c.typeLabel = "Super-Earth";
            c.surfaceGuess = "Probably rocky, but large enough it may hold a thicker atmosphere than Earth's.";
            c.atmosphereGuess = "Composition not directly measured." + temperatureNote(eqTempK, isRockySized);
            c.displayColor = glm::vec3(0.35f, 0.8f, 0.55f);
        } else if (rEarth < 4.0) {
            c.type = PlanetType::MiniNeptune;
            c.typeLabel = "Sub-Neptune / mini-Neptune";
            c.surfaceGuess = "Likely no solid surface - probably a rocky/icy core under a deep envelope.";
            c.atmosphereGuess = "Probably a substantial hydrogen/helium or volatile-rich envelope (unmeasured)." + temperatureNote(eqTempK, isRockySized);
            c.displayColor = glm::vec3(0.3f, 0.6f, 0.9f);
        } else if (rEarth < 6.0) { // NASA size categories: Neptune-size 2-6 Re, Jupiter-size 6-15 Re
            c.type = PlanetType::IceGiant;
            c.typeLabel = "Ice giant (Neptune/Uranus-like)";
            c.surfaceGuess = "No solid surface expected - a deep ice/gas envelope over a small core.";
            c.atmosphereGuess = "Likely hydrogen, helium and icy volatiles, similar in spirit to Neptune's." + temperatureNote(eqTempK, isRockySized);
            c.displayColor = glm::vec3(0.4f, 0.85f, 0.9f);
        } else {
            c.type = PlanetType::GasGiant;
            c.typeLabel = "Gas giant";
            c.surfaceGuess = "No solid surface - predominantly gas all the way down to a small/uncertain core.";
            c.atmosphereGuess = "Predominantly hydrogen and helium, similar in spirit to Jupiter or Saturn." + temperatureNote(eqTempK, isRockySized);
            c.displayColor = glm::vec3(1.0f, 0.65f, 0.15f);
        }
        return c;
    }

    // --- Mass-based (lowest confidence): mass alone can't distinguish a
    // dense rocky planet from a puffy low-density one, so this branch is
    // explicitly flagged as lower-confidence in the UI.
    PlanetClassification classifyByMassEarth(double mEarth, double eqTempK) {
        PlanetClassification c;
        c.basedOnRadius = false;
        c.hasAnyPhysicalData = true;
        bool isRockySized = mEarth < 10.0;

        if (mEarth < 2.0) {
            c.type = PlanetType::Rocky;
            c.typeLabel = "Likely rocky / terrestrial";
            c.surfaceGuess = "Mass suggests a rocky body, but this is a lower-confidence guess (no radius or density measured).";
            c.atmosphereGuess = "Unknown - no radius, density, or spectroscopic data available." + temperatureNote(eqTempK, isRockySized);
            c.displayColor = glm::vec3(0.75f, 0.55f, 0.35f);
        } else if (mEarth < 10.0) {
            c.type = PlanetType::SuperEarth;
            c.typeLabel = "Likely super-Earth / mini-Neptune";
            c.surfaceGuess = "Mass alone can't tell rocky from gas-enveloped at this range.";
            c.atmosphereGuess = "Unknown - no radius, density, or spectroscopic data available." + temperatureNote(eqTempK, isRockySized);
            c.displayColor = glm::vec3(0.35f, 0.8f, 0.55f);
        } else if (mEarth < 100.0) {
            c.type = PlanetType::IceGiant;
            c.typeLabel = "Likely ice giant";
            c.surfaceGuess = "Mass is in the Neptune/Uranus range - probably no solid surface.";
            c.atmosphereGuess = "Unknown - no radius, density, or spectroscopic data available." + temperatureNote(eqTempK, isRockySized);
            c.displayColor = glm::vec3(0.4f, 0.85f, 0.9f);
        } else {
            c.type = PlanetType::GasGiant;
            c.typeLabel = "Likely gas giant";
            c.surfaceGuess = "Mass is in the Jupiter/Saturn range - almost certainly no solid surface.";
            c.atmosphereGuess = "Probably hydrogen/helium dominated, similar in spirit to Jupiter." + temperatureNote(eqTempK, isRockySized);
            c.displayColor = glm::vec3(1.0f, 0.65f, 0.15f);
        }
        return c;
    }

    // --- A small, hand-curated table of real published atmospheric
    // findings for well-studied targets. This is NOT fetched live - the
    // Exoplanet Archive doesn't carry this data, and there's no equivalent
    // bulk-queryable public API for atmosphere composition. These entries
    // reflect publicly reported JWST/Hubble results known as of early 2026,
    // paraphrased and hedged appropriately; ongoing science may update or
    // dispute individual findings, and this list only covers a handful of
    // the thousands of known planets. Extend it by adding entries below.
    const std::vector<std::pair<std::string, std::string>>& knownAtmosphereFindings() {
        static const std::vector<std::pair<std::string, std::string>> table = {
            {"WASP-39 b", "JWST spectroscopy (2022-2023) detected carbon dioxide, water vapor, sulfur dioxide (evidence of photochemistry), and other trace species."},
            {"WASP-96 b", "JWST (2022) detected water vapor along with evidence of clouds and haze."},
            {"WASP-17 b", "JWST (2023) detected silicate cloud particles (quartz) high in the atmosphere - a first for any exoplanet."},
            {"HD 209458 b", "The first exoplanet with a directly detected atmosphere - sodium absorption, found via Hubble in 2002; later work added water vapor and other species."},
            {"HD 189733 b", "Hubble and Spitzer observations found water vapor and evidence of high-altitude haze; a strikingly deep blue color was inferred from scattered light."},
            {"K2-18 b", "JWST (2023) detected methane and carbon dioxide; a tentative dimethyl sulfide signal reported the same year remains scientifically debated and unconfirmed."},
            {"GJ 1214 b", "Hubble (2014) found an unexpectedly featureless spectrum, suggesting high-altitude clouds or haze obscuring the atmosphere's composition."},
            {"55 Cancri e", "JWST thermal-emission data (2023) hinted at a possible secondary atmosphere (CO2/CO-rich), though this remains an active area of study rather than a settled result."},
            {"TRAPPIST-1 b", "JWST thermal-emission measurements (2023) found no strong evidence of a thick atmosphere, consistent with a bare rocky surface."},
            {"TRAPPIST-1 c", "JWST (2023) likewise found no strong evidence of a substantial CO2-rich atmosphere."},
            {"WASP-107 b", "JWST (2023) detected water, sulfur dioxide and evidence of internal heating, plus signs of photochemical haze."},
        };
        return table;
    }
}

PlanetClassification PlanetClassifier::Classify(const PlanetRecord& planet) {
    PlanetClassification result;

    // Radius wins for large planets: bulk density can't separate giants from
    // small volatile-rich worlds (Jupiter's 1.33 g/cm^3 overlaps icy
    // mini-Neptunes, and a density-only rule mislabeled Jupiter - caught by
    // test CLS-2). Nothing rocky or icy reaches 6 Earth radii, which is
    // where NASA's "Jupiter-size" category begins, so above that, radius
    // alone is decisive. Below it, density remains the better signal.
    double rEarthKnown = planet.radiusJupiter * kJupiterRadiiToEarth;
    if (planet.radiusJupiter > 0.0 && rEarthKnown >= 6.0) {
        result = classifyByRadiusEarth(rEarthKnown, planet.eqTempK);
    } else if (planet.densityGcm3 > 0.0) {
        result = classifyByDensity(planet.densityGcm3, planet.eqTempK);
    } else if (planet.radiusJupiter > 0.0) {
        result = classifyByRadiusEarth(planet.radiusJupiter * kJupiterRadiiToEarth, planet.eqTempK);
    } else if (planet.massJupiter > 0.0) {
        result = classifyByMassEarth(planet.massJupiter * kJupiterMassToEarth, planet.eqTempK);
    } else {
        result.type = PlanetType::Unknown;
        result.typeLabel = "Unknown composition";
        result.surfaceGuess = "No radius, density, or mass on record for this planet - can't estimate.";
        result.atmosphereGuess = "No radius, density, or mass on record for this planet - can't estimate.";
        result.displayColor = glm::vec3(0.6f, 0.6f, 0.6f);
        result.hasAnyPhysicalData = false;
    }

    for (const auto& entry : knownAtmosphereFindings()) {
        if (entry.first == planet.name) {
            result.hasRealAtmosphereFindings = true;
            result.realAtmosphereFindings = entry.second;
            break;
        }
    }

    return result;
}
