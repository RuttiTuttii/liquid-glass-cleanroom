#version 440
#extension GL_GOOGLE_include_directive : require
#include "include/glass_common.glsl"

// -----------------------------------------------------------------------------
// [1] Stage I/O, Uniform Buffer & Inputs
// -----------------------------------------------------------------------------
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float stepWidth;     // Jump Flooding stride k in texels (N/2, N/4, ..., 1, 1)
    float threshold;
    float useRedChannel;
    vec4 texelAndSize;   // xy = 1 / texSize, zw = texSize
    vec4 uvClampRect;    // uMin, vMin, uMax, vMax
} u;

layout(binding = 1) uniform sampler2D maskTex;
layout(binding = 2) uniform sampler2D seedTex;

// -----------------------------------------------------------------------------
// [2] 3x3 Jump Flooding Step with Sub-Pixel Boundary Residual
// -----------------------------------------------------------------------------
void main() {
    vec2 texel = u.texelAndSize.xy;
    vec2 texSize = u.texelAndSize.zw;
    vec2 pCoord = floor(qt_TexCoord0 * texSize);
    vec2 pUV = (pCoord + 0.5) * texel;

    float alphaP = sampleMaskChannel(maskTex, pUV, u.uvClampRect, u.useRedChannel);
    bool isInside = alphaP > u.threshold;

    vec4 bestSeed = vec4(-1.0, -1.0, 0.0, 0.0);
    float bestRankDist = 1e20;

    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            vec2 nCoord = pCoord + vec2(float(dx), float(dy)) * u.stepWidth;
            vec2 nUV = (nCoord + 0.5) * texel;
            if (nUV.x < u.uvClampRect.x || nUV.x > u.uvClampRect.z ||
                nUV.y < u.uvClampRect.y || nUV.y > u.uvClampRect.w) {
                continue;
            }

            vec4 cand = textureLod(seedTex, nUV, 0.0);
            if (cand.x < 0.0) {
                continue;
            }

            // Rank candidates using grid distance plus a sub-texel coverage residual
            // matching the true anti-aliased edge position across both inside/outside sides.
            vec2 qUV = (cand.xy + 0.5) * texel;
            float seedAlpha = sampleMaskChannel(maskTex, qUV, u.uvClampRect, u.useRedChannel);
            float residual = isInside ? seedAlpha : (1.0 - seedAlpha);
            float rankDist = length(pCoord - cand.xy) + residual;

            if (rankDist < bestRankDist) {
                bestRankDist = rankDist;
                bestSeed = vec4(cand.xy, seedAlpha, 1.0);
            }
        }
    }

    fragColor = bestSeed;
}
