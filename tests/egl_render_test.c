/*
 * Clean-Room Headless EGL / OpenGL 4.6 SPIR-V GPU Verification & Render Showcase.
 *
 * Executes the compiled OpenGL SPIR-V binaries (build/gl_spv/) inside a headless
 * EGL Surfaceless OpenGL 4.6 Core context:
 *   1. Clamped 13-tap downsample pyramid zero-bleed GPU verification.
 *   2. 5-pass GPU JFA mask-to-SDF + smoothed normal pipeline verification.
 *   3. Variable mip-LOD blur + coverage un-premultiply GPU verification.
 *   4. Full composite showcase render (dark/bright backdrop + G2 squircle +
 *      coalescing dual-pill + JFA masked shape + 3D Snell capsule & torus)
 *      exported to build/render_preview.png.
 *   5. 3D Snell thin-lens TIR (ior < 1) GPU NaN/bounds check.
 *   6. Single-buffer dFdx/dFdy sdf_pack_normals, noBleedMode & compositeBackdrop checks.
 *
 * Licensed under the MIT License (2026).
 */

#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* =========================================================================
 * [1] Dynamic OpenGL 4.6 Core Entry-Point Loader
 * ========================================================================= */

static PFNGLCREATESHADERPROC p_glCreateShader;
static PFNGLDELETESHADERPROC p_glDeleteShader;
static PFNGLSHADERBINARYPROC p_glShaderBinary;
static PFNGLSPECIALIZESHADERPROC p_glSpecializeShader;
static PFNGLGETSHADERIVPROC p_glGetShaderiv;
static PFNGLGETSHADERINFOLOGPROC p_glGetShaderInfoLog;
static PFNGLCREATEPROGRAMPROC p_glCreateProgram;
static PFNGLDELETEPROGRAMPROC p_glDeleteProgram;
static PFNGLPROGRAMPARAMETERIPROC p_glProgramParameteri;
static PFNGLATTACHSHADERPROC p_glAttachShader;
static PFNGLLINKPROGRAMPROC p_glLinkProgram;
static PFNGLGETPROGRAMIVPROC p_glGetProgramiv;
static PFNGLGETPROGRAMINFOLOGPROC p_glGetProgramInfoLog;
static PFNGLUSEPROGRAMPROC p_glUseProgram;
static PFNGLGENPROGRAMPIPELINESPROC p_glGenProgramPipelines;
static PFNGLDELETEPROGRAMPIPELINESPROC p_glDeleteProgramPipelines;
static PFNGLBINDPROGRAMPIPELINEPROC p_glBindProgramPipeline;
static PFNGLUSEPROGRAMSTAGESPROC p_glUseProgramStages;

static PFNGLGENVERTEXARRAYSPROC p_glGenVertexArrays;
static PFNGLDELETEVERTEXARRAYSPROC p_glDeleteVertexArrays;
static PFNGLBINDVERTEXARRAYPROC p_glBindVertexArray;
static PFNGLGENBUFFERSPROC p_glGenBuffers;
static PFNGLDELETEBUFFERSPROC p_glDeleteBuffers;
static PFNGLBINDBUFFERPROC p_glBindBuffer;
static PFNGLBUFFERDATAPROC p_glBufferData;
static PFNGLBUFFERSUBDATAPROC p_glBufferSubData;
static PFNGLBINDBUFFERBASEPROC p_glBindBufferBase;
static PFNGLENABLEVERTEXATTRIBARRAYPROC p_glEnableVertexAttribArray;
static PFNGLVERTEXATTRIBPOINTERPROC p_glVertexAttribPointer;

static PFNGLGENTEXTURESPROC p_glGenTextures;
static PFNGLDELETETEXTURESPROC p_glDeleteTextures;
static PFNGLACTIVETEXTUREPROC p_glActiveTexture;
static PFNGLBINDTEXTUREPROC p_glBindTexture;
static PFNGLTEXIMAGE2DPROC p_glTexImage2D;
static PFNGLTEXPARAMETERIPROC p_glTexParameteri;
static PFNGLGETTEXIMAGEPROC p_glGetTexImage;

static PFNGLGENFRAMEBUFFERSPROC p_glGenFramebuffers;
static PFNGLDELETEFRAMEBUFFERSPROC p_glDeleteFramebuffers;
static PFNGLBINDFRAMEBUFFERPROC p_glBindFramebuffer;
static PFNGLFRAMEBUFFERTEXTURE2DPROC p_glFramebufferTexture2D;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC p_glCheckFramebufferStatus;
static PFNGLCOPYTEXSUBIMAGE2DPROC p_glCopyTexSubImage2D;

static PFNGLVIEWPORTPROC p_glViewport;
static PFNGLCLEARCOLORPROC p_glClearColor;
static PFNGLCLEARPROC p_glClear;
static PFNGLENABLEPROC p_glEnable;
static PFNGLDISABLEPROC p_glDisable;
static PFNGLBLENDFUNCPROC p_glBlendFunc;
static PFNGLDRAWARRAYSPROC p_glDrawArrays;
static PFNGLREADPIXELSPROC p_glReadPixels;
static PFNGLGETERRORPROC p_glGetError;
static PFNGLGETSTRINGPROC p_glGetString;

static void *load_gl_proc(const char *name) {
    void *fn = (void *)eglGetProcAddress(name);
    if (!fn) {
        fprintf(stderr, "Missing GL entry point: %s\n", name);
        exit(1);
    }
    return fn;
}

static void init_gl_procs(void) {
#define LOAD(fn) p_##fn = (typeof(p_##fn))load_gl_proc(#fn)
    LOAD(glCreateShader);
    LOAD(glDeleteShader);
    LOAD(glShaderBinary);
    LOAD(glSpecializeShader);
    LOAD(glGetShaderiv);
    LOAD(glGetShaderInfoLog);
    LOAD(glCreateProgram);
    LOAD(glDeleteProgram);
    LOAD(glProgramParameteri);
    LOAD(glAttachShader);
    LOAD(glLinkProgram);
    LOAD(glGetProgramiv);
    LOAD(glGetProgramInfoLog);
    LOAD(glUseProgram);
    LOAD(glGenProgramPipelines);
    LOAD(glDeleteProgramPipelines);
    LOAD(glBindProgramPipeline);
    LOAD(glUseProgramStages);
    LOAD(glGenVertexArrays);
    LOAD(glDeleteVertexArrays);
    LOAD(glBindVertexArray);
    LOAD(glGenBuffers);
    LOAD(glDeleteBuffers);
    LOAD(glBindBuffer);
    LOAD(glBufferData);
    LOAD(glBufferSubData);
    LOAD(glBindBufferBase);
    LOAD(glEnableVertexAttribArray);
    LOAD(glVertexAttribPointer);
    LOAD(glGenTextures);
    LOAD(glDeleteTextures);
    LOAD(glActiveTexture);
    LOAD(glBindTexture);
    LOAD(glTexImage2D);
    LOAD(glTexParameteri);
    LOAD(glGetTexImage);
    LOAD(glGenFramebuffers);
    LOAD(glDeleteFramebuffers);
    LOAD(glBindFramebuffer);
    LOAD(glFramebufferTexture2D);
    LOAD(glCheckFramebufferStatus);
    LOAD(glCopyTexSubImage2D);
    LOAD(glViewport);
    LOAD(glClearColor);
    LOAD(glClear);
    LOAD(glEnable);
    LOAD(glDisable);
    LOAD(glBlendFunc);
    LOAD(glDrawArrays);
    LOAD(glReadPixels);
    LOAD(glGetError);
    LOAD(glGetString);
#undef LOAD
}

/* =========================================================================
 * [2] Self-Contained Uncompressed-DEFLATE PNG Writer (Zero External Deps)
 * ========================================================================= */

static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, size_t len) {
    crc ^= 0xffffffffu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= buf[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1u));
        }
    }
    return crc ^ 0xffffffffu;
}

static void write_be32(FILE *fp, uint32_t v) {
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    fwrite(b, 1, 4, fp);
}

static void write_png_chunk(FILE *fp, const char tag[4], const uint8_t *data, uint32_t len) {
    write_be32(fp, len);
    fwrite(tag, 1, 4, fp);
    if (len > 0) fwrite(data, 1, len, fp);
    uint32_t crc = crc32_update(0, (const uint8_t *)tag, 4);
    if (len > 0) crc = crc32_update(crc, data, len);
    write_be32(fp, crc);
}

static uint8_t linear_to_srgb_u8(float c) {
    float x = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
    float s = (x <= 0.0031308f) ? (12.92f * x) : (1.055f * powf(x, 1.0f / 2.4f) - 0.055f);
    int v = (int)lroundf(s * 255.0f);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (uint8_t)v;
}

static int save_linear_rgba_png(const char *path, int w, int h, const float *rgba) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;

    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, fp);

    uint8_t ihdr[13] = {
        (uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
        (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h,
        8, 2, 0, 0, 0 /* 8-bit RGB */
    };
    write_png_chunk(fp, "IHDR", ihdr, 13);

    size_t row_bytes = (size_t)w * 3 + 1;
    size_t raw_size = row_bytes * (size_t)h;
    uint8_t *raw = (uint8_t *)malloc(raw_size);
    if (!raw) {
        fclose(fp);
        return -1;
    }

    /* Convert linear float RGBA bottom-up scanlines to top-down sRGB RGB bytes. */
    for (int y = 0; y < h; ++y) {
        int gl_y = h - 1 - y;
        uint8_t *dst = raw + (size_t)y * row_bytes;
        dst[0] = 0; /* Filter type None */
        for (int x = 0; x < w; ++x) {
            const float *px = &rgba[((size_t)gl_y * (size_t)w + (size_t)x) * 4];
            dst[1 + x * 3 + 0] = linear_to_srgb_u8(px[0]);
            dst[1 + x * 3 + 1] = linear_to_srgb_u8(px[1]);
            dst[1 + x * 3 + 2] = linear_to_srgb_u8(px[2]);
        }
    }

    uint32_t s1 = 1, s2 = 0;
    for (size_t i = 0; i < raw_size; ++i) {
        s1 = (s1 + raw[i]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    uint32_t adler = (s2 << 16) | s1;

    /* Emit zlib header + stored DEFLATE blocks (<= 65535 bytes each) + Adler-32. */
    size_t num_blocks = (raw_size + 65534) / 65535;
    size_t zsize = 2 + raw_size + num_blocks * 5 + 4;
    uint8_t *zbuf = (uint8_t *)malloc(zsize);
    if (!zbuf) {
        free(raw);
        fclose(fp);
        return -1;
    }

    size_t pos = 0;
    zbuf[pos++] = 0x78;
    zbuf[pos++] = 0x01;
    size_t rem = raw_size, off = 0;
    while (rem > 0) {
        uint16_t blk = (uint16_t)(rem > 65535 ? 65535 : rem);
        int last = (rem <= 65535) ? 1 : 0;
        zbuf[pos++] = (uint8_t)last;
        zbuf[pos++] = (uint8_t)(blk & 0xff);
        zbuf[pos++] = (uint8_t)(blk >> 8);
        uint16_t nblk = (uint16_t)~blk;
        zbuf[pos++] = (uint8_t)(nblk & 0xff);
        zbuf[pos++] = (uint8_t)(nblk >> 8);
        memcpy(zbuf + pos, raw + off, blk);
        pos += blk;
        off += blk;
        rem -= blk;
    }
    zbuf[pos++] = (uint8_t)(adler >> 24);
    zbuf[pos++] = (uint8_t)(adler >> 16);
    zbuf[pos++] = (uint8_t)(adler >> 8);
    zbuf[pos++] = (uint8_t)adler;

    write_png_chunk(fp, "IDAT", zbuf, (uint32_t)pos);
    write_png_chunk(fp, "IEND", NULL, 0);

    free(zbuf);
    free(raw);
    fclose(fp);
    return 0;
}

/* =========================================================================
 * [3] SPIR-V Program Loading, Texture/FBO Creation & Draw Helpers
 * ========================================================================= */

typedef struct {
    GLuint pipe;
    GLuint ubo;
    GLuint vert;
    GLuint down;
    GLuint blur;
    GLuint seed;
    GLuint step;
    GLuint res;
    GLuint smooth;
    GLuint pack;
    GLuint panel;
    GLuint snell;
} ShaderProgs;

static GLuint load_spv_shader(GLenum stage, const char *dir, const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.spv", dir, name);
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Failed to open SPIR-V file: %s\n", path);
        exit(1);
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    void *data = malloc((size_t)sz);
    if (!data || fread(data, 1, (size_t)sz, fp) != (size_t)sz) {
        fprintf(stderr, "Failed to read SPIR-V file: %s\n", path);
        exit(1);
    }
    fclose(fp);

    GLuint sh = p_glCreateShader(stage);
    p_glShaderBinary(1, &sh, GL_SHADER_BINARY_FORMAT_SPIR_V, data, (GLsizei)sz);
    p_glSpecializeShader(sh, "main", 0, NULL, NULL);
    free(data);

    GLint ok = 0;
    p_glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        p_glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "SPIR-V specialization failed for %s:\n%s\n", path, log);
        exit(1);
    }
    return sh;
}

static GLuint build_separable_spv_prog(GLenum stage, const char *spv_dir, const char *name) {
    GLuint sh = load_spv_shader(stage, spv_dir, name);
    GLuint prog = p_glCreateProgram();
    p_glProgramParameteri(prog, GL_PROGRAM_SEPARABLE, GL_TRUE);
    p_glAttachShader(prog, sh);
    p_glLinkProgram(prog);
    p_glDeleteShader(sh);

    GLint ok = 0;
    p_glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        p_glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "Separable program link failed (%s):\n%s\n", name, log);
        exit(1);
    }
    return prog;
}

static void load_all_programs(ShaderProgs *p, const char *spv_dir) {
    p_glGenProgramPipelines(1, &p->pipe);
    p->vert   = build_separable_spv_prog(GL_VERTEX_SHADER,   spv_dir, "glass_quad.vert");
    p->down   = build_separable_spv_prog(GL_FRAGMENT_SHADER, spv_dir, "downsample_clamped.frag");
    p->blur   = build_separable_spv_prog(GL_FRAGMENT_SHADER, spv_dir, "variable_blur.frag");
    p->seed   = build_separable_spv_prog(GL_FRAGMENT_SHADER, spv_dir, "jfa_seed.frag");
    p->step   = build_separable_spv_prog(GL_FRAGMENT_SHADER, spv_dir, "jfa_step.frag");
    p->res    = build_separable_spv_prog(GL_FRAGMENT_SHADER, spv_dir, "sdf_resolve.frag");
    p->smooth = build_separable_spv_prog(GL_FRAGMENT_SHADER, spv_dir, "sdf_smooth.frag");
    p->pack   = build_separable_spv_prog(GL_FRAGMENT_SHADER, spv_dir, "sdf_pack_normals.frag");
    p->panel  = build_separable_spv_prog(GL_FRAGMENT_SHADER, spv_dir, "liquid_glass_panel.frag");
    p->snell  = build_separable_spv_prog(GL_FRAGMENT_SHADER, spv_dir, "snell_lens_glass.frag");
}

static GLuint create_tex2d(int w, int h, GLenum internal_fmt, GLenum filter, const float *init_rgba) {
    GLuint tex = 0;
    p_glGenTextures(1, &tex);
    p_glBindTexture(GL_TEXTURE_2D, tex);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)filter);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)filter);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    p_glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internal_fmt, w, h, 0, GL_RGBA, GL_FLOAT, init_rgba);
    return tex;
}

static GLuint create_fbo_for_tex(GLuint tex, int level) {
    GLuint fbo = 0;
    p_glGenFramebuffers(1, &fbo);
    p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, level);
    if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Incomplete FBO for texture %u level %d\n", tex, level);
        exit(1);
    }
    return fbo;
}

static void draw_fullscreen(
    const ShaderProgs *p,
    GLuint frag_prog,
    const void *data,
    size_t sz,
    GLuint t1,
    GLuint t2,
    GLuint t3
) {
    p_glUseProgram(0);
    p_glUseProgramStages(p->pipe, GL_VERTEX_SHADER_BIT, p->vert);
    p_glUseProgramStages(p->pipe, GL_FRAGMENT_SHADER_BIT, frag_prog);
    p_glBindProgramPipeline(p->pipe);
    p_glBindBuffer(GL_UNIFORM_BUFFER, p->ubo);
    p_glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)sz, data);
    p_glBindBufferBase(GL_UNIFORM_BUFFER, 0, p->ubo);
    if (t1) { p_glActiveTexture(GL_TEXTURE1); p_glBindTexture(GL_TEXTURE_2D, t1); }
    if (t2) { p_glActiveTexture(GL_TEXTURE2); p_glBindTexture(GL_TEXTURE_2D, t2); }
    if (t3) { p_glActiveTexture(GL_TEXTURE3); p_glBindTexture(GL_TEXTURE_2D, t3); }
    p_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void verify_buffer_finite(const float *buf, size_t num_floats, const char *label) {
    for (size_t i = 0; i < num_floats; ++i) {
        if (!isfinite(buf[i])) {
            fprintf(stderr, "FAIL [%s]: non-finite value at index %zu\n", label, i);
            exit(1);
        }
    }
}

/* =========================================================================
 * [4] Std140 UBO Layouts & Preset Initializers (Mirror Shader Bindings)
 * ========================================================================= */

typedef struct {
    float qt_Matrix[16];
    float qt_Opacity;
    float p0, p1, p2;
} UboHeader;

typedef struct {
    UboHeader hdr;
    float srcTexelSize[4];
    float uvClampRect[4];
} DownsampleUbo;

typedef struct {
    UboHeader hdr;
    float texelAndAngle[4];
    float resolveParams[4];
    float uvClampRect[4];
} VariableBlurUbo;

typedef struct {
    UboHeader hdr;
    float texelAndSize[4];
    float uvClampRect[4];
    float flags[4];
} JfaUbo;

typedef struct {
    UboHeader hdr;
    float texelAndScale[4];
    float packBiasAndPad[4];
    float uvClampRect[4];
} SdfPackUbo;

typedef struct {
    UboHeader hdr;
    float viewportAndSdf[4];
    float panelCenterAndHalf[4];
    float cornerRadii[4];
    float shapeParams[4];
    float secondaryCenterHalf[4];
    float secondaryRadii[4];
    float uvClampRect[4];
    float normalTransform[4];
    float dispTransform[4];
    float refractionParams[4];
    float dispersionParams[4];
    float bleedParams[4];
    float blurBandOffsets[4];
    float blurBandInvRanges[4];
    float blurBandLods[4];
    float shadowParams[4];
    float faceMatrix[12];
    float bleedMatrix[12];
    float shadowMatrix[12];
    float bevelSpecParams[4];
    float bevelSpecShape[4];
    float specColorAndLuma[4];
    float rimGlowParams[4];
    float toneClampParams[4];
    float sdfPackParams[4];
} PanelUbo;

typedef struct {
    UboHeader hdr;
    float viewportAndShape[4];
    float capsulePts[4];
    float opticsParams[4];
    float uvClampRect[4];
    float dispTransform[4];
    float shadowParams[4];
    float faceMatrix[12];
    float shadowMatrix[12];
    float bevelSpecParams[4];
    float bevelSpecShape[4];
    float specColorAndLuma[4];
    float toneClampParams[4];
} SnellUbo;

static void fill_identity_header(UboHeader *hdr, float p0, float p1, float p2) {
    memset(hdr, 0, sizeof(*hdr));
    hdr->qt_Matrix[0] = 1.0f;
    hdr->qt_Matrix[5] = 1.0f;
    hdr->qt_Matrix[10] = 1.0f;
    hdr->qt_Matrix[15] = 1.0f;
    hdr->qt_Opacity = 1.0f;
    hdr->p0 = p0;
    hdr->p1 = p1;
    hdr->p2 = p2;
}

static void set_tinted_affine(
    float m[12],
    float rGain, float gGain, float bGain,
    float rLift, float gLift, float bLift
) {
    memset(m, 0, sizeof(float) * 12);
    m[0] = rGain; m[3] = rLift;
    m[5] = gGain; m[7] = gLift;
    m[10] = bGain; m[11] = bLift;
}

static void set_identity_affine(float m[12], float contrast, float lift) {
    set_tinted_affine(m, contrast, contrast, contrast, lift, lift, lift);
}

static void init_default_panel_ubo(PanelUbo *pu, int w, int h) {
    memset(pu, 0, sizeof(*pu));
    fill_identity_header(&pu->hdr, 0.0f, 0.0f, 0.0f);
    pu->viewportAndSdf[0] = (float)w; pu->viewportAndSdf[1] = (float)h;
    pu->viewportAndSdf[2] = 1.0f;     pu->viewportAndSdf[3] = 0.0f;
    pu->uvClampRect[0] = 0.5f / w;        pu->uvClampRect[1] = 0.5f / h;
    pu->uvClampRect[2] = 1.0f - 0.5f / w; pu->uvClampRect[3] = 1.0f - 0.5f / h;
    pu->normalTransform[0] = 1.0f; pu->normalTransform[3] = 1.0f;
    pu->dispTransform[0] = 1.0f / w; pu->dispTransform[3] = 1.0f / h;
    pu->refractionParams[0] = 32.0f; pu->refractionParams[2] = 1.0f / 26.0f;
    pu->dispersionParams[0] = 6.5f;  pu->dispersionParams[1] = 1.35f; pu->dispersionParams[3] = 5.0f;
    pu->bleedParams[0] = 5.0f;       pu->bleedParams[1] = 0.30f; pu->bleedParams[2] = 1.35f; pu->bleedParams[3] = 1.0f;
    pu->blurBandOffsets[0] = 0.0f;   pu->blurBandOffsets[1] = 8.0f; pu->blurBandOffsets[2] = 22.0f;
    pu->blurBandInvRanges[0] = 1.0f / 4.0f; pu->blurBandInvRanges[1] = 1.0f / 14.0f; pu->blurBandInvRanges[2] = 1.0f / 20.0f;
    pu->blurBandLods[0] = 0.4f;      pu->blurBandLods[1] = 1.8f; pu->blurBandLods[2] = 2.8f;
    pu->shadowParams[0] = -3.0f;     pu->shadowParams[1] = 10.0f;
    pu->shadowParams[2] = 1.0f / 24.0f; pu->shadowParams[3] = 0.42f;
    set_tinted_affine(pu->faceMatrix,  1.06f, 1.06f, 1.08f, 0.035f, 0.04f,  0.055f);
    set_tinted_affine(pu->bleedMatrix, 1.10f, 1.10f, 1.12f, 0.06f,  0.065f, 0.08f);
    set_identity_affine(pu->shadowMatrix, 0.0f, 0.0f);
    pu->bevelSpecParams[0] = -0.7071f; pu->bevelSpecParams[1] = 0.7071f;
    pu->bevelSpecParams[2] = 3.2f;
    pu->bevelSpecShape[0] = -0.25f;    pu->bevelSpecShape[1] = 1.2f;
    pu->bevelSpecShape[2] = 0.65f;     pu->bevelSpecShape[3] = 0.32f;
    pu->specColorAndLuma[0] = 0.95f;   pu->specColorAndLuma[1] = 0.97f;
    pu->specColorAndLuma[2] = 1.00f;   pu->specColorAndLuma[3] = 0.85f;
    pu->rimGlowParams[0] = -1.2f;      pu->rimGlowParams[1] = 2.2f;
    pu->rimGlowParams[2] = 0.14f;
    pu->toneClampParams[0] = 1.0f;     pu->toneClampParams[2] = 1.0f; pu->toneClampParams[3] = 1.0f;
    pu->sdfPackParams[0] = 1.0f;       pu->sdfPackParams[1] = 1.0f;
}

static void init_default_capsule_ubo(SnellUbo *cap, int w, int h) {
    memset(cap, 0, sizeof(*cap));
    fill_identity_header(&cap->hdr, 0.0f, 0.0f, 0.0f);
    cap->viewportAndShape[0] = (float)w; cap->viewportAndShape[1] = (float)h;
    cap->viewportAndShape[2] = 44.0f;    cap->viewportAndShape[3] = 6.0f;
    cap->capsulePts[0] = 165.0f; cap->capsulePts[1] = 128.0f;
    cap->capsulePts[2] = 365.0f; cap->capsulePts[3] = 128.0f;
    cap->opticsParams[0] = 1.50f; cap->opticsParams[1] = 8.0f;
    cap->opticsParams[2] = 0.25f; cap->opticsParams[3] = 0.85f;
    cap->uvClampRect[0] = 0.5f / w;        cap->uvClampRect[1] = 0.5f / h;
    cap->uvClampRect[2] = 1.0f - 0.5f / w; cap->uvClampRect[3] = 1.0f - 0.5f / h;
    cap->dispTransform[0] = 1.0f / w; cap->dispTransform[3] = 1.0f / h;
    cap->shadowParams[0] = -3.0f;     cap->shadowParams[1] = 10.0f;
    cap->shadowParams[2] = 1.0f / 20.0f; cap->shadowParams[3] = 0.45f;
    set_tinted_affine(cap->faceMatrix, 1.05f, 1.06f, 1.09f, 0.02f, 0.03f, 0.05f);
    set_identity_affine(cap->shadowMatrix, 0.0f, 0.0f);
    cap->bevelSpecParams[0] = -0.7071f; cap->bevelSpecParams[1] = 0.7071f;
    cap->bevelSpecParams[2] = 4.0f;
    cap->bevelSpecShape[0] = -0.2f; cap->bevelSpecShape[1] = 1.0f;
    cap->bevelSpecShape[2] = 0.6f;  cap->bevelSpecShape[3] = 0.35f;
    cap->specColorAndLuma[0] = 1.0f; cap->specColorAndLuma[1] = 1.0f;
    cap->specColorAndLuma[2] = 1.0f; cap->specColorAndLuma[3] = 0.85f;
    cap->toneClampParams[0] = 1.0f; cap->toneClampParams[2] = 1.0f; cap->toneClampParams[3] = 1.0f;
}

/* =========================================================================
 * [5] Synthetic Showcase Backdrop & Supersampled Lobe-Mask Generators
 * ========================================================================= */

static float *generate_backdrop(int w, int h) {
    float *img = (float *)malloc((size_t)w * (size_t)h * 4 * sizeof(float));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float u = (float)x / (float)w;
            float v = (float)y / (float)h;
            float r, g, b;

            if (x < w / 2) {
                /* Left half: dark indigo/slate with neon diagonals and fine bars. */
                r = 0.03f + 0.06f * (1.0f - v);
                g = 0.04f + 0.08f * u * 2.0f;
                b = 0.10f + 0.18f * v;

                float diag = sinf((x * 0.065f + y * 0.065f));
                if (diag > 0.72f)       { r += 0.55f; g += 0.22f; b += 0.05f; }
                else if (diag < -0.72f) { r += 0.04f; g += 0.45f; b += 0.62f; }

                if ((y % 36 >= 10 && y % 36 <= 13) && (x % 48 >= 6 && x % 48 <= 34)) {
                    r = 0.85f; g = 0.88f; b = 0.94f;
                }
            } else {
                /* Right half: luminous warm paper/checkerboard with vivid bands. */
                int chk = (((x - w / 2) / 48) + (y / 48)) & 1;
                r = chk ? 0.92f : 0.76f;
                g = chk ? 0.90f : 0.82f;
                b = chk ? 0.86f : 0.92f;

                float stripe = sinf((x * 0.07f - y * 0.07f));
                if (stripe > 0.78f)       { r = 0.82f; g = 0.14f; b = 0.28f; }
                else if (stripe < -0.78f) { r = 0.08f; g = 0.36f; b = 0.45f; }

                if ((y % 36 >= 10 && y % 36 <= 12) && (x % 48 >= 8 && x % 48 <= 38)) {
                    r = 0.04f; g = 0.05f; b = 0.07f;
                }
            }

            if (fabsf(v - 0.50f) < 0.045f) {
                float ph = u * 6.283185f;
                r = 0.45f + 0.40f * sinf(ph);
                g = 0.45f + 0.40f * sinf(ph + 2.094f);
                b = 0.45f + 0.40f * sinf(ph + 4.188f);
            }

            size_t idx = ((size_t)y * (size_t)w + (size_t)x) * 4;
            img[idx + 0] = r; img[idx + 1] = g; img[idx + 2] = b; img[idx + 3] = 1.0f;
        }
    }
    return img;
}

/* Generates a 4x4 supersampled anti-aliased multi-lobe star/flower mask. */
static float *generate_lobe_mask(
    int w, int h,
    float cx, float cy,
    float base_r, float lobe_amp, float lobes
) {
    float *m = (float *)malloc((size_t)w * (size_t)h * 4 * sizeof(float));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float cov = 0.0f;
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) {
                    float px = (float)x + (sx + 0.5f) * 0.25f - 0.5f - cx;
                    float py = (float)y + (sy + 0.5f) * 0.25f - 0.5f - cy;
                    float target_r = base_r + lobe_amp * cosf(lobes * atan2f(py, px));
                    if (hypotf(px, py) <= target_r) cov += 1.0f;
                }
            }
            cov /= 16.0f;
            size_t idx = ((size_t)y * (size_t)w + (size_t)x) * 4;
            m[idx + 0] = cov; m[idx + 1] = cov; m[idx + 2] = cov; m[idx + 3] = cov;
        }
    }
    return m;
}

/* =========================================================================
 * [6] Reusable 5-Pass GPU JFA Mask-to-SDF & Normal Map Pipeline
 * ========================================================================= */

/* Runs: jfa_seed -> log2 ping-pong jfa_step -> sdf_resolve -> 2x sdf_smooth -> sdf_pack_normals.
 * Caller owns the returned packed SDF+normal texture (and optional out_crisp_tex). */
static GLuint run_gpu_jfa_pipeline(
    const ShaderProgs *p,
    GLuint mask_tex,
    int w, int h,
    float threshold,
    float dist_scale,
    float sigma,
    GLuint *out_crisp_tex
) {
    GLuint seed_a = create_tex2d(w, h, GL_RGBA32F, GL_NEAREST, NULL);
    GLuint seed_b = create_tex2d(w, h, GL_RGBA32F, GL_NEAREST, NULL);
    GLuint crisp  = create_tex2d(w, h, GL_RGBA16F, GL_LINEAR, NULL);
    GLuint tmp    = create_tex2d(w, h, GL_RGBA16F, GL_LINEAR, NULL);
    GLuint sm     = create_tex2d(w, h, GL_RGBA16F, GL_LINEAR, NULL);
    GLuint packed = create_tex2d(w, h, GL_RGBA16F, GL_LINEAR, NULL);

    GLuint fbo_sa = create_fbo_for_tex(seed_a, 0);
    GLuint fbo_sb = create_fbo_for_tex(seed_b, 0);
    GLuint fbo_cr = create_fbo_for_tex(crisp, 0);
    GLuint fbo_tm = create_fbo_for_tex(tmp, 0);
    GLuint fbo_sm = create_fbo_for_tex(sm, 0);
    GLuint fbo_pk = create_fbo_for_tex(packed, 0);

    p_glDisable(GL_BLEND);
    p_glViewport(0, 0, w, h);

    JfaUbo jubo;
    fill_identity_header(&jubo.hdr, threshold, 0.0f, 0.0f);
    jubo.texelAndSize[0] = 1.0f / w; jubo.texelAndSize[1] = 1.0f / h;
    jubo.texelAndSize[2] = (float)w; jubo.texelAndSize[3] = (float)h;
    jubo.uvClampRect[0]  = 0.5f / w;        jubo.uvClampRect[1] = 0.5f / h;
    jubo.uvClampRect[2]  = 1.0f - 0.5f / w; jubo.uvClampRect[3] = 1.0f - 0.5f / h;
    memset(jubo.flags, 0, sizeof(jubo.flags));

    /* Pass B1: 4-connected seed extraction */
    p_glBindFramebuffer(GL_FRAMEBUFFER, fbo_sa);
    draw_fullscreen(p, p->seed, &jubo, sizeof(jubo), mask_tex, 0, 0);

    /* Pass B2: logarithmic JFA ping-pong + extra 1-px pass */
    int max_dim = (w > h) ? w : h;
    int step = 1;
    while (step * 2 < max_dim) step *= 2;
    int ping = 0;
    for (int extra = 0; step >= 1 || extra == 0; ) {
        int cur_step = (step >= 1) ? step : 1;
        jubo.hdr.p0 = (float)cur_step;
        jubo.hdr.p1 = threshold;
        p_glBindFramebuffer(GL_FRAMEBUFFER, ping == 0 ? fbo_sb : fbo_sa);
        draw_fullscreen(p, p->step, &jubo, sizeof(jubo), mask_tex, ping == 0 ? seed_a : seed_b, 0);
        ping ^= 1;
        if (step > 1) {
            step /= 2;
        } else if (step == 1) {
            step = 0; /* run final extra 1-px pass next */
        } else {
            extra = 1;
        }
    }
    GLuint final_seed = (ping == 0) ? seed_a : seed_b;

    /* Pass B3: sub-pixel signed distance resolve */
    jubo.hdr.p0 = dist_scale;
    jubo.hdr.p1 = 0.0f;
    jubo.hdr.p2 = threshold;
    jubo.flags[0] = 0.0f;
    jubo.flags[1] = (float)max_dim;
    p_glBindFramebuffer(GL_FRAMEBUFFER, fbo_cr);
    draw_fullscreen(p, p->res, &jubo, sizeof(jubo), mask_tex, final_seed, 0);

    /* Pass B4a: separable 2-pass Gaussian pre-blur */
    jubo.hdr.p0 = sigma;
    jubo.hdr.p1 = 1.0f;
    jubo.texelAndSize[2] = 1.0f; jubo.texelAndSize[3] = 0.0f;
    p_glBindFramebuffer(GL_FRAMEBUFFER, fbo_tm);
    draw_fullscreen(p, p->smooth, &jubo, sizeof(jubo), crisp, 0, 0);

    jubo.texelAndSize[2] = 0.0f; jubo.texelAndSize[3] = 1.0f;
    p_glBindFramebuffer(GL_FRAMEBUFFER, fbo_sm);
    draw_fullscreen(p, p->smooth, &jubo, sizeof(jubo), tmp, 0, 0);

    /* Pass B4b: dual-buffer crisp distance + smoothed normals pack */
    SdfPackUbo pubo;
    fill_identity_header(&pubo.hdr, 0.0f, 0.0f, 0.0f);
    pubo.texelAndScale[0] = 1.0f / w; pubo.texelAndScale[1] = 1.0f / h;
    pubo.texelAndScale[2] = 1.0f;     pubo.texelAndScale[3] = 1.0f;
    memset(pubo.packBiasAndPad, 0, sizeof(pubo.packBiasAndPad));
    memcpy(pubo.uvClampRect, jubo.uvClampRect, sizeof(pubo.uvClampRect));
    p_glBindFramebuffer(GL_FRAMEBUFFER, fbo_pk);
    draw_fullscreen(p, p->pack, &pubo, sizeof(pubo), crisp, sm, 0);

    GLuint fbos[] = {fbo_sa, fbo_sb, fbo_cr, fbo_tm, fbo_sm, fbo_pk};
    p_glDeleteFramebuffers(6, fbos);
    GLuint tmp_texs[] = {seed_a, seed_b, tmp, sm};
    p_glDeleteTextures(4, tmp_texs);
    if (out_crisp_tex) {
        *out_crisp_tex = crisp;
    } else {
        p_glDeleteTextures(1, &crisp);
    }
    return packed;
}

/* =========================================================================
 * [7] GPU Verification Tests & Composite Showcase Renderer
 * ========================================================================= */

static void run_test_clamped_downsample(const ShaderProgs *p) {
    const int N = 128;
    float *src = (float *)calloc((size_t)N * (size_t)N * 4, sizeof(float));
    for (int y = 16; y < N - 16; ++y) {
        for (int x = 16; x < N - 16; ++x) {
            size_t idx = ((size_t)y * N + x) * 4;
            src[idx + 0] = 0.95f; src[idx + 1] = 0.65f; src[idx + 2] = 0.35f; src[idx + 3] = 1.0f;
        }
    }
    GLuint mips[5];
    mips[0] = create_tex2d(N, N, GL_RGBA16F, GL_LINEAR, src);
    free(src);

    int cur = N, vmin = 16, vmax = N - 16;
    for (int lvl = 1; lvl <= 4; ++lvl) {
        int nxt = cur / 2;
        mips[lvl] = create_tex2d(nxt, nxt, GL_RGBA16F, GL_LINEAR, NULL);
        GLuint fbo = create_fbo_for_tex(mips[lvl], 0);
        p_glViewport(0, 0, nxt, nxt);

        DownsampleUbo du;
        fill_identity_header(&du.hdr, 0.0f, 0.0f, 0.0f);
        du.srcTexelSize[0] = 1.0f / cur; du.srcTexelSize[1] = 1.0f / cur;
        du.srcTexelSize[2] = 0.0f;       du.srcTexelSize[3] = 0.0f;
        du.uvClampRect[0] = (vmin + 0.5f) / cur; du.uvClampRect[1] = (vmin + 0.5f) / cur;
        du.uvClampRect[2] = (vmax - 0.5f) / cur; du.uvClampRect[3] = (vmax - 0.5f) / cur;

        draw_fullscreen(p, p->down, &du, sizeof(du), mips[lvl - 1], 0, 0);
        p_glDeleteFramebuffers(1, &fbo);
        cur = nxt; vmin /= 2; vmax /= 2;
    }

    float out8[8 * 8 * 4];
    p_glBindTexture(GL_TEXTURE_2D, mips[4]);
    p_glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, out8);
    verify_buffer_finite(out8, sizeof(out8) / sizeof(float), "GPU downsample mip4");

    float max_err = 0.0f;
    for (int y = vmin; y < vmax; ++y) {
        for (int x = vmin; x < vmax; ++x) {
            const float *px = &out8[(y * 8 + x) * 4];
            float errs[4] = {
                fabsf(px[0] - 0.95f), fabsf(px[1] - 0.65f),
                fabsf(px[2] - 0.35f), fabsf(px[3] - 1.00f)
            };
            for (int c = 0; c < 4; ++c) {
                if (errs[c] > max_err) max_err = errs[c];
            }
        }
    }
    printf("  [GPU Test 1] Clamped 13-tap 4-mip downsample max bleed error = %.6f\n", max_err);
    if (max_err > 2e-3f) {
        fprintf(stderr, "FAIL: GPU downsample bleed %f\n", max_err);
        exit(1);
    }
    p_glDeleteTextures(5, mips);
}

static void run_test_jfa_sdf_pipeline(
    const ShaderProgs *p,
    int mw, int mh,
    GLuint star_mask_tex,
    GLuint *out_crisp,
    GLuint *out_packed
) {
    *out_packed = run_gpu_jfa_pipeline(p, star_mask_tex, mw, mh, 0.25f, 1.0f, 1.25f, out_crisp);

    float *jfa_read = (float *)malloc((size_t)mw * mh * 4 * sizeof(float));
    p_glBindTexture(GL_TEXTURE_2D, *out_packed);
    p_glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, jfa_read);
    verify_buffer_finite(jfa_read, (size_t)mw * mh * 4, "GPU JFA SDF+Normal map");

    float max_nlen_err = 0.0f;
    int band_count = 0;
    for (int i = 0; i < mw * mh; ++i) {
        float d = jfa_read[i * 4 + 0];
        if (fabsf(d) > 1.0f && fabsf(d) < 12.0f) {
            float e = fabsf(hypotf(jfa_read[i * 4 + 1], jfa_read[i * 4 + 2]) - 1.0f);
            if (e > max_nlen_err) max_nlen_err = e;
            band_count++;
        }
    }
    free(jfa_read);
    printf("  [GPU Test 2] 5-pass JFA SDF+normals (%d band px): max |norm-1| = %.5f\n", band_count, max_nlen_err);
    if (max_nlen_err > 0.02f || band_count < 1000) {
        fprintf(stderr, "FAIL: JFA GPU normal validation failed\n");
        exit(1);
    }
}

static GLuint build_backdrop_mip_pyramid(const ShaderProgs *p, int w, int h, const float *bg_data) {
    GLuint bg_tex = 0;
    p_glGenTextures(1, &bg_tex);
    p_glBindTexture(GL_TEXTURE_2D, bg_tex);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 5);
    for (int lvl = 0; lvl <= 5; ++lvl) {
        p_glTexImage2D(GL_TEXTURE_2D, lvl, GL_RGBA16F, w >> lvl, h >> lvl, 0, GL_RGBA, GL_FLOAT, (lvl == 0) ? bg_data : NULL);
    }

    GLuint prev_tex = create_tex2d(w, h, GL_RGBA16F, GL_LINEAR, bg_data);
    int lw = w, lh = h;
    for (int lvl = 1; lvl <= 5; ++lvl) {
        int nw = lw / 2, nh = lh / 2;
        GLuint dst_tex = create_tex2d(nw, nh, GL_RGBA16F, GL_LINEAR, NULL);
        GLuint mfbo = create_fbo_for_tex(dst_tex, 0);
        p_glViewport(0, 0, nw, nh);

        DownsampleUbo du;
        fill_identity_header(&du.hdr, 0.0f, 0.0f, 0.0f);
        du.srcTexelSize[0] = 1.0f / lw;        du.srcTexelSize[1] = 1.0f / lh;
        du.srcTexelSize[2] = 0.0f;             du.srcTexelSize[3] = 0.0f;
        du.uvClampRect[0]  = 0.5f / lw;        du.uvClampRect[1] = 0.5f / lh;
        du.uvClampRect[2]  = 1.0f - 0.5f / lw; du.uvClampRect[3] = 1.0f - 0.5f / lh;
        draw_fullscreen(p, p->down, &du, sizeof(du), prev_tex, 0, 0);

        p_glBindTexture(GL_TEXTURE_2D, bg_tex);
        p_glCopyTexSubImage2D(GL_TEXTURE_2D, lvl, 0, 0, 0, 0, nw, nh);
        p_glDeleteFramebuffers(1, &mfbo);
        p_glDeleteTextures(1, &prev_tex);
        prev_tex = dst_tex;
        lw = nw; lh = nh;
    }
    p_glDeleteTextures(1, &prev_tex);
    return bg_tex;
}

static void run_test_variable_blur(const ShaderProgs *p, int w, int h, GLuint bg_tex, GLuint mask_tex) {
    GLuint vb_tex = create_tex2d(w, h, GL_RGBA16F, GL_LINEAR, NULL);
    GLuint vb_fbo = create_fbo_for_tex(vb_tex, 0);
    p_glViewport(0, 0, w, h);

    VariableBlurUbo vbu;
    fill_identity_header(&vbu.hdr, 6.0f, 5.0f, 0.42f);
    vbu.texelAndAngle[0] = 1.0f / w; vbu.texelAndAngle[1] = 1.0f / h;
    vbu.texelAndAngle[2] = 0.4636f;  vbu.texelAndAngle[3] = 0.0f;
    vbu.resolveParams[0] = 1.0f;     vbu.resolveParams[1] = 1e-4f;
    vbu.resolveParams[2] = 0.5f / 255.0f; vbu.resolveParams[3] = 0.0f;
    vbu.uvClampRect[0] = 0.5f / w;        vbu.uvClampRect[1] = 0.5f / h;
    vbu.uvClampRect[2] = 1.0f - 0.5f / w; vbu.uvClampRect[3] = 1.0f - 0.5f / h;
    p_glBindFramebuffer(GL_FRAMEBUFFER, vb_fbo);
    draw_fullscreen(p, p->blur, &vbu, sizeof(vbu), bg_tex, mask_tex, mask_tex);

    float *vb_read = (float *)malloc((size_t)w * h * 4 * sizeof(float));
    p_glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, vb_read);
    verify_buffer_finite(vb_read, (size_t)w * h * 4, "GPU variable_blur");
    free(vb_read);

    p_glDeleteFramebuffers(1, &vb_fbo);
    p_glDeleteTextures(1, &vb_tex);
    printf("  [GPU Test 3] variable_blur + unpremultiply_resolve finite & verified.\n");
}

static void render_showcase_composite(
    const ShaderProgs *p,
    int w, int h,
    GLuint bg_tex,
    const float *bg_data,
    const char *out_png,
    const PanelUbo *base_pu,
    const SnellUbo *cap
) {
    GLuint out_tex = create_tex2d(w, h, GL_RGBA16F, GL_LINEAR, bg_data);
    GLuint out_fbo = create_fbo_for_tex(out_tex, 0);
    p_glBindFramebuffer(GL_FRAMEBUFFER, out_fbo);
    p_glViewport(0, 0, w, h);
    p_glEnable(GL_BLEND);
    p_glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    /* Widget 1: Hero G2-Squircle panel bridging dark & bright halves */
    PanelUbo hero = *base_pu;
    hero.panelCenterAndHalf[0] = 512.0f; hero.panelCenterAndHalf[1] = 355.0f;
    hero.panelCenterAndHalf[2] = 195.0f; hero.panelCenterAndHalf[3] = 110.0f;
    hero.cornerRadii[0] = 44.0f; hero.cornerRadii[1] = 44.0f;
    hero.cornerRadii[2] = 44.0f; hero.cornerRadii[3] = 44.0f;
    hero.shapeParams[0] = 1.0f;
    draw_fullscreen(p, p->panel, &hero, sizeof(hero), bg_tex, 0, 0);

    /* Widget 2: Coalescing dual-pill liquid droplet (top-left) */
    PanelUbo dual = *base_pu;
    dual.panelCenterAndHalf[0] = 148.0f; dual.panelCenterAndHalf[1] = 495.0f;
    dual.panelCenterAndHalf[2] = 68.0f;  dual.panelCenterAndHalf[3] = 36.0f;
    dual.cornerRadii[0] = 36.0f; dual.cornerRadii[1] = 36.0f;
    dual.cornerRadii[2] = 36.0f; dual.cornerRadii[3] = 36.0f;
    dual.shapeParams[0] = 0.65f; dual.shapeParams[1] = 0.15f;
    dual.shapeParams[2] = 28.0f; dual.shapeParams[3] = 1.0f;
    dual.secondaryCenterHalf[0] = 224.0f; dual.secondaryCenterHalf[1] = 482.0f;
    dual.secondaryCenterHalf[2] = 42.0f;  dual.secondaryCenterHalf[3] = 34.0f;
    dual.secondaryRadii[0] = 34.0f; dual.secondaryRadii[1] = 34.0f;
    dual.secondaryRadii[2] = 34.0f; dual.secondaryRadii[3] = 34.0f;
    dual.refractionParams[0] = 26.0f; dual.refractionParams[2] = 1.0f / 22.0f;
    dual.bleedParams[2] = 0.85f;
    draw_fullscreen(p, p->panel, &dual, sizeof(dual), bg_tex, 0, 0);

    /* Widget 3: Arbitrary JFA-masked 6-lobe flower badge (Mode B, top-right) */
    {
        const int JW = 512, JH = 320;
        float *jmask = generate_lobe_mask(JW, JH, 418.0f, 240.0f, 44.0f, 13.0f, 6.0f);
        GLuint jm_tex = create_tex2d(JW, JH, GL_RGBA16F, GL_LINEAR, jmask);
        free(jmask);
        GLuint jn = run_gpu_jfa_pipeline(p, jm_tex, JW, JH, 0.25f, 2.0f, 1.25f, NULL);

        p_glBindFramebuffer(GL_FRAMEBUFFER, out_fbo);
        p_glViewport(0, 0, w, h);
        p_glEnable(GL_BLEND);
        PanelUbo jfa_panel = *base_pu;
        jfa_panel.hdr.p0 = 1.0f;
        jfa_panel.refractionParams[0] = 28.0f;
        jfa_panel.refractionParams[2] = 1.0f / 20.0f;
        jfa_panel.bleedParams[2] = 0.65f;
        jfa_panel.bleedParams[3] = 0.0f;
        draw_fullscreen(p, p->panel, &jfa_panel, sizeof(jfa_panel), bg_tex, jn, 0);

        p_glDeleteTextures(1, &jn);
        p_glDeleteTextures(1, &jm_tex);
    }

    /* Widget 4a & 4b: 3D Snell thin-lens capsule pill & torus loupe */
    draw_fullscreen(p, p->snell, cap, sizeof(*cap), bg_tex, 0, 0);

    SnellUbo tor = *cap;
    tor.hdr.p0 = 1.0f;
    tor.viewportAndShape[2] = 28.0f;
    tor.viewportAndShape[3] = 5.0f;
    tor.capsulePts[0] = 790.0f; tor.capsulePts[1] = 138.0f;
    tor.capsulePts[2] = 68.0f;  tor.capsulePts[3] = 0.0f;
    tor.opticsParams[0] = 1.48f;
    draw_fullscreen(p, p->snell, &tor, sizeof(tor), bg_tex, 0, 0);

    /* Validate final composite RGBA16F pixels and export linear->sRGB PNG */
    float *final_rgba = (float *)malloc((size_t)w * h * 4 * sizeof(float));
    p_glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, final_rgba);
    verify_buffer_finite(final_rgba, (size_t)w * h * 4, "Final 1024x640 showcase");

    float max_rgb = 0.0f, min_rgb = 1e9f;
    for (int i = 0; i < w * h; ++i) {
        const float *px = &final_rgba[i * 4];
        for (int c = 0; c < 3; ++c) {
            if (px[c] > max_rgb) max_rgb = px[c];
            if (px[c] < min_rgb) min_rgb = px[c];
            if (px[c] > px[3] * 1.005f + 1e-3f) {
                fprintf(stderr, "FAIL: premultiplied bound violated at px %d (%.4f > %.4f)\n", i, px[c], px[3]);
                exit(1);
            }
        }
    }
    printf("  [GPU Test 4] Final composite 1024x640 verified: zero NaNs, premultiplied bounds ok, range [%.4f, %.4f]\n",
           min_rgb, max_rgb);

    if (save_linear_rgba_png(out_png, w, h, final_rgba) != 0) {
        fprintf(stderr, "Failed to save %s\n", out_png);
        exit(1);
    }
    printf("  Saved inspectable GPU showcase PNG -> %s\n", out_png);
    free(final_rgba);
    p_glDeleteFramebuffers(1, &out_fbo);
    p_glDeleteTextures(1, &out_tex);
}

static void run_test_snell_tir(const ShaderProgs *p, GLuint bg_tex, const SnellUbo *cap) {
    GLuint tir_tex = create_tex2d(256, 256, GL_RGBA16F, GL_LINEAR, NULL);
    GLuint tir_fbo = create_fbo_for_tex(tir_tex, 0);
    p_glDisable(GL_BLEND);
    p_glBindFramebuffer(GL_FRAMEBUFFER, tir_fbo);
    p_glViewport(0, 0, 256, 256);

    SnellUbo tir = *cap;
    tir.viewportAndShape[0] = 256.0f; tir.viewportAndShape[1] = 256.0f;
    tir.viewportAndShape[2] = 48.0f;  tir.viewportAndShape[3] = 4.0f;
    tir.capsulePts[0] = 84.0f;  tir.capsulePts[1] = 128.0f;
    tir.capsulePts[2] = 172.0f; tir.capsulePts[3] = 128.0f;
    tir.opticsParams[0] = 0.65f; /* Triggers k < 0 Total Internal Reflection */
    draw_fullscreen(p, p->snell, &tir, sizeof(tir), bg_tex, 0, 0);

    float *tir_read = (float *)malloc(256 * 256 * 4 * sizeof(float));
    p_glReadPixels(0, 0, 256, 256, GL_RGBA, GL_FLOAT, tir_read);
    verify_buffer_finite(tir_read, 256 * 256 * 4, "GPU Snell TIR ior=0.65");
    for (int i = 0; i < 256 * 256; ++i) {
        const float *px = &tir_read[i * 4];
        for (int c = 0; c < 3; ++c) {
            if (px[c] < -1e-3f || px[c] > px[3] * 1.005f + 1e-3f) {
                fprintf(stderr, "FAIL: TIR overlay premul bound violated at px %d (c=%.4f a=%.4f)\n", i, px[c], px[3]);
                exit(1);
            }
        }
    }
    free(tir_read);
    p_glDeleteFramebuffers(1, &tir_fbo);
    p_glDeleteTextures(1, &tir_tex);
    printf("  [GPU Test 5] Snell TIR (ior=0.65) overlay premultiplied bounds & zero-NaN verified.\n");
}

static void run_test_pack_and_bleed_modes(
    const ShaderProgs *p,
    int mw, int mh,
    GLuint sdf_crisp,
    GLuint bg_tex,
    const PanelUbo *base_pu,
    const SnellUbo *cap
) {
    GLuint aux_tex = create_tex2d(mw, mh, GL_RGBA16F, GL_LINEAR, NULL);
    GLuint aux_fbo = create_fbo_for_tex(aux_tex, 0);
    p_glDisable(GL_BLEND);
    p_glBindFramebuffer(GL_FRAMEBUFFER, aux_fbo);
    p_glViewport(0, 0, mw, mh);

    SdfPackUbo sb;
    fill_identity_header(&sb.hdr, 1.0f, 1.0f, 0.0f); /* singleBufferMode=1, discardZeroAlpha=1 */
    sb.texelAndScale[0] = 1.0f / mw; sb.texelAndScale[1] = 1.0f / mh;
    sb.texelAndScale[2] = 0.5f;      sb.texelAndScale[3] = 0.5f;
    sb.packBiasAndPad[0] = 0.5f;     sb.packBiasAndPad[1] = 0.5f;
    sb.packBiasAndPad[2] = 0.0f;     sb.packBiasAndPad[3] = 0.0f;
    sb.uvClampRect[0] = 0.5f / mw;        sb.uvClampRect[1] = 0.5f / mh;
    sb.uvClampRect[2] = 1.0f - 0.5f / mw; sb.uvClampRect[3] = 1.0f - 0.5f / mh;
    draw_fullscreen(p, p->pack, &sb, sizeof(sb), sdf_crisp, 0, 0);

    float *sb_read = (float *)malloc((size_t)mw * mh * 4 * sizeof(float));
    p_glReadPixels(0, 0, mw, mh, GL_RGBA, GL_FLOAT, sb_read);
    verify_buffer_finite(sb_read, (size_t)mw * mh * 4, "GPU single-buffer dFdx/dFdy sdf_pack_normals");
    for (int i = 0; i < mw * mh; ++i) {
        float nx = sb_read[i * 4 + 1] * 2.0f - 1.0f;
        float ny = sb_read[i * 4 + 2] * 2.0f - 1.0f;
        float nlen = hypotf(nx, ny);
        if (nlen > 1.05f) {
            fprintf(stderr, "FAIL: single-buffer normal norm > 1.05 (%f)\n", nlen);
            exit(1);
        }
    }
    free(sb_read);

    PanelUbo nb = *base_pu;
    nb.hdr.p1 = 0.0f; /* compositeBackdrop=0 */
    nb.hdr.p2 = 1.0f; /* noBleedMode=1 */
    nb.viewportAndSdf[0] = (float)mw; nb.viewportAndSdf[1] = (float)mh;
    nb.panelCenterAndHalf[0] = 128.0f; nb.panelCenterAndHalf[1] = 128.0f;
    nb.panelCenterAndHalf[2] = 60.0f;  nb.panelCenterAndHalf[3] = 40.0f;
    nb.cornerRadii[0] = nb.cornerRadii[1] = nb.cornerRadii[2] = nb.cornerRadii[3] = 20.0f;
    nb.shapeParams[0] = 1.0f;
    p_glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    p_glClear(GL_COLOR_BUFFER_BIT);
    draw_fullscreen(p, p->panel, &nb, sizeof(nb), bg_tex, 0, 0);

    float corner_px[4];
    p_glReadPixels(4, 4, 1, 1, GL_RGBA, GL_FLOAT, corner_px);
    if (corner_px[3] > 1e-5f || corner_px[0] > 1e-5f) {
        fprintf(stderr, "FAIL: noBleedMode=1 leaked outside panel (%f, %f)\n", corner_px[0], corner_px[3]);
        exit(1);
    }

    nb.hdr.p1 = 1.0f;
    draw_fullscreen(p, p->panel, &nb, sizeof(nb), bg_tex, 0, 0);
    p_glReadPixels(4, 4, 1, 1, GL_RGBA, GL_FLOAT, corner_px);
    if (fabsf(corner_px[3] - 1.0f) > 1e-3f) {
        fprintf(stderr, "FAIL: compositeBackdrop=1 alpha != 1.0 (%f)\n", corner_px[3]);
        exit(1);
    }

    SnellUbo sc = *cap;
    sc.hdr.p1 = 1.0f;
    sc.viewportAndShape[0] = (float)mw; sc.viewportAndShape[1] = (float)mh;
    draw_fullscreen(p, p->snell, &sc, sizeof(sc), bg_tex, 0, 0);
    p_glReadPixels(4, 4, 1, 1, GL_RGBA, GL_FLOAT, corner_px);
    if (fabsf(corner_px[3] - 1.0f) > 1e-3f) {
        fprintf(stderr, "FAIL: Snell compositeBackdrop=1 alpha != 1.0 (%f)\n", corner_px[3]);
        exit(1);
    }

    p_glDeleteFramebuffers(1, &aux_fbo);
    p_glDeleteTextures(1, &aux_tex);
    printf("  [GPU Test 6] Single-buffer dFdx/dFdy, noBleedMode, and compositeBackdrop=1 verified.\n");
}

/* =========================================================================
 * [8] Headless EGL Context Bootstrap & Main Entry Point
 * ========================================================================= */

static void init_egl_gl46(EGLDisplay *out_dpy, EGLContext *out_ctx) {
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    if (dpy == EGL_NO_DISPLAY) dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    EGLint maj = 0, min = 0;
    if (!eglInitialize(dpy, &maj, &min) || !eglBindAPI(EGL_OPENGL_API)) {
        fprintf(stderr, "Failed to initialize EGL OpenGL API\n");
        exit(1);
    }
    const EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };
    EGLConfig cfg;
    EGLint ncfg = 0;
    if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &ncfg) || ncfg == 0) {
        fprintf(stderr, "eglChooseConfig failed\n");
        exit(1);
    }
    const EGLint ctx_attr[] = {
        EGL_CONTEXT_MAJOR_VERSION, 4,
        EGL_CONTEXT_MINOR_VERSION, 6,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE
    };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        fprintf(stderr, "Failed to create/bind OpenGL 4.6 EGL context\n");
        exit(1);
    }
    init_gl_procs();
    *out_dpy = dpy;
    *out_ctx = ctx;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <gl_spv_dir> <out_png_path>\n", argv[0]);
        return 1;
    }
    const char *spv_dir = argv[1];
    const char *out_png = argv[2];

    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLContext ctx = EGL_NO_CONTEXT;
    init_egl_gl46(&dpy, &ctx);
    printf("EGL/GL GPU: %s | %s\n", p_glGetString(GL_VENDOR), p_glGetString(GL_RENDERER));

    /* Setup fullscreen quad VAO/VBO & shared std140 UBO buffer */
    const float quad_verts[] = {
        -1.0f, -1.0f, 0.0f, 1.0f,   0.0f, 0.0f,
         1.0f, -1.0f, 0.0f, 1.0f,   1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,   0.0f, 1.0f,
         1.0f,  1.0f, 0.0f, 1.0f,   1.0f, 1.0f,
    };
    GLuint vao = 0, vbo = 0;
    p_glGenVertexArrays(1, &vao);
    p_glBindVertexArray(vao);
    p_glGenBuffers(1, &vbo);
    p_glBindBuffer(GL_ARRAY_BUFFER, vbo);
    p_glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts), quad_verts, GL_STATIC_DRAW);
    p_glEnableVertexAttribArray(0);
    p_glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    p_glEnableVertexAttribArray(1);
    p_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(4 * sizeof(float)));

    ShaderProgs progs;
    memset(&progs, 0, sizeof(progs));
    p_glGenBuffers(1, &progs.ubo);
    p_glBindBuffer(GL_UNIFORM_BUFFER, progs.ubo);
    p_glBufferData(GL_UNIFORM_BUFFER, 4096, NULL, GL_DYNAMIC_DRAW);
    load_all_programs(&progs, spv_dir);
    p_glDisable(GL_BLEND);

    /* Run GPU checks 1..2 (Downsample + 5-pass JFA SDF/Normals) */
    run_test_clamped_downsample(&progs);

    const int MW = 256, MH = 256;
    float *star_data = generate_lobe_mask(MW, MH, MW * 0.5f, MH * 0.5f, MW * 0.30f, MW * 0.085f, 5.0f);
    GLuint star_mask_tex = create_tex2d(MW, MH, GL_RGBA16F, GL_LINEAR, star_data);
    free(star_data);
    GLuint sdf_crisp = 0, sdf_normal_map = 0;
    run_test_jfa_sdf_pipeline(&progs, MW, MH, star_mask_tex, &sdf_crisp, &sdf_normal_map);

    /* Run GPU checks 3..6 (Variable Blur, Showcase Composite, Snell TIR, Pack/Bleed modes) */
    const int W = 1024, H = 640;
    float *bg_data = generate_backdrop(W, H);
    GLuint bg_tex = build_backdrop_mip_pyramid(&progs, W, H, bg_data);
    run_test_variable_blur(&progs, W, H, bg_tex, star_mask_tex);

    PanelUbo base_pu;
    SnellUbo cap;
    init_default_panel_ubo(&base_pu, W, H);
    init_default_capsule_ubo(&cap, W, H);

    render_showcase_composite(&progs, W, H, bg_tex, bg_data, out_png, &base_pu, &cap);
    free(bg_data);

    run_test_snell_tir(&progs, bg_tex, &cap);
    run_test_pack_and_bleed_modes(&progs, MW, MH, sdf_crisp, bg_tex, &base_pu, &cap);

    /* Release GPU & EGL resources */
    GLuint texs[] = {star_mask_tex, sdf_crisp, sdf_normal_map, bg_tex};
    p_glDeleteTextures(4, texs);
    GLuint all_progs[] = {
        progs.vert, progs.down, progs.blur, progs.seed, progs.step,
        progs.res, progs.smooth, progs.pack, progs.panel, progs.snell
    };
    for (int i = 0; i < 10; ++i) p_glDeleteProgram(all_progs[i]);
    p_glDeleteProgramPipelines(1, &progs.pipe);
    p_glDeleteBuffers(1, &progs.ubo);
    p_glDeleteBuffers(1, &vbo);
    p_glDeleteVertexArrays(1, &vao);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);

    printf("ALL GPU EGL/SPIR-V VERIFICATION TESTS PASSED.\n");
    return 0;
}
