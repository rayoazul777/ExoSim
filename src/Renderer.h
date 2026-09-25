#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include "SolarSystemAppearance.h"

// Bare-bones GL 3.3 core renderer. Draws each orbit as a line loop
// (precomputed ellipse points) and each planet as a GL_POINTS vertex,
// all in one wireframe color scheme - deliberately simple so the
// retro-futurist look comes from the CRT post-process pass, not from
// the geometry itself.
class Renderer {
public:
    bool init();
    void shutdown();

    // Uploads/replaces the line-loop vertices for one orbit ring (AU units).
    void setOrbitPath(size_t planetIndex, const std::vector<glm::vec3>& points);

    // Frees all orbit ring buffers - call this before setOrbitPath-ing
    // a freshly selected star system, so the old system's rings don't
    // linger (and so leftover higher-indexed slots from a bigger
    // previous system don't still draw).
    void clearOrbits();

    // Draws all orbit rings + current planet positions.
    // viewProj: combined camera view*projection matrix.
    // planetPositions: current position (AU) per planet, same order as setOrbitPath calls.
    void render(const glm::mat4& viewProj,
                const std::vector<glm::vec3>& planetPositions);

    // Draws a single planet as a CRT-style wireframe globe (latitude/
    // longitude grid lines over a unit sphere, transformed by `model`).
    // Used by the planet detail view - a stylized model, not a
    // to-scale rendering of the real planet's radius. `alpha` defaults to
    // the original fixed look; lower it (e.g. for speculative moons) to
    // visually distinguish less-certain content.
    void renderPlanetDetail(const glm::mat4& viewProj, const glm::mat4& model, const glm::vec3& color, float alpha = 0.55f);

    // Draws the system's host star at the origin using the same
    // wireframe-sphere primitive, sized (via `model`'s scale) from the
    // star's real radius and colored by StarClassifier's spectral-type color.
    void renderStar(const glm::mat4& viewProj, const glm::mat4& model, const glm::vec3& color);

    // Draws one ad-hoc line loop (e.g. a distance-reference ring, or a
    // moon's orbit path) from an explicit point list, at the given color
    // and alpha. Builds and tears down a scratch buffer each call - fine
    // for the handful of these drawn per frame, avoids persistent buffer
    // bookkeeping for something this infrequent.
    void renderAuxiliaryRing(const glm::mat4& viewProj, const std::vector<glm::vec3>& points,
                              const glm::vec3& color, float alpha);

    // Draws a SOLID-shaded planet surface: a procedurally generated,
    // stylized approximation (banded clouds for gas/ice giants, a
    // cratered pattern for rocky ones) driven entirely by a shader - not
    // a real image (no exoplanet has been imaged in this kind of detail).
    // planetType matches PlanetType's int values (0=Unknown..5=GasGiant).
    // `seed` varies the procedural pattern per planet; `timeSeconds`
    // slowly animates cloud bands. Typically followed by a renderPlanetDetail
    // call (with a very slightly larger `model` scale, e.g. *1.01, to avoid
    // z-fighting) to lay the wireframe targeting-grid on top.
    //
    // `knownAppearance`: when non-null (only ever passed for our own 8
    // Solar System planets - see SolarSystemAppearance), switches to a
    // real-appearance rendering path (secondary-color bands, continents,
    // polar caps, a great-spot feature, haze) instead of the generic
    // classification-driven look, and planetType/baseColor are ignored.
    void renderPlanetSurface(const glm::mat4& viewProj, const glm::mat4& model,
                              const glm::vec3& viewPosWorld, int planetType,
                              const glm::vec3& baseColor, const glm::vec3& accentColor,
                              float timeSeconds, float seed,
                              const PlanetAppearance* knownAppearance = nullptr);

    // Draws a planet's rings in the model's local XZ plane from REAL band
    // radii (NASA ring fact sheets; see SolarSystemAppearance), in units of
    // the planet's radius. Gaps like the Cassini Division fall at their real
    // radii because they're simply where no band is. Only the fine ringlet
    // texture within each band is stylized. Up to 16 bands.
    void renderPlanetRings(const glm::mat4& viewProj, const glm::mat4& model,
                            const std::vector<RingBand>& bands,
                            const glm::vec3& color, float seed);

    // Fractal octave count for the surface shader - raise it as the camera
    // nears the surface so zooming reveals new detail (see kSurfaceFS).
    void setSurfaceDetail(float octaves) { m_surfaceDetailOctaves = octaves; }
    // Real equilibrium temperature (K) of the body being drawn, or 0 if
    // unknown - selects illustrative exoplanet variants (lava/ice/hot giant).
    void setSurfaceEqTemp(float eqTempK) { m_surfaceEqTempK = eqTempK; }

    // Draws a full-screen procedural background: stars, soft nebula
    // patches, and a galactic band. Call FIRST in a scene (right after
    // clearing, before anything else) - depth test is disabled
    // internally so it never occludes real geometry drawn after it.
    // `invViewProjRotationOnly` should be inverse(proj * rotation-only
    // view) - camera TRANSLATION is deliberately excluded, so the
    // background behaves as if infinitely far away and only changes as
    // the camera rotates, not as it orbits around a point. galacticLat/
    // LonDeg orient the galactic band using the current system's real
    // galactic coordinates when known (0/0 otherwise - see Dataset.h).
    void renderSkybox(const glm::mat4& invViewProjRotationOnly, float timeSeconds,
                       float galacticLatDeg, float galacticLonDeg, float seed);

    // Uploads the real Hipparcos star catalog once (call after init(),
    // whenever the catalog is loaded/changed - NOT every frame). Each
    // parallel array must be the same length: unit direction (from real
    // RA/Dec), RGB color (from real B-V color index), and point size
    // (from real visual magnitude) - see main.cpp for the conversions.
    void setStarCatalog(const std::vector<glm::vec3>& directions,
                         const std::vector<glm::vec3>& colors,
                         const std::vector<float>& pointSizes);

    // Draws the real star catalog as point sprites. `viewProjRot` is
    // proj * rotation-only view (the forward transform - NOT inverted,
    // unlike renderSkybox's matrix) so real star directions project to
    // the correct screen position while still ignoring camera
    // translation, matching the skybox's "infinitely distant" behavior.
    // Safe to call with an empty catalog (draws nothing).
    void renderStarCatalog(const glm::mat4& viewProjRot);

private:
    GLuint m_lineShader = 0;
    GLuint m_pointShader = 0;

    struct OrbitBuffer { GLuint vao = 0, vbo = 0; GLsizei count = 0; };
    std::vector<OrbitBuffer> m_orbits;

    GLuint m_pointsVao = 0, m_pointsVbo = 0;

    GLuint m_sphereVao = 0, m_sphereVbo = 0;
    GLsizei m_sphereVertCount = 0;

    // Solid-shaded UV sphere (triangles, with UVs) for the procedural
    // planet surface pass - separate from the wireframe-only sphere above.
    GLuint m_solidSphereVao = 0, m_solidSphereVbo = 0, m_solidSphereEbo = 0;
    GLsizei m_solidSphereIndexCount = 0;
    GLuint m_surfaceShader = 0;
    float m_surfaceDetailOctaves = 5.0f;
    float m_surfaceEqTempK = 0.0f;

    // Flat unit annulus (inner=1, outer=1 in local space; actual radii
    // are applied in the vertex shader via uniforms) for planet rings.
    GLuint m_ringVao = 0, m_ringVbo = 0;
    GLsizei m_ringVertCount = 0;
    GLuint m_ringShader = 0;

    // Full-screen quad for the procedural skybox background.
    GLuint m_skyboxVao = 0, m_skyboxVbo = 0;
    GLuint m_skyboxShader = 0;

    // Real Hipparcos star catalog point buffer.
    GLuint m_starCatalogVao = 0, m_starCatalogVbo = 0;
    GLsizei m_starCatalogCount = 0;
    GLuint m_starPointShader = 0;

    void buildSphereWireframe();
    void buildSolidSphereMesh();
    void buildRingMesh();
    void buildSkyboxQuad();

    GLuint compileShader(GLenum type, const char* src);
    GLuint linkProgram(GLuint vs, GLuint fs);
};
