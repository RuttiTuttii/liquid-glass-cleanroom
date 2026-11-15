/**
 * Clean-Room Liquid Glass Shader Suite — WebGL2 (GLSL ES 3.00)
 *
 * SPDX-License-Identifier: MIT
 *
 * Original Web Lite implementation derived mathematically from the repository's
 * clean-room GLSL 440 shaders (`shaders/downsample_clamped.frag`,
 * `shaders/include/glass_common.glsl`, `glass_sdf.glsl`, `glass_optics.glsl`)
 * and `docs/WEB_PORT_PLAN.md`.
 */

// -----------------------------------------------------------------------------
// [1] Fullscreen Quad Vertex Shader (13-Tap Pyramid Downsample Passes)
// -----------------------------------------------------------------------------
export const VERTEX_FULLSCREEN = `#version 300 es
precision highp float;

layout(location = 0) in vec2 aPos;
out vec2 vUv;

void main() {
    vUv = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
`;

// -----------------------------------------------------------------------------
// [2] Clamped 13-Tap Energy-Preserving Dual-Ring Downsample Fragment Shader
// -----------------------------------------------------------------------------
export const FRAGMENT_DOWNSAMPLE_CLAMPED = `#version 300 es
precision highp float;

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uSrcTex;
uniform vec2 uSrcTexelSize; // 1.0 / srcWidth, 1.0 / srcHeight
uniform vec4 uUvClampRect;  // uMin, vMin, uMax, vMax (inset >= 0.5 src texel)

vec4 tapClamped(vec2 offsetTexels) {
    vec2 uv = clamp(vUv + offsetTexels * uSrcTexelSize, uUvClampRect.xy, uUvClampRect.zw);
    return texture(uSrcTex, uv);
}

void main() {
    // 4 inner diagonal taps (weight 0.125 each -> 0.500 total)
    vec4 innerRing =
        tapClamped(vec2(-1.0, -1.0)) +
        tapClamped(vec2( 1.0, -1.0)) +
        tapClamped(vec2(-1.0,  1.0)) +
        tapClamped(vec2( 1.0,  1.0));

    // 1 center tap (weight 0.125 total)
    vec4 centerTap = tapClamped(vec2(0.0, 0.0));

    // 4 cardinal cross taps (weight 0.0625 each -> 0.250 total)
    vec4 cardinalRing =
        tapClamped(vec2(-2.0,  0.0)) +
        tapClamped(vec2( 2.0,  0.0)) +
        tapClamped(vec2( 0.0, -2.0)) +
        tapClamped(vec2( 0.0,  2.0));

    // 4 outer corners (weight 0.03125 each -> 0.125 total)
    vec4 outerCorners =
        tapClamped(vec2(-2.0, -2.0)) +
        tapClamped(vec2( 2.0, -2.0)) +
        tapClamped(vec2(-2.0,  2.0)) +
        tapClamped(vec2( 2.0,  2.0));

    fragColor = innerRing * 0.125
              + centerTap * 0.125
              + cardinalRing * 0.0625
              + outerCorners * 0.03125;
}
`;

// -----------------------------------------------------------------------------
// [3] Per-Panel Bounding Quad Vertex Shader (Top-Down CSS Bounds -> GL NDC)
// -----------------------------------------------------------------------------
export const VERTEX_PANEL_QUAD = `#version 300 es
precision highp float;

layout(location = 0) in vec2 aUnitPos; // [-1, +1] unit quad

uniform vec2 uViewportCss;
uniform vec4 uPanelBoundsCss; // centerX, centerY, halfWidth + margin, halfHeight + margin

void main() {
    vec2 posCss = uPanelBoundsCss.xy + aUnitPos * uPanelBoundsCss.zw;
    vec2 ndc = vec2(
        (posCss.x / max(uViewportCss.x, 1.0)) * 2.0 - 1.0,
        1.0 - (posCss.y / max(uViewportCss.y, 1.0)) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
}
`;

// -----------------------------------------------------------------------------
// [4] Web Lite Liquid Glass Fragment Shader (Clean-Room G2 Squircle + 3-Mip Optics)
// -----------------------------------------------------------------------------
export const FRAGMENT_LIQUID_GLASS = `#version 300 es
precision highp float;

out vec4 fragColor;

// Scene pyramid: 0 = full-res sceneTex, 1 = 1/2, 2 = 1/4, 3 = 1/8
uniform sampler2D uMip0;
uniform sampler2D uMip1;
uniform sampler2D uMip2;
uniform sampler2D uMip3;

uniform vec2  uViewportCss;
uniform float uDpr;
uniform vec4  uUvClampRect;       // 0.5px-safe clamp bounds for backdrop textures

// Primary panel geometry in top-down CSS pixels
uniform vec4  uRectCss;           // centerX, centerY, halfWidth, halfHeight
uniform vec4  uCornerRadii;       // rTR, rTL, rBR, rBL (top-down CSS convention)
uniform float uSmoothness;        // squircle superellipse factor [0..1]
uniform float uEllipseMorph;      // [0..1] blend toward pure ellipse (droplet/pill)

// Optional coalesced secondary glass lobe (for smoothMin liquid merging / tab droplet)
uniform float uHasSecondary;      // 0.0 or 1.0
uniform vec4  uSecondaryRectCss;  // centerX, centerY, halfWidth, halfHeight
uniform vec2  uSecondaryParams;   // cornerRadius, smoothUnionK (CSS px)

// Core optical parameters (mapped cleanly from native glass_uniforms.hpp)
uniform float uRefractionPx;      // peak radial displacement near bevel rim (0..48)
uniform float uBevelWidthPx;      // rounded bevel ramp width inside edge (6..48)
uniform float uDispersionPx;      // lateral chromatic spread magnitude (0..12)
uniform float uEdgeLod;           // mip LOD right at the clear refractive rim (0..2.0)
uniform float uFrostLod;          // mip LOD inside the frosted core (0..3.0)
uniform float uSpecularStrength;  // directional top-left specular arc gain (0..1.5)
uniform float uRimGlowStrength;   // isotropic Fresnel cosine rim glow (0..1.2)
uniform float uRefractionMode;    // +1.0 inward bulge vs -1.0 outward lens
uniform float uQualityTaps;       // 0.0 = 3-tap Web Lite dispersion, 1.0 = 7-tap Studio
uniform float uShadowStrength;    // elevation contact + ambient shadow intensity

// Per-panel material tinting, opacity & role overrides
uniform vec4  uPanelTint;         // rgb linear-ish tint color, a = fill tint opacity
uniform vec4  uPanelOptics;       // x = refractScale, y = minFrostLod, z = shadowScale, w = rimContour
uniform float uPanelOpacity;      // overall glass surface visibility [0..1]
uniform float uLensBoost;         // extra interior magnification factor (for dragged droplet)
uniform float uTime;              // subtle shimmer phase when animating (default 0)

// -----------------------------------------------------------------------------
// [4.1] Fast Color Space Conversions, PRNG Dither & Luminance Helpers
// -----------------------------------------------------------------------------
vec3 srgbToLinear(vec3 c) {
    vec3 safe = max(c, vec3(0.0));
    return safe * (safe * (safe * 0.305306011 + 0.682171111) + 0.012522878);
}

vec3 linearToSrgb(vec3 c) {
    vec3 safe = clamp(c, 0.0, 1.0);
    vec3 s1 = sqrt(safe);
    vec3 s2 = sqrt(s1);
    vec3 s3 = sqrt(s2);
    return 0.585122381 * s1 + 0.783140355 * s2 - 0.368262736 * s3;
}

float rec709Luminance(vec3 linearRgb) {
    return dot(linearRgb, vec3(0.2126, 0.7152, 0.0722));
}

float lumaAdaptiveResponse(float y) {
    float yc = clamp(y, 0.0, 1.0);
    float s = yc * yc * (3.0 - 2.0 * yc);
    return mix(1.30, 0.62, s);
}

// Triangular-PDF Interleaved Gradient Noise in [-1, +1] to mask quantization banding
float triangularDither(vec2 fragCoord) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    float u = fract(magic.z * fract(dot(fragCoord, magic.xy)));
    float centered = u * 2.0 - 1.0;
    float s = (centered >= 0.0) ? 1.0 : -1.0;
    return s * (1.0 - sqrt(max(0.0, 1.0 - abs(centered))));
}

// -----------------------------------------------------------------------------
// [4.2] G2-Squircle & Smooth Minimum Liquid SDF Primitives (Top-Down CSS px)
// -----------------------------------------------------------------------------
float smoothMinPoly(float d1, float d2, float k) {
    float safeK = max(k, 1e-4);
    float h = max(safeK - abs(d1 - d2), 0.0) / safeK;
    return min(d1, d2) - 0.25 * h * h * safeK;
}

float sdSquircleEllipse(vec2 p, vec2 halfExt, vec4 radii, float smoothness, float ellipseMorph) {
    vec2 safeH = max(halfExt, vec2(1e-3));
    float minExt = min(safeH.x, safeH.y);

    // Top-down CSS space: y < 0 is TOP, y >= 0 is BOTTOM
    vec2 rPair = (p.x >= 0.0) ? vec2(radii.x, radii.z) : vec2(radii.y, radii.w);
    float rRaw = (p.y < 0.0) ? rPair.x : rPair.y;
    float r = clamp(rRaw, 0.0, minExt);

    float s = clamp(smoothness, 0.0, 1.0);
    float rEff = min(r * (1.0 + 0.42 * s), minExt);
    float pExp = 2.0 + 2.2 * s;

    vec2 q = abs(p) - (safeH - vec2(rEff));
    vec2 qp = max(q, vec2(0.0));
    float m = max(qp.x, qp.y);

    float dCorner = 0.0;
    if (m > 1e-5) {
        vec2 u = qp / m;
        float ux = (u.x > 1e-5) ? pow(u.x, pExp) : 0.0;
        float uy = (u.y > 1e-5) ? pow(u.y, pExp) : 0.0;
        dCorner = m * pow(ux + uy, 1.0 / pExp);
    }
    float dInside = min(max(q.x, q.y), 0.0);
    float dSquircle = dCorner + dInside - rEff;

    float e = clamp(ellipseMorph, 0.0, 1.0);
    if (e <= 1e-4) {
        return dSquircle;
    }

    vec2 pe = p / safeH;
    float k0 = length(pe);
    float dEllipse = (k0 > 1e-5)
        ? k0 * (k0 - 1.0) / max(length(pe / safeH), 1e-5)
        : -minExt;
    return mix(dSquircle, dEllipse, e);
}

float sdPanelComposite(vec2 fragCss) {
    vec2 p1 = fragCss - uRectCss.xy;
    float d1 = sdSquircleEllipse(p1, uRectCss.zw, uCornerRadii, uSmoothness, uEllipseMorph);
    if (uHasSecondary < 0.5) {
        return d1;
    }
    vec2 p2 = fragCss - uSecondaryRectCss.xy;
    float d2 = sdSquircleEllipse(p2, uSecondaryRectCss.zw, vec4(uSecondaryParams.x), uSmoothness, 0.0);
    return smoothMinPoly(d1, d2, uSecondaryParams.y);
}

vec2 panelSdfGradientCss(vec2 fragCss) {
    const float eps = 0.65;
    float dx = sdPanelComposite(fragCss + vec2(eps, 0.0)) - sdPanelComposite(fragCss - vec2(eps, 0.0));
    float dy = sdPanelComposite(fragCss + vec2(0.0, eps)) - sdPanelComposite(fragCss - vec2(0.0, eps));
    vec2 g = vec2(dx, dy);
    float len = length(g);
    return (len > 1e-5) ? (g / len) : vec2(0.0, -1.0);
}

// -----------------------------------------------------------------------------
// [4.3] Clamped 3-Level Mip Pyramid Sampler & Spectral Chromatic Dispersion
// -----------------------------------------------------------------------------
vec3 samplePyramidLinear(vec2 uv, float lod) {
    vec2 clampedUv = clamp(uv, uUvClampRect.xy, uUvClampRect.zw);
    float safeLod = clamp(lod, 0.0, 3.0);

    vec3 texA;
    vec3 texB;
    float frac;

    if (safeLod <= 1.0) {
        texA = texture(uMip0, clampedUv).rgb;
        texB = texture(uMip1, clampedUv).rgb;
        frac = safeLod;
    } else if (safeLod <= 2.0) {
        texA = texture(uMip1, clampedUv).rgb;
        texB = texture(uMip2, clampedUv).rgb;
        frac = safeLod - 1.0;
    } else {
        texA = texture(uMip2, clampedUv).rgb;
        texB = texture(uMip3, clampedUv).rgb;
        frac = safeLod - 2.0;
    }
    return mix(srgbToLinear(texA), srgbToLinear(texB), frac);
}

vec3 spectralTapWeight7(int i) {
    if (i == 0) return vec3(0.00, 0.00, 0.20);
    if (i == 1) return vec3(0.00, 0.10, 0.30);
    if (i == 2) return vec3(0.05, 0.25, 0.30);
    if (i == 3) return vec3(0.15, 0.30, 0.15);
    if (i == 4) return vec3(0.30, 0.25, 0.05);
    if (i == 5) return vec3(0.30, 0.10, 0.00);
    return             vec3(0.20, 0.00, 0.00);
}

vec3 sampleDispersedLinear(vec2 uvBase, vec2 dispUvDelta, float lod) {
    if (dot(dispUvDelta, dispUvDelta) < 1e-11) {
        return samplePyramidLinear(uvBase, lod);
    }

    if (uQualityTaps < 0.5) {
        // Web Lite fast 3-tap balanced RGB spectral prism
        float r = samplePyramidLinear(uvBase + dispUvDelta * 0.85, lod).r;
        float g = samplePyramidLinear(uvBase,                      lod).g;
        float b = samplePyramidLinear(uvBase - dispUvDelta * 0.85, lod).b;
        return vec3(r, g, b);
    }

    // Studio 7-tap unitary-column spectral curve (zero grayscale fringing)
    vec3 accum = vec3(0.0);
    for (int i = 0; i < 7; ++i) {
        float shift = (float(i) - 3.0) / 3.0;
        vec3 s = samplePyramidLinear(uvBase + shift * dispUvDelta, lod);
        accum += s * spectralTapWeight7(i);
    }
    return accum;
}

// -----------------------------------------------------------------------------
// [4.4] Convex Quarter-Circle Bevel, Specular Arcs & Glass Compositing
// -----------------------------------------------------------------------------
float convexBevelProfile(float d, float bevelWidthPx) {
    float safeW = max(bevelWidthPx, 1.0);
    float u = clamp((-d) / safeW, 0.0, 1.0);
    return 1.0 - sqrt(clamp(u * (2.0 - u), 0.0, 1.0));
}

vec2 cssToBackdropUv(vec2 posCss) {
    return vec2(posCss.x / max(uViewportCss.x, 1.0), 1.0 - posCss.y / max(uViewportCss.y, 1.0));
}

void main() {
    // Convert GL bottom-left gl_FragCoord into exact top-down CSS pixel coordinates.
    vec2 fragCss = vec2(gl_FragCoord.x / uDpr, uViewportCss.y - gl_FragCoord.y / uDpr);

    float d = sdPanelComposite(fragCss);

    // 4-tier early out outside the soft shadow apron
    if (d > 22.0 || uPanelOpacity <= 0.002) {
        discard;
    }

    // Anti-aliased silhouette coverage in CSS pixels (~1px smooth AA edge)
    float aaWidth = max(0.75 / max(uDpr, 1.0), 0.55);
    float insideMask = smoothstep(aaWidth, -aaWidth, d);

    // Continuous feather-soft outer elevation shadow curve from d = 0..18 CSS px (zero AA seam)
    float shadowSpread = smoothstep(18.0, 0.0, max(d, 0.0));
    float shadowAlpha = pow(shadowSpread, 2.35) * uShadowStrength * uPanelOptics.z * 0.115 * uPanelOpacity;

    // Early out for pure shadow fragments (zero scene texture fetches needed)
    if (d >= aaWidth) {
        if (shadowAlpha <= 0.002) {
            discard;
        }
        fragColor = vec4(0.0, 0.0, 0.0, shadowAlpha);
        return;
    }

    // Compute analytical/central-diff SDF normal in top-down CSS space (pointing outward)
    vec2 nCss = panelSdfGradientCss(fragCss);

    // Quarter-circle bevel height slope: 1.0 at rim, 0.0 across flat interior
    float bevelSlope = convexBevelProfile(d, uBevelWidthPx);
    float depthInside = max(-d, 0.0);

    // Multi-band mip roughness interpolation: clearer crisp edge -> frosted body
    float targetFrost = max(uFrostLod, uPanelOptics.y);
    float targetEdge = (uPanelOptics.y > 2.2) ? max(uEdgeLod, 0.45) : uEdgeLod;
    float rimToBody = smoothstep(0.0, max(uBevelWidthPx * 1.15, 2.0), depthInside);
    float activeLod = mix(targetEdge, targetFrost, rimToBody);

    // Refraction offset (inward pulls edge from panel center; outward expands slightly)
    float effRefract = uRefractionPx * uPanelOptics.x;
    vec2 refractOffsetCss = -nCss * (bevelSlope * effRefract * uRefractionMode);

    // Optional lens magnification boost (active while dragging inflated tab droplet!)
    if (uLensBoost > 0.001) {
        vec2 rel = fragCss - uRectCss.xy;
        vec2 lensScale = rel * (-0.18 * uLensBoost * (1.0 - bevelSlope * 0.5));
        refractOffsetCss += lensScale;
        activeLod *= (1.0 - 0.65 * uLensBoost);
    }

    // Subtle orthogonal/tangential micro-dither hides mip transition banding
    float dither = triangularDither(gl_FragCoord.xy);
    float ditherLod = clamp(activeLod + dither * 0.05, 0.0, 3.0);

    vec2 refractedCss = fragCss + refractOffsetCss;
    vec2 refractedUv = cssToBackdropUv(refractedCss);

    // Lateral dispersion vector parallel to refractive bevel displacement
    vec2 dispVecCss = -nCss * (bevelSlope * bevelSlope * uDispersionPx * uRefractionMode);
    vec2 dispUvDelta = vec2(
        dispVecCss.x / max(uViewportCss.x, 1.0),
        -dispVecCss.y / max(uViewportCss.y, 1.0)
    );

    vec3 refractedLin = sampleDispersedLinear(refractedUv, dispUvDelta, ditherLod);

    // Subtle interior glass scatter: blend a sliver of frosted mip for soft milky volume
    vec3 coreMistLin = samplePyramidLinear(cssToBackdropUv(fragCss), clamp(targetFrost + 0.55, 0.0, 3.0));
    float mistMix = clamp(uPanelTint.a * 0.35 + (1.0 - bevelSlope) * 0.16 * (1.0 - uLensBoost), 0.0, 0.72);
    vec3 glassLin = mix(refractedLin, coreMistLin, mistMix);

    // Rec.709 adaptive luminance response (pops bright glints on dark walls without clipping white cards)
    float luma = rec709Luminance(glassLin);
    float adaptive = lumaAdaptiveResponse(luma);

    // Tint overlay in linear space (supports light milky frosted glass & dark translucent glass)
    vec3 tintLin = srgbToLinear(uPanelTint.rgb);
    float tintAlpha = clamp(uPanelTint.a * (1.0 - 0.45 * uLensBoost), 0.0, 0.92);
    glassLin = mix(glassLin, tintLin, tintAlpha);

    // Specular highlight & opposing secondary counter-glint in top-down CSS space
    // Top-left key light direction in CSS Y-down coords points from (-1, -1), i.e., surface facing top-left has nCss ~ (-0.707, -0.707)
    vec2 keyLightDir = normalize(vec2(-0.72, -0.69));
    vec2 fillLightDir = -keyLightDir;

    float rimZone = smoothstep(3.2, 0.0, abs(d)) * 0.85 + bevelSlope * 0.55;
    float keyAlign = max(0.0, dot(nCss, keyLightDir));
    float fillAlign = max(0.0, dot(nCss, fillLightDir));

    float keySpec = pow(keyAlign, 3.0) * smoothstep(0.0, 1.0, rimZone) * uSpecularStrength * 0.88;
    float fillSpec = pow(fillAlign, 4.0) * smoothstep(2.4, 0.0, abs(d)) * uSpecularStrength * 0.38;

    // Cosine Fresnel rim glow tracing the crisp outer 1.2px glass wall
    float rimWall = exp(-abs(d + 0.55) * 1.15);
    float rimGlow = rimWall * (0.20 + 0.44 * uRimGlowStrength) * (0.90 + 0.10 * sin(uTime * 1.4));

    vec3 highlightColor = vec3(1.0);
    glassLin += highlightColor * ((keySpec + fillSpec + rimGlow) * adaptive);

    // Crisp hairline contour separator inside light edges so bright white-on-white pills stay defined
    float hairline = exp(-abs(d + 0.35) * 2.1);
    glassLin = mix(glassLin, glassLin * 0.84, hairline * uPanelOptics.w);

    // Subtle interior caustic ring right inside the bevel inflection on droplets/lenses
    float causticBand = exp(-pow((depthInside - uBevelWidthPx * 0.38) / max(uBevelWidthPx * 0.35, 1.5), 2.0));
    glassLin += vec3(0.95, 0.98, 1.0) * (causticBand * bevelSlope * 0.08 * adaptive);

    // Hue-preserving soft HDR clamp & encode back to sRGB for premultiplied alpha output
    float peak = max(glassLin.r, max(glassLin.g, glassLin.b));
    if (peak > 1.0) {
        float y = rec709Luminance(glassLin);
        if (y >= 1.0) {
            glassLin = vec3(1.0);
        } else {
            float t = clamp((1.0 - y) / max(peak - y, 1e-5), 0.0, 1.0);
            glassLin = mix(vec3(y), glassLin, t);
        }
    }

    vec3 glassSrgb = linearToSrgb(glassLin);

    // Seamlessly blend outer elevation shadow -> opaque premultiplied glass across the AA transition.
    vec4 shadowPremul = vec4(0.0, 0.0, 0.0, shadowAlpha);
    vec4 glassPremul = vec4(glassSrgb * uPanelOpacity, uPanelOpacity);
    vec4 combined = mix(shadowPremul, glassPremul, insideMask);
    if (combined.a <= 0.002) {
        discard;
    }

    fragColor = combined;
}
`;
