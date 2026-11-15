#version 440
#extension GL_GOOGLE_include_directive : require
#include "include/glass_common.glsl"

// -----------------------------------------------------------------------------
// [1] Stage I/O, Uniform Buffer & Dual/Single SDF Inputs
// -----------------------------------------------------------------------------
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float singleBufferMode; // >= 0.5: adaptive dFdx/dFdy on crispSdfTex; < 0.5: dual-buffer
    float discardZeroAlpha; // >= 0.5: discard empty regions when crispSdfTex.a <= 0
    float _pad0;
    vec4 texelAndScale;     // xy = 1 / texSize, zw = packScale (1.0 float, 0.5 UNORM8)
    vec4 packBiasAndPad;    // xy = packBias (0.0 float, 0.5 UNORM8)
    vec4 uvClampRect;       // uMin, vMin, uMax, vMax
} u;

layout(binding = 1) uniform sampler2D crispSdfTex;
layout(binding = 2) uniform sampler2D smoothSdfTex;

// -----------------------------------------------------------------------------
// [2] Gradient Evaluation & SDF + Normal Channel Packing
// -----------------------------------------------------------------------------
void main() {
    // Screen derivatives must execute in uniform control flow prior to any discard.
    vec2 derivStep = vec2(abs(dFdx(qt_TexCoord0.x)), abs(dFdy(qt_TexCoord0.y)));
    vec2 uv = clampUV(qt_TexCoord0, u.uvClampRect);
    vec4 crispSample = textureLod(crispSdfTex, uv, 0.0);

    if (u.singleBufferMode >= 0.5 && u.discardZeroAlpha >= 0.5 && crispSample.a <= 0.0) {
        discard;
    }

    vec2 stepUV = (u.singleBufferMode >= 0.5)
        ? max(derivStep, u.texelAndScale.xy)
        : u.texelAndScale.xy;

    vec2 uvL = clampUV(uv - vec2(stepUV.x, 0.0), u.uvClampRect);
    vec2 uvR = clampUV(uv + vec2(stepUV.x, 0.0), u.uvClampRect);
    vec2 uvD = clampUV(uv - vec2(0.0, stepUV.y), u.uvClampRect);
    vec2 uvU = clampUV(uv + vec2(0.0, stepUV.y), u.uvClampRect);

    // Sample smoothSdfTex in dual-buffer mode for jitter-free bevel normals while keeping
    // the exact crisp edge distance in .r; fall back to crispSdfTex in single-buffer mode.
    float sL, sR, sD, sU;
    if (u.singleBufferMode >= 0.5) {
        sL = textureLod(crispSdfTex, uvL, 0.0).r;
        sR = textureLod(crispSdfTex, uvR, 0.0).r;
        sD = textureLod(crispSdfTex, uvD, 0.0).r;
        sU = textureLod(crispSdfTex, uvU, 0.0).r;
    } else {
        sL = textureLod(smoothSdfTex, uvL, 0.0).r;
        sR = textureLod(smoothSdfTex, uvR, 0.0).r;
        sD = textureLod(smoothSdfTex, uvD, 0.0).r;
        sU = textureLod(smoothSdfTex, uvU, 0.0).r;
    }

    vec2 grad = vec2(sR - sL, sU - sD);
    float len2 = dot(grad, grad);
    vec2 normal = (len2 > 1e-10) ? (grad * inversesqrt(len2)) : vec2(0.0);
    vec2 packedNormal = normal * u.texelAndScale.zw + u.packBiasAndPad.xy;

    fragColor = vec4(crispSample.r, packedNormal.x, packedNormal.y, 1.0) * u.qt_Opacity;
}
