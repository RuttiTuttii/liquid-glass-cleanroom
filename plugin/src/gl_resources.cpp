// Clean-Room Liquid Glass Hyprland Plugin — Owned OpenGL ES 3.2 Pipeline Implementation
// SPDX-License-Identifier: MIT

#include "gl_resources.hpp"
#include "../build/generated_shaders.hpp"

#include <EGL/egl.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace LiquidGlassPlugin {

// -----------------------------------------------------------------------------
// [1] RAII OpenGL ES 3.2 State Guard
// -----------------------------------------------------------------------------

CGLStateGuard::CGLStateGuard() {
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_drawFBO);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &m_readFBO);
    glGetIntegerv(GL_CURRENT_PROGRAM, &m_program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &m_vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &m_arrayBuf);
    glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &m_uboGeneric);
    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 0, &m_uboSlot0);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &m_activeTex);

    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_texUnit0);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_texUnit1);
    glActiveTexture(GL_TEXTURE2);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_texUnit2);

    glGetIntegerv(GL_VIEWPORT, m_viewport);
    glGetIntegerv(GL_SCISSOR_BOX, m_scissor);
    m_scissorTest = glIsEnabled(GL_SCISSOR_TEST);
    m_blend       = glIsEnabled(GL_BLEND);
    m_cullFace    = glIsEnabled(GL_CULL_FACE);
    m_depthTest   = glIsEnabled(GL_DEPTH_TEST);
    m_stencilTest = glIsEnabled(GL_STENCIL_TEST);
}

CGLStateGuard::~CGLStateGuard() {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(m_texUnit0));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(m_texUnit1));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(m_texUnit2));
    glActiveTexture(static_cast<GLenum>(m_activeTex));

    glBindBufferBase(GL_UNIFORM_BUFFER, 0, static_cast<GLuint>(m_uboSlot0));
    glBindBuffer(GL_UNIFORM_BUFFER, static_cast<GLuint>(m_uboGeneric));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(m_arrayBuf));
    glBindVertexArray(static_cast<GLuint>(m_vao));
    glUseProgram(static_cast<GLuint>(m_program));

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(m_readFBO));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(m_drawFBO));
    glViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
    glScissor(m_scissor[0], m_scissor[1], m_scissor[2], m_scissor[3]);

    auto setCap = [](GLenum cap, GLboolean enabled) {
        if (enabled)
            glEnable(cap);
        else
            glDisable(cap);
    };
    setCap(GL_SCISSOR_TEST, m_scissorTest);
    setCap(GL_BLEND, m_blend);
    setCap(GL_CULL_FACE, m_cullFace);
    setCap(GL_DEPTH_TEST, m_depthTest);
    setCap(GL_STENCIL_TEST, m_stencilTest);
}

// -----------------------------------------------------------------------------
// [2] Shader Compilation, Fullscreen Quad VAO & UBO Setup
// -----------------------------------------------------------------------------

GLuint CLiquidGlassPipeline::compileStage(GLenum type, const char* src, const char* label, std::string& errOut) {
    GLuint sh = glCreateShader(type);
    if (!sh) {
        errOut = std::string("glCreateShader failed for ") + label;
        return 0;
    }
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(static_cast<size_t>(std::max(logLen, 1)), '\0');
        glGetShaderInfoLog(sh, logLen, nullptr, log.data());
        errOut = std::string("GLSL compile error [") + label + "]: " + log;
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint CLiquidGlassPipeline::linkProgram(GLuint vert, GLuint frag, const char* label, std::string& errOut) {
    GLuint prog = glCreateProgram();
    if (!prog) {
        errOut = std::string("glCreateProgram failed for ") + label;
        return 0;
    }
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(static_cast<size_t>(std::max(logLen, 1)), '\0');
        glGetProgramInfoLog(prog, logLen, nullptr, log.data());
        errOut = std::string("GLSL link error [") + label + "]: " + log;
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

bool CLiquidGlassPipeline::ensureQuadAndUbo(std::string& errOut) {
    // Fullscreen NDC triangle strip matching glass_quad.vert attributes:
    // location 0: vec4 qt_Vertex (x, y, 0, 1)
    // location 1: vec2 qt_MultiTexCoord0 (u, v)
    static constexpr float kQuadVerts[4 * 6] = {
        -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 1.0f,
    };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ubo);
    if (!m_vao || !m_vbo || !m_ubo) {
        errOut = "Failed to allocate GL VAO/VBO/UBO";
        return false;
    }

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVerts), kQuadVerts, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(4 * sizeof(float)));

    glBindBuffer(GL_UNIFORM_BUFFER, m_ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(GlassPanelUniforms), nullptr, GL_DYNAMIC_DRAW);

    // 1x1 neutral zero texture bound cleanly to unused optional SDF map sampler (binding 2).
    static constexpr uint8_t kZeroPixel[4] = {0, 0, 0, 0};
    glGenTextures(1, &m_dummyTex);
    glBindTexture(GL_TEXTURE_2D, m_dummyTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, kZeroPixel);

    return true;
}

bool CLiquidGlassPipeline::ensurePrograms(std::string& errOut) {
    GLuint vs = compileStage(GL_VERTEX_SHADER, Shaders::kQuadVertES32, "quad.vert", errOut);
    if (!vs)
        return false;

    auto buildPair = [&](const char* fragSrc, const char* name, GLuint& outProg) -> bool {
        GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragSrc, name, errOut);
        if (!fs)
            return false;
        outProg = linkProgram(vs, fs, name, errOut);
        glDeleteShader(fs);
        return outProg != 0;
    };

    const bool okCopy   = buildPair(Shaders::kCopyPrepareFragES32, "copy_prepare.frag", m_copyProg);
    const bool okDown   = okCopy && buildPair(Shaders::kDownsampleFragES32, "downsample_clamped.frag", m_downProg);
    const bool okFinish = okDown && buildPair(Shaders::kPanelFinishFragES32, "liquid_glass_panel.frag", m_finishProg);
    glDeleteShader(vs);
    if (!okFinish)
        return false;

    m_encodeSrgbLoc = glGetUniformLocation(m_finishProg, "u_encodeSrgb");
    return true;
}

bool CLiquidGlassPipeline::init(std::string& errOut) {
    if (m_initialized)
        return true;

    CGLStateGuard guard;
    if (!ensureQuadAndUbo(errOut) || !ensurePrograms(errOut)) {
        destroy(true);
        return false;
    }

    m_initialized = true;
    return true;
}

void CLiquidGlassPipeline::releasePyramid() {
    for (GLuint& fbo : m_pyrFBOs) {
        if (fbo) {
            glDeleteFramebuffers(1, &fbo);
            fbo = 0;
        }
    }
    if (m_pyrTex) {
        glDeleteTextures(1, &m_pyrTex);
        m_pyrTex = 0;
    }
    m_pyrW      = 0;
    m_pyrH      = 0;
    m_mipLevels = 0;
    m_pyrFp16   = false;
}

void CLiquidGlassPipeline::destroy(bool hasCurrentContext) {
    const bool canCallGL = hasCurrentContext && (eglGetCurrentContext() != EGL_NO_CONTEXT);
    if (canCallGL) {
        releasePyramid();
        if (m_finishProg) glDeleteProgram(m_finishProg);
        if (m_downProg) glDeleteProgram(m_downProg);
        if (m_copyProg) glDeleteProgram(m_copyProg);
        if (m_dummyTex) glDeleteTextures(1, &m_dummyTex);
        if (m_ubo) glDeleteBuffers(1, &m_ubo);
        if (m_vbo) glDeleteBuffers(1, &m_vbo);
        if (m_vao) glDeleteVertexArrays(1, &m_vao);
    }
    m_finishProg = m_downProg = m_copyProg = 0;
    m_dummyTex = m_ubo = m_vbo = m_vao = 0;
    m_pyrFBOs.fill(0);
    m_pyrTex        = 0;
    m_pyrW = m_pyrH = m_mipLevels = 0;
    m_pyrFp16       = false;
    m_encodeSrgbLoc = -1;
    m_initialized   = false;
}

// -----------------------------------------------------------------------------
// [3] Mip-Pyramid Storage & Per-Level FBO Management
// -----------------------------------------------------------------------------

bool CLiquidGlassPipeline::ensurePyramid(int w, int h, bool wantFp16, std::string& errOut) {
    const int maxDim    = std::max({w, h, 1});
    const int levels    = std::clamp(1 + static_cast<int>(std::floor(std::log2(static_cast<double>(maxDim)))), 1, kMaxMipLevels);

    if (m_pyrTex && m_pyrW == w && m_pyrH == h && m_mipLevels == levels && m_pyrFp16 == wantFp16)
        return true;

    releasePyramid();

    auto tryAllocate = [&](GLenum internalFormat) -> bool {
        glGenTextures(1, &m_pyrTex);
        glBindTexture(GL_TEXTURE_2D, m_pyrTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
        glTexStorage2D(GL_TEXTURE_2D, levels, internalFormat, w, h);

        glGenFramebuffers(levels, m_pyrFBOs.data());
        for (int lvl = 0; lvl < levels; ++lvl) {
            glBindFramebuffer(GL_FRAMEBUFFER, m_pyrFBOs[lvl]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_pyrTex, lvl);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                return false;
        }
        return true;
    };

    if (wantFp16 && tryAllocate(GL_RGBA16F)) {
        m_pyrFp16 = true;
    } else {
        releasePyramid();
        if (!tryAllocate(GL_RGBA8)) {
            releasePyramid();
            errOut = "Failed to create mip-pyramid FBO chain";
            return false;
        }
        m_pyrFp16 = false;
    }

    m_pyrW      = w;
    m_pyrH      = h;
    m_mipLevels = levels;
    return true;
}

void CLiquidGlassPipeline::drawFullscreenQuad() {
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// -----------------------------------------------------------------------------
// [4] Three-Stage Clean-Room Execution (Copy -> Downsample -> Liquid Glass)
// -----------------------------------------------------------------------------

bool CLiquidGlassPipeline::renderPanel(
    GLuint                       srcTex,
    bool                         srcForceOpaque,
    bool                         isHdrLinear,
    GLuint                       targetFBO,
    int                          fbW,
    int                          fbH,
    const PixelBox&              panelBox,
    float                        radiusPx,
    float                        roundingPower,
    float                        strength,
    const std::vector<PixelBox>& damageBoxes,
    const GlassRenderParams&     params,
    std::string&                 errOut
) {
    if (fbW <= 0 || fbH <= 0 || !srcTex || !targetFBO || damageBoxes.empty())
        return true;

    if (!m_initialized && !init(errOut))
        return false;

    CGLStateGuard guard;
    if (!ensurePyramid(fbW, fbH, true, errOut))
        return false;

    // 1. Determine tight scissored backdrop footprint needed for every refracted
    //    dispersion tap and deepest mip level inside `damageBoxes`.
    int dmgMinX = fbW;
    int dmgMinY = fbH;
    int dmgMaxX = 0;
    int dmgMaxY = 0;
    for (const PixelBox& b : damageBoxes) {
        if (b.w <= 0 || b.h <= 0)
            continue;
        dmgMinX = std::min(dmgMinX, std::max(0, b.x));
        dmgMinY = std::min(dmgMinY, std::max(0, b.y));
        dmgMaxX = std::max(dmgMaxX, std::min(fbW, b.x + b.w));
        dmgMaxY = std::max(dmgMaxY, std::min(fbH, b.y + b.h));
    }
    if (dmgMinX >= dmgMaxX || dmgMinY >= dmgMaxY)
        return true;

    const float sScale     = std::clamp(strength, 0.0f, 2.0f);
    const int   mipApron   = 1 << std::max(m_mipLevels - 1, 0);
    const int   padPx      = static_cast<int>(std::ceil((params.refractionPx + params.dispersionPx) * sScale)) + mipApron * 2 + 4;
    const int   pyrX0      = std::max(0, dmgMinX - padPx);
    const int   pyrY0      = std::max(0, dmgMinY - padPx);
    const int   pyrX1      = std::min(fbW, dmgMaxX + padPx);
    const int   pyrY1      = std::min(fbH, dmgMaxY + padPx);
    const int   maxPyrLvl  = std::max(m_mipLevels - 1, 0);

    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_SCISSOR_TEST);

    glBindVertexArray(m_vao);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_ubo);

    const float uMin = static_cast<float>(pyrX0) / static_cast<float>(fbW);
    const float vMin = static_cast<float>(pyrY0) / static_cast<float>(fbH);
    const float uMax = static_cast<float>(pyrX1) / static_cast<float>(fbW);
    const float vMax = static_cast<float>(pyrY1) / static_cast<float>(fbH);

    // 2. Stage 1: Copy source scene -> mip level 0 (`m_pyrTex`), decoding sRGB to linear light.
    {
        CopyPrepareUniforms cu{};
        setIdentityMat4(cu.qt_Matrix);
        cu.qt_Opacity     = 1.0f;
        cu.forceOpaque    = srcForceOpaque ? 1.0f : 0.0f;
        cu.decodeSrgb     = isHdrLinear ? 0.0f : 1.0f;
        cu.uvClampRect[0] = uMin + 0.5f / static_cast<float>(fbW);
        cu.uvClampRect[1] = vMin + 0.5f / static_cast<float>(fbH);
        cu.uvClampRect[2] = uMax - 0.5f / static_cast<float>(fbW);
        cu.uvClampRect[3] = vMax - 0.5f / static_cast<float>(fbH);
        if (cu.uvClampRect[0] > cu.uvClampRect[2])
            cu.uvClampRect[0] = cu.uvClampRect[2] = 0.5f * (uMin + uMax);
        if (cu.uvClampRect[1] > cu.uvClampRect[3])
            cu.uvClampRect[1] = cu.uvClampRect[3] = 0.5f * (vMin + vMax);

        glBindBuffer(GL_UNIFORM_BUFFER, m_ubo);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(cu), &cu);

        glBindFramebuffer(GL_FRAMEBUFFER, m_pyrFBOs[0]);
        glViewport(0, 0, fbW, fbH);
        glScissor(pyrX0, pyrY0, pyrX1 - pyrX0, pyrY1 - pyrY0);

        glUseProgram(m_copyProg);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, srcTex);
        drawFullscreenQuad();
    }

    // 3. Stage 2: Smooth 13-tap clamped mip-pyramid downsampling across levels `1..max`.
    glUseProgram(m_downProg);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_pyrTex);

    for (int lvl = 1; lvl < m_mipLevels; ++lvl) {
        const int srcW = std::max(1, fbW >> (lvl - 1));
        const int srcH = std::max(1, fbH >> (lvl - 1));
        const int dstW = std::max(1, fbW >> lvl);
        const int dstH = std::max(1, fbH >> lvl);

        DownsampleUniforms du{};
        setIdentityMat4(du.qt_Matrix);
        du.qt_Opacity      = 1.0f;
        du.srcLod          = static_cast<float>(lvl - 1);
        du.srcTexelSize[0] = 1.0f / static_cast<float>(srcW);
        du.srcTexelSize[1] = 1.0f / static_cast<float>(srcH);
        du.uvClampRect[0]  = std::clamp(uMin + 0.5f * du.srcTexelSize[0], 0.0f, 1.0f);
        du.uvClampRect[1]  = std::clamp(vMin + 0.5f * du.srcTexelSize[1], 0.0f, 1.0f);
        du.uvClampRect[2]  = std::clamp(uMax - 0.5f * du.srcTexelSize[0], 0.0f, 1.0f);
        du.uvClampRect[3]  = std::clamp(vMax - 0.5f * du.srcTexelSize[1], 0.0f, 1.0f);
        if (du.uvClampRect[0] > du.uvClampRect[2])
            du.uvClampRect[0] = du.uvClampRect[2] = 0.5f * (uMin + uMax);
        if (du.uvClampRect[1] > du.uvClampRect[3])
            du.uvClampRect[1] = du.uvClampRect[3] = 0.5f * (vMin + vMax);

        glBindBuffer(GL_UNIFORM_BUFFER, m_ubo);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(du), &du);

        const int sx0 = std::max(0, static_cast<int>(std::floor(uMin * dstW)) - 2);
        const int sy0 = std::max(0, static_cast<int>(std::floor(vMin * dstH)) - 2);
        const int sx1 = std::min(dstW, static_cast<int>(std::ceil(uMax * dstW)) + 2);
        const int sy1 = std::min(dstH, static_cast<int>(std::ceil(vMax * dstH)) + 2);

        glBindFramebuffer(GL_FRAMEBUFFER, m_pyrFBOs[lvl]);
        glViewport(0, 0, dstW, dstH);
        glScissor(sx0, sy0, std::max(1, sx1 - sx0), std::max(1, sy1 - sy0));
        drawFullscreenQuad();
    }

    // 4. Stage 3: Analytic G2-squircle refraction + spectral dispersion + luma specular finish.
    {
        GlassPanelUniforms pu{};
        fillPanelUniforms(
            pu,
            fbW,
            fbH,
            static_cast<float>(panelBox.x),
            static_cast<float>(panelBox.y),
            static_cast<float>(panelBox.w),
            static_cast<float>(panelBox.h),
            radiusPx,
            roundingPower,
            sScale,
            pyrX0,
            pyrY0,
            pyrX1,
            pyrY1,
            maxPyrLvl,
            isHdrLinear,
            params
        );

        glBindBuffer(GL_UNIFORM_BUFFER, m_ubo);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(pu), &pu);

        glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
        glViewport(0, 0, fbW, fbH);

        glUseProgram(m_finishProg);
        if (m_encodeSrgbLoc >= 0)
            glUniform1f(m_encodeSrgbLoc, isHdrLinear ? 0.0f : 1.0f);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_pyrTex);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_dummyTex);

        for (const PixelBox& b : damageBoxes) {
            const int x0 = std::max(0, b.x);
            const int y0 = std::max(0, b.y);
            const int x1 = std::min(fbW, b.x + b.w);
            const int y1 = std::min(fbH, b.y + b.h);
            if (x1 <= x0 || y1 <= y0)
                continue;
            glScissor(x0, y0, x1 - x0, y1 - y0);
            drawFullscreenQuad();
        }
    }

    return true;
}

} // namespace LiquidGlassPlugin
