// Clean-Room Liquid Glass Hyprland Plugin — Entry Point & Hook Lifecycle
// SPDX-License-Identifier: MIT

#include "config.hpp"
#include "glass_provider.hpp"

#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/GLRenderer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/gl/blur/Factory.hpp>
#include <hyprland/src/state/MonitorState.hpp>

#include <EGL/egl.h>

#include <format>
#include <stdexcept>
#include <string>

namespace {

// -----------------------------------------------------------------------------
// [1] Plugin Global State, Hook Trampoline & Compositor Blur Slot Attachment
// -----------------------------------------------------------------------------

template <auto MemberPtr>
struct CRendererBlurSlotAccess {
    friend UP<Render::GL::IGLBlurProvider> Render::GL::CHyprGLRenderer::* accessRendererBlurSlot() {
        return MemberPtr;
    }
};
UP<Render::GL::IGLBlurProvider> Render::GL::CHyprGLRenderer::* accessRendererBlurSlot();
template struct CRendererBlurSlotAccess<&Render::GL::CHyprGLRenderer::m_blur>;

HANDLE                       g_handle         = nullptr;
CFunctionHook*               g_createBlurHook = nullptr;
SP<IPC::Socket1::SCommand>   g_statusCmd;
std::string                  g_attachDiag     = "uninitialized";

using CreateBlurProviderFn = UP<Render::GL::IGLBlurProvider> (*)(Render::eBlurType, Render::GL::CHyprOpenGLImpl&);

UP<Render::GL::IGLBlurProvider> hkCreateBlurProvider(Render::eBlurType type, Render::GL::CHyprOpenGLImpl& impl) {
    auto origFn   = reinterpret_cast<CreateBlurProviderFn>(g_createBlurHook->m_original);
    auto fallback = origFn(type, impl);
    return Hyprutils::Memory::makeUnique<LiquidGlassPlugin::CLiquidGlassBlurProvider>(std::move(fallback), type);
}

void ensureEglCurrentIfAvailable() {
    if (eglGetCurrentContext() != EGL_NO_CONTEXT)
        return;
    if (Render::GL::g_pHyprOpenGL && Render::GL::g_pHyprOpenGL->m_eglContext != nullptr)
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
}

void damageAllMonitors() {
    if (!g_pHyprRenderer || !State::monitorState())
        return;
    for (const auto& mon : State::monitorState()->monitors()) {
        if (mon)
            g_pHyprRenderer->damageMonitor(mon);
    }
}

void attachLiquidGlassProvider() {
    if (!g_pHyprRenderer) {
        g_attachDiag = std::format("g_pHyprRenderer is null (&g_pHyprRenderer={:p})",
                                   static_cast<const void*>(&g_pHyprRenderer));
        return;
    }
    auto* glRenderer = dynamic_cast<Render::GL::CHyprGLRenderer*>(g_pHyprRenderer.get());
    if (!glRenderer) {
        g_attachDiag = std::format("g_pHyprRenderer ({:p}) is NOT CHyprGLRenderer",
                                   static_cast<const void*>(g_pHyprRenderer.get()));
        return;
    }

    auto& slot = glRenderer->*accessRendererBlurSlot();
    if (!slot) {
        ensureEglCurrentIfAvailable();
        glRenderer->refreshBlurProvider();
        g_attachDiag = slot ? std::format("slot created via refreshBlurProvider (ptr={:p})", static_cast<const void*>(slot.get()))
                            : "slot STILL null after refreshBlurProvider";
    } else if (!dynamic_cast<LiquidGlassPlugin::CLiquidGlassBlurProvider*>(slot.get())) {
        const auto wrappedType = slot->type();
        slot = Hyprutils::Memory::makeUnique<LiquidGlassPlugin::CLiquidGlassBlurProvider>(std::move(slot), wrappedType);
        g_attachDiag = std::format("wrapped existing slot in-place (type={})", static_cast<int>(wrappedType));
    } else {
        g_attachDiag = "already attached";
    }
    damageAllMonitors();
}

void detachLiquidGlassProvider() {
    if (!g_pHyprRenderer)
        return;
    auto* glRenderer = dynamic_cast<Render::GL::CHyprGLRenderer*>(g_pHyprRenderer.get());
    if (!glRenderer)
        return;

    ensureEglCurrentIfAvailable();
    auto& slot = glRenderer->*accessRendererBlurSlot();
    if (auto* lg = dynamic_cast<LiquidGlassPlugin::CLiquidGlassBlurProvider*>(slot.get())) {
        auto restored = lg->takeFallback();
        if (restored) {
            slot = std::move(restored);
        } else {
            slot.reset();
            glRenderer->refreshBlurProvider();
        }
    } else {
        glRenderer->refreshBlurProvider();
    }
    damageAllMonitors();
}

void* findCreateBlurProviderAddr(HANDLE handle) {
    const auto matches = HyprlandAPI::findFunctionsByName(handle, "createBlurProvider");
    for (const auto& m : matches) {
        if (m.demangled == "Render::GL::createBlurProvider(Render::eBlurType, Render::GL::CHyprOpenGLImpl&)")
            return m.address;
    }
    return nullptr;
}

IPC::Socket1::SResponse handleStatusCmd(const IPC::Socket1::SRequest& req) {
    const auto& cfg    = LiquidGlassPlugin::Settings::pluginConfig();
    const auto  params = cfg.readParams();
    const auto* inst   = LiquidGlassPlugin::CLiquidGlassBlurProvider::activeInstance();
    const auto  st     = inst ? inst->stats() : LiquidGlassPlugin::SProviderStats{};

    if (req.format == IPC::Socket1::FORMAT_JSON) {
        return IPC::Socket1::SResponse(std::format(
            "{{\"loaded\":true,\"enabled\":{},\"active\":{},\"gl_initialized\":{},"
            "\"fp16_pyramid\":{},\"mip_levels\":{},\"last_fb\":[{},{}],"
            "\"glass_calls\":{},\"fallback_calls\":{},\"refraction\":{:.2f},"
            "\"dispersion\":{:.2f},\"frost_lod\":{:.2f},\"specular\":{:.2f},"
            "\"last_error\":\"{}\"}}",
            cfg.isEnabled(),
            inst != nullptr,
            st.glInitialized,
            st.fp16Pyramid,
            st.mipLevels,
            st.lastFbW,
            st.lastFbH,
            st.glassCalls,
            st.fallbackCalls,
            params.refractionPx,
            params.dispersionPx,
            params.frostLod,
            params.specularStrength,
            st.lastError
        ));
    }

    return IPC::Socket1::SResponse(std::format(
        "Liquid Glass Plugin (Clean-Room MIT)\n"
        "  enabled:        {}\n"
        "  provider:       {}\n"
        "  gl_initialized: {}\n"
        "  pyramid:        {}x{} ({} mips, {})\n"
        "  calls:          glass={}, fallback={}\n"
        "  attach_diag:    {}\n"
        "  refraction:     {:.2f}px | bevel={:.2f}px | dispersion={:.2f}px\n"
        "  frost_lod:      {:.2f} | edge_lod={:.2f} | specular={:.2f}\n"
        "  last_error:     {}\n",
        cfg.isEnabled(),
        inst ? "attached" : "inactive",
        st.glInitialized,
        st.lastFbW,
        st.lastFbH,
        st.mipLevels,
        st.fp16Pyramid ? "RGBA16F" : "RGBA8",
        st.glassCalls,
        st.fallbackCalls,
        g_attachDiag,
        params.refractionPx,
        params.bevelWidthPx,
        params.dispersionPx,
        params.frostLod,
        params.edgeLod,
        params.specularStrength,
        st.lastError.empty() ? "none" : st.lastError
    ));
}

void cleanupAll() {
    if (g_statusCmd && g_handle) {
        HyprlandAPI::unregisterHyprCtlCommand(g_handle, g_statusCmd);
        g_statusCmd.reset();
    }

    if (g_createBlurHook) {
        g_createBlurHook->unhook();
        if (g_handle)
            HyprlandAPI::removeFunctionHook(g_handle, g_createBlurHook);
        g_createBlurHook = nullptr;
    }

    detachLiquidGlassProvider();
    LiquidGlassPlugin::Settings::pluginConfig().reset();
    g_handle = nullptr;
}

} // namespace

// -----------------------------------------------------------------------------
// [2] Exported Hyprland Plugin ABI Entry Points
// -----------------------------------------------------------------------------

// Force emission of `APICALL inline EXPORT __hyprland_api_get_client_hash` under -O2
// so Hyprland's dynamic loader always locates the ABI version hash via dlsym().
[[maybe_unused]] static const auto g_keepClientHashFn = &__hyprland_api_get_client_hash;

APICALL EXPORT std::string pluginAPIVersion() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO pluginInit(HANDLE handle) {
    g_handle = handle;

    const auto ver = HyprlandAPI::getHyprlandVersion(handle);
    LOG(Log::INFO, "[liquid-glass] Initializing against Hyprland {} ({}, client={})",
        ver.tag, ver.hash, (*g_keepClientHashFn)());

    if (!LiquidGlassPlugin::Settings::pluginConfig().registerAll(handle)) {
        cleanupAll();
        throw std::runtime_error("[liquid-glass] Failed to register plugin configuration values");
    }

    void* targetAddr = findCreateBlurProviderAddr(handle);
    if (!targetAddr) {
        cleanupAll();
        throw std::runtime_error("[liquid-glass] Could not locate Render::GL::createBlurProvider symbol");
    }

    g_createBlurHook = HyprlandAPI::createFunctionHook(handle, targetAddr, rc<void*>(&hkCreateBlurProvider));
    if (!g_createBlurHook || !g_createBlurHook->hook()) {
        cleanupAll();
        throw std::runtime_error("[liquid-glass] Failed to hook Render::GL::createBlurProvider");
    }

    g_statusCmd = HyprlandAPI::registerHyprCtlCommand(
        handle,
        IPC::Socket1::SCommand{
            .name    = "liquid_glass:status",
            .match   = IPC::Socket1::COMMAND_MATCH_EXACT,
            .handler = handleStatusCmd,
        }
    );

    attachLiquidGlassProvider();
    LOG(Log::INFO, "[liquid-glass] Loaded and attached successfully");

    return {
        "liquid-glass",
        "Clean-room optical Liquid Glass blur/refraction provider",
        "RuttiTuttii",
        "0.1.0",
    };
}

APICALL EXPORT void pluginExit() {
    cleanupAll();
    LOG(Log::INFO, "[liquid-glass] Unloaded cleanly");
}
