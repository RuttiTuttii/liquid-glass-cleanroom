#version 440
#extension GL_GOOGLE_include_directive : require
#include "include/glass_common.glsl"
#include "include/glass_sdf.glsl"
#include "include/glass_optics.glsl"

// -----------------------------------------------------------------------------
// [1] Stage I/O, Uniform Buffer & Backdrop Sampler
// -----------------------------------------------------------------------------
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float shapeMode;          // < 0.5: capsule [p0, p1]; >= 0.5: torus ring (center, majorRadius)
    float compositeBackdrop;  // >= 0.5: opaque composite over backdropTex; < 0.5: premul overlay
    float noBleedMode;        // >= 0.5: suppress outer shadow for fixed clipped layer surfaces

    vec4 viewportAndShape;    // xy = viewportPx, z = lensRadiusPx, w = levitationPx
    vec4 capsulePts;          // capsule: xy = p0Px, zw = p1Px | torus: xy = centerPx, z = majorRadiusPx
    vec4 opticsParams;        // x = ior, y = dispersionStrength, z = mipLod, w = tirReflectivity
    vec4 uvClampRect;         // uMin, vMin, uMax, vMax
    vec4 dispTransform;       // mat2 mapping pixel displacement to UV offset
    vec4 shadowParams;        // xy = shadowOffsetPx, z = shadowInvRadius, w = shadowOpacity

    vec4 faceMatrixRow0;      // Lens interior 3x4 color-grade rows
    vec4 faceMatrixRow1;
    vec4 faceMatrixRow2;

    vec4 shadowMatrixRow0;    // Shadow 3x4 color-grade / tint rows
    vec4 shadowMatrixRow1;
    vec4 shadowMatrixRow2;

    vec4 bevelSpecParams;     // xy = lightDir, z = bevelWidth, w = bevelOffset
    vec4 bevelSpecShape;      // x = cutoffCos, y = rimSoftness, z = edgeDecay, w = bounceIntensity
    vec4 specColorAndLuma;    // xyz = linearSpecColor, w = lumaAdaptStrength
    vec4 toneClampParams;     // x = clampLimit, y = floorLimit, z = preserveHue, w = hdrScale
} u;

layout(binding = 1) uniform sampler2D backdropTex;

// -----------------------------------------------------------------------------
// [2] Capsule / Torus Cross-Section Geometry Dispatcher
// -----------------------------------------------------------------------------
void evalLensShape(vec2 pPx, out float delta, out vec2 outwardDir) {
    if (u.shapeMode < 0.5) {
        evalCapsuleGeometry(pPx, u.capsulePts.xy, u.capsulePts.zw, delta, outwardDir);
    } else {
        evalTorusGeometry(pPx, u.capsulePts.xy, u.capsulePts.z, delta, outwardDir);
    }
}

void main() {
    // -------------------------------------------------------------------------
    // [3] Coordinate Setup, Primary & Shadow SDF Distances, Boundary AA
    // -------------------------------------------------------------------------
    vec2 viewport = max(u.viewportAndShape.xy, vec2(1.0));
    vec2 pPx = qt_TexCoord0 * viewport;
    vec2 uvClamped = clampUV(qt_TexCoord0, u.uvClampRect);
    float radiusPx = max(u.viewportAndShape.z, 1e-3);

    float delta;
    vec2 outwardDir;
    evalLensShape(pPx, delta, outwardDir);
    float d = delta - radiusPx;

    float deltaSh;
    vec2 outwardSh;
    evalLensShape(pPx + u.shadowParams.xy, deltaSh, outwardSh);
    float dSh = deltaSh - radiusPx;

    // Evaluate screen derivative in uniform control flow for crisp bevel AA.
    float fwD = fwidth(d);
    float aaPerimeter = clamp(radiusPx - delta + 0.5, 0.0, 1.0);
    float refractMix = clamp(radiusPx - delta, 0.0, 1.0);

    // -------------------------------------------------------------------------
    // [4] 3D Snell Thin-Lens Ray-Tracing & Tangent-Aligned Dispersion
    // -------------------------------------------------------------------------
    vec2 pixelDisp;
    vec3 n3D;
    float tirFactor;
    traceSnellThinLens(
        delta,
        radiusPx,
        outwardDir,
        u.viewportAndShape.w,
        u.opticsParams.x,
        pixelDisp,
        n3D,
        tirFactor
    );

    mat2 mDisp = unpackMat2(u.dispTransform);
    vec2 uvOffset = mDisp * pixelDisp;
    vec2 uvRefract = clampUV(uvClamped + uvOffset, u.uvClampRect);

    // Align spectral prism smear with lateral refracted ray tangent, scaling gently with slope.
    float dispLen = length(pixelDisp);
    vec2 dispDirPx = (dispLen > 1e-5) ? (pixelDisp / dispLen) : outwardDir;
    vec2 dispDelta = (mDisp * (dispDirPx * u.opticsParams.y)) * (1.0 - n3D.z);

    vec4 refractedSample = sampleBackdropDispersed(
        backdropTex,
        uvRefract,
        dispDelta,
        u.uvClampRect,
        u.opticsParams.z
    );
    vec4 unrefractedSample = textureLod(backdropTex, uvClamped, 0.0);

    // -------------------------------------------------------------------------
    // [5] Linear Grading, Total Internal Reflection (TIR) & Bevel Specular
    // -------------------------------------------------------------------------
    vec3 refLin = unpremultiplySafe(refractedSample, 1e-5);
    vec3 unrefLin = unpremultiplySafe(unrefractedSample, 1e-5);
    vec3 blendedBg = mix(unrefLin, refLin, refractMix);

    vec3 glassLinear = applyAffineGrade(blendedBg, u.faceMatrixRow0, u.faceMatrixRow1, u.faceMatrixRow2);

    // Apply silvery critical-angle reflection tint inside the Total Internal Reflection band.
    if (tirFactor > 0.0) {
        vec3 tirTint = mix(glassLinear, u.specColorAndLuma.xyz * 0.85, clamp(tirFactor * u.opticsParams.w, 0.0, 1.0));
        glassLinear = mix(glassLinear, tirTint, refractMix);
    }

    float luma = rec709Luminance(blendedBg);
    float lumaMod = mix(1.0, lumaAdaptiveResponse(luma), clamp(u.specColorAndLuma.w, 0.0, 1.0));

    // Project 3D surface normal into 2D for directional rim specular evaluation.
    vec2 n2D = n3D.xy;
    float n2Len2 = dot(n2D, n2D);
    vec2 nUnit2D = (n2Len2 > 1e-8) ? (n2D * inversesqrt(n2Len2)) : outwardDir;

    float bevelSpec = evalBevelHighlight(
        d, fwD, nUnit2D,
        u.bevelSpecParams.xy,
        u.bevelSpecParams.z,
        u.bevelSpecParams.w,
        u.bevelSpecShape.x,
        u.bevelSpecShape.y,
        u.bevelSpecShape.z,
        u.bevelSpecShape.w
    );
    glassLinear += u.specColorAndLuma.xyz * bevelSpec * lumaMod;
    glassLinear = huePreservingClamp(
        glassLinear * u.toneClampParams.w,
        u.toneClampParams.x,
        u.toneClampParams.y,
        u.toneClampParams.z
    );

    // -------------------------------------------------------------------------
    // [6] Soft Drop Shadow & Final Backdrop / Overlay Composite
    // -------------------------------------------------------------------------
    float shadowFactor = (u.noBleedMode < 0.5)
        ? evalShadowFalloff(dSh, u.shadowParams.z, u.shadowParams.w)
        : 0.0;

    if (u.compositeBackdrop >= 0.5) {
        fragColor = compositeGlassOverBackdrop(
            unrefLin, glassLinear, aaPerimeter, shadowFactor,
            u.shadowMatrixRow0, u.shadowMatrixRow1, u.shadowMatrixRow2,
            u.toneClampParams, u.qt_Opacity
        );
    } else {
        vec3 outerPremul;
        float outerAlpha;
        initOverlayShadow(
            shadowFactor,
            u.shadowMatrixRow0, u.shadowMatrixRow1, u.shadowMatrixRow2,
            u.toneClampParams, outerPremul, outerAlpha
        );
        fragColor = finalizeOverlayGlass(outerPremul, outerAlpha, glassLinear, aaPerimeter, u.qt_Opacity);
    }
}
