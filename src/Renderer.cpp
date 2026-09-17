#include "Renderer.h"
#include <iostream>
#include <cmath>

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
out vec3 vNormalWorld;
out vec3 vFragWorldPos;
void main() {
    vUV = aUV;
    // aPos lies exactly on a unit sphere, so it IS the local-space normal.
    // mat3(uModel) is valid here since the planet model matrix is only
    // ever rotation + uniform scale (no shear), so it doesn't need the
    // full inverse-transpose normal matrix.
    vNormalWorld = normalize(mat3(uModel) * aPos);
    vFragWorldPos = vec3(uModel * vec4(aPos, 1.0));
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

// Procedural, stylized planet surface - NOT a real image (no exoplanet
// has ever been imaged in this kind of detail). Banded clouds for
// gas/ice/mini-Neptune types, a cratered pattern for rocky types, driven
// entirely by hash-based value noise computed per-pixel so it stays
// sharp at any zoom level rather than pixelating like a fixed texture.
const char* kSurfaceFS = R"(
#version 330 core
in vec2 vUV;
in vec3 vNormalWorld;
in vec3 vFragWorldPos;
out vec4 FragColor;

uniform vec3 uBaseColor;
uniform vec3 uAccentColor;
uniform float uTime;
uniform float uSeed;
uniform int uPlanetType; // 0=Unknown 1=Rocky 2=SuperEarth 3=MiniNeptune 4=IceGiant 5=GasGiant
uniform vec3 uViewPosWorld;

// Known-appearance path (only ever used for our own Solar System's
// planets - see SolarSystemAppearance). When uIsKnownBody is 0, none of
// these matter and the generic classification-driven path below runs.
uniform int uIsKnownBody;
uniform vec3 uSecondaryColor;
uniform int uHasContinents;
uniform int uHasPolarCaps;
uniform int uHasSpot;
uniform vec3 uSpotColor;
uniform vec2 uSpotUV;
uniform float uHazeIntensity;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 4; i++) {
        value += amplitude * valueNoise(p);
        p *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

// Jittered-cell ("Worley"-style) distance field, used for the cratered
// rocky-planet look: distance from each point to the nearest of several
// randomly offset lattice points.
float cellDistance(vec2 p) {
    vec2 cell = floor(p);
    float minDist = 10.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 neighbor = cell + vec2(float(x), float(y));
            vec2 jitter = vec2(hash21(neighbor), hash21(neighbor + vec2(13.7, 71.3)));
            vec2 point = neighbor + jitter;
            minDist = min(minDist, length(p - point));
        }
    }
    return minDist;
}

// Shortest wraparound distance between two points on a UV cylinder
// (x wraps 0..1, y does not - fine for a small spot feature).
float wrapDist(vec2 a, vec2 b) {
    float dx = min(abs(a.x - b.x), 1.0 - abs(a.x - b.x));
    float dy = a.y - b.y;
    return length(vec2(dx, dy));
}

void main() {
    vec3 normal = normalize(vNormalWorld);
    vec3 viewDir = normalize(uViewPosWorld - vFragWorldPos);

    vec3 color = uBaseColor;

    if (uIsKnownBody == 1) {
        // Real Solar System planet: banded blend of primary/secondary,
        // then layer on whichever real features this planet has.
        float bandNoise = fbm(vec2(vUV.x * 4.0, vUV.y * 9.0) + uSeed) * 0.18;
        float band = sin((vUV.y + bandNoise) * 22.0 + uTime * 0.04);
        float bandMix = smoothstep(-1.0, 1.0, band);
        color = mix(uBaseColor, uSecondaryColor, bandMix * 0.6);

        if (uHasContinents == 1) {
            // Earth: fbm as a land/ocean mask instead of horizontal bands.
            float landMask = fbm(vUV * 5.0 + uSeed * 0.3);
            float landEdge = smoothstep(0.42, 0.5, landMask);
            color = mix(uBaseColor, uSecondaryColor, landEdge);
            float clouds = fbm(vUV * 8.0 + vec2(uTime * 0.015, 0.0)) * 0.4;
            color += vec3(clouds * 0.12);
        }

        if (uHasPolarCaps == 1) {
            float poleDist = min(vUV.y, 1.0 - vUV.y);
            float cap = smoothstep(0.12, 0.03, poleDist);
            color = mix(color, vec3(0.93, 0.94, 0.97), cap);
        }

        if (uHasSpot == 1) {
            float d = wrapDist(vUV, uSpotUV);
            float spotMask = smoothstep(0.10, 0.05, d);
            color = mix(color, uSpotColor, spotMask);
        }

        if (uHazeIntensity > 0.0) {
            float haze = fbm(vUV * 6.0 + vec2(uTime * 0.03, 0.0));
            color = mix(color, uBaseColor * 1.25, haze * uHazeIntensity);
        }
    } else if (uPlanetType == 5 || uPlanetType == 4 || uPlanetType == 3) {
        // Gas giant / ice giant / mini-Neptune: horizontal cloud bands,
        // slowly animated, with a bit of turbulent wobble in the band edges.
        float turbulence = fbm(vec2(vUV.x * 5.0, vUV.y * 18.0) + uSeed) * 0.18;
        float band = sin((vUV.y + turbulence) * 28.0 + uTime * 0.06);
        float bandMix = smoothstep(-1.0, 1.0, band);
        color = mix(uBaseColor * 0.55, uBaseColor * 1.35, bandMix);
        // A little extra swirl detail on top.
        float swirl = fbm(vUV * 10.0 + uSeed + uTime * 0.02) * 0.15;
        color += swirl * uBaseColor * 0.3;
    } else if (uPlanetType == 1 || uPlanetType == 2) {
        // Rocky / super-Earth: cratered terrain via jittered-cell distance
        // field, plus broad highland/lowland color variation from fbm.
        vec2 p = vUV * 12.0 + uSeed;
        float d = cellDistance(p);
        float craterFloor = smoothstep(0.0, 0.5, d);
        float craterRim = smoothstep(0.32, 0.42, d) - smoothstep(0.42, 0.52, d);
        float highlands = fbm(vUV * 6.0 + uSeed) * 0.35;
        color = uBaseColor * (0.65 + 0.35 * craterFloor + highlands) + craterRim * 0.22;
    } else {
        // Unknown composition: flat, slightly noisy gray - deliberately
        // unremarkable since we have no physical data to work from.
        float n = fbm(vUV * 8.0 + uSeed) * 0.15;
        color = uBaseColor * (0.85 + n);
    }

    // Fake terminator-style shading from view angle (stylized, not a real
    // light source) so the sphere doesn't look flat.
    float ndotv = clamp(dot(normal, viewDir), 0.0, 1.0);
    color *= mix(0.35, 1.0, ndotv);

    // CRT-style rim glow, tinted with the same phosphor accent used
    // elsewhere in the app, for visual continuity with the orbit view.
    float rimGlow = pow(1.0 - ndotv, 3.0);
    color += uAccentColor * rimGlow * 0.35;

    FragColor = vec4(color, 1.0);
}
)";

const char* kRingVS = R"(
#version 330 core
layout(location = 0) in vec2 aDir;    // unit-circle direction (cos, sin)
layout(location = 1) in float aRadial; // 0 = inner rim, 1 = outer rim
uniform mat4 uMVP;
uniform float uInnerRadius;
uniform float uOuterRadius;
out float vRadial;
void main() {
    float r = mix(uInnerRadius, uOuterRadius, aRadial);
    vec3 localPos = vec3(aDir.x * r, 0.0, aDir.y * r);
    vRadial = aRadial;
    gl_Position = uMVP * vec4(localPos, 1.0);
}
)";

// Stylized ring banding - a radial-only pattern (real ring gaps like
// Saturn's Cassini Division ARE purely radial, so this is physically
// reasonable in structure even though the exact band positions here are
// illustrative, not measured).
const char* kRingFS = R"(
#version 330 core
in float vRadial;
out vec4 FragColor;
uniform vec3 uColor;
uniform float uOpacity;
uniform float uSeed;

float hash1(float x) { return fract(sin(x * 127.1) * 43758.5453); }

void main() {
    float r = vRadial;
    float band = sin(r * 40.0 + uSeed) * 0.5 + 0.5;
    float gapMask = smoothstep(0.15, 0.35, abs(sin(r * 23.0 + uSeed * 3.1)));
    float shade = mix(0.55, 1.0, band) * mix(0.5, 1.0, gapMask);

    float edgeFade = smoothstep(0.0, 0.06, r) * smoothstep(1.0, 0.94, r);
    FragColor = vec4(uColor * shade, uOpacity * edgeFade);
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

    glGenVertexArrays(1, &m_pointsVao);
    glGenBuffers(1, &m_pointsVbo);

    buildSphereWireframe();
    buildSolidSphereMesh();
    buildRingMesh();

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return m_lineShader != 0 && m_pointShader != 0 && m_surfaceShader != 0 && m_ringShader != 0;
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
    constexpr int kRings = 32;
    constexpr int kSegs = 48;
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
    } else {
        glUniform1i(glGetUniformLocation(m_surfaceShader, "uIsKnownBody"), 0);
        glUniform3f(glGetUniformLocation(m_surfaceShader, "uBaseColor"), baseColor.r, baseColor.g, baseColor.b);
        glUniform1i(glGetUniformLocation(m_surfaceShader, "uHasContinents"), 0);
        glUniform1i(glGetUniformLocation(m_surfaceShader, "uHasPolarCaps"), 0);
        glUniform1i(glGetUniformLocation(m_surfaceShader, "uHasSpot"), 0);
        glUniform1f(glGetUniformLocation(m_surfaceShader, "uHazeIntensity"), 0.0f);
    }

    glBindVertexArray(m_solidSphereVao);
    glDrawElements(GL_TRIANGLES, m_solidSphereIndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Renderer::renderPlanetRings(const glm::mat4& viewProj, const glm::mat4& model,
                                  float innerRadius, float outerRadius,
                                  const glm::vec3& color, float opacity, float seed) {
    glUseProgram(m_ringShader);
    glm::mat4 mvp = viewProj * model;
    glUniformMatrix4fv(glGetUniformLocation(m_ringShader, "uMVP"), 1, GL_FALSE, &mvp[0][0]);
    glUniform1f(glGetUniformLocation(m_ringShader, "uInnerRadius"), innerRadius);
    glUniform1f(glGetUniformLocation(m_ringShader, "uOuterRadius"), outerRadius);
    glUniform3f(glGetUniformLocation(m_ringShader, "uColor"), color.r, color.g, color.b);
    glUniform1f(glGetUniformLocation(m_ringShader, "uOpacity"), opacity);
    glUniform1f(glGetUniformLocation(m_ringShader, "uSeed"), seed);

    // Transparent geometry: test depth against the opaque planet (so the
    // far half of the ring is correctly hidden behind it) but don't write
    // depth (so it doesn't occlude itself or the wireframe grid drawn after).
    glDepthMask(GL_FALSE);
    glBindVertexArray(m_ringVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, m_ringVertCount);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
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
