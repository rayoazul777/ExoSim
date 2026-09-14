#include "Renderer.h"
#include <iostream>

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
void main() { FragColor = vec4(uColor, 0.55); }
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

    glGenVertexArrays(1, &m_pointsVao);
    glGenBuffers(1, &m_pointsVbo);

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return m_lineShader != 0 && m_pointShader != 0;
}

void Renderer::shutdown() {
    for (auto& o : m_orbits) {
        glDeleteBuffers(1, &o.vbo);
        glDeleteVertexArrays(1, &o.vao);
    }
    m_orbits.clear();
    glDeleteBuffers(1, &m_pointsVbo);
    glDeleteVertexArrays(1, &m_pointsVao);
    glDeleteProgram(m_lineShader);
    glDeleteProgram(m_pointShader);
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
