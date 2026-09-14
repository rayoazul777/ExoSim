#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

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

    // Draws all orbit rings + current planet positions.
    // viewProj: combined camera view*projection matrix.
    // planetPositions: current position (AU) per planet, same order as setOrbitPath calls.
    void render(const glm::mat4& viewProj,
                const std::vector<glm::vec3>& planetPositions);

private:
    GLuint m_lineShader = 0;
    GLuint m_pointShader = 0;

    struct OrbitBuffer { GLuint vao = 0, vbo = 0; GLsizei count = 0; };
    std::vector<OrbitBuffer> m_orbits;

    GLuint m_pointsVao = 0, m_pointsVbo = 0;

    GLuint compileShader(GLenum type, const char* src);
    GLuint linkProgram(GLuint vs, GLuint fs);
};
