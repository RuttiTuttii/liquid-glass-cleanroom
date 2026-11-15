// Clean-Room Liquid Glass Hyprland Plugin — IGLBlurProvider Implementation
// SPDX-License-Identifier: MIT

#include "glass_provider.hpp"
#include "config.hpp"

#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/render/GLRenderer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <EGL/egl.h>
#include <drm_fourcc.h>

#include <algorithm>
#include <cmath>

namespace LiquidGlassPlugin {

CLiquidGlassBlurProvider* CLiquidGlassBlurProvider::s_active = nullptr;

// -----------------------------------------------------------------------------
// [1] Lifecycle & Active Provider Registry
// -----------------------------------------------------------------------------

CLiquidGlassBlurProvider::CLiquidGlassBlurProvider(UP<Render::GL::IGLBlurProvider> fallback, Render::eBlurType wrappedType)
    : m_fallback(std::move(fallback)), m_wrappedType(wrappedType) {
    s_active = this;
    LOG(Log::INFO, "[liquid-glass] Attached blur provider wrapper (wrapped type={})", static_cast<int>(wrappedType));
}

CLiquidGlassBlurProvider::~CLiquidGlassBlurProvider() {
    shutdown(eglGetCurrentContext() != EGL_NO_CONTEXT);
    if (s_active == this)
        s_active = nullptr;
}

CLiquidGlassBlurProvider* CLiquidGlassBlurProvider::activeInstance() {
    return s_active;
}

void CLiquidGlassBlurProvider::shutdown(bool hasCurrentContext) {
    m_pipeline.destroy(hasCurrentContext);
    m_outPool.clear();
    std::lock_guard<std::mutex> lock(m_statsMu);
    m_stats.glInitialized = false;
}

SProviderStats CLiquidGlassBlurProvider::stats() const {
    std::lock_guard<std::mutex> lock(m_statsMu);
    return m_stats;
}

// -----------------------------------------------------------------------------
// [2] Metadata, Live Blur Queries & Damage Margin Expansion
// -----------------------------------------------------------------------------

Render::eBlurType CLiquidGlassBlurProvider::type() const noexcept {
    return m_fallback ? m_fallback->type() : m_wrappedType;
}

bool CLiquidGlassBlurProvider::isAnimated(Render::CRenderContext& ctx) const noexcept {
    if (!Settings::pluginConfig().isEnabled() && m_fallback)
        return m_fallback->isAnimated(ctx);
    return false;
}

bool CLiquidGlassBlurProvider::requiresLiveBlur() const noexcept {
    if (!Settings::pluginConfig().isEnabled() && m_fallback)
        return m_fallback->requiresLiveBlur();
    return true;
}

void CLiquidGlassBlurProvider::expandDamage(CRegion& damage, float multiplier) const {
    const auto& cfg = Settings::pluginConfig();
    if (!cfg.isEnabled()) {
        if (m_fallback)
            m_fallback->expandDamage(damage, multiplier);
        return;
    }

    const GlassRenderParams params = cfg.readParams();
    const float             scale  = std::max(multiplier, 1.0f);
    const float             optic  = std::ceil(params.refractionPx + params.dispersionPx + 8.0f);
    const float             pad    = std::max(cfg.damagePadPx(), optic) * scale;
    damage.expand(static_cast<double>(pad));
}

// -----------------------------------------------------------------------------
// [3] Output Framebuffer Pooling & Safe Native Fallback Delegation
// -----------------------------------------------------------------------------

SP<Render::GL::CGLFramebuffer> CLiquidGlassBlurProvider::acquireOutputFB(
    int                                 fbW,
    int                                 fbH,
    DRMFormat                           preferredFmt,
    NColorManagement::PImageDescription desc,
    bool&                               outFreshAlloc
) {
    outFreshAlloc = false;
    if (!g_pHyprRenderer)
        return nullptr;

    const DRMFormat reqFmt = (preferredFmt != DRM_FORMAT_INVALID) ? preferredFmt : DRM_FORMAT_ARGB8888;

    auto allocateSlot = [&](SP<Render::GL::CGLFramebuffer>& fb) -> bool {
        if (!fb)
            return false;
        const bool sameSize = static_cast<int>(std::lround(fb->m_size.x)) == fbW &&
                              static_cast<int>(std::lround(fb->m_size.y)) == fbH;
        const bool fmtMatch = (fb->m_drmFormat == reqFmt) || (fb->m_drmFormat == DRM_FORMAT_ARGB8888 && reqFmt == DRM_FORMAT_INVALID);
        if (!fb->isAllocated() || !sameSize || !fmtMatch) {
            if (!fb->alloc(fbW, fbH, reqFmt)) {
                if (reqFmt == DRM_FORMAT_ARGB8888 || !fb->alloc(fbW, fbH, DRM_FORMAT_ARGB8888))
                    return false;
            }
            outFreshAlloc = true;
        }
        if (desc)
            fb->setImageDescription(desc);
        const GLuint fbID = fb->getFBID();
        return fbID != 0 && fbID != static_cast<GLuint>(-1);
    };

    for (auto& slot : m_outPool) {
        if (slot && slot.strongRef() == 1) {
            if (allocateSlot(slot))
                return slot;
        }
    }

    auto fresh = Hyprutils::Memory::dynamicPointerCast<Render::GL::CGLFramebuffer>(
        g_pHyprRenderer->createFB("liquid-glass-out")
    );
    if (!allocateSlot(fresh))
        return nullptr;

    if (m_outPool.size() < 4)
        m_outPool.push_back(fresh);

    outFreshAlloc = true;
    return fresh;
}

SP<Render::GL::CGLFramebuffer> CLiquidGlassBlurProvider::delegateFallback(
    Render::CRenderContext&        ctx,
    SP<Render::GL::CGLFramebuffer> source,
    float                          strength,
    const CRegion&                 originalDamage,
    const Render::SBlurContext&    context
) {
    {
        std::lock_guard<std::mutex> lock(m_statsMu);
        m_stats.fallbackCalls++;
    }
    if (!m_fallback)
        return source;
    auto out = m_fallback->blur(ctx, source, strength, originalDamage, context);
    return Hyprutils::Memory::dynamicPointerCast<Render::GL::CGLFramebuffer>(out);
}

// -----------------------------------------------------------------------------
// [4] Panel-Shaped Liquid Glass Execution in OpenGL ES 3.2
// -----------------------------------------------------------------------------

SP<Render::GL::CGLFramebuffer> CLiquidGlassBlurProvider::blurGL(
    Render::CRenderContext&        ctx,
    SP<Render::GL::CGLFramebuffer> source,
    float                          strength,
    const CRegion&                 originalDamage,
    const Render::SBlurContext&    context
) {
    const auto& cfg = Settings::pluginConfig();
    if (!cfg.isEnabled() || !context.shape.has_value() || strength <= 0.001f || !source || !source->isAllocated())
        return delegateFallback(ctx, source, strength, originalDamage, context);

    const Render::SBlurShape& shape = *context.shape;
    const int                 fbW   = static_cast<int>(std::lround(source->m_size.x));
    const int                 fbH   = static_cast<int>(std::lround(source->m_size.y));
    const auto                tex   = source->getTexture();
    if (fbW <= 0 || fbH <= 0 || !tex || tex->m_texID == 0 || shape.box.w < 2.0 || shape.box.h < 2.0)
        return delegateFallback(ctx, source, strength, originalDamage, context);

    const auto desc        = source->imageDescription();
    const bool isHdrLinear = desc &&
        (desc->value().transferFunction == NColorManagement::CM_TRANSFER_FUNCTION_LINEAR ||
         desc->value().transferFunction == NColorManagement::CM_TRANSFER_FUNCTION_EXT_LINEAR);
    const bool srcForceOpaque = (tex->m_type == Render::TEXTURE_RGBX) || tex->m_opaque;

    bool justAllocated = false;
    auto outFB         = acquireOutputFB(fbW, fbH, source->m_drmFormat, desc, justAllocated);
    if (!outFB)
        return delegateFallback(ctx, source, strength, originalDamage, context);

    std::vector<PixelBox> damageBoxes;
    if (justAllocated || originalDamage.empty()) {
        damageBoxes.push_back(PixelBox{0, 0, fbW, fbH});
    } else {
        CRegion clipped = originalDamage;
        clipped.intersect(0.0, 0.0, static_cast<double>(fbW), static_cast<double>(fbH));
        clipped.forEachRect([&](const pixman_box32_t& r) {
            if (r.x2 > r.x1 && r.y2 > r.y1)
                damageBoxes.push_back(PixelBox{r.x1, r.y1, r.x2 - r.x1, r.y2 - r.y1});
        });
        if (damageBoxes.empty())
            return outFB;
    }

    const PixelBox panelBox{
        static_cast<int>(std::lround(shape.box.x)),
        static_cast<int>(std::lround(shape.box.y)),
        static_cast<int>(std::lround(shape.box.w)),
        static_cast<int>(std::lround(shape.box.h)),
    };
    const GlassRenderParams params = cfg.readParams();

    std::string err;
    if (!m_pipeline.renderPanel(
            tex->m_texID,
            srcForceOpaque,
            isHdrLinear,
            outFB->getFBID(),
            fbW,
            fbH,
            panelBox,
            std::max(0.0f, shape.radius),
            std::max(1.0f, shape.roundingPower),
            std::clamp(strength, 0.0f, 1.5f),
            damageBoxes,
            params,
            err
        )) {
        LOG(Log::ERR, "[liquid-glass] renderPanel failed ({}), delegating to native fallback", err);
        std::lock_guard<std::mutex> lock(m_statsMu);
        m_stats.lastError = err;
        return delegateFallback(ctx, source, strength, originalDamage, context);
    }

    {
        std::lock_guard<std::mutex> lock(m_statsMu);
        m_stats.glassCalls++;
        m_stats.lastFbW       = fbW;
        m_stats.lastFbH       = fbH;
        m_stats.mipLevels     = m_pipeline.mipLevels();
        m_stats.fp16Pyramid   = m_pipeline.isFp16();
        m_stats.glInitialized = m_pipeline.isInitialized();
    }

    return outFB;
}

} // namespace LiquidGlassPlugin
