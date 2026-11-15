// Clean-Room Liquid Glass Hyprland Plugin — Std140 Uniform Buffer & Math Helpers
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace LiquidGlassPlugin {

// -----------------------------------------------------------------------------
// [1] Std140 Uniform Blocks Matching Canonical Shaders Byte-for-Byte
// -----------------------------------------------------------------------------

struct alignas(16) CopyPrepareUniforms {
    float qt_Matrix[16];
    float qt_Opacity;
    float forceOpaque;
    float decodeSrgb;
    float pad0;
    float uvClampRect[4];
};
static_assert(sizeof(CopyPrepareUniforms) == 96, "CopyPrepareUniforms std140 size mismatch");
static_assert(offsetof(CopyPrepareUniforms, uvClampRect) == 80, "CopyPrepareUniforms offset mismatch");

struct alignas(16) DownsampleUniforms {
    float qt_Matrix[16];
    float qt_Opacity;
    float srcLod;
    float pad0;
    float pad1;
    float srcTexelSize[4];
    float uvClampRect[4];
};
static_assert(sizeof(DownsampleUniforms) == 112, "DownsampleUniforms std140 size mismatch");
static_assert(offsetof(DownsampleUniforms, uvClampRect) == 96, "DownsampleUniforms offset mismatch");

struct alignas(16) GlassPanelUniforms {
    float qt_Matrix[16];
    float qt_Opacity;
    float geometryMode;
    float compositeBackdrop;
    float noBleedMode;

    float viewportAndSdf[4];
    float panelCenterAndHalf[4];
    float cornerRadii[4];
    float shapeParams[4];
    float secondaryCenterHalf[4];
    float secondaryRadii[4];

    float uvClampRect[4];
    float normalTransform[4];
    float dispTransform[4];

    float refractionParams[4];
    float dispersionParams[4];
    float bleedParams[4];
    float blurBandOffsets[4];
    float blurBandInvRanges[4];
    float blurBandLods[4];

    float shadowParams[4];

    float faceMatrixRow0[4];
    float faceMatrixRow1[4];
    float faceMatrixRow2[4];

    float bleedMatrixRow0[4];
    float bleedMatrixRow1[4];
    float bleedMatrixRow2[4];

    float shadowMatrixRow0[4];
    float shadowMatrixRow1[4];
    float shadowMatrixRow2[4];

    float bevelSpecParams[4];
    float bevelSpecShape[4];
    float specColorAndLuma[4];
    float rimGlowParams[4];
    float toneClampParams[4];
    float sdfPackParams[4];
};
static_assert(sizeof(GlassPanelUniforms) == 576, "GlassPanelUniforms std140 size mismatch");
static_assert(offsetof(GlassPanelUniforms, viewportAndSdf) == 80, "GlassPanelUniforms offset mismatch");
static_assert(offsetof(GlassPanelUniforms, uvClampRect) == 176, "GlassPanelUniforms offset mismatch");
static_assert(offsetof(GlassPanelUniforms, sdfPackParams) == 560, "GlassPanelUniforms offset mismatch");

// -----------------------------------------------------------------------------
// [2] Per-Draw Render Parameters & Color-Space Conversion Helpers
// -----------------------------------------------------------------------------

struct GlassRenderParams {
    float refractionPx       = 22.0f;
    float bevelWidthPx       = 24.0f;
    float bevelOffsetPx      = 0.0f;
    float dispersionPx       = 3.5f;
    float dispersionCurve    = 1.35f;
    float frostLod           = 2.3f;
    float edgeLod            = 0.35f;
    float specularStrength   = 0.55f;
    float specWidthPx        = 2.5f;
    float rimGlowStrength    = 0.18f;
    float lumaAdaptStrength  = 1.0f;
    float squircleSmoothness = -1.0f; // < 0: auto-derive from Hyprland roundingPower
    float vibrancy           = 0.22f;
    float brightness         = 1.02f;
    float contrast           = 1.08f;
    float tintAlpha          = 0.14f;
    float tintSrgb[3]        = {0.92f, 0.95f, 1.0f};
};

inline void setIdentityMat4(float m[16]) {
    for (int i = 0; i < 16; ++i)
        m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

inline float srgbChannelToLinear(float c) {
    const float s = std::clamp(c, 0.0f, 1.0f);
    return (s <= 0.04045f) ? (s / 12.92f) : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

// -----------------------------------------------------------------------------
// [3] Uniform Packing From Hyprland Panel Box + User Configuration
// -----------------------------------------------------------------------------

inline void fillAffineGradeRows(
    float        row0[4],
    float        row1[4],
    float        row2[4],
    float        vibrancy,
    float        contrast,
    float        brightness,
    float        tintAlpha,
    const float  tintLinear[3]
) {
    static constexpr float kLuma[3] = {0.2126f, 0.7152f, 0.0722f};
    const float            sat      = std::max(0.0f, 1.0f + vibrancy);
    const float            ta       = std::clamp(tintAlpha, 0.0f, 1.0f);
    const float            gain     = contrast * brightness * (1.0f - ta);
    // Pivot 0.214 linear approximates perceptual mid-gray (sRGB ~0.5) cleanly.
    const float            pivotBias = 0.214f * (1.0f - contrast) * brightness * (1.0f - ta);

    auto writeRow = [&](float* out, int i) {
        for (int j = 0; j < 3; ++j) {
            const float satWeight = (1.0f - sat) * kLuma[j] + ((i == j) ? sat : 0.0f);
            out[j]                = satWeight * gain;
        }
        out[3] = pivotBias + tintLinear[i] * ta;
    };

    writeRow(row0, 0);
    writeRow(row1, 1);
    writeRow(row2, 2);
}

inline void fillPanelUniforms(
    GlassPanelUniforms&      u,
    int                      fbW,
    int                      fbH,
    float                    boxX,
    float                    boxY,
    float                    boxW,
    float                    boxH,
    float                    radiusPx,
    float                    roundingPower,
    float                    strength,
    int                      pyrX0,
    int                      pyrY0,
    int                      pyrX1,
    int                      pyrY1,
    int                      maxPyrLevel,
    bool                     isHdrLinear,
    const GlassRenderParams& params
) {
    u = {};
    setIdentityMat4(u.qt_Matrix);

    const float W         = static_cast<float>(std::max(fbW, 1));
    const float H         = static_cast<float>(std::max(fbH, 1));
    const float sScale    = std::clamp(strength, 0.0f, 2.0f);
    const float halfW     = std::max(boxW * 0.5f, 1.0f);
    const float halfH     = std::max(boxH * 0.5f, 1.0f);
    const float minHalf   = std::min(halfW, halfH);
    const float clampedR  = std::clamp(radiusPx, 0.0f, minHalf);

    // Match Hyprland's window corner rounding curve cleanly by default so bezel
    // specular never gets sliced at circular corners, or accept explicit G2 override.
    float smoothness = params.squircleSmoothness;
    if (smoothness < 0.0f) {
        smoothness = std::clamp((roundingPower - 2.0f) / 2.5f, 0.0f, 1.0f);
    } else {
        smoothness = std::clamp(smoothness, 0.0f, 1.0f);
    }

    u.qt_Opacity        = 1.0f;
    u.geometryMode      = 0.0f; // Analytic G2-squircle SDF
    u.compositeBackdrop = 1.0f; // Clean backdrop passthrough outside boundary AA
    u.noBleedMode       = 1.0f; // Hyprland renders its own drop shadows

    u.viewportAndSdf[0] = W;
    u.viewportAndSdf[1] = H;
    u.viewportAndSdf[2] = 1.0f;
    u.viewportAndSdf[3] = 0.0f;

    u.panelCenterAndHalf[0] = boxX + boxW * 0.5f;
    u.panelCenterAndHalf[1] = boxY + boxH * 0.5f;
    u.panelCenterAndHalf[2] = halfW;
    u.panelCenterAndHalf[3] = halfH;

    u.cornerRadii[0] = clampedR;
    u.cornerRadii[1] = clampedR;
    u.cornerRadii[2] = clampedR;
    u.cornerRadii[3] = clampedR;

    u.shapeParams[0] = smoothness;
    u.shapeParams[1] = 0.0f;
    u.shapeParams[2] = 0.0f;
    u.shapeParams[3] = 0.0f;

    // Inset UV clamp rectangle by 0.5 texels at the deepest mip level so displaced
    // or dispersed taps never bleed stale pixels from outside `pyrX0..pyrX1`.
    const float mipFootprint = std::ldexp(0.5f, std::max(maxPyrLevel, 0));
    const float padU         = mipFootprint / W;
    const float padV         = mipFootprint / H;
    float       uMin         = static_cast<float>(pyrX0) / W + padU;
    float       vMin         = static_cast<float>(pyrY0) / H + padV;
    float       uMax         = static_cast<float>(pyrX1) / W - padU;
    float       vMax         = static_cast<float>(pyrY1) / H - padV;
    if (uMin > uMax)
        uMin = uMax = 0.5f * (uMin + uMax);
    if (vMin > vMax)
        vMin = vMax = 0.5f * (vMin + vMax);

    u.uvClampRect[0] = std::clamp(uMin, 0.0f, 1.0f);
    u.uvClampRect[1] = std::clamp(vMin, 0.0f, 1.0f);
    u.uvClampRect[2] = std::clamp(uMax, 0.0f, 1.0f);
    u.uvClampRect[3] = std::clamp(vMax, 0.0f, 1.0f);

    u.normalTransform[0] = 1.0f;
    u.normalTransform[3] = 1.0f;

    u.dispTransform[0] = 1.0f / W;
    u.dispTransform[3] = 1.0f / H;

    const float bevelW = std::clamp(params.bevelWidthPx, 2.0f, std::max(minHalf, 2.0f));
    u.refractionParams[0] = std::max(0.0f, params.refractionPx * sScale);
    u.refractionParams[1] = params.bevelOffsetPx;
    u.refractionParams[2] = 1.0f / std::max(bevelW, 1.0f);
    u.refractionParams[3] = 0.0f;

    u.dispersionParams[0] = std::max(0.0f, params.dispersionPx * sScale);
    u.dispersionParams[1] = std::max(0.25f, params.dispersionCurve);
    u.dispersionParams[2] = 0.0f;
    u.dispersionParams[3] = 0.0f;

    const float maxLod   = static_cast<float>(std::max(maxPyrLevel, 0));
    const float edgeLod  = std::clamp(params.edgeLod * sScale, 0.0f, maxLod);
    const float frostLod = std::clamp(params.frostLod * sScale, edgeLod, maxLod);

    u.bleedParams[0] = 0.0f;
    u.bleedParams[1] = 0.25f; // subtle chromatic/bevel clarity grade near rim
    u.bleedParams[2] = edgeLod;
    u.bleedParams[3] = 1.0f;  // enable 3-band frosted depth transition

    u.blurBandOffsets[0] = 1.5f;
    u.blurBandOffsets[1] = bevelW * 0.30f;
    u.blurBandOffsets[2] = bevelW * 0.72f;

    u.blurBandInvRanges[0] = 1.0f / std::max(bevelW * 0.35f, 3.0f);
    u.blurBandInvRanges[1] = 1.0f / std::max(bevelW * 0.45f, 4.0f);
    u.blurBandInvRanges[2] = 1.0f / std::max(bevelW * 0.55f, 6.0f);

    u.blurBandLods[0] = edgeLod + 0.32f * (frostLod - edgeLod);
    u.blurBandLods[1] = edgeLod + 0.72f * (frostLod - edgeLod);
    u.blurBandLods[2] = frostLod;

    const float tintLin[3] = {
        srgbChannelToLinear(params.tintSrgb[0]),
        srgbChannelToLinear(params.tintSrgb[1]),
        srgbChannelToLinear(params.tintSrgb[2]),
    };

    fillAffineGradeRows(
        u.faceMatrixRow0, u.faceMatrixRow1, u.faceMatrixRow2,
        params.vibrancy, params.contrast, params.brightness,
        params.tintAlpha, tintLin
    );

    // Bevel rim stays slightly crisper with extra contrast and vibrance.
    fillAffineGradeRows(
        u.bleedMatrixRow0, u.bleedMatrixRow1, u.bleedMatrixRow2,
        params.vibrancy * 1.18f, params.contrast * 1.04f, params.brightness * 1.02f,
        params.tintAlpha * 0.65f, tintLin
    );

    u.shadowMatrixRow0[0] = 1.0f;
    u.shadowMatrixRow1[1] = 1.0f;
    u.shadowMatrixRow2[2] = 1.0f;

    // Key directional top-left light vector in Hyprland's top-down FBO coordinates.
    u.bevelSpecParams[0] = -0.62f;
    u.bevelSpecParams[1] = -0.78f;
    u.bevelSpecParams[2] = std::max(0.5f, params.specWidthPx);
    u.bevelSpecParams[3] = std::max(0.0f, params.bevelOffsetPx);

    u.bevelSpecShape[0] = -0.15f; // broad grazing arc
    u.bevelSpecShape[1] = 1.8f;   // smooth shoulder falloff
    u.bevelSpecShape[2] = 0.85f;  // crisp edge decay into interior
    u.bevelSpecShape[3] = 0.32f;  // subtle secondary opposing bounce lobe

    const float specGain  = std::max(0.0f, params.specularStrength);
    u.specColorAndLuma[0] = 0.96f * specGain;
    u.specColorAndLuma[1] = 0.98f * specGain;
    u.specColorAndLuma[2] = 1.00f * specGain;
    u.specColorAndLuma[3] = std::clamp(params.lumaAdaptStrength, 0.0f, 1.0f);

    const float rimFactor = (specGain > 1e-5f) ? (std::max(0.0f, params.rimGlowStrength) / specGain) : 0.0f;
    u.rimGlowParams[0]    = -0.8f;
    u.rimGlowParams[1]    = 1.6f;
    u.rimGlowParams[2]    = rimFactor;
    u.rimGlowParams[3]    = 0.0f;

    u.toneClampParams[0] = isHdrLinear ? 10.0f : 1.0f;
    u.toneClampParams[1] = 0.0f;
    u.toneClampParams[2] = 1.0f;
    u.toneClampParams[3] = 1.0f;

    u.sdfPackParams[0] = 2.0f;
    u.sdfPackParams[1] = 2.0f;
    u.sdfPackParams[2] = -1.0f;
    u.sdfPackParams[3] = -1.0f;
}

} // namespace LiquidGlassPlugin
