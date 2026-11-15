#version 440
#extension GL_GOOGLE_include_directive : require
#include "include/glass_common.glsl"

// -----------------------------------------------------------------------------
// [1] Stage I/O, Uniform Buffer & Clamped Tap Helper
// -----------------------------------------------------------------------------
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float srcLod;
    float _pad0;
    float _pad1;
    vec4 srcTexelSize;   // xy = 1 / srcSize, zw = 0
    vec4 uvClampRect;    // uMin, vMin, uMax, vMax (inset by >= 0.5 source texel)
} u;

layout(binding = 1) uniform sampler2D srcTex;

// Reads a bilinearly filtered tap whose coordinates stay strictly inside the
// capture rect so border padding or neighboring atlas regions never leak in.
vec4 tapClamped(vec2 offsetTexels) {
    vec2 uv = clampUV(qt_TexCoord0 + offsetTexels * u.srcTexelSize.xy, u.uvClampRect);
    return textureLod(srcTex, uv, u.srcLod);
}

// -----------------------------------------------------------------------------
// [2] Energy-Preserving 13-Tap Dual-Ring Box Downsample (Sum of Weights = 1.0)
// -----------------------------------------------------------------------------
void main() {
    // 4 inner diagonal taps (0.125 each = 0.50 total)
    vec4 innerRing =
        tapClamped(vec2(-1.0, -1.0)) +
        tapClamped(vec2( 1.0, -1.0)) +
        tapClamped(vec2(-1.0,  1.0)) +
        tapClamped(vec2( 1.0,  1.0));

    // 1 center tap (0.125 total)
    vec4 centerTap = tapClamped(vec2(0.0, 0.0));

    // 4 cardinal cross taps (0.0625 each = 0.25 total)
    vec4 cardinalRing =
        tapClamped(vec2(-2.0,  0.0)) +
        tapClamped(vec2( 2.0,  0.0)) +
        tapClamped(vec2( 0.0, -2.0)) +
        tapClamped(vec2( 0.0,  2.0));

    // 4 outer corner taps (0.03125 each = 0.125 total)
    vec4 outerCorners =
        tapClamped(vec2(-2.0, -2.0)) +
        tapClamped(vec2( 2.0, -2.0)) +
        tapClamped(vec2(-2.0,  2.0)) +
        tapClamped(vec2( 2.0,  2.0));

    vec4 result = innerRing * 0.125 + centerTap * 0.125 + cardinalRing * 0.0625 + outerCorners * 0.03125;
    fragColor = result * u.qt_Opacity;
}
