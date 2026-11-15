// Clean-Room Liquid Glass Hyprland Plugin — IGLBlurProvider Implementation
// SPDX-License-Identifier: MIT

#pragma once

#include "gl_resources.hpp"

#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/gl/blur/Provider.hpp>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace LiquidGlassPlugin {

// -----------------------------------------------------------------------------
// [1] Runtime Diagnostics Snapshot
// -----------------------------------------------------------------------------

struct SProviderStats {
    uint64_t    glassCalls    = 0;
    uint64_t    fallbackCalls = 0;
    int         lastFbW       = 0;
    int         lastFbH       = 0;
    int         mipLevels     = 0;
    bool        fp16Pyramid   = false;
    bool        glInitialized = false;
    std::string lastError;
};

// -----------------------------------------------------------------------------
// [2] Clean-Room Liquid Glass Blur Provider (Wraps Native Hyprland Fallback)
// -----------------------------------------------------------------------------

class CLiquidGlassBlurProvider : public Render::GL::IGLBlurProvider {
  public:
    CLiquidGlassBlurProvider(UP<Render::GL::IGLBlurProvider> fallback, Render::eBlurType wrappedType);
    ~CLiquidGlassBlurProvider() override;

    Render::eBlurType type() const noexcept override;
    bool              isAnimated(Render::CRenderContext& ctx) const noexcept override;
    bool              requiresLiveBlur() const noexcept override;

    void              expandDamage(CRegion& damage, float multiplier = 1.F) const override;

    void              shutdown(bool hasCurrentContext);
    UP<Render::GL::IGLBlurProvider> takeFallback() {
        return std::move(m_fallback);
    }
    SProviderStats    stats() const;

    static CLiquidGlassBlurProvider* activeInstance();

  protected:
    SP<Render::GL::CGLFramebuffer> blurGL(
        Render::CRenderContext&          ctx,
        SP<Render::GL::CGLFramebuffer>   source,
        float                            strength,
        const CRegion&                   originalDamage,
        const Render::SBlurContext&      context
    ) override;

  private:
    SP<Render::GL::CGLFramebuffer> acquireOutputFB(
        int                                 fbW,
        int                                 fbH,
        DRMFormat                           preferredFmt,
        NColorManagement::PImageDescription desc,
        bool&                               outFreshAlloc
    );

    SP<Render::GL::CGLFramebuffer> delegateFallback(
        Render::CRenderContext&        ctx,
        SP<Render::GL::CGLFramebuffer> source,
        float                          strength,
        const CRegion&                 originalDamage,
        const Render::SBlurContext&    context
    );

    UP<Render::GL::IGLBlurProvider>             m_fallback;
    Render::eBlurType                           m_wrappedType;
    CLiquidGlassPipeline                        m_pipeline;
    std::vector<SP<Render::GL::CGLFramebuffer>> m_outPool;

    mutable std::mutex                          m_statsMu;
    SProviderStats                              m_stats;

    static CLiquidGlassBlurProvider*            s_active;
};

} // namespace LiquidGlassPlugin
