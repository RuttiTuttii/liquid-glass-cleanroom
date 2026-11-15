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
    float distanceScale; // storage normalization multiplier (1.0 for float SDF)
    float distanceBias;  // storage offset (0.0 for signed float SDF)
    float threshold;
    vec4 texelAndSize;   // xy = 1 / texSize, zw = texSize
    vec4 uvClampRect;    // uMin, vMin, uMax, vMax
    vec4 flags;          // x = useRedChannel, y = fallbackDistance
} u;

layout(binding = 1) uniform sampler2D maskTex;
layout(binding = 2) uniform sampler2D seedTex;

// -----------------------------------------------------------------------------
// [2] Sub-Pixel Signed Distance Reconstruction
// -----------------------------------------------------------------------------
void main() {
    vec2 texel = u.texelAndSize.xy;
    vec2 texSize = u.texelAndSize.zw;
    vec2 pCoord = floor(qt_TexCoord0 * texSize);
    vec2 pUV = (pCoord + 0.5) * texel;

    float alphaP = sampleMaskChannel(maskTex, pUV, u.uvClampRect, u.flags.x);
    bool isInside = alphaP > u.threshold;
    vec4 seed = textureLod(seedTex, clampUV(pUV, u.uvClampRect), 0.0);

    float dPx;
    float seedAlpha = alphaP;
    if (seed.x >= 0.0) {
        // Subtract (seedAlpha - 0.5) via + (0.5 - seedAlpha) so the 0.5 coverage contour
        // settles smoothly at zero distance without integer-grid stair-stepping.
        float unsignedDist = length(pCoord - seed.xy);
        vec2 qUV = (seed.xy + 0.5) * texel;
        seedAlpha = sampleMaskChannel(maskTex, qUV, u.uvClampRect, u.flags.x);
        dPx = (isInside ? -unsignedDist : unsignedDist) + (0.5 - seedAlpha);
    } else {
        float fallback = max(u.flags.y, max(texSize.x, texSize.y));
        dPx = isInside ? -fallback : fallback;
    }

    float dOut = dPx * u.distanceScale + u.distanceBias;
    fragColor = vec4(dOut, dPx, seedAlpha, 1.0) * u.qt_Opacity;
}
