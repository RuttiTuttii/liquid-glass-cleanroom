#version 440
#extension GL_GOOGLE_include_directive : require
#include "include/glass_common.glsl"

// -----------------------------------------------------------------------------
// [1] Stage I/O, Uniform Buffer & Pyramid/Mask Samplers
// -----------------------------------------------------------------------------
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float blurRadius;
    float maxLod;
    float scatterScale;
    vec4 texelAndAngle;  // xy = base 1/texSize, z = scatterAngleRad, w = useBlurMask (>=0.5)
    vec4 resolveParams;  // x = unpremultiplyMode (>=0.5), y = eps, z = ditherAmplitude, w = maskUseRed
    vec4 uvClampRect;    // uMin, vMin, uMax, vMax
} u;

layout(binding = 1) uniform sampler2D pyramidTex;
layout(binding = 2) uniform sampler2D blurMaskTex;
layout(binding = 3) uniform sampler2D coverageMaskTex;

// -----------------------------------------------------------------------------
// [2] Spatially Varying Blur & Coverage-Preserving Resolve
// -----------------------------------------------------------------------------
void main() {
    vec2 uv = clampUV(qt_TexCoord0, u.uvClampRect);

    // Modulate target blur radius by an optional spatial mask (feathering).
    float r = u.blurRadius;
    if (u.texelAndAngle.w >= 0.5) {
        r *= sampleMaskChannel(blurMaskTex, uv, u.uvClampRect, u.resolveParams.w);
    }

    // Smooth-knee LOD transfer curve: avoids log2 singularity at r = 0 while remaining
    // C0-continuous across the knee transition at r = 2 px.
    float rSafe = max(r, 0.0);
    float knee = (rSafe < 2.0) ? (0.5 * rSafe + 1.0) : rSafe;
    float lod = clamp(log2(knee), 0.0, max(u.maxLod, 0.0));

    // 4-tap rotated cross scatter scaled gently to zero as blur radius -> 0.
    float scatterAmp = u.scatterScale * min(rSafe * 0.5, 1.0) * exp2(lod);
    vec2 d1 = rotate2D(vec2(scatterAmp, 0.0), u.texelAndAngle.z) * u.texelAndAngle.xy;
    vec2 d2 = vec2(-d1.y, d1.x);

    vec4 s0 = textureLod(pyramidTex, clampUV(uv + d1, u.uvClampRect), lod);
    vec4 s1 = textureLod(pyramidTex, clampUV(uv - d1, u.uvClampRect), lod);
    vec4 s2 = textureLod(pyramidTex, clampUV(uv + d2, u.uvClampRect), lod);
    vec4 s3 = textureLod(pyramidTex, clampUV(uv - d2, u.uvClampRect), lod);
    vec4 blurred = (s0 + s1 + s2 + s3) * 0.25;

    // Optional un-premultiply + crisp mask restore prevents feathered layer silhouettes
    // from doubling alpha softness or picking up dark haloing at cut edges.
    vec4 outColor = blurred;
    if (u.resolveParams.x >= 0.5) {
        float eps = max(u.resolveParams.y, 1e-5);
        vec3 unpremul = unpremultiplySafe(blurred, eps);
        float crispAlpha = clamp(sampleMaskChannel(coverageMaskTex, uv, u.uvClampRect, u.resolveParams.w), 0.0, 1.0);
        outColor = vec4(unpremul * crispAlpha, crispAlpha);
    }

    // Subtle triangular-PDF dither breaks 8-bit contour banding on smooth ramps.
    if (u.resolveParams.z > 0.0) {
        float dither = triangularDither(gl_FragCoord.xy) * u.resolveParams.z;
        outColor.rgb = clamp(outColor.rgb + vec3(dither) * outColor.a, vec3(0.0), vec3(max(outColor.a, 0.0)));
    }

    fragColor = outColor * u.qt_Opacity;
}
