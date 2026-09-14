#include "CrtPostProcess.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace {
const char* kQuadVS = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

std::string readFile(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
}

void CrtPostProcess::createFramebuffer(int width, int height) {
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_colorTex);
    glBindTexture(GL_TEXTURE_2D, m_colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTex, 0);

    glGenRenderbuffers(1, &m_depthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[CrtPostProcess] Framebuffer incomplete!\n";
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void CrtPostProcess::destroyFramebuffer() {
    if (m_colorTex) glDeleteTextures(1, &m_colorTex);
    if (m_depthRbo) glDeleteRenderbuffers(1, &m_depthRbo);
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
    m_colorTex = m_depthRbo = m_fbo = 0;
}

bool CrtPostProcess::init(int width, int height) {
    m_width = width; m_height = height;
    createFramebuffer(width, height);

    // Fullscreen quad (two triangles), pos + uv interleaved.
    float verts[] = {
        -1.f, -1.f,  0.f, 0.f,
         1.f, -1.f,  1.f, 0.f,
         1.f,  1.f,  1.f, 1.f,
        -1.f, -1.f,  0.f, 0.f,
         1.f,  1.f,  1.f, 1.f,
        -1.f,  1.f,  0.f, 1.f,
    };
    glGenVertexArrays(1, &m_quadVao);
    glGenBuffers(1, &m_quadVbo);
    glBindVertexArray(m_quadVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    // Fragment shader lives on disk (shaders/crt.frag) so you can
    // tweak the look live without recompiling C++.
    std::string fragSrc = readFile("shaders/crt.frag");
    if (fragSrc.empty()) {
        std::cerr << "[CrtPostProcess] shaders/crt.frag not found or empty - check working directory.\n";
        return false;
    }

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &kQuadVS, nullptr);
    glCompileShader(vs);

    const char* fragSrcPtr = fragSrc.c_str();
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragSrcPtr, nullptr);
    glCompileShader(fs);
    GLint ok = 0;
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(fs, sizeof(log), nullptr, log);
        std::cerr << "[CrtPostProcess] crt.frag compile error: " << log << "\n";
    }

    m_shader = glCreateProgram();
    glAttachShader(m_shader, vs);
    glAttachShader(m_shader, fs);
    glLinkProgram(m_shader);
    glDeleteShader(vs);
    glDeleteShader(fs);

    return true;
}

void CrtPostProcess::resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    destroyFramebuffer();
    createFramebuffer(width, height);
    m_width = width; m_height = height;
}

void CrtPostProcess::shutdown() {
    destroyFramebuffer();
    glDeleteBuffers(1, &m_quadVbo);
    glDeleteVertexArrays(1, &m_quadVao);
    glDeleteProgram(m_shader);
}

void CrtPostProcess::beginScene() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
    glClearColor(0.01f, 0.02f, 0.015f, 1.0f); // near-black CRT background
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
}

void CrtPostProcess::endSceneAndPresent(float timeSeconds) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_width, m_height);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_colorTex);
    glUniform1i(glGetUniformLocation(m_shader, "uScene"), 0);
    glUniform1f(glGetUniformLocation(m_shader, "uTime"), timeSeconds);
    glUniform2f(glGetUniformLocation(m_shader, "uResolution"),
                static_cast<float>(m_width), static_cast<float>(m_height));

    glBindVertexArray(m_quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}
