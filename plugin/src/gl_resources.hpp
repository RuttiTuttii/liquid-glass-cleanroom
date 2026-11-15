// Clean-Room Liquid Glass Hyprland Plugin — Owned OpenGL ES 3.2 Pipeline & State Guard
// SPDX-License-Identifier: MIT

#pragma once

#include <GLES3/gl32.h>
#include <array>
#include <string>
#include <vector>

#include "glass_uniforms.hpp"

namespace LiquidGlassPlugin {

// -----------------------------------------------------------------------------
// [1] Integer Pixel Damage Box Helper & RAII OpenGL State Guard
// -----------------------------------------------------------------------------

struct PixelBox {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

class CGLStateGuard {
  public:
    CGLStateGuard();
    ~CGLStateGuard();

    CGLStateGuard(const CGLStateGuard&)            = delete;
    CGLStateGuard& operator=(const CGLStateGuard&) = delete;

  private:
    GLint     m_drawFBO      = 0;
    GLint     m_readFBO      = 0;
    GLint     m_program      = 0;
    GLint     m_vao          = 0;
    GLint     m_arrayBuf     = 0;
    GLint     m_uboGeneric   = 0;
    GLint     m_uboSlot0     = 0;
    GLint     m_activeTex    = GL_TEXTURE0;
    GLint     m_texUnit0     = 0;
    GLint     m_texUnit1     = 0;
    GLint     m_texUnit2     = 0;
    GLint     m_viewport[4]  = {0, 0, 0, 0};
    GLint     m_scissor[4]   = {0, 0, 0, 0};
    GLboolean m_scissorTest  = GL_FALSE;
    GLboolean m_blend        = GL_FALSE;
    GLboolean m_cullFace     = GL_FALSE;
    GLboolean m_depthTest    = GL_FALSE;
    GLboolean m_stencilTest  = GL_FALSE;
};

// -----------------------------------------------------------------------------
// [2] Mip-Pyramid + Liquid Glass Finish OpenGL ES 3.2 Renderer
// -----------------------------------------------------------------------------

class CLiquidGlassPipeline {
  public:
    static constexpr int kMaxMipLevels = 5;

    CLiquidGlassPipeline()  = default;
    ~CLiquidGlassPipeline() {
        destroy(false);
    }

    CLiquidGlassPipeline(const CLiquidGlassPipeline&)            = delete;
    CLiquidGlassPipeline& operator=(const CLiquidGlassPipeline&) = delete;

    bool init(std::string& errOut);
    void destroy(bool hasCurrentContext = true);
    bool isInitialized() const {
        return m_initialized;
    }

    // Executes linear copy -> 13-tap clamped mip pyramid -> liquid glass finish
    // into `targetFBO` (`targetW` x `targetH`). Caller retains ownership of `srcTex`
    // and `targetFBO`.
    bool renderPanel(
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
    );

    GLuint pyramidTexture() const {
        return m_pyrTex;
    }
    int mipLevels() const {
        return m_mipLevels;
    }
    bool isFp16() const {
        return m_pyrFp16;
    }

  private:
    bool ensureQuadAndUbo(std::string& errOut);
    bool ensurePrograms(std::string& errOut);
    bool ensurePyramid(int w, int h, bool wantFp16, std::string& errOut);
    void releasePyramid();
    void drawFullscreenQuad();

    static GLuint compileStage(GLenum type, const char* src, const char* label, std::string& errOut);
    static GLuint linkProgram(GLuint vert, GLuint frag, const char* label, std::string& errOut);

    bool                             m_initialized   = false;
    GLuint                           m_vao           = 0;
    GLuint                           m_vbo           = 0;
    GLuint                           m_ubo           = 0;
    GLuint                           m_dummyTex      = 0;
    GLuint                           m_copyProg      = 0;
    GLuint                           m_downProg      = 0;
    GLuint                           m_finishProg    = 0;
    GLint                            m_encodeSrgbLoc = -1;

    GLuint                           m_pyrTex        = 0;
    std::array<GLuint, kMaxMipLevels> m_pyrFBOs      = {};
    int                              m_pyrW          = 0;
    int                              m_pyrH          = 0;
    int                              m_mipLevels     = 0;
    bool                             m_pyrFp16       = false;
};

} // namespace LiquidGlassPlugin
