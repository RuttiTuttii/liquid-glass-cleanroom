#version 440
#extension GL_GOOGLE_include_directive : require
#include "include/glass_common.glsl"

// -----------------------------------------------------------------------------
// [1] Stage I/O, Uniform Buffer & Mask Input
// -----------------------------------------------------------------------------
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float threshold;     // interior/exterior alpha cutoff (e.g. 0.50 or 0.25)
    float useRedChannel; // >= 0.5: read mask from .r; < 0.5: read from .a
    float _pad0;
    vec4 texelAndSize;   // xy = 1 / texSize, zw = texSize
    vec4 uvClampRect;    // uMin, vMin, uMax, vMax
} u;

layout(binding = 1) uniform sampler2D maskTex;

// -----------------------------------------------------------------------------
// [2] 4-Connected Boundary Seed Extraction
// -----------------------------------------------------------------------------
void main() {
    vec2 texel = u.texelAndSize.xy;
    vec2 texSize = u.texelAndSize.zw;
    vec2 pCoord = floor(qt_TexCoord0 * texSize);
    vec2 centerUV = (pCoord + 0.5) * texel;

    // Sample center texel and 4-connected axial neighbors to detect perimeter pixels
    // lying strictly inside the mask while touching an exterior texel.
    float aC = sampleMaskChannel(maskTex, centerUV, u.uvClampRect, u.useRedChannel);
    float aL = sampleMaskChannel(maskTex, centerUV + vec2(-texel.x, 0.0), u.uvClampRect, u.useRedChannel);
    float aR = sampleMaskChannel(maskTex, centerUV + vec2( texel.x, 0.0), u.uvClampRect, u.useRedChannel);
    float aD = sampleMaskChannel(maskTex, centerUV + vec2(0.0, -texel.y), u.uvClampRect, u.useRedChannel);
    float aU = sampleMaskChannel(maskTex, centerUV + vec2(0.0,  texel.y), u.uvClampRect, u.useRedChannel);

    float minNeighbor = min(min(aL, aR), min(aD, aU));

    if (aC > u.threshold && minNeighbor <= u.threshold) {
        // Valid interior boundary seed: integer coords in .xy, alpha in .z, sentinel 1.0 in .w.
        fragColor = vec4(pCoord.x, pCoord.y, aC, 1.0) * u.qt_Opacity;
    } else {
        // Non-seed sentinel encoded with negative coordinates (-1, -1) and valid = 0.
        fragColor = vec4(-1.0, -1.0, 0.0, 0.0);
    }
}
