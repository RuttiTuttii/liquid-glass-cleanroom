// Clean-Room Liquid Glass Hyprland Plugin — Live Config Registration & Snapshot
// SPDX-License-Identifier: MIT

#pragma once

#include "glass_uniforms.hpp"

#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace LiquidGlassPlugin::Settings {

// -----------------------------------------------------------------------------
// [1] Registered Plugin Config Handles (`plugin:liquid_glass:*`)
// -----------------------------------------------------------------------------

class CPluginConfig {
  public:
    bool              registerAll(HANDLE handle);
    void              reset();

    bool              isEnabled() const;
    float             damagePadPx() const;
    GlassRenderParams readParams() const;

    SP<::Config::Values::Bool>  enabled;
    SP<::Config::Values::Float> refraction;
    SP<::Config::Values::Float> bevelWidth;
    SP<::Config::Values::Float> bevelOffset;
    SP<::Config::Values::Float> dispersion;
    SP<::Config::Values::Float> dispersionCurve;
    SP<::Config::Values::Float> frostLod;
    SP<::Config::Values::Float> edgeLod;
    SP<::Config::Values::Float> specular;
    SP<::Config::Values::Float> specWidth;
    SP<::Config::Values::Float> rimGlow;
    SP<::Config::Values::Float> lumaAdapt;
    SP<::Config::Values::Float> squircleSmoothness;
    SP<::Config::Values::Float> vibrancy;
    SP<::Config::Values::Float> brightness;
    SP<::Config::Values::Float> contrast;
    SP<::Config::Values::Float> tintAlpha;
    SP<::Config::Values::Color> tintColor;
    SP<::Config::Values::Float> damagePad;
};

CPluginConfig& pluginConfig();

} // namespace LiquidGlassPlugin::Settings
