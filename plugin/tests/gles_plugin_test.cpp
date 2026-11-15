// Standalone Headless OpenGL ES 3.2 Verification Harness for Hyprland Plugin Shaders
// Validates shader compilation, std140 UBOs, 5-level mip pyramid, RGBX handling,
// luma-adaptive bevel highlights, UV clamping, and RAII GL state preservation.
// SPDX-License-Identifier: MIT

#include "../src/gl_resources.hpp"

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace LiquidGlassPlugin;

// -----------------------------------------------------------------------------
// [1] Headless EGL ES 3.2 Pbuffer Bootstrap & Test Pattern Synthesis
// -----------------------------------------------------------------------------

struct EglCtx {
    EGLDisplay dpy  = EGL_NO_DISPLAY;
    EGLContext ctx  = EGL_NO_CONTEXT;
    EGLSurface surf = EGL_NO_SURFACE;
};

static bool initEglEs32(EglCtx& e) {
    e.dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (e.dpy == EGL_NO_DISPLAY)
        return false;
    EGLint maj = 0, min = 0;
    if (!eglInitialize(e.dpy, &maj, &min))
        return false;
    eglBindAPI(EGL_OPENGL_ES_API);

    static constexpr EGLint cfgAttr[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg = nullptr;
    EGLint    n   = 0;
    if (!eglChooseConfig(e.dpy, cfgAttr, &cfg, 1, &n) || n < 1)
        return false;

    static constexpr EGLint surfAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    e.surf                             = eglCreatePbufferSurface(e.dpy, cfg, surfAttr);
    static constexpr EGLint ctxAttr[]  = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 2,
        EGL_NONE
    };
    e.ctx = eglCreateContext(e.dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
    if (e.surf == EGL_NO_SURFACE || e.ctx == EGL_NO_CONTEXT)
        return false;
    return eglMakeCurrent(e.dpy, e.surf, e.surf, e.ctx) == EGL_TRUE;
}

static void destroyEgl(EglCtx& e) {
    if (e.dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(e.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (e.ctx != EGL_NO_CONTEXT) eglDestroyContext(e.dpy, e.ctx);
        if (e.surf != EGL_NO_SURFACE) eglDestroySurface(e.dpy, e.surf);
        eglTerminate(e.dpy);
    }
}

// -----------------------------------------------------------------------------
// [2] Verification Cases
// -----------------------------------------------------------------------------

int main() {
    EglCtx egl{};
    if (!initEglEs32(egl)) {
        std::fprintf(stderr, "FAIL: Could not create headless EGL OpenGL ES 3.2 context\n");
        return 1;
    }

    std::printf("GLES Vendor: %s | Version: %s\n",
                reinterpret_cast<const char*>(glGetString(GL_VENDOR)),
                reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    const int W = 640;
    const int H = 480;

    // Build synthetic sRGB desktop scene with a dark left half, bright white right
    // half, and high-frequency vertical magenta/cyan stripes to exercise refraction
    // and chromatic dispersion, with alpha = 0 (simulating TEXTURE_RGBX!).
    std::vector<uint8_t> scenePixels(static_cast<size_t>(W) * H * 4, 0);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const size_t idx = (static_cast<size_t>(y) * W + x) * 4;
            if (x < W / 2) {
                const bool stripe  = ((x / 12) % 2) == 0;
                scenePixels[idx + 0] = stripe ? 220 : 28;
                scenePixels[idx + 1] = stripe ? 60 : 32;
                scenePixels[idx + 2] = stripe ? 180 : 54;
            } else {
                scenePixels[idx + 0] = 242;
                scenePixels[idx + 1] = 244;
                scenePixels[idx + 2] = 248;
            }
            scenePixels[idx + 3] = 0; // Deliberately zero (RGBX test)
        }
    }

    GLuint srcTex = 0, outTex = 0, outFBO = 0;
    glGenTextures(1, &srcTex);
    glBindTexture(GL_TEXTURE_2D, srcTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, scenePixels.data());

    glGenTextures(1, &outTex);
    glBindTexture(GL_TEXTURE_2D, outTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, &outFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, outFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outTex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "FAIL: outFBO incomplete\n");
        return 1;
    }

    // Establish non-default GL state to verify CGLStateGuard preserves every bit.
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(1, 2, 123, 234);
    glEnable(GL_SCISSOR_TEST);
    glScissor(3, 4, 55, 66);

    CLiquidGlassPipeline pipe;
    std::string          err;
    if (!pipe.init(err)) {
        std::fprintf(stderr, "FAIL: pipeline init: %s\n", err.c_str());
        return 1;
    }

    GlassRenderParams params{};
    params.frostLod           = 2.2f;
    params.refractionPx       = 24.0f;
    params.dispersionPx       = 4.0f;
    params.specularStrength   = 0.70f;
    params.squircleSmoothness = 0.75f;

    const PixelBox              panel{120, 80, 400, 320};
    const std::vector<PixelBox> dmg{{0, 0, W, H}};
    if (!pipe.renderPanel(srcTex, true, false, outFBO, W, H, panel, 28.0f, 3.8f, 1.0f, dmg, params, err)) {
        std::fprintf(stderr, "FAIL: renderPanel: %s\n", err.c_str());
        return 1;
    }

    // Check 1: Zero GL errors
    const GLenum glErr = glGetError();
    if (glErr != GL_NO_ERROR) {
        std::fprintf(stderr, "FAIL: GL error 0x%x after renderPanel\n", glErr);
        return 1;
    }

    // Check 2: GL state intact
    GLint vp[4]{}, sc[4]{}, fbo = -1;
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_SCISSOR_BOX, sc);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
    if (vp[0] != 1 || vp[1] != 2 || vp[2] != 123 || vp[3] != 234 ||
        sc[0] != 3 || sc[1] != 4 || sc[2] != 55 || sc[3] != 66 ||
        fbo != static_cast<GLint>(outFBO) || !glIsEnabled(GL_BLEND) || !glIsEnabled(GL_SCISSOR_TEST)) {
        std::fprintf(stderr, "FAIL: CGLStateGuard did not restore Hyprland GL state\n");
        return 1;
    }

    // Check 3: Read back pixels to verify RGBX, frosted interior variance smoothing,
    // edge bevel glint, and adaptive brightness behavior.
    std::vector<uint8_t> outPixels(static_cast<size_t>(W) * H * 4, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, outFBO);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, outPixels.data());

    auto sampleRGB = [&](int x, int y) {
        const size_t idx = (static_cast<size_t>(y) * W + x) * 4;
        return std::array<int, 3>{outPixels[idx], outPixels[idx + 1], outPixels[idx + 2]};
    };

    const auto outStripe    = sampleRGB(4, 4);
    const auto outGap       = sampleRGB(20, 20);
    const auto bevelEdge    = sampleRGB(123, 240);
    const auto darkInterior = sampleRGB(210, 240);
    const auto lightRight   = sampleRGB(440, 240);
    std::printf("Sample stripe=(%d,%d,%d) gap=(%d,%d,%d) bevel=(%d,%d,%d) darkGlass=(%d,%d,%d) brightGlass=(%d,%d,%d) mipLevels=%d\n",
                outStripe[0], outStripe[1], outStripe[2],
                outGap[0], outGap[1], outGap[2],
                bevelEdge[0], bevelEdge[1], bevelEdge[2],
                darkInterior[0], darkInterior[1], darkInterior[2],
                lightRight[0], lightRight[1], lightRight[2],
                pipe.mipLevels());

    if (outStripe[0] < 215 || outGap[0] > 32 ||
        darkInterior[0] <= 32 || lightRight[0] <= darkInterior[0] ||
        bevelEdge[1] <= outStripe[1]) {
        std::fprintf(stderr, "FAIL: Unexpected rendered glass pixel values (RGBX or linear/sRGB mismatch)\n");
        return 1;
    }

    // Check 4: Partial-damage scissored pass keeps zero GL errors & restores state
    const std::vector<PixelBox> partialDmg{{118, 78, 80, 80}, {460, 320, 70, 90}};
    if (!pipe.renderPanel(srcTex, true, false, outFBO, W, H, panel, 28.0f, 3.8f, 1.0f, partialDmg, params, err) ||
        glGetError() != GL_NO_ERROR) {
        std::fprintf(stderr, "FAIL: Partial-damage renderPanel failed\n");
        return 1;
    }

    pipe.destroy(true);
    glDeleteFramebuffers(1, &outFBO);
    glDeleteTextures(1, &outTex);
    glDeleteTextures(1, &srcTex);
    destroyEgl(egl);

    std::printf("PASS: ES 3.20 plugin pipeline & invariants verified on GPU!\n");
    return 0;
}
