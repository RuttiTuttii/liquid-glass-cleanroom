#ifndef GLASS_OPTICS_GLSL
#define GLASS_OPTICS_GLSL

#include "glass_common.glsl"

// -----------------------------------------------------------------------------
// [1] Convex Bevel & Outer Ambient Bleed Displacement Profiles
// -----------------------------------------------------------------------------

// Quarter-circle bevel height slope: steepest (1.0) at rim u = 0 and C1
// tangent (0.0) where the rounded bevel merges into the flat interior u = 1.
float convexBevelProfile(float d, float bevelOffset, float invBevelWidth) {
    float u = clamp((-d - bevelOffset) * invBevelWidth, 0.0, 1.0);
    return 1.0 - sqrt(clamp(u * (2.0 - u), 0.0, 1.0));
}

// Smooth cubic apron bleed curve extending outward across d in (0, bleedWidth).
float outerBleedProfile(float d, float bleedWidth) {
    if (bleedWidth <= 1e-5 || d <= 0.0) {
        return 0.0;
    }
    float t = clamp(1.0 - d / bleedWidth, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// -----------------------------------------------------------------------------
// [2] Spectral Dispersion & Multi-Band Mip-Roughness Samplers
// -----------------------------------------------------------------------------

// Samples the backdrop pyramid with a clamped 7-tap spectral prism curve.
// Skips the loop cleanly when dispersionDelta is near zero.
vec4 sampleBackdropDispersed(
    sampler2D backdropTex,
    vec2 uvRefract,
    vec2 dispersionDelta,
    vec4 uvClampRect,
    float lod
) {
    if (dot(dispersionDelta, dispersionDelta) < 1e-12) {
        return textureLod(backdropTex, clampUV(uvRefract, uvClampRect), lod);
    }

    vec3 rgbAccum = vec3(0.0);
    float alphaAccum = 0.0;
    const vec3 lumaWeights = vec3(0.2126, 0.7152, 0.0722);

    for (int i = 0; i < 7; ++i) {
        float shift = spectralTapShift(i);
        vec2 tapUV = clampUV(uvRefract + shift * dispersionDelta, uvClampRect);
        vec4 s = textureLod(backdropTex, tapUV, lod);
        vec3 w = spectralTapWeight(i);
        rgbAccum += s.rgb * w;
        alphaAccum += s.a * dot(w, lumaWeights);
    }
    return vec4(rgbAccum, alphaAccum);
}

// Blends 3 depth-keyed mip roughness tiers (sharp edge refraction -> mid frost
// -> deep interior diffusion) with proportionally attenuated chromatic dispersion.
vec4 sampleMultiBandBackdrop(
    sampler2D backdropTex,
    vec2 uvRefract,
    vec2 dispersionDelta,
    vec4 uvClampRect,
    float d,
    float baseLod,
    float multiBandEnabled,
    vec3 bandOffsets,
    vec3 bandInvRanges,
    vec3 bandLods
) {
    if (multiBandEnabled < 0.5) {
        return sampleBackdropDispersed(backdropTex, uvRefract, dispersionDelta, uvClampRect, baseLod);
    }

    float depth = max(-d, 0.0);
    float wA = clamp((depth - bandOffsets.x) * bandInvRanges.x, 0.0, 1.0);
    float wB = clamp((depth - bandOffsets.y) * bandInvRanges.y, 0.0, 1.0);
    float wC = clamp((depth - bandOffsets.z) * bandInvRanges.z, 0.0, 1.0);

    wA = wA * wA * (3.0 - 2.0 * wA);
    wB = wB * wB * (3.0 - 2.0 * wB);
    wC = wC * wC * (3.0 - 2.0 * wC);

    vec4 sA = sampleBackdropDispersed(backdropTex, uvRefract, dispersionDelta, uvClampRect, bandLods.x);
    vec4 sB = sampleBackdropDispersed(backdropTex, uvRefract, dispersionDelta * 0.65, uvClampRect, bandLods.y);
    vec4 sC = sampleBackdropDispersed(backdropTex, uvRefract, dispersionDelta * 0.30, uvClampRect, bandLods.z);

    vec4 blended = mix(sA, sB, wB);
    blended = mix(blended, sC, wC);
    return mix(sampleBackdropDispersed(backdropTex, uvRefract, dispersionDelta, uvClampRect, baseLod), blended, wA);
}

// -----------------------------------------------------------------------------
// [3] Physical 3D Snell Thin-Lens Ray-Tracing & Total Internal Reflection (TIR)
// -----------------------------------------------------------------------------

// Traces a primary view ray through a semicircular cylindrical/toroidal glass
// profile raised by `levitationPx` above the backdrop plane z = 0.
// Emits finite pixel displacement and soft `tirFactor` when ior < 1 hits critical angle.
void traceSnellThinLens(
    float delta,
    float radiusPx,
    vec2 outwardDir2D,
    float levitationPx,
    float ior,
    out vec2 pixelDisp,
    out vec3 surfaceNormal3D,
    out float tirFactor
) {
    float safeR = max(radiusPx, 1e-4);
    float rho = clamp(delta / safeR, 0.0, 1.0);
    float nz = sqrt(max(0.0, 1.0 - rho * rho));
    surfaceNormal3D = vec3(rho * outwardDir2D, nz);

    float zSurface = nz * safeR + max(levitationPx, 0.0);
    float eta = 1.0 / max(ior, 1e-4);
    float k = 1.0 - eta * eta * (1.0 - nz * nz);

    if (k < 0.0) {
        // Total Internal Reflection near grazing lens rim: zero sqrt(-k) hazard.
        tirFactor = clamp(-k / max(eta * eta, 1e-4), 0.0, 1.0);
        pixelDisp = clamp(-outwardDir2D * safeR * 0.35, vec2(-2.0 * safeR), vec2(2.0 * safeR));
        return;
    }

    tirFactor = 0.0;
    vec3 refractedDir = (eta * nz - sqrt(k)) * surfaceNormal3D - vec3(0.0, 0.0, eta);
    float tz = min(refractedDir.z, -1e-4);
    float tHit = -zSurface / tz;
    pixelDisp = clamp(tHit * refractedDir.xy, vec2(-2.0 * safeR), vec2(2.0 * safeR));
}

// -----------------------------------------------------------------------------
// [4] Drop Shadow Falloff & Shared Backdrop/Overlay Output Compositing
// -----------------------------------------------------------------------------

// Raised-cosine drop shadow curve: smooth across [0, shadowRadius], zero outside.
float evalShadowFalloff(float dSh, float shadowInvRadius, float shadowOpacity) {
    if (shadowOpacity <= 1e-5 || shadowInvRadius <= 1e-6) {
        return 0.0;
    }
    float sigma = clamp((-dSh) * abs(shadowInvRadius) + 0.5, 0.0, 1.0);
    float bell = 0.5 * (1.0 - cos(GLASS_PI * sigma));
    return bell * clamp(shadowOpacity, 0.0, 1.0);
}

// Applies optional shadow grading onto `outerBg`, clamps dynamic range cleanly,
// and composites opaque final glass over the backdrop.
vec4 compositeGlassOverBackdrop(
    vec3 outerBg,
    vec3 glassLinear,
    float coverage,
    float shadowFactor,
    vec4 shadowRow0,
    vec4 shadowRow1,
    vec4 shadowRow2,
    vec4 toneClampParams,
    float qtOpacity
) {
    vec3 bg = outerBg;
    if (shadowFactor > 1e-4) {
        vec3 shadowGraded = applyAffineGrade(bg, shadowRow0, shadowRow1, shadowRow2);
        bg = mix(bg, shadowGraded, shadowFactor);
    }
    bg = huePreservingClamp(
        bg * toneClampParams.w,
        toneClampParams.x,
        toneClampParams.y,
        toneClampParams.z
    );
    vec3 finalRgb = mix(bg, glassLinear, coverage);
    return vec4(finalRgb, 1.0) * qtOpacity;
}

// Initializes straight-to-premultiplied outer shadow accumulators for standalone overlay mode.
void initOverlayShadow(
    float shadowFactor,
    vec4 shadowRow0,
    vec4 shadowRow1,
    vec4 shadowRow2,
    vec4 toneClampParams,
    out vec3 outerPremul,
    out float outerAlpha
) {
    vec3 shadowTint = applyAffineGrade(vec3(0.0), shadowRow0, shadowRow1, shadowRow2);
    shadowTint = huePreservingClamp(shadowTint, toneClampParams.x, 0.0, toneClampParams.z);
    outerPremul = shadowTint * shadowFactor;
    outerAlpha = shadowFactor;
}

// Blends outer shadow/bleed apron with opaque glass interior into strict premultiplied RGBA.
vec4 finalizeOverlayGlass(
    vec3 outerPremul,
    float outerAlpha,
    vec3 glassLinear,
    float coverage,
    float qtOpacity
) {
    float outAlpha = mix(outerAlpha, 1.0, coverage);
    vec3 outPremul = mix(outerPremul, glassLinear, coverage);
    return vec4(outPremul, outAlpha) * qtOpacity;
}

#endif
