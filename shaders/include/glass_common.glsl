#ifndef GLASS_COMMON_GLSL
#define GLASS_COMMON_GLSL

const float GLASS_PI = 3.14159265359;

// -----------------------------------------------------------------------------
// [1] 2D Coordinate, Matrix & Domain Clamping Helpers
// -----------------------------------------------------------------------------

// Rotates a 2D vector counter-clockwise by `angleRad`.
vec2 rotate2D(vec2 v, float angleRad) {
    float c = cos(angleRad);
    float s = sin(angleRad);
    return vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}

// Reconstructs a 2x2 column-major matrix from packed vec4 (x, y, z, w).
mat2 unpackMat2(vec4 m) {
    return mat2(m.x, m.y, m.z, m.w);
}

// Clamps normalized UVs strictly inside the safe sub-rect [uMin, vMin, uMax, vMax]
// inset by >= 0.5 texels to stop bilinear taps from bleeding across borders.
vec2 clampUV(vec2 uv, vec4 rect) {
    return clamp(uv, rect.xy, rect.zw);
}

// Samples either the red channel or alpha channel of a clamped mask texture at LOD 0.
float sampleMaskChannel(sampler2D maskTex, vec2 uv, vec4 uvClampRect, float useRedChannel) {
    vec4 s = textureLod(maskTex, clampUV(uv, uvClampRect), 0.0);
    return (useRedChannel > 0.5) ? s.r : s.a;
}

// Polynomial smooth minimum (C1 continuous union across transition radius `k`).
float smoothMinPoly(float d1, float d2, float k) {
    float safeK = max(k, 1e-5);
    float h = max(safeK - abs(d1 - d2), 0.0) / safeK;
    return min(d1, d2) - 0.25 * h * h * safeK;
}

// -----------------------------------------------------------------------------
// [2] Luminance, Un-premultiplication & Linear Color Grading
// -----------------------------------------------------------------------------

// Rec.709 relative luminance in linear-light RGB space.
float rec709Luminance(vec3 linearRgb) {
    return dot(linearRgb, vec3(0.2126, 0.7152, 0.0722));
}

// Perceptual adaptive specular gain: boosts crispness over dark/mid backdrops
// while pulling back smoothly over bright backgrounds to prevent clipping.
float lumaAdaptiveResponse(float y) {
    float yc = clamp(y, 0.0, 1.0);
    float smoothCurve = yc * yc * (3.0 - 2.0 * yc);
    return mix(1.25, 0.65, smoothCurve);
}

// Converts premultiplied RGBA back to straight RGB safely, flushing sub-epsilon
// noise to zero so sparse borders do not amplify garbage during color grading.
vec3 unpremultiplySafe(vec4 premul, float eps) {
    if (premul.a <= eps) {
        return vec3(0.0);
    }
    vec3 c = premul.rgb / premul.a;
    vec3 zeroMask = step(vec3(1e-5), abs(c));
    return c * zeroMask;
}

// Applies a 3x4 linear-light affine color matrix packed as three vec4 rows
// (xyz = channel weights, w = constant additive bias).
vec3 applyAffineGrade(vec3 rgb, vec4 row0, vec4 row1, vec4 row2) {
    return vec3(
        dot(row0.xyz, rgb) + row0.w,
        dot(row1.xyz, rgb) + row1.w,
        dot(row2.xyz, rgb) + row2.w
    );
}

// Clamps HDR/SDR highlights while preserving perceived hue and luminance:
// when any channel exceeds `clampLimit`, chrominance is pulled along the
// constant-luminance axis toward neutral `vec3(Y)` instead of hard-clipping R/G/B.
vec3 huePreservingClamp(vec3 c, float clampLimit, float floorLimit, float preserveHue) {
    vec3 res = c;
    float peak = max(res.r, max(res.g, res.b));
    if (preserveHue > 0.5 && peak > clampLimit) {
        float y = rec709Luminance(res);
        if (y >= clampLimit) {
            res = vec3(clampLimit);
        } else {
            float denom = max(peak - y, 1e-6);
            float t = clamp((clampLimit - y) / denom, 0.0, 1.0);
            res = vec3(y) + (res - vec3(y)) * t;
        }
    } else {
        res = min(res, vec3(clampLimit));
    }
    return max(res, vec3(floorLimit));
}

// -----------------------------------------------------------------------------
// [3] 7-Tap Spectral Dispersion Curve & Triangular Anti-Banding Dither
// -----------------------------------------------------------------------------

// Overlapping RGB spectral sensitivity weights across 7 dispersion taps (0..6).
// Column sums across i = 0..6 strictly equal (1.0, 1.0, 1.0) so uniform gray/white
// surfaces remain completely achromatic under strong chromatic dispersion.
vec3 spectralTapWeight(int i) {
    if (i == 0) return vec3(0.00, 0.00, 0.20);
    if (i == 1) return vec3(0.00, 0.10, 0.30);
    if (i == 2) return vec3(0.05, 0.25, 0.30);
    if (i == 3) return vec3(0.15, 0.30, 0.15);
    if (i == 4) return vec3(0.30, 0.25, 0.05);
    if (i == 5) return vec3(0.30, 0.10, 0.00);
    return             vec3(0.20, 0.00, 0.00);
}

// Symmetric normalized tap coordinate in [-1.0, +1.0] for index i in 0..6.
float spectralTapShift(int i) {
    return (float(i) - 3.0) / 3.0;
}

// Screen-space Interleaved Gradient Noise (IGN) in [0, 1).
float interleavedGradientNoise(vec2 p) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(p, magic.xy)));
}

// Transforms uniform IGN into a [-1, +1] triangular-PDF dither offset to hide
// low-slope blur banding without visible high-frequency grain.
float triangularDither(vec2 fragCoord) {
    float u = interleavedGradientNoise(fragCoord);
    float centered = u * 2.0 - 1.0;
    float s = (centered >= 0.0) ? 1.0 : -1.0;
    return s * (1.0 - sqrt(max(0.0, 1.0 - abs(centered))));
}

#endif
