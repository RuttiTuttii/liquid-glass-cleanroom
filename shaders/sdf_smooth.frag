#version 440
#extension GL_GOOGLE_include_directive : require
#include "include/glass_common.glsl"

// -----------------------------------------------------------------------------
// [1] Stage I/O, Uniform Buffer & Input SDF Texture
// -----------------------------------------------------------------------------
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float sigma;         // Gaussian std dev in texels (clamped >= 0.35)
    float twoPass;       // >= 0.5: 1D separable 5-tap pass; < 0.5: 5x5 single pass
    float _pad0;
    vec4 texelAndDir;    // xy = 1 / texSize, zw = axis dir (1,0) or (0,1)
    vec4 uvClampRect;    // uMin, vMin, uMax, vMax
} u;

layout(binding = 1) uniform sampler2D sdfTex;

// -----------------------------------------------------------------------------
// [2] Pre-Blur SDF Filter (Suppresses JFA Voronoi Faceting in Surface Normals)
// -----------------------------------------------------------------------------
void main() {
    vec2 texel = u.texelAndDir.xy;
    float safeSigma = max(u.sigma, 0.35);
    float invTwoSigma2 = 0.5 / (safeSigma * safeSigma);

    vec4 centerSample = textureLod(sdfTex, clampUV(qt_TexCoord0, u.uvClampRect), 0.0);
    float accum = 0.0;
    float weightSum = 0.0;

    if (u.twoPass >= 0.5) {
        // Separable 1D horizontal/vertical pass for multi-pass pipelines.
        vec2 stepVec = u.texelAndDir.zw * texel;
        for (int i = -2; i <= 2; ++i) {
            float fi = float(i);
            float w = exp(-(fi * fi) * invTwoSigma2);
            vec2 tapUV = clampUV(qt_TexCoord0 + fi * stepVec, u.uvClampRect);
            accum += textureLod(sdfTex, tapUV, 0.0).r * w;
            weightSum += w;
        }
    } else {
        // Single-pass isotropic 5x5 Gaussian blur when minimizing render targets.
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                vec2 off = vec2(float(dx), float(dy));
                float w = exp(-dot(off, off) * invTwoSigma2);
                vec2 tapUV = clampUV(qt_TexCoord0 + off * texel, u.uvClampRect);
                accum += textureLod(sdfTex, tapUV, 0.0).r * w;
                weightSum += w;
            }
        }
    }

    // Output smoothed SDF in .r while forwarding raw .g/.b diagnostics unchanged.
    float dSmooth = accum / max(weightSum, 1e-6);
    fragColor = vec4(dSmooth, centerSample.g, centerSample.b, 1.0) * u.qt_Opacity;
}
