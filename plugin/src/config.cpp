// Clean-Room Liquid Glass Hyprland Plugin — Live Config Registration & Snapshot
// SPDX-License-Identifier: MIT

#include "config.hpp"

#include <hyprland/src/config/supplementary/propRefresher/PropRefresher.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/helpers/Color.hpp>

#include <array>

namespace LiquidGlassPlugin::Settings {

// -----------------------------------------------------------------------------
// [1] Table-Driven Float & Color Option Descriptors
// -----------------------------------------------------------------------------

namespace {

struct FloatSpec {
    const char*                     name;
    const char*                     description;
    float                           defVal;
    float                           minVal;
    float                           maxVal;
    SP<::Config::Values::Float> CPluginConfig::* member;
};

constexpr ::Config::Supplementary::PropRefreshBits kBlurRefresh = ::Config::Supplementary::REFRESH_BLUR_FB;

constexpr std::array<FloatSpec, 17> kFloatSpecs = {{
    {"plugin:liquid_glass:refraction", "Rim refraction displacement in pixels", 22.0f, 0.0f, 128.0f, &CPluginConfig::refraction},
    {"plugin:liquid_glass:bevel_width", "Refractive bevel band width in pixels", 24.0f, 1.0f, 128.0f, &CPluginConfig::bevelWidth},
    {"plugin:liquid_glass:bevel_offset", "Inset/outset offset of the refractive bevel in pixels", 0.0f, -32.0f, 64.0f, &CPluginConfig::bevelOffset},
    {"plugin:liquid_glass:dispersion", "Spectral R/G/B rim dispersion in pixels", 3.5f, 0.0f, 32.0f, &CPluginConfig::dispersion},
    {"plugin:liquid_glass:dispersion_curve", "Power exponent shaping rim dispersion profile", 1.35f, 0.25f, 4.0f, &CPluginConfig::dispersionCurve},
    {"plugin:liquid_glass:frost_lod", "Interior frosted backdrop mip level (0..4)", 2.3f, 0.0f, 4.0f, &CPluginConfig::frostLod},
    {"plugin:liquid_glass:edge_lod", "Bevel rim backdrop mip level (0..4)", 0.35f, 0.0f, 4.0f, &CPluginConfig::edgeLod},
    {"plugin:liquid_glass:specular", "Directional bevel specular highlight strength", 0.55f, 0.0f, 3.0f, &CPluginConfig::specular},
    {"plugin:liquid_glass:spec_width", "Directional bevel specular band width in pixels", 2.5f, 0.5f, 32.0f, &CPluginConfig::specWidth},
    {"plugin:liquid_glass:rim_glow", "Fresnel-like ambient rim glow intensity", 0.18f, 0.0f, 2.0f, &CPluginConfig::rimGlow},
    {"plugin:liquid_glass:luma_adapt", "Background luminance adaptation weight for specular glints", 1.0f, 0.0f, 1.0f, &CPluginConfig::lumaAdapt},
    {"plugin:liquid_glass:squircle_smoothness", "G2-squircle corner factor (-1 auto-matches Hyprland rounding_power)", -1.0f, -1.0f, 1.0f, &CPluginConfig::squircleSmoothness},
    {"plugin:liquid_glass:vibrancy", "Glass saturation boost above 1.0 (-1..2)", 0.22f, -1.0f, 2.0f, &CPluginConfig::vibrancy},
    {"plugin:liquid_glass:brightness", "Linear glass brightness multiplier", 1.02f, 0.1f, 3.0f, &CPluginConfig::brightness},
    {"plugin:liquid_glass:contrast", "Mid-gray pivot contrast inside the glass", 1.08f, 0.2f, 3.0f, &CPluginConfig::contrast},
    {"plugin:liquid_glass:tint_alpha", "Tint blend factor applied over the frosted interior", 0.14f, 0.0f, 0.9f, &CPluginConfig::tintAlpha},
    {"plugin:liquid_glass:damage_pad", "Minimum blur damage expansion margin in pixels", 28.0f, 0.0f, 128.0f, &CPluginConfig::damagePad},
}};

float readFloat(const SP<::Config::Values::Float>& v, float fallback) {
    return v ? v->value() : fallback;
}

} // namespace

// -----------------------------------------------------------------------------
// [2] Registration, Teardown & Thread-Safe Parameter Snapshot
// -----------------------------------------------------------------------------

bool CPluginConfig::registerAll(HANDLE handle) {
    using namespace ::Config::Values;

    enabled = makeConfigValue<Bool>(
        "plugin:liquid_glass:enabled",
        "Enable clean-room Liquid Glass refraction & blur material",
        true,
        SBoolValueOptions{.refresh = kBlurRefresh}
    );
    if (!HyprlandAPI::addConfigValueV2(handle, enabled)) {
        LOG(Log::ERR, "[liquid-glass] Failed to register config value plugin:liquid_glass:enabled");
        return false;
    }

    for (const auto& spec : kFloatSpecs) {
        auto val = makeConfigValue<Float>(
            spec.name,
            spec.description,
            spec.defVal,
            SFloatValueOptions{.min = spec.minVal, .max = spec.maxVal, .refresh = kBlurRefresh}
        );
        if (!HyprlandAPI::addConfigValueV2(handle, val)) {
            LOG(Log::ERR, "[liquid-glass] Failed to register config value {}", spec.name);
            return false;
        }
        this->*spec.member = std::move(val);
    }

    tintColor = makeConfigValue<Color>(
        "plugin:liquid_glass:tint_color",
        "sRGB glass tint color (ARGB hex)",
        static_cast<::Config::INTEGER>(0xFFEAF2FF),
        SColorValueOptions{.refresh = kBlurRefresh}
    );
    if (!HyprlandAPI::addConfigValueV2(handle, tintColor)) {
        LOG(Log::ERR, "[liquid-glass] Failed to register config value plugin:liquid_glass:tint_color");
        return false;
    }

    return true;
}

void CPluginConfig::reset() {
    enabled.reset();
    tintColor.reset();
    for (const auto& spec : kFloatSpecs) {
        (this->*spec.member).reset();
    }
}

bool CPluginConfig::isEnabled() const {
    return enabled ? enabled->value() : true;
}

float CPluginConfig::damagePadPx() const {
    return readFloat(damagePad, 28.0f);
}

GlassRenderParams CPluginConfig::readParams() const {
    GlassRenderParams p{};
    p.refractionPx       = readFloat(refraction, p.refractionPx);
    p.bevelWidthPx       = readFloat(bevelWidth, p.bevelWidthPx);
    p.bevelOffsetPx      = readFloat(bevelOffset, p.bevelOffsetPx);
    p.dispersionPx       = readFloat(dispersion, p.dispersionPx);
    p.dispersionCurve    = readFloat(dispersionCurve, p.dispersionCurve);
    p.frostLod           = readFloat(frostLod, p.frostLod);
    p.edgeLod            = readFloat(edgeLod, p.edgeLod);
    p.specularStrength   = readFloat(specular, p.specularStrength);
    p.specWidthPx        = readFloat(specWidth, p.specWidthPx);
    p.rimGlowStrength    = readFloat(rimGlow, p.rimGlowStrength);
    p.lumaAdaptStrength  = readFloat(lumaAdapt, p.lumaAdaptStrength);
    p.squircleSmoothness = readFloat(squircleSmoothness, p.squircleSmoothness);
    p.vibrancy           = readFloat(vibrancy, p.vibrancy);
    p.brightness         = readFloat(brightness, p.brightness);
    p.contrast           = readFloat(contrast, p.contrast);
    p.tintAlpha          = readFloat(tintAlpha, p.tintAlpha);

    if (tintColor) {
        const CHyprColor c(static_cast<uint64_t>(tintColor->value()));
        p.tintSrgb[0] = static_cast<float>(c.r);
        p.tintSrgb[1] = static_cast<float>(c.g);
        p.tintSrgb[2] = static_cast<float>(c.b);
    }

    return p;
}

CPluginConfig& pluginConfig() {
    static CPluginConfig instance;
    return instance;
}

} // namespace LiquidGlassPlugin::Settings
