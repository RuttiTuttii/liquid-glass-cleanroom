#ifndef GLASS_SDF_GLSL
#define GLASS_SDF_GLSL

#include "glass_common.glsl"

// -----------------------------------------------------------------------------
// [1] Analytic Panel SDF Primitives (G2-Squircle, Ellipse Morph & Smooth Union)
// -----------------------------------------------------------------------------

// Evaluates a rounded rectangle with continuous G2-style superellipse corners
// ("squircle", controlled by `smoothness`) and an optional blend toward an ellipse.
// cornerRadii order: vec4(r(+x,+y), r(-x,+y), r(+x,-y), r(-x,-y)).
float sdSquircleEllipse(
    vec2 p,
    vec2 halfExtents,
    vec4 cornerRadii,
    float smoothness,
    float ellipseMorph
) {
    vec2 safeH = max(halfExtents, vec2(1e-4));
    float minExt = min(safeH.x, safeH.y);

    // Select corner radius matching the pixel's quadrant.
    vec2 rPair = (p.x >= 0.0) ? vec2(cornerRadii.x, cornerRadii.z) : vec2(cornerRadii.y, cornerRadii.w);
    float rRaw = (p.y >= 0.0) ? rPair.x : rPair.y;
    float r = clamp(rRaw, 0.0, minExt);

    // Expand corner transition radius and superellipse exponent p = 2..4.5 with smoothness.
    float s = clamp(smoothness, 0.0, 1.0);
    float rEff = min(r * (1.0 + 0.5 * s), minExt);
    float pExp = 2.0 + 2.5 * s;

    vec2 q = abs(p) - (safeH - vec2(rEff));
    vec2 qp = max(q, vec2(0.0));
    float m = max(qp.x, qp.y);

    // Normalize by max component before pow() so extreme values never overflow FP16/FP32.
    float dCorner = 0.0;
    if (m > 1e-6) {
        vec2 u = qp / m;
        float ux = (u.x > 1e-6) ? pow(u.x, pExp) : 0.0;
        float uy = (u.y > 1e-6) ? pow(u.y, pExp) : 0.0;
        dCorner = m * pow(ux + uy, 1.0 / pExp);
    }
    float dInside = min(max(q.x, q.y), 0.0);
    float dSquircle = dCorner + dInside - rEff;

    float e = clamp(ellipseMorph, 0.0, 1.0);
    if (e <= 1e-5) {
        return dSquircle;
    }

    // Approximate Euclidean distance to ellipse boundary for shape morphing.
    vec2 pe = p / safeH;
    float k0 = length(pe);
    float dEllipse = (k0 > 1e-6)
        ? k0 * (k0 - 1.0) / max(length(pe / safeH), 1e-6)
        : -minExt;

    return mix(dSquircle, dEllipse, e);
}

// Evaluates primary panel SDF with an optional C1 smooth-minimum coalesced pill.
float sdPanelComposite(
    vec2 pPx,
    vec2 centerPx,
    vec2 halfExtentsPx,
    vec4 cornerRadii,
    float smoothness,
    float ellipseMorph,
    float smoothUnionK,
    float secondaryEnabled,
    vec2 secCenterPx,
    vec2 secHalfExtentsPx,
    vec4 secCornerRadii
) {
    float d1 = sdSquircleEllipse(pPx - centerPx, halfExtentsPx, cornerRadii, smoothness, ellipseMorph);
    if (secondaryEnabled < 0.5) {
        return d1;
    }
    float d2 = sdSquircleEllipse(pPx - secCenterPx, secHalfExtentsPx, secCornerRadii, smoothness, ellipseMorph);
    return smoothMinPoly(d1, d2, smoothUnionK);
}

// Computes the outward unit normal of `sdPanelComposite` via symmetric central
// differences, blended with exact ellipse gradients during ellipse morphs.
vec2 gradPanelComposite(
    vec2 pPx,
    vec2 centerPx,
    vec2 halfExtentsPx,
    vec4 cornerRadii,
    float smoothness,
    float ellipseMorph,
    float smoothUnionK,
    float secondaryEnabled,
    vec2 secCenterPx,
    vec2 secHalfExtentsPx,
    vec4 secCornerRadii
) {
    const float eps = 0.25;
    float dx = sdPanelComposite(pPx + vec2(eps, 0.0), centerPx, halfExtentsPx, cornerRadii, smoothness, ellipseMorph, smoothUnionK, secondaryEnabled, secCenterPx, secHalfExtentsPx, secCornerRadii)
             - sdPanelComposite(pPx - vec2(eps, 0.0), centerPx, halfExtentsPx, cornerRadii, smoothness, ellipseMorph, smoothUnionK, secondaryEnabled, secCenterPx, secHalfExtentsPx, secCornerRadii);
    float dy = sdPanelComposite(pPx + vec2(0.0, eps), centerPx, halfExtentsPx, cornerRadii, smoothness, ellipseMorph, smoothUnionK, secondaryEnabled, secCenterPx, secHalfExtentsPx, secCornerRadii)
             - sdPanelComposite(pPx - vec2(0.0, eps), centerPx, halfExtentsPx, cornerRadii, smoothness, ellipseMorph, smoothUnionK, secondaryEnabled, secCenterPx, secHalfExtentsPx, secCornerRadii);

    vec2 g = vec2(dx, dy);
    float len2 = dot(g, g);
    vec2 fdNormal = (len2 > 1e-12) ? (g * inversesqrt(len2)) : vec2(0.0, 1.0);

    if (ellipseMorph > 0.0 && secondaryEnabled < 0.5) {
        vec2 safeH = max(halfExtentsPx, vec2(1e-4));
        vec2 pe = (pPx - centerPx) / (safeH * safeH);
        float eLen2 = dot(pe, pe);
        vec2 ellipseNormal = (eLen2 > 1e-12) ? (pe * inversesqrt(eLen2)) : fdNormal;
        return normalize(mix(fdNormal, ellipseNormal, clamp(ellipseMorph, 0.0, 1.0)));
    }
    return fdNormal;
}

// -----------------------------------------------------------------------------
// [2] 3D Lens Cross-Section Geometry (Capsule Pill & Torus Ring)
// -----------------------------------------------------------------------------

// Computes shortest distance `delta` and outward unit normal from 2D spine [a, b].
void evalCapsuleGeometry(
    vec2 p,
    vec2 a,
    vec2 b,
    out float delta,
    out vec2 outwardDir
) {
    vec2 ab = b - a;
    float abLen2 = dot(ab, ab);
    float t = (abLen2 > 1e-8) ? clamp(dot(p - a, ab) / abLen2, 0.0, 1.0) : 0.0;
    vec2 closest = a + t * ab;
    vec2 diff = p - closest;
    delta = length(diff);
    if (delta > 1e-5) {
        outwardDir = diff / delta;
    } else if (abLen2 > 1e-8) {
        // Exactly on the spine segment: pick perpendicular normal so normals stay defined.
        vec2 axis = normalize(ab);
        outwardDir = vec2(-axis.y, axis.x);
    } else {
        outwardDir = vec2(0.0, 1.0);
    }
}

// Computes tube-center distance `delta` and sign-flipped outward normal for a ring.
void evalTorusGeometry(
    vec2 p,
    vec2 center,
    float majorRadius,
    out float delta,
    out vec2 outwardDir
) {
    vec2 v = p - center;
    float rLen = length(v);
    vec2 radial = (rLen > 1e-5) ? (v / rLen) : vec2(1.0, 0.0);
    float signedRadial = rLen - majorRadius;
    delta = abs(signedRadial);
    outwardDir = (signedRadial >= 0.0) ? radial : -radial;
}

// -----------------------------------------------------------------------------
// [3] Resolution-Independent Anti-Aliasing, Bevel Specular & Rim Glow
// -----------------------------------------------------------------------------

// Linear boundary coverage ramp across screen-space derivative `fwD`.
float evalAntialiasedCoverage(float d, float fwD, float shapeMask) {
    float aaWidth = max(fwD, 1e-4);
    return clamp(0.5 - d / aaWidth, 0.0, 1.0) * shapeMask;
}

// Evaluates directional rim specular highlight across an interior bevel band,
// combining a crisp primary key-light arc with a softer opposing bounce lobe.
float evalBevelHighlight(
    float d,
    float fwD,
    vec2 n,
    vec2 lightDir,
    float bevelWidth,
    float bevelOffset,
    float cutoffCos,
    float rimSoftness,
    float edgeDecay,
    float bounceIntensity
) {
    float wAA = clamp(fwD, 1e-4, 2.0) * (2.0 / 3.0);
    float dPrime = -d - bevelOffset;
    float mOuter = clamp((bevelWidth - dPrime) / wAA + 0.5, 0.0, 1.0);
    float mInner = clamp(dPrime / wAA + 0.5, 0.0, 1.0);
    float lLen2 = dot(lightDir, lightDir);
    vec2 lUnit = (lLen2 > 1e-8) ? (lightDir * inversesqrt(lLen2)) : vec2(-0.70710678, -0.70710678);

    float a = clamp((dot(lUnit, n) - cutoffCos) / max(1.0 - cutoffCos, 1e-4), 0.0, 1.0);
    float mDecay = mix(1.0, 1.0 - clamp(dPrime / max(bevelWidth, 1e-4), 0.0, 1.0), clamp(edgeDecay, 0.0, 1.0));

    float primary = (mOuter * mInner * mDecay * a) / max(1.0 + (1.0 - a) * rimSoftness, 1e-4);
    float oppDot = max(dot(-lUnit, n), 0.0);
    float bounce = mOuter * mInner * mDecay * (oppDot * oppDot * oppDot) * bounceIntensity;
    return primary + bounce;
}

// Evaluates a cosine-bell perimeter stroke centered at `rimCenter` with
// independent intensity controls inside (`d <= 0`) and outside (`d > 0`).
float evalCosineRimGlow(
    float d,
    float rimCenter,
    float rimHalfWidth,
    float innerAlpha,
    float outerAlpha
) {
    if (rimHalfWidth <= 1e-5) {
        return 0.0;
    }
    float phase = clamp(abs(d - rimCenter) / rimHalfWidth, 0.0, 1.0);
    float bell = 0.5 * (1.0 + cos(GLASS_PI * phase));
    float sideAlpha = (d <= 0.0) ? innerAlpha : outerAlpha;
    return bell * sideAlpha;
}

#endif
