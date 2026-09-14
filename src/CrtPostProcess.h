#pragma once
#include <glad/glad.h>

// Renders the scene into an offscreen framebuffer, then draws that
// framebuffer to the screen through a scanline/glow/vignette shader
// for the CRT vector-scope look. Call beginScene() before drawing
// the 3D scene, endSceneAndPresent() after.
class CrtPostProcess {
public:
    bool init(int width, int height);
    void resize(int width, int height);
    void shutdown();

    void beginScene();               // binds offscreen FBO, clears it
    void endSceneAndPresent(float timeSeconds); // draws fullscreen quad with CRT shader

private:
    GLuint m_fbo = 0, m_colorTex = 0, m_depthRbo = 0;
    GLuint m_quadVao = 0, m_quadVbo = 0;
    GLuint m_shader = 0;
    int m_width = 0, m_height = 0;

    void createFramebuffer(int width, int height);
    void destroyFramebuffer();
};
