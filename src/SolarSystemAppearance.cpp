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
        a.isBanded = true;
        a.primaryColor = glm::vec3(0.85f, 0.68f, 0.48f);    // tan/orange bands
        a.secondaryColor = glm::vec3(0.65f, 0.48f, 0.32f);
        a.hasGreatSpot = true;
        a.spotColor = glm::vec3(0.72f, 0.30f, 0.20f);       // the Great Red Spot
        a.spotLatitude = 0.62f;                             // southern hemisphere
        a.spotLongitude = 0.35f;
        a.axialTiltDeg = 3.1f;
        // Real but extremely faint dusty rings (discovered by Voyager 1) -
        // real radii (NASA Jovian ring fact sheet, planet radius 71,492 km),
        // opacities kept very low on purpose so they don't read as Saturn-like.
        a.hasRings = true;
        a.ringColor = glm::vec3(0.55f, 0.48f, 0.40f);
        a.ringBands = {
            {1.2869f, 1.7135f, 0.05f}, // Halo ring: 92000-122500 km
            {1.7135f, 1.8044f, 0.12f}, // Main ring: 122500-129000 km
            {1.8044f, 3.1612f, 0.03f}, // Gossamer rings: 129000-226000 km
        };
    } else if (name == "Saturn") {
        a.isKnown = true;
        a.isBanded = true;
        a.primaryColor = glm::vec3(0.85f, 0.76f, 0.56f);    // pale gold
        a.secondaryColor = glm::vec3(0.74f, 0.65f, 0.46f);
        a.hasRings = true;
        a.ringColor = glm::vec3(0.82f, 0.76f, 0.62f);
        // Real radii (NASA Saturnian ring fact sheet), planet radius 60,268 km.
        // The Cassini Division (117,580-122,170 km) and Encke gap
        // (~133,423-133,745 km) appear as the real gaps between bands.
        a.ringBands = {
            {1.1100f, 1.2363f, 0.12f}, // D ring: 66900-74510 km
            {1.2388f, 1.5265f, 0.35f}, // C ring: 74658-92000 km
            {1.5265f, 1.9510f, 0.95f}, // B ring: 92000-117580 km
            {2.0271f, 2.2138f, 0.75f}, // A ring (inner): 122170-133423 km
            {2.2192f, 2.2694f, 0.65f}, // A ring (outer, past Encke gap): 133745-136775 km
            {2.3218f, 2.3301f, 0.50f}, // F ring: 139930-140430 km
        };
        a.axialTiltDeg = 26.7f;
    } else if (name == "Uranus") {
        a.isKnown = true;
        a.isBanded = true;
        a.primaryColor = glm::vec3(0.62f, 0.85f, 0.86f);    // pale cyan
        a.secondaryColor = glm::vec3(0.52f, 0.76f, 0.78f);
        a.hasRings = true;                                  // real, but much fainter/darker than Saturn's
        a.ringColor = glm::vec3(0.45f, 0.55f, 0.58f);
        // Real radii (NASA Uranian ring fact sheet), planet radius 25,559 km.
        // Uranus's rings are narrow and dark; nearby rings are grouped
        // into single bands here since they're only a few km-100s of km wide.
        a.ringBands = {
            {1.6369f, 1.6656f, 0.25f}, // 6/5/4 rings: 41837-42571 km
            {1.7496f, 1.7865f, 0.30f}, // Alpha/Beta rings: 44718-45661 km
            {1.8458f, 1.8897f, 0.30f}, // Eta/Gamma/Delta rings: 47176-48300 km
            {1.9993f, 2.0032f, 0.55f}, // Epsilon ring: 51099-51199 km
        };
        a.axialTiltDeg = 97.8f;                             // famously rolls on its side
    } else if (name == "Neptune") {
        a.isKnown = true;
        a.isBanded = true;
        a.primaryColor = glm::vec3(0.16f, 0.28f, 0.75f);    // deep blue
        a.secondaryColor = glm::vec3(0.12f, 0.20f, 0.60f);
        a.hasGreatSpot = true;
        a.spotColor = glm::vec3(0.08f, 0.14f, 0.40f);       // the (historical) Great Dark Spot
        a.spotLatitude = 0.45f;
        a.spotLongitude = 0.6f;
        a.axialTiltDeg = 28.3f;
        // Real but very faint, dark rings (confirmed by Voyager 2) - real
        // radii (NASA Neptunian ring fact sheet, planet radius 24,764 km),
        // kept very dim. (The Adams ring's famous clumpy "arcs" aren't modeled.)
        a.hasRings = true;
        a.ringColor = glm::vec3(0.40f, 0.42f, 0.48f);
        a.ringBands = {
            {1.6516f, 1.7324f, 0.05f}, // Galle ring: 40900-42900 km
            {2.1459f, 2.1507f, 0.12f}, // Le Verrier ring: 53140-53260 km
            {2.1483f, 2.3098f, 0.04f}, // Lassell ring: 53200-57200 km
            {2.5403f, 2.5423f, 0.15f}, // Adams ring: 62907-62957 km
        };
    } else if (name == "Pluto") {
        // Imaged up close by New Horizons (2015) - real, not statistical.
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.60f, 0.46f, 0.36f);    // tan/reddish-brown terrain
        a.secondaryColor = glm::vec3(0.40f, 0.26f, 0.20f);  // darker reddish regions (Cthulhu Macula)
        a.hasGreatSpot = true;
        a.spotColor = glm::vec3(0.90f, 0.86f, 0.76f);       // Tombaugh Regio, the famous bright "heart"
        a.spotLatitude = 0.55f;
        a.spotLongitude = 0.5f;
        a.axialTiltDeg = 122.5f;                            // extreme tilt, similar in spirit to Uranus
    } else if (name == "Ceres") {
        // Imaged up close by Dawn (2015) - real, not statistical.
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.52f, 0.50f, 0.48f);    // gray, cratered
        a.secondaryColor = glm::vec3(0.42f, 0.40f, 0.38f);
        a.hasGreatSpot = true;
        a.spotColor = glm::vec3(0.95f, 0.95f, 0.95f);       // the bright salt deposits in Occator crater
        a.spotLatitude = 0.4f;
        a.spotLongitude = 0.2f;
        a.axialTiltDeg = 4.0f;
    }
    // Eris, Haumea and Makemake are deliberately left unlisted (isKnown
    // stays false): unlike Pluto and Ceres, no spacecraft has imaged them
    // up close, so we genuinely don't have a real appearance to show -
    // they use the honest generic classifier look instead, same as any
    // real exoplanet.

    // --- Moons (only the ones our own missions have actually imaged
    // up close - see main.cpp, which only ever applies this lookup to
    // moons flagged isReal=true, never to speculative exoplanet moons) ---
    else if (name == "Moon") {
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.72f, 0.70f, 0.66f);    // pale gray, heavily cratered
        a.secondaryColor = glm::vec3(0.55f, 0.53f, 0.50f);
        a.axialTiltDeg = 6.68f;
    } else if (name == "Phobos" || name == "Deimos") {
        // Real bodies are irregular/potato-shaped - our sphere is a
        // simplification for both these and every other non-round body.
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.40f, 0.36f, 0.33f);    // dark, reddish-gray
        a.secondaryColor = glm::vec3(0.30f, 0.27f, 0.25f);
    } else if (name == "Io") {
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.85f, 0.70f, 0.35f);    // sulfur yellow-orange
        a.secondaryColor = glm::vec3(0.70f, 0.35f, 0.15f);  // volcanic deposits
        a.hasGreatSpot = true;
        a.spotColor = glm::vec3(0.25f, 0.08f, 0.05f);       // an active volcanic caldera
        a.spotLatitude = 0.5f;
        a.spotLongitude = 0.4f;
    } else if (name == "Europa") {
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.83f, 0.86f, 0.90f);    // icy white
        a.secondaryColor = glm::vec3(0.55f, 0.38f, 0.32f);  // reddish-brown surface cracks (lineae)
        // Real Europa's lineae are long, linear, often great-circle-like
        // cracks - rendered as such now (arc geometry), though the specific
        // arc orientations are procedural, not a map of the real ones.
        a.hasLineae = true;
    } else if (name == "Ganymede") {
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.58f, 0.55f, 0.52f);    // grayish-brown, cratered/grooved
        a.secondaryColor = glm::vec3(0.42f, 0.40f, 0.40f);
    } else if (name == "Callisto") {
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.42f, 0.38f, 0.35f);    // dark, heavily cratered
        a.secondaryColor = glm::vec3(0.30f, 0.27f, 0.25f);
    } else if (name == "Titan") {
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.82f, 0.62f, 0.36f);    // hazy orange
        a.secondaryColor = glm::vec3(0.70f, 0.50f, 0.28f);
        a.hazeIntensity = 0.75f;                            // thick atmosphere obscures the surface
    } else if (name == "Rhea") {
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.80f, 0.80f, 0.80f);    // bright icy surface
        a.secondaryColor = glm::vec3(0.68f, 0.68f, 0.68f);
    } else if (name == "Titania" || name == "Oberon") {
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.55f, 0.55f, 0.58f);    // dark icy-gray
        a.secondaryColor = glm::vec3(0.42f, 0.42f, 0.45f);
    } else if (name == "Triton") {
        a.isKnown = true;
        a.primaryColor = glm::vec3(0.85f, 0.80f, 0.80f);    // pale pink-white nitrogen frost
        a.secondaryColor = glm::vec3(0.70f, 0.65f, 0.66f);
        a.axialTiltDeg = 157.0f;                            // reflects its steep retrograde-like orbit
    }
    // Everything else (every real exoplanet, every speculative moon, and
    // the demo's own Sun) is left as the default isKnown=false -
    // PlanetClassifier's generic procedural look applies instead.

    return a;
}
