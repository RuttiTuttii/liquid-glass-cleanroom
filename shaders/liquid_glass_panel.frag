#version 440
#extension GL_GOOGLE_include_directive : require
#include "include/glass_common.glsl"
#include "include/glass_sdf.glsl"
#include "include/glass_optics.glsl"

// -----------------------------------------------------------------------------
// [1] Stage I/O, Unified Std140 Uniform Buffer & Samplers
// -----------------------------------------------------------------------------
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float geometryMode;       // < 0.5: analytic G2-squircle/smin; >= 0.5: sampled SDF+normal map
    float compositeBackdrop;  // >= 0.5: opaque composite over backdropTex; < 0.5: premul overlay
    float noBleedMode;        // >= 0.5: suppress outer shadow and ambient bleed apron

    vec4 viewportAndSdf;      // xy = viewportPx, z = sdfScale, w = sdfBias
    vec4 panelCenterAndHalf;  // xy = centerPx, zw = halfExtentsPx
    vec4 cornerRadii;         // r(+x,+y), r(-x,+y), r(+x,-y), r(-x,-y)
    vec4 shapeParams;         // x = smoothness, y = ellipseMorph, z = smoothUnionK, w = secondaryEnabled
    vec4 secondaryCenterHalf; // xy = secCenterPx, zw = secHalfExtentsPx
    vec4 secondaryRadii;      // r(+x,+y), r(-x,+y), r(+x,-y), r(-x,-y)

    vec4 uvClampRect;         // uMin, vMin, uMax, vMax
    vec4 normalTransform;     // mat2 columns packed in xyzw
    vec4 dispTransform;       // mat2 mapping displacement px to UV offset

    vec4 refractionParams;    // x = refractStrength, y = bevelOffset, z = invBevelWidth, w = refractAngleRad
    vec4 dispersionParams;    // x = dispersionStrength, y = curvePow, z = dispersionAngleRad, w = bleedWidth
    vec4 bleedParams;         // x = bleedRefractStrength, y = bleedMixStrength, z = baseLod, w = multiBandEnabled
    vec4 blurBandOffsets;     // xyz = distance offsets A, B, C
    vec4 blurBandInvRanges;   // xyz = inv transition ranges A, B, C
    vec4 blurBandLods;        // xyz = mip LODs A, B, C

    vec4 shadowParams;        // xy = shadowOffsetPx, z = shadowInvRadius, w = shadowOpacity

    vec4 faceMatrixRow0;      // Interior glass 3x4 color-grade rows
    vec4 faceMatrixRow1;
    vec4 faceMatrixRow2;

    vec4 bleedMatrixRow0;     // Rim / outer bleed 3x4 color-grade rows
    vec4 bleedMatrixRow1;
    vec4 bleedMatrixRow2;

    vec4 shadowMatrixRow0;    // Shadow 3x4 color-grade / tint rows
    vec4 shadowMatrixRow1;
    vec4 shadowMatrixRow2;

    vec4 bevelSpecParams;     // xy = lightDir, z = bevelWidth, w = bevelOffset
    vec4 bevelSpecShape;      // x = cutoffCos, y = rimSoftness, z = edgeDecay, w = bounceIntensity
    vec4 specColorAndLuma;    // xyz = linearSpecColor, w = lumaAdaptStrength
    vec4 rimGlowParams;       // x = rimCenter, y = rimHalfWidth, z = innerAlpha, w = outerAlpha
    vec4 toneClampParams;     // x = clampLimit, y = floorLimit, z = preserveHue, w = hdrScale
    vec4 sdfPackParams;       // xy = normal unpackScale, zw = normal unpackBias
} u;

layout(binding = 1) uniform sampler2D backdropTex;
layout(binding = 2) uniform sampler2D sdfMapTex;

// -----------------------------------------------------------------------------
// [2] Analytic Panel Geometry Wrappers
// -----------------------------------------------------------------------------
float evalAnalyticPanelSdf(vec2 samplePx) {
    return sdPanelComposite(
        samplePx,
        u.panelCenterAndHalf.xy,
        u.panelCenterAndHalf.zw,
        u.cornerRadii,
        u.shapeParams.x,
        u.shapeParams.y,
        u.shapeParams.z,
        u.shapeParams.w,
        u.secondaryCenterHalf.xy,
        u.secondaryCenterHalf.zw,
        u.secondaryRadii
    );
}

vec2 evalAnalyticPanelNormal(vec2 samplePx) {
    return gradPanelComposite(
        samplePx,
        u.panelCenterAndHalf.xy,
        u.panelCenterAndHalf.zw,
        u.cornerRadii,
        u.shapeParams.x,
        u.shapeParams.y,
        u.shapeParams.z,
        u.shapeParams.w,
        u.secondaryCenterHalf.xy,
        u.secondaryCenterHalf.zw,
        u.secondaryRadii
    );
}

void main() {
    // -------------------------------------------------------------------------
    // [3] Coordinate Setup, SDF Evaluation & Antialiased Boundary Coverage
    // -------------------------------------------------------------------------
    vec2 viewport = max(u.viewportAndSdf.xy, vec2(1.0));
    vec2 pPx = qt_TexCoord0 * viewport;
    vec2 uvClamped = clampUV(qt_TexCoord0, u.uvClampRect);

    float d;
    vec2 n;
    float dSh;

    if (u.geometryMode < 0.5) {
        d = evalAnalyticPanelSdf(pPx);
        n = evalAnalyticPanelNormal(pPx);
        dSh = evalAnalyticPanelSdf(pPx + u.shadowParams.xy);
    } else {
        vec4 sdfVal = textureLod(sdfMapTex, uvClamped, 0.0);
        d = sdfVal.r * u.viewportAndSdf.z + u.viewportAndSdf.w;

        vec2 rawN = sdfVal.gb * u.sdfPackParams.xy + u.sdfPackParams.zw;
        vec2 xformN = unpackMat2(u.normalTransform) * rawN;
        float nLen2 = dot(xformN, xformN);
        n = (nLen2 > 1e-10) ? (xformN * inversesqrt(nLen2)) : vec2(0.0, 1.0);

        vec2 shUV = clampUV(qt_TexCoord0 + u.shadowParams.xy / viewport, u.uvClampRect);
        dSh = textureLod(sdfMapTex, shUV, 0.0).r * u.viewportAndSdf.z + u.viewportAndSdf.w;
    }

    // Compute derivative in uniform control flow so AA stays scale-independent under transforms.
    float fwD = fwidth(d);
    float coverage = evalAntialiasedCoverage(d, fwD, 1.0);

    // -------------------------------------------------------------------------
    // [4] Convex Bevel Refraction & Multi-Band Spectral Dispersion Sampling
    // -------------------------------------------------------------------------
    mat2 mDisp = unpackMat2(u.dispTransform);
    vec2 nRefract = rotate2D(n, u.refractionParams.w);
    vec2 nAberr = rotate2D(n, u.dispersionParams.z);

    float hLens = convexBevelProfile(d, u.refractionParams.y, u.refractionParams.z);
    vec2 uvRefract = clampUV(uvClamped + hLens * u.refractionParams.x * (mDisp * nRefract), u.uvClampRect);
    float dispScale = pow(max(hLens, 0.0), max(u.dispersionParams.y, 0.25));
    vec2 dispDelta = dispScale * u.dispersionParams.x * (mDisp * nAberr);

    vec4 interiorSample = sampleMultiBandBackdrop(
        backdropTex,
        uvRefract,
        dispDelta,
        u.uvClampRect,
        d,
        u.bleedParams.z,
        u.bleedParams.w,
        u.blurBandOffsets.xyz,
        u.blurBandInvRanges.xyz,
        u.blurBandLods.xyz
    );

    // -------------------------------------------------------------------------
    // [5] Linear Affine Grading, Bevel Specular & Hue-Preserving Clamp
    // -------------------------------------------------------------------------
    vec3 bgLinear = unpremultiplySafe(interiorSample, 1e-5);
    vec3 faceGraded = applyAffineGrade(bgLinear, u.faceMatrixRow0, u.faceMatrixRow1, u.faceMatrixRow2);
    vec3 bleedGraded = applyAffineGrade(bgLinear, u.bleedMatrixRow0, u.bleedMatrixRow1, u.bleedMatrixRow2);
    vec3 glassLinear = mix(faceGraded, bleedGraded, clamp(hLens * u.bleedParams.y, 0.0, 1.0));

    float luma = rec709Luminance(bgLinear);
    float lumaMod = mix(1.0, lumaAdaptiveResponse(luma), clamp(u.specColorAndLuma.w, 0.0, 1.0));
    float bevelSpec = evalBevelHighlight(
        d, fwD, n,
        u.bevelSpecParams.xy,
        u.bevelSpecParams.z,
        u.bevelSpecParams.w,
        u.bevelSpecShape.x,
        u.bevelSpecShape.y,
        u.bevelSpecShape.z,
        u.bevelSpecShape.w
    );
    float rimGlow = evalCosineRimGlow(
        d,
        u.rimGlowParams.x,
        u.rimGlowParams.y,
        u.rimGlowParams.z,
        u.rimGlowParams.w
    );
    glassLinear += u.specColorAndLuma.xyz * (bevelSpec + rimGlow) * lumaMod;
    glassLinear = huePreservingClamp(
        glassLinear * u.toneClampParams.w,
        u.toneClampParams.x,
        u.toneClampParams.y,
        u.toneClampParams.z
    );

    // -------------------------------------------------------------------------
    // [6] Outer Ambient Bleed Apron, Soft Shadow & Final Composite Output
    // -------------------------------------------------------------------------
    float shadowFactor = (u.noBleedMode < 0.5)
        ? evalShadowFalloff(dSh, u.shadowParams.z, u.shadowParams.w)
        : 0.0;
    float hBleed = (u.noBleedMode < 0.5)
        ? outerBleedProfile(d, u.dispersionParams.w)
        : 0.0;
    float bleedWeight = clamp(hBleed * u.bleedParams.y, 0.0, 1.0);

    vec2 uvBleed = clampUV(uvClamped + hBleed * u.bleedParams.x * (mDisp * nRefract), u.uvClampRect);
    vec4 bleedSample = (bleedWeight > 1e-4)
        ? sampleBackdropDispersed(backdropTex, uvBleed, hBleed * dispDelta * 0.5, u.uvClampRect, u.bleedParams.z)
        : vec4(0.0);

    if (u.compositeBackdrop >= 0.5) {
        vec4 baseBg = textureLod(backdropTex, uvClamped, 0.0);
        vec3 outerBg = unpremultiplySafe(baseBg, 1e-5);
        if (bleedWeight > 1e-4) {
            vec3 bLin = unpremultiplySafe(bleedSample, 1e-5);
            vec3 bGraded = applyAffineGrade(bLin, u.bleedMatrixRow0, u.bleedMatrixRow1, u.bleedMatrixRow2);
            outerBg = mix(outerBg, bGraded, bleedWeight);
        }
        fragColor = compositeGlassOverBackdrop(
            outerBg, glassLinear, coverage, shadowFactor,
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
        if (bleedWeight > 1e-4) {
            vec3 bLin = unpremultiplySafe(bleedSample, 1e-5);
            vec3 bGraded = applyAffineGrade(bLin, u.bleedMatrixRow0, u.bleedMatrixRow1, u.bleedMatrixRow2);
            bGraded = huePreservingClamp(
                bGraded * u.toneClampParams.w,
                u.toneClampParams.x, u.toneClampParams.y, u.toneClampParams.z
            );
            outerPremul = mix(outerPremul, bGraded, bleedWeight);
            outerAlpha = max(outerAlpha, bleedWeight);
            outerPremul = min(outerPremul, vec3(outerAlpha * u.toneClampParams.x));
        }
        fragColor = finalizeOverlayGlass(outerPremul, outerAlpha, glassLinear, coverage, u.qt_Opacity);
    }
}
