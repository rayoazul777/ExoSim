#include "Renderer.h"
#include <iostream>
#include <cmath>
#include <algorithm>

namespace {
const char* kLineVS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uViewProj;
void main() { gl_Position = uViewProj * vec4(aPos, 1.0); }
)";

const char* kLineFS = R"(
#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
uniform float uAlpha;
void main() { FragColor = vec4(uColor, uAlpha); }
)";

const char* kPointVS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uViewProj;
void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    gl_PointSize = 6.0;
}
)";

const char* kPointFS = R"(
#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
void main() {
    // Soft round point instead of a square.
    vec2 c = gl_PointCoord - vec2(0.5);
    if (dot(c, c) > 0.25) discard;
    FragColor = vec4(uColor, 1.0);
}
)";

const char* kSurfaceVS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uMVP;
uniform mat4 uModel;
out vec2 vUV;
out vec3 vLocalPos;
out vec3 vNormalWorld;
out vec3 vFragWorldPos;
void main() {
    vUV = aUV;
    vLocalPos = aPos; // unit-sphere position: the 3D noise domain (no UV seams/pole pinching)
    // mat3(uModel) is valid as a normal matrix here: planet/moon models are
    // only ever rotation + uniform scale + translation (no shear).
    vNormalWorld = normalize(mat3(uModel) * aPos);
    vFragWorldPos = vec3(uModel * vec4(aPos, 1.0));
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

// Procedural planet surface. KEY DESIGN POINT (the zoom-blur fix): all
// noise is 3D, sampled on the unit-sphere position, and the number of
// fractal octaves (uDetailOctaves) is raised by the CPU as the camera
// approaches the surface - so zooming in reveals genuinely new, finer
// detail instead of magnifying the same few soft blobs. Terrain is also
// bump-shaded from its own height field, so crater rims and coastlines
// stay crisp up close.
//
// Honesty note: for exoplanets, every pattern here is an illustrative
// stylization chosen from the planet's type + equilibrium temperature -
// never a real image. Known Solar System bodies use their real colors and
// real feature TYPES (continents, caps, spots, rings, cracks), with
// procedurally generated fine detail.
const char* kSurfaceFS = R"(
#version 330 core
in vec2 vUV;
in vec3 vLocalPos;
in vec3 vNormalWorld;
in vec3 vFragWorldPos;
out vec4 FragColor;

uniform mat4 uModel;
uniform vec3 uBaseColor;
uniform vec3 uAccentColor;
uniform float uTime;
uniform float uSeed;
uniform int uPlanetType; // 0=Unknown 1=Rocky 2=SuperEarth 3=MiniNeptune 4=IceGiant 5=GasGiant
uniform vec3 uViewPosWorld;
uniform float uDetailOctaves; // raised as the camera nears the surface
uniform float uEqTempK;       // real equilibrium temperature if known, else 0

uniform int uIsKnownBody;
uniform int uKnownBanded;
uniform vec3 uSecondaryColor;
uniform int uHasContinents;
uniform int uHasPolarCaps;
uniform int uHasSpot;
uniform vec3 uSpotColor;
uniform vec2 uSpotUV;
uniform float uHazeIntensity;
uniform int uHasLineae;

// ---------- 3D noise ----------
float hash31(vec3 p) {
    p = fract(p * vec3(443.897, 441.423, 437.195));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}
vec3 hash33(vec3 p) {
    return vec3(hash31(p), hash31(p + 31.7), hash31(p + 74.3));
}
float vnoise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float n000 = hash31(i);
    float n100 = hash31(i + vec3(1,0,0));
    float n010 = hash31(i + vec3(0,1,0));
    float n110 = hash31(i + vec3(1,1,0));
    float n001 = hash31(i + vec3(0,0,1));
    float n101 = hash31(i + vec3(1,0,1));
    float n011 = hash31(i + vec3(0,1,1));
    float n111 = hash31(i + vec3(1,1,1));
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}
// Fractal noise with a FRACTIONAL octave count: the last octave fades in
// smoothly, so detail grows continuously while zooming (no popping).
float fbm3(vec3 p, float octaves) {
    float v = 0.0, a = 0.5, norm = 0.0;
    for (int i = 0; i < 12; i++) {
        float w = clamp(octaves - float(i), 0.0, 1.0);
        if (w <= 0.0) break;
        v += a * w * vnoise3(p);
        norm += a * w;
        p = p * 2.03 + vec3(17.1, 3.7, 9.2);
        a *= 0.5;
    }
    return v / max(norm, 1e-4);
}
// 3D cellular (Worley) noise: x = nearest feature distance, y = second nearest.
vec2 worley3(vec3 p) {
    vec3 c = floor(p);
    float f1 = 10.0, f2 = 10.0;
    for (int z = -1; z <= 1; z++)
    for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++) {
        vec3 n = c + vec3(x, y, z);
        float d = length(p - (n + hash33(n)));
        if (d < f1) { f2 = f1; f1 = d; } else if (d < f2) { f2 = d; }
    }
    return vec2(f1, f2);
}
// Crater height contribution at one scale: bowl + raised rim.
float craters(vec3 p) {
    vec2 w = worley3(p);
    float bowl = smoothstep(0.0, 0.45, w.x) - 1.0;
    float rim = smoothstep(0.30, 0.42, w.x) * (1.0 - smoothstep(0.42, 0.55, w.x));
    return bowl * 0.5 + rim * 0.6;
}
vec3 dirFromUV(vec2 uv) {
    float phi = uv.x * 6.2831853;
    float theta = uv.y * 3.14159265;
    return vec3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi));
}
// Great-circle "crack" lines (Europa lineae / ice-world fractures): analytic,
// so they stay razor sharp at any zoom.
float lineae(vec3 P, float seed, int count, float width) {
    float m = 0.0;
    for (int i = 0; i < 16; i++) {
        if (i >= count) break;
        vec3 n = normalize(hash33(vec3(seed, float(i) * 7.13, 3.3)) * 2.0 - 1.0);
        vec3 wob = normalize(hash33(vec3(float(i) * 3.1, seed, 9.9)) * 2.0 - 1.0);
        float d = abs(dot(P, n) + 0.02 * sin(dot(P, wob) * 25.0));
        float fw = fwidth(d) + 1e-5;
        float core = 1.0 - smoothstep(width - fw, width + fw, d);
        // Europa's lineae are often DOUBLE ridges: add a faint parallel twin.
        float twin = 1.0 - smoothstep(width - fw, width + fw, abs(d - width * 3.0));
        m = max(m, max(core, twin * 0.5));
    }
    return m;
}

// Terrain height field for rocky bodies (used for both color and bump).
float terrainHeight(vec3 P, float oct) {
    vec3 sp = P * 2.2 + uSeed * 0.013;
    float h = fbm3(sp, oct) * 0.6;
    h += craters(P * 4.0 + uSeed * 0.07) * 0.25;
    h += craters(P * 11.0 + uSeed * 0.11) * 0.12;
    if (oct > 7.0) h += craters(P * 32.0 + uSeed * 0.17) * 0.06 * clamp(oct - 7.0, 0.0, 1.0);
    return h;
}

void main() {
    vec3 P = normalize(vLocalPos);
    float oct = uDetailOctaves;
    vec3 viewDir = normalize(uViewPosWorld - vFragWorldPos);
    vec3 nWorld = normalize(vNormalWorld);
    // Stylized key light (a fixed offset from the view direction - not the
    // real star position) so bump-shaded relief reads clearly.
    vec3 L = normalize(viewDir + normalize(vec3(0.45, 0.55, 0.2)));

    vec3 color = uBaseColor;
    vec3 emissive = vec3(0.0);
    bool useBump = false;
    float variant = fract(sin(uSeed * 12.9898) * 43758.5453);

    if (uIsKnownBody == 1) {
        if (uKnownBanded == 1) {
            // Giant planet: latitude bands with turbulent 3D-noise edges and
            // fine streaks stretched along the bands (detail grows with zoom).
            vec3 sp = vec3(P.x * 2.0, P.y * 7.0, P.z * 2.0) + uSeed * 0.01;
            float turb = fbm3(sp + vec3(uTime * 0.01, 0.0, 0.0), oct) * 0.22;
            float band = sin((P.y + turb) * 24.0);
            color = mix(uBaseColor, uSecondaryColor, smoothstep(-1.0, 1.0, band) * 0.8);
            float streak = fbm3(vec3(P.x * 6.0, P.y * 40.0, P.z * 6.0) + uSeed, oct);
            color *= 0.88 + 0.24 * streak;
        } else {
            // Rocky known body: real colors, procedural relief.
            float h = terrainHeight(P, oct);
            color = mix(uSecondaryColor, uBaseColor, clamp(h + 0.55, 0.0, 1.0));
            useBump = true;
        }

        if (uHasContinents == 1) {
            // Earth: continent shapes as angular-distance blobs at roughly
            // correct relative positions, with fractal 3D-noise coastlines
            // that keep sharpening as you zoom in.
            vec3 c[6] = vec3[6](dirFromUV(vec2(0.20, 0.32)), dirFromUV(vec2(0.30, 0.62)),
                                dirFromUV(vec2(0.52, 0.52)), dirFromUV(vec2(0.64, 0.28)),
                                dirFromUV(vec2(0.82, 0.68)), dirFromUV(vec2(0.50, 0.95)));
            float r[6] = float[6](0.11, 0.09, 0.13, 0.17, 0.06, 0.20);
            float coastNoise = (fbm3(P * 5.0 + 11.0, oct) - 0.5) * 0.35;
            float land = 0.0;
            for (int k = 0; k < 6; k++) {
                float ang = acos(clamp(dot(P, c[k]), -1.0, 1.0));
                float rad = r[k] * 3.14159265 * 1.25;
                float fw = fwidth(ang) + 1e-4;
                land = max(land, 1.0 - smoothstep(rad - fw, rad + fw, ang + coastNoise));
            }
            float h = terrainHeight(P, oct);
            vec3 landCol = mix(uSecondaryColor, vec3(0.55, 0.47, 0.30), clamp(fbm3(P * 3.0 + 5.0, oct) * 1.4 - 0.4, 0.0, 1.0));
            landCol *= 0.8 + 0.4 * clamp(h + 0.5, 0.0, 1.0);
            vec3 oceanCol = uBaseColor * (0.75 + 0.25 * fbm3(P * 8.0, oct));
            color = mix(oceanCol, landCol, land);
            useBump = land > 0.5;
            float clouds = smoothstep(0.5, 0.75, fbm3(P * 3.0 + vec3(uTime * 0.01, 0.0, 0.0), oct));
            color = mix(color, vec3(0.95), clouds * 0.55);
        }

        if (uHasPolarCaps == 1) {
            float capEdge = 0.9 + (fbm3(P * 6.0 + 3.0, oct) - 0.5) * 0.06;
            float fw = fwidth(P.y) + 1e-4;
            float cap = smoothstep(capEdge - fw, capEdge + fw, abs(P.y));
            color = mix(color, vec3(0.93, 0.94, 0.97), cap);
        }

        if (uHasSpot == 1) {
            vec3 sc = dirFromUV(uSpotUV);
            float ang = acos(clamp(dot(P, sc), -1.0, 1.0));
            float swirl = (fbm3(P * 14.0 + uSeed, oct) - 0.5) * 0.08;
            float spot = 1.0 - smoothstep(0.22, 0.30, ang + swirl);
            color = mix(color, uSpotColor, spot);
        }

        if (uHasLineae == 1) {
            float l = lineae(P, uSeed, 14, 0.006);
            color = mix(color, uSecondaryColor, l * 0.85);
        }

        if (uHazeIntensity > 0.0) {
            float haze = fbm3(P * 2.5 + vec3(uTime * 0.01, 0.0, 0.0), oct);
            color = mix(color, uBaseColor * 1.2, uHazeIntensity * (0.7 + 0.3 * haze));
            useBump = false; // thick atmosphere hides relief (Venus, Titan)
        }
    } else if (uPlanetType == 5 || uPlanetType == 4 || uPlanetType == 3) {
        // Exoplanet giants: seeded band count/turbulence/hue; hot giants
        // (eqT > 1000 K) get darker, reddish, glowing bands; some get a storm.
        vec3 hueShift = mix(vec3(1.0), vec3(0.8 + 0.4 * variant, 0.9, 1.2 - 0.4 * variant), 0.5);
        vec3 base = uBaseColor * hueShift;
        float bands = 10.0 + variant * 30.0;
        vec3 sp = vec3(P.x * 2.0, P.y * 7.0, P.z * 2.0) + uSeed * 0.01;
        float turb = fbm3(sp + vec3(uTime * 0.01, 0.0, 0.0), oct) * (0.1 + 0.25 * variant);
        float band = sin((P.y + turb) * bands);
        color = mix(base * 0.55, base * 1.3, smoothstep(-1.0, 1.0, band));
        color *= 0.88 + 0.24 * fbm3(vec3(P.x * 6.0, P.y * 40.0, P.z * 6.0) + uSeed, oct);
        if (uEqTempK > 1000.0) {
            color = mix(color, vec3(0.35, 0.12, 0.08), 0.55);
            emissive += vec3(0.9, 0.35, 0.1) * smoothstep(0.4, 1.0, band) * 0.25;
        }
        if (variant > 0.5) {
            vec3 sc = normalize(hash33(vec3(uSeed, 1.0, 2.0)) * 2.0 - 1.0);
            sc.y *= 0.5; sc = normalize(sc);
            float ang = acos(clamp(dot(P, sc), -1.0, 1.0));
            color = mix(color, base * 0.5, 1.0 - smoothstep(0.12, 0.18, ang));
        }
    } else if (uPlanetType == 1 || uPlanetType == 2) {
        float h = terrainHeight(P, oct);
        useBump = true;
        if (uEqTempK > 1000.0) {
            // Lava world: dark crust, glowing fissures along cellular edges.
            vec2 w = worley3(P * 6.0 + uSeed * 0.05);
            float fissure = 1.0 - smoothstep(0.0, 0.06 + 0.04 * fbm3(P * 20.0, oct), w.y - w.x);
            color = mix(vec3(0.12, 0.10, 0.09), vec3(0.25, 0.20, 0.18), clamp(h + 0.5, 0.0, 1.0));
            emissive = vec3(1.0, 0.42, 0.08) * fissure * 1.2;
        } else if (uEqTempK > 0.0 && uEqTempK < 200.0) {
            // Ice world: bright ice, blue shading, sharp fracture lines.
            color = mix(vec3(0.72, 0.80, 0.90), vec3(0.95, 0.97, 1.0), clamp(h + 0.5, 0.0, 1.0));
            color = mix(color, vec3(0.45, 0.55, 0.70), lineae(P, uSeed, 10, 0.004) * 0.7);
        } else {
            // Barren / desert world: seeded palette (rust, gray, tan).
            vec3 pal = variant < 0.33 ? vec3(0.62, 0.38, 0.25) : (variant < 0.66 ? vec3(0.50, 0.49, 0.47) : vec3(0.70, 0.60, 0.42));
            color = mix(pal * 0.6, mix(pal, uBaseColor, 0.3), clamp(h + 0.55, 0.0, 1.0));
        }
    } else {
        // Unknown composition: deliberately plain - no physical data to go on.
        color = uBaseColor * (0.85 + 0.15 * fbm3(P * 4.0 + uSeed, oct));
    }

    // Lighting: bump-shaded relief where terrain applies, otherwise the
    // stylized view-facing falloff.
    float lit;
    if (useBump) {
        float e = 0.002;
        vec3 t1 = normalize(cross(P, abs(P.y) < 0.99 ? vec3(0,1,0) : vec3(1,0,0)));
        vec3 t2 = cross(P, t1);
        float h0 = terrainHeight(P, oct);
        float h1 = terrainHeight(normalize(P + t1 * e), oct);
        float h2 = terrainHeight(normalize(P + t2 * e), oct);
        float strength = 0.9;
        vec3 nLocal = normalize(P - strength * ((h1 - h0) / e * t1 + (h2 - h0) / e * t2) * 0.02);
        vec3 nBump = normalize(mat3(uModel) * nLocal);
        lit = 0.25 + 0.85 * clamp(dot(nBump, L), 0.0, 1.0);
    } else {
        float ndotl = clamp(dot(nWorld, L), 0.0, 1.0);
        lit = 0.3 + 0.75 * ndotl;
    }
    color *= lit;
    color += emissive;

    // CRT-style phosphor rim glow for continuity with the rest of the app.
    float ndotv = clamp(dot(nWorld, viewDir), 0.0, 1.0);
    color += uAccentColor * pow(1.0 - ndotv, 4.0) * 0.3;

    FragColor = vec4(color, 1.0);
}
)";

const char* kRingVS = R"(
#version 330 core
layout(location = 0) in vec2 aDir;
layout(location = 1) in float aRadial; // 0 = inner edge of the whole ring system, 1 = outer edge
uniform mat4 uMVP;
uniform float uInnerRadius;
uniform float uOuterRadius;
out float vR; // radius in planet radii
void main() {
    float r = mix(uInnerRadius, uOuterRadius, aRadial);
    vR = r;
    gl_Position = uMVP * vec4(aDir.x * r, 0.0, aDir.y * r, 1.0);
}
)";

// Rings built from REAL band radii (NASA ring fact sheets) passed as a
// uniform array: gaps like the Cassini Division sit at their real measured
// radii because they're simply where no band is. Within each band, a fine
// ringlet texture adds visual structure - that sub-structure IS stylized
// (the real ringlet pattern isn't a simple bulk dataset), but the band
// boundaries it sits inside are real.
const char* kRingFS = R"(
#version 330 core
in float vR;
out vec4 FragColor;
uniform vec3 uColor;
uniform float uSeed;
uniform vec3 uBands[16]; // x = inner, y = outer, z = opacity
uniform int uBandCount;
float hash1(float x) { return fract(sin(x * 127.1 + uSeed) * 43758.5453); }
void main() {
    float r = vR;
    float w = fwidth(r) + 1e-6;
    float alpha = 0.0;
    for (int i = 0; i < 16; i++) {
        if (i >= uBandCount) break;
        vec3 b = uBands[i];
        // Anti-aliased coverage: bands narrower than a pixel (Uranus's
        // epsilon ring, Neptune's Adams ring) fade to partial coverage
        // instead of flickering in and out.
        float cov = smoothstep(b.x - w, b.x + w, r) * (1.0 - smoothstep(b.y - w, b.y + w, r));
        alpha = max(alpha, cov * b.z);
    }
    float ringlets = 0.75 + 0.25 * sin(r * 900.0 + hash1(floor(r * 60.0)) * 6.28);
    FragColor = vec4(uColor * (0.8 + 0.2 * ringlets), alpha * ringlets);
}
)";

const char* kSkyboxVS = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 vNdc;
void main() {
    vNdc = aPos;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// Procedural background: sparse stars, soft nebula patches, and a
// galactic band. Everything here is stylized (hash-based noise), not a
// real star catalog - what IS real is the galactic band's orientation,
// driven by the current system's actual measured galactic coordinates
// when the Archive has them (see main.cpp / Dataset.h's galacticLat/LonDeg).
const char* kSkyboxFS = R"(
#version 330 core
in vec2 vNdc;
out vec4 FragColor;

uniform mat4 uInvViewProjRot;
uniform float uTime;
uniform float uGalacticLatRad;
uniform float uGalacticLonRad;
uniform float uSeed;

float hash31(vec3 p) {
    p = fract(p * vec3(443.897, 441.423, 437.195));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

void main() {
    // Reconstruct a world-space ray direction from NDC, using a
    // rotation-only inverse view-projection so the sky doesn't shift as
    // the camera orbits around a point - only as it actually rotates.
    vec4 nearP = uInvViewProjRot * vec4(vNdc, -1.0, 1.0);
    vec4 farP  = uInvViewProjRot * vec4(vNdc,  1.0, 1.0);
    nearP /= nearP.w;
    farP  /= farP.w;
    vec3 dir = normalize(farP.xyz - nearP.xyz);

    vec3 color = vec3(0.01, 0.015, 0.026); // near-black, faint blue tint

    // Note: procedural stars used to be generated here via hash noise.
    // They've been replaced by renderStarCatalog's real Hipparcos-catalog
    // points, drawn as a separate pass - see main.cpp. This shader now
    // only supplies what still has no simple bulk real-data source: the
    // nebula patches and the galactic band (whose ORIENTATION is real,
    // even though the patches themselves are stylized).

    // Nebula: soft colored patches via a spherical projection of the
    // ray direction, so patches stay fixed in world space.
    float theta = atan(dir.z, dir.x);
    float phi = acos(clamp(dir.y, -1.0, 1.0));
    vec2 sphUV = vec2(theta / 6.2831853 + 0.5, phi / 3.14159265);
    float n1 = hash31(vec3(sphUV * 6.0, uSeed));
    float n2 = hash31(vec3(sphUV * 13.0 + 3.7, uSeed * 2.1));
    float nebula = smoothstep(0.55, 0.95, n1 * 0.6 + n2 * 0.4) * 0.16;
    vec3 nebulaColor = mix(vec3(0.32, 0.14, 0.42), vec3(0.14, 0.22, 0.48), n2);
    color += nebulaColor * nebula;

    // Galactic band: brightest along the great circle perpendicular to
    // the system's real galactic-plane normal.
    vec3 galNormal = normalize(vec3(sin(uGalacticLatRad),
                                     cos(uGalacticLatRad) * sin(uGalacticLonRad),
                                     cos(uGalacticLatRad) * cos(uGalacticLonRad)));
    float bandDist = abs(dot(dir, galNormal));
    float band = 1.0 - smoothstep(0.0, 0.12, bandDist);
    float bandNoise = hash31(dir * 30.0 + uSeed * 3.3);
    band *= (0.7 + 0.3 * bandNoise);
    color += vec3(0.55, 0.5, 0.6) * band * 0.5;

    FragColor = vec4(color, 1.0);
}
)";

// Real star catalog (Hipparcos, via CDS VizieR) rendered as point
// sprites. Unlike the skybox background, direction/color/size here come
// from genuinely measured data (RA/Dec, B-V color index, visual
// magnitude), precomputed once on the CPU when the catalog loads - see
// main.cpp. This shader just projects and draws them.
const char* kStarPointVS = R"(
#version 330 core
layout(location = 0) in vec3 aDir;       // unit direction, real RA/Dec converted to Cartesian
layout(location = 1) in vec3 aColor;     // from the star's real B-V color index
layout(location = 2) in float aPointSize; // from the star's real visual magnitude
uniform mat4 uViewProjRot; // proj * rotation-only view (NOT inverted - forward projection)
out vec3 vColor;
void main() {
    vColor = aColor;
    vec4 clip = uViewProjRot * vec4(aDir * 50.0, 1.0);
    // Force depth to just inside the far plane, regardless of the
    // current camera's actual near/far range, so real stars are never
    // near/far-clipped by whatever projection the active view is using.
    clip.z = clip.w * 0.9999;
    gl_Position = clip;
    gl_PointSize = aPointSize;
}
)";

const char* kStarPointFS = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    // Soft round point instead of a hard square.
    vec2 c = gl_PointCoord - vec2(0.5);
    float d = dot(c, c);
    if (d > 0.25) discard;
    float falloff = 1.0 - smoothstep(0.15, 0.25, d);
    FragColor = vec4(vColor, falloff);
}
)";

} // namespace

GLuint Renderer::compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "[Renderer] Shader compile error: " << log << "\n";
    }
    return s;
}

GLuint Renderer::linkProgram(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::cerr << "[Renderer] Program link error: " << log << "\n";
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

bool Renderer::init() {
    GLuint lvs = compileShader(GL_VERTEX_SHADER, kLineVS);
    GLuint lfs = compileShader(GL_FRAGMENT_SHADER, kLineFS);
    m_lineShader = linkProgram(lvs, lfs);

    GLuint pvs = compileShader(GL_VERTEX_SHADER, kPointVS);
    GLuint pfs = compileShader(GL_FRAGMENT_SHADER, kPointFS);
    m_pointShader = linkProgram(pvs, pfs);

    GLuint svs = compileShader(GL_VERTEX_SHADER, kSurfaceVS);
    GLuint sfs = compileShader(GL_FRAGMENT_SHADER, kSurfaceFS);
    m_surfaceShader = linkProgram(svs, sfs);

    GLuint rvs = compileShader(GL_VERTEX_SHADER, kRingVS);
    GLuint rfs = compileShader(GL_FRAGMENT_SHADER, kRingFS);
    m_ringShader = linkProgram(rvs, rfs);

    GLuint kvs = compileShader(GL_VERTEX_SHADER, kSkyboxVS);
    GLuint kfs = compileShader(GL_FRAGMENT_SHADER, kSkyboxFS);
    m_skyboxShader = linkProgram(kvs, kfs);

    GLuint spvs = compileShader(GL_VERTEX_SHADER, kStarPointVS);
    GLuint spfs = compileShader(GL_FRAGMENT_SHADER, kStarPointFS);
    m_starPointShader = linkProgram(spvs, spfs);

    glGenVertexArrays(1, &m_starCatalogVao);
    glGenBuffers(1, &m_starCatalogVbo);

    glGenVertexArrays(1, &m_pointsVao);
    glGenBuffers(1, &m_pointsVbo);

    buildSphereWireframe();
    buildSolidSphereMesh();
    buildRingMesh();
    buildSkyboxQuad();

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return m_lineShader != 0 && m_pointShader != 0 && m_surfaceShader != 0 && m_ringShader != 0
        && m_skyboxShader != 0 && m_starPointShader != 0;
}

void Renderer::buildSphereWireframe() {
    // Unit sphere wireframe: a handful of latitude rings + longitude
    // semicircles, stored as GL_LINES (consecutive vertex pairs are
    // one segment each). This is deliberately low-fidelity - it reads
    // as a retro "wireframe globe" rather than a smooth mesh.
    constexpr int kLatBands = 8;   // horizontal rings (excluding poles)
    constexpr int kLonBands = 12;  // vertical half-circles
    constexpr int kSegs = 32;
    constexpr float kPi = 3.14159265358979323846f;

    std::vector<glm::vec3> verts;

    // Latitude rings (circles of constant Y).
    for (int b = 1; b < kLatBands; ++b) {
        float theta = kPi * static_cast<float>(b) / static_cast<float>(kLatBands);
        float y = std::cos(theta);
        float r = std::sin(theta);
        std::vector<glm::vec3> ring;
        ring.reserve(kSegs + 1);
        for (int s = 0; s <= kSegs; ++s) {
            float phi = 2.0f * kPi * static_cast<float>(s) / static_cast<float>(kSegs);
            ring.emplace_back(r * std::cos(phi), y, r * std::sin(phi));
        }
        for (int s = 0; s < kSegs; ++s) {
            verts.push_back(ring[s]);
            verts.push_back(ring[s + 1]);
        }
    }

    // Longitude semicircles (pole to pole).
    for (int l = 0; l < kLonBands; ++l) {
        float phi = 2.0f * kPi * static_cast<float>(l) / static_cast<float>(kLonBands);
        std::vector<glm::vec3> ring;
        ring.reserve(kSegs + 1);
        for (int s = 0; s <= kSegs; ++s) {
            float theta = kPi * static_cast<float>(s) / static_cast<float>(kSegs);
            float y = std::cos(theta);
            float r = std::sin(theta);
            ring.emplace_back(r * std::cos(phi), y, r * std::sin(phi));
        }
        for (int s = 0; s < kSegs; ++s) {
            verts.push_back(ring[s]);
            verts.push_back(ring[s + 1]);
        }
    }

    glGenVertexArrays(1, &m_sphereVao);
    glGenBuffers(1, &m_sphereVbo);
    glBindVertexArray(m_sphereVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_sphereVbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(glm::vec3), verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glBindVertexArray(0);

    m_sphereVertCount = static_cast<GLsizei>(verts.size());
}

void Renderer::buildSolidSphereMesh() {
    // Indexed triangle UV-sphere: position (vec3) + UV (vec2) per vertex,
    // interleaved. UV.x = longitude fraction, UV.y = latitude fraction -
    // the surface shader uses these directly for its procedural pattern.
    constexpr int kRings = 64;   // higher-res silhouette for close-up zoom
    constexpr int kSegs = 128;
    constexpr float kPi = 3.14159265358979323846f;

    std::vector<float> verts; // x,y,z,u,v per vertex
    verts.reserve((kRings + 1) * (kSegs + 1) * 5);

    for (int i = 0; i <= kRings; ++i) {
        float theta = kPi * static_cast<float>(i) / static_cast<float>(kRings); // 0 (north pole) .. pi (south)
        float y = std::cos(theta);
        float r = std::sin(theta);
        for (int j = 0; j <= kSegs; ++j) {
            float phi = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(kSegs);
            float x = r * std::cos(phi);
            float z = r * std::sin(phi);
            verts.push_back(x);
            verts.push_back(y);
            verts.push_back(z);
            verts.push_back(static_cast<float>(j) / static_cast<float>(kSegs));
            verts.push_back(static_cast<float>(i) / static_cast<float>(kRings));
        }
    }

    std::vector<unsigned int> indices;
    indices.reserve(kRings * kSegs * 6);
    int rowStride = kSegs + 1;
    for (int i = 0; i < kRings; ++i) {
        for (int j = 0; j < kSegs; ++j) {
            unsigned int a = i * rowStride + j;
            unsigned int b = a + rowStride;
            unsigned int c = a + 1;
            unsigned int d = b + 1;
            indices.push_back(a); indices.push_back(b); indices.push_back(c);
            indices.push_back(c); indices.push_back(b); indices.push_back(d);
        }
    }

    glGenVertexArrays(1, &m_solidSphereVao);
    glGenBuffers(1, &m_solidSphereVbo);
    glGenBuffers(1, &m_solidSphereEbo);

    glBindVertexArray(m_solidSphereVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_solidSphereVbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_solidSphereEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);

    m_solidSphereIndexCount = static_cast<GLsizei>(indices.size());
}

void Renderer::buildRingMesh() {
    // Unit annulus as a triangle strip: for each angular segment, emit an
    // inner-rim vertex (radial=0) then an outer-rim vertex (radial=1).
    // Actual inner/outer radii are applied in the vertex shader via
    // uniforms, so this one mesh is reused for every ring drawn.
    constexpr int kSegs = 96;
    constexpr float kPi = 3.14159265358979323846f;

    std::vector<float> verts; // dirX, dirY, radialFlag per vertex
    verts.reserve((kSegs + 1) * 2 * 3);

    for (int s = 0; s <= kSegs; ++s) {
        float phi = 2.0f * kPi * static_cast<float>(s) / static_cast<float>(kSegs);
        float dx = std::cos(phi);
        float dy = std::sin(phi);
        verts.push_back(dx); verts.push_back(dy); verts.push_back(0.0f); // inner
        verts.push_back(dx); verts.push_back(dy); verts.push_back(1.0f); // outer
    }

    glGenVertexArrays(1, &m_ringVao);
    glGenBuffers(1, &m_ringVbo);
    glBindVertexArray(m_ringVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_ringVbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    m_ringVertCount = static_cast<GLsizei>(verts.size() / 3);
}

void Renderer::buildSkyboxQuad() {
    float verts[] = {
        -1.f, -1.f,
         1.f, -1.f,
         1.f,  1.f,
        -1.f, -1.f,
         1.f,  1.f,
        -1.f,  1.f,
    };
    glGenVertexArrays(1, &m_skyboxVao);
    glGenBuffers(1, &m_skyboxVbo);
    glBindVertexArray(m_skyboxVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_skyboxVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

void Renderer::shutdown() {
    for (auto& o : m_orbits) {
        glDeleteBuffers(1, &o.vbo);
        glDeleteVertexArrays(1, &o.vao);
    }
    m_orbits.clear();
    glDeleteBuffers(1, &m_pointsVbo);
    glDeleteVertexArrays(1, &m_pointsVao);
    glDeleteBuffers(1, &m_sphereVbo);
    glDeleteVertexArrays(1, &m_sphereVao);
    glDeleteBuffers(1, &m_solidSphereVbo);
    glDeleteBuffers(1, &m_solidSphereEbo);
    glDeleteVertexArrays(1, &m_solidSphereVao);
    glDeleteBuffers(1, &m_ringVbo);
    glDeleteVertexArrays(1, &m_ringVao);
    glDeleteProgram(m_ringShader);
    glDeleteProgram(m_surfaceShader);
    glDeleteBuffers(1, &m_skyboxVbo);
    glDeleteVertexArrays(1, &m_skyboxVao);
    glDeleteProgram(m_skyboxShader);
    glDeleteBuffers(1, &m_starCatalogVbo);
    glDeleteVertexArrays(1, &m_starCatalogVao);
    glDeleteProgram(m_starPointShader);
    glDeleteProgram(m_lineShader);
    glDeleteProgram(m_pointShader);
}

void Renderer::clearOrbits() {
    for (auto& o : m_orbits) {
        if (o.vbo) glDeleteBuffers(1, &o.vbo);
        if (o.vao) glDeleteVertexArrays(1, &o.vao);
    }
    m_orbits.clear();
}

void Renderer::setOrbitPath(size_t planetIndex, const std::vector<glm::vec3>& points) {
    if (planetIndex >= m_orbits.size()) m_orbits.resize(planetIndex + 1);

    OrbitBuffer& buf = m_orbits[planetIndex];
    if (buf.vao == 0) {
        glGenVertexArrays(1, &buf.vao);
        glGenBuffers(1, &buf.vbo);
    }
    glBindVertexArray(buf.vao);
    glBindBuffer(GL_ARRAY_BUFFER, buf.vbo);
    glBufferData(GL_ARRAY_BUFFER, points.size() * sizeof(glm::vec3), points.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    buf.count = static_cast<GLsizei>(points.size());
    glBindVertexArray(0);
}

void Renderer::render(const glm::mat4& viewProj, const std::vector<glm::vec3>& planetPositions) {
    // --- Orbit rings ---
    glUseProgram(m_lineShader);
    GLint locVP = glGetUniformLocation(m_lineShader, "uViewProj");
    GLint locColor = glGetUniformLocation(m_lineShader, "uColor");
    glUniformMatrix4fv(locVP, 1, GL_FALSE, &viewProj[0][0]);
    glUniform3f(locColor, 0.15f, 0.95f, 0.55f); // phosphor green
    glUniform1f(glGetUniformLocation(m_lineShader, "uAlpha"), 0.55f);

    for (auto& o : m_orbits) {
        if (o.count == 0) continue;
        glBindVertexArray(o.vao);
        glDrawArrays(GL_LINE_LOOP, 0, o.count);
    }

    // --- Planet points ---
    if (!planetPositions.empty()) {
        glBindVertexArray(m_pointsVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_pointsVbo);
        glBufferData(GL_ARRAY_BUFFER, planetPositions.size() * sizeof(glm::vec3),
                     planetPositions.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

        glUseProgram(m_pointShader);
        GLint locVP2 = glGetUniformLocation(m_pointShader, "uViewProj");
        GLint locColor2 = glGetUniformLocation(m_pointShader, "uColor");
        glUniformMatrix4fv(locVP2, 1, GL_FALSE, &viewProj[0][0]);
        glUniform3f(locColor2, 1.0f, 0.65f, 0.15f); // amber

        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(planetPositions.size()));
    }

    glBindVertexArray(0);
}

void Renderer::renderPlanetDetail(const glm::mat4& viewProj, const glm::mat4& model, const glm::vec3& color, float alpha) {
    glUseProgram(m_lineShader);
    glm::mat4 mvp = viewProj * model;
    glUniformMatrix4fv(glGetUniformLocation(m_lineShader, "uViewProj"), 1, GL_FALSE, &mvp[0][0]);
    glUniform3f(glGetUniformLocation(m_lineShader, "uColor"), color.r, color.g, color.b);
    glUniform1f(glGetUniformLocation(m_lineShader, "uAlpha"), alpha);

    glBindVertexArray(m_sphereVao);
    glDrawArrays(GL_LINES, 0, m_sphereVertCount);
    glBindVertexArray(0);
}

void Renderer::renderStar(const glm::mat4& viewProj, const glm::mat4& model, const glm::vec3& color) {
    // Same wireframe-sphere primitive as renderPlanetDetail - kept as a
    // separate named entry point since a star and a planet are
    // conceptually different things to a caller, even though the draw
    // call underneath is identical.
    renderPlanetDetail(viewProj, model, color);
}

void Renderer::renderPlanetSurface(const glm::mat4& viewProj, const glm::mat4& model,
                                    const glm::vec3& viewPosWorld, int planetType,
                                    const glm::vec3& baseColor, const glm::vec3& accentColor,
                                    float timeSeconds, float seed,
                                    const PlanetAppearance* knownAppearance) {
    glUseProgram(m_surfaceShader);
    glm::mat4 mvp = viewProj * model;
    glUniformMatrix4fv(glGetUniformLocation(m_surfaceShader, "uMVP"), 1, GL_FALSE, &mvp[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(m_surfaceShader, "uModel"), 1, GL_FALSE, &model[0][0]);
    glUniform3f(glGetUniformLocation(m_surfaceShader, "uViewPosWorld"), viewPosWorld.x, viewPosWorld.y, viewPosWorld.z);
    glUniform1f(glGetUniformLocation(m_surfaceShader, "uTime"), timeSeconds);
    glUniform1f(glGetUniformLocation(m_surfaceShader, "uSeed"), seed);
    glUniform1i(glGetUniformLocation(m_surfaceShader, "uPlanetType"), planetType);
    glUniform3f(glGetUniformLocation(m_surfaceShader, "uAccentColor"), accentColor.r, accentColor.g, accentColor.b);
    glUniform1f(glGetUniformLocation(m_surfaceShader, "uDetailOctaves"), m_surfaceDetailOctaves);
    glUniform1f(glGetUniformLocation(m_surfaceShader, "uEqTempK"), m_surfaceEqTempK);

    if (knownAppearance != nullptr) {
        const PlanetAppearance& a = *knownAppearance;
        glUniform1i(glGetUniformLocation(m_surfaceShader, "uIsKnownBody"), 1);
        glUniform3f(glGetUniformLocation(m_surfaceShader, "uBaseColor"), a.primaryColor.r, a.primaryColor.g, a.primaryColor.b);
        glUniform3f(glGetUniformLocation(m_surfaceShader, "uSecondaryColor"), a.secondaryColor.r, a.secondaryColor.g, a.secondaryColor.b);
        glUniform1i(glGetUniformLocation(m_surfaceShader, "uHasContinents"), a.hasContinents ? 1 : 0);
        glUniform1i(glGetUniformLocation(m_surfaceShader, "uHasPolarCaps"), a.hasPolarCaps ? 1 : 0);
        glUniform1i(glGetUniformLocation(m_surfaceShader, "uHasSpot"), a.hasGreatSpot ? 1 : 0);
        glUniform3f(glGetUniformLocation(m_surfaceShader, "uSpotColor"), a.spotColor.r, a.spotColor.g, a.spotColor.b);
        glUniform2f(glGetUniformLocation(m_surfaceShader, "uSpotUV"), a.spotLongitude, a.spotLatitude);
        glUniform1f(glGetUniformLocation(m_surfaceShader, "uHazeIntensity"), a.hazeIntensity);
        glUniform1i(glGetUniformLocation(m_surfaceShader, "uHasLineae"), a.hasLineae ? 1 : 0);
        glUniform1i(glGetUniformLocation(m_surfaceShader, "uKnownBanded"), a.isBanded ? 1 : 0);
    } else {
        glUniform1i(glGetUniformLocation(m_surfaceShader, "uIsKnownBody"), 0);
        glUniform3f(glGetUniformLocation(m_surfaceShader, "uBaseColor"), baseColor.r, baseColor.g, baseColor.b);
        glUniform1i(glGetUniformLocation(m_surfaceShader, "uHasContinents"), 0);
        glUniform1i(glGetUniformLocation(m_surfaceShader, "uHasPolarCaps"), 0);
        glUniform1i(glGetUniformLocation(m_surfaceShader, "uHasSpot"), 0);
        glUniform1f(glGetUniformLocation(m_surfaceShader, "uHazeIntensity"), 0.0f);
        glUniform1i(glGetUniformLocation(m_surfaceShader, "uHasLineae"), 0);
        glUniform1i(glGetUniformLocation(m_surfaceShader, "uKnownBanded"), 0);
    }

    glBindVertexArray(m_solidSphereVao);
    glDrawElements(GL_TRIANGLES, m_solidSphereIndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Renderer::renderPlanetRings(const glm::mat4& viewProj, const glm::mat4& model,
                                  const std::vector<RingBand>& bands,
                                  const glm::vec3& color, float seed) {
    if (bands.empty()) return;
    float inner = bands[0].innerRadius, outer = bands[0].outerRadius;
    std::vector<float> packed;
    int count = static_cast<int>(std::min<size_t>(bands.size(), 16));
    for (int i = 0; i < count; ++i) {
        inner = std::min(inner, bands[i].innerRadius);
        outer = std::max(outer, bands[i].outerRadius);
        packed.push_back(bands[i].innerRadius);
        packed.push_back(bands[i].outerRadius);
        packed.push_back(bands[i].opacity);
    }

    glUseProgram(m_ringShader);
    glm::mat4 mvp = viewProj * model;
    glUniformMatrix4fv(glGetUniformLocation(m_ringShader, "uMVP"), 1, GL_FALSE, &mvp[0][0]);
    glUniform1f(glGetUniformLocation(m_ringShader, "uInnerRadius"), inner);
    glUniform1f(glGetUniformLocation(m_ringShader, "uOuterRadius"), outer);
    glUniform3f(glGetUniformLocation(m_ringShader, "uColor"), color.r, color.g, color.b);
    glUniform1f(glGetUniformLocation(m_ringShader, "uSeed"), seed);
    glUniform3fv(glGetUniformLocation(m_ringShader, "uBands"), count, packed.data());
    glUniform1i(glGetUniformLocation(m_ringShader, "uBandCount"), count);

    // Transparent geometry: test depth against the opaque planet (so the
    // far half of the ring is correctly hidden behind it) but don't write
    // depth (so it doesn't occlude itself or the wireframe grid drawn after).
    glDepthMask(GL_FALSE);
    glBindVertexArray(m_ringVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, m_ringVertCount);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
}

void Renderer::renderSkybox(const glm::mat4& invViewProjRotationOnly, float timeSeconds,
                             float galacticLatDeg, float galacticLonDeg, float seed) {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(m_skyboxShader);
    glUniformMatrix4fv(glGetUniformLocation(m_skyboxShader, "uInvViewProjRot"), 1, GL_FALSE, &invViewProjRotationOnly[0][0]);
    glUniform1f(glGetUniformLocation(m_skyboxShader, "uTime"), timeSeconds);
    glUniform1f(glGetUniformLocation(m_skyboxShader, "uGalacticLatRad"), glm::radians(galacticLatDeg));
    glUniform1f(glGetUniformLocation(m_skyboxShader, "uGalacticLonRad"), glm::radians(galacticLonDeg));
    glUniform1f(glGetUniformLocation(m_skyboxShader, "uSeed"), seed);

    glBindVertexArray(m_skyboxVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::setStarCatalog(const std::vector<glm::vec3>& directions,
                               const std::vector<glm::vec3>& colors,
                               const std::vector<float>& pointSizes) {
    size_t n = directions.size();
    if (colors.size() != n || pointSizes.size() != n) {
        std::cerr << "[Renderer] setStarCatalog: mismatched array sizes, ignoring.\n";
        return;
    }

    std::vector<float> verts; // dir.xyz, color.rgb, pointSize per star = 7 floats
    verts.reserve(n * 7);
    for (size_t i = 0; i < n; ++i) {
        verts.push_back(directions[i].x);
        verts.push_back(directions[i].y);
        verts.push_back(directions[i].z);
        verts.push_back(colors[i].r);
        verts.push_back(colors[i].g);
        verts.push_back(colors[i].b);
        verts.push_back(pointSizes[i]);
    }

    glBindVertexArray(m_starCatalogVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_starCatalogVbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(6 * sizeof(float)));
    glBindVertexArray(0);

    m_starCatalogCount = static_cast<GLsizei>(n);
}

void Renderer::renderStarCatalog(const glm::mat4& viewProjRot) {
    if (m_starCatalogCount == 0) return;

    glDisable(GL_DEPTH_TEST);
    glUseProgram(m_starPointShader);
    glUniformMatrix4fv(glGetUniformLocation(m_starPointShader, "uViewProjRot"), 1, GL_FALSE, &viewProjRot[0][0]);

    glBindVertexArray(m_starCatalogVao);
    glDrawArrays(GL_POINTS, 0, m_starCatalogCount);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderAuxiliaryRing(const glm::mat4& viewProj, const std::vector<glm::vec3>& points,
                                    const glm::vec3& color, float alpha) {
    if (points.size() < 2) return;

    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, points.size() * sizeof(glm::vec3), points.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

    glUseProgram(m_lineShader);
    glUniformMatrix4fv(glGetUniformLocation(m_lineShader, "uViewProj"), 1, GL_FALSE, &viewProj[0][0]);
    glUniform3f(glGetUniformLocation(m_lineShader, "uColor"), color.r, color.g, color.b);
    glUniform1f(glGetUniformLocation(m_lineShader, "uAlpha"), alpha);
    glDrawArrays(GL_LINE_LOOP, 0, static_cast<GLsizei>(points.size()));

    glBindVertexArray(0);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
}
