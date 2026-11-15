/**
 * Clean-Room Liquid Glass — Synchronized DOM-to-Texture Scene Mirror
 *
 * SPDX-License-Identifier: MIT
 *
 * Mirrors the active interactive DOM content stage (wallpaper gradients,
 * Apple-style inset grouped cards, vivid photography tiles, profile banners,
 * switches, and row typography) onto an offscreen HiDPI Canvas2D backing
 * texture at exact `getBoundingClientRect()` screen coordinates so the WebGL2
 * shader refracts live scrolling content 1:1.
 */

function roundRectPath(ctx, x, y, w, h, r) {
    const cr = Math.max(0, Math.min(r, w * 0.5, h * 0.5));
    ctx.beginPath();
    ctx.roundRect(x, y, w, h, cr);
}

/**
 * Draws vivid procedural Apple-style wallpaper / studio stage backgrounds
 * designed specifically to showcase bevel refraction, dispersion, and blur.
 */
function paintWallpaperBackdrop(ctx, w, h, theme, wallpaper, timeSec) {
    const isDark = theme === 'dark';

    // Base studio / OS surface fill
    ctx.fillStyle = isDark ? '#0c0f18' : '#f2f2f7';
    ctx.fillRect(0, 0, w, h);

    if (wallpaper === 'grid') {
        // High-contrast chromatic calibration grid for measuring refraction displacements
        ctx.fillStyle = isDark ? '#0b0e17' : '#f4f6fb';
        ctx.fillRect(0, 0, w, h);

        const step = 36;
        ctx.lineWidth = 1;
        for (let x = 0; x < w; x += step) {
            ctx.strokeStyle = (x / step) % 4 === 0
                ? (isDark ? 'rgba(99, 179, 237, 0.42)' : 'rgba(0, 112, 243, 0.34)')
                : (isDark ? 'rgba(255,255,255,0.09)' : 'rgba(15, 23, 42, 0.11)');
            ctx.beginPath();
            ctx.moveTo(x + 0.5, 0);
            ctx.lineTo(x + 0.5, h);
            ctx.stroke();
        }
        for (let y = 0; y < h; y += step) {
            ctx.strokeStyle = (y / step) % 4 === 0
                ? (isDark ? 'rgba(244, 114, 182, 0.40)' : 'rgba(225, 29, 72, 0.28)')
                : (isDark ? 'rgba(255,255,255,0.09)' : 'rgba(15, 23, 42, 0.11)');
            ctx.beginPath();
            ctx.moveTo(0, y + 0.5);
            ctx.lineTo(w, y + 0.5);
            ctx.stroke();
        }

        // Crisp concentric rings + diagonal prism ribbon behind sidebars & headers
        const grad = ctx.createLinearGradient(0, 0, w, h);
        grad.addColorStop(0, 'rgba(56, 189, 248, 0.22)');
        grad.addColorStop(0.45, 'rgba(168, 85, 247, 0.20)');
        grad.addColorStop(1, 'rgba(251, 146, 60, 0.22)');
        ctx.fillStyle = grad;
        ctx.fillRect(0, 0, w, h);
        return;
    }

    if (wallpaper === 'alpine') {
        // Multi-stop translucent mountain-dawn aurora mesh
        const sky = ctx.createLinearGradient(0, 0, w * 0.2, h);
        if (isDark) {
            sky.addColorStop(0, '#091529');
            sky.addColorStop(0.5, '#132a47');
            sky.addColorStop(1, '#1f1635');
        } else {
            sky.addColorStop(0, '#d8ecff');
            sky.addColorStop(0.48, '#f0e6ff');
            sky.addColorStop(1, '#ffe9d6');
        }
        ctx.fillStyle = sky;
        ctx.fillRect(0, 0, w, h);

        // Soft layered dunes/waves
        const waves = isDark
            ? ['rgba(56,189,248,0.28)', 'rgba(139,92,246,0.24)', 'rgba(236,72,153,0.20)']
            : ['rgba(59,130,246,0.22)', 'rgba(168,85,247,0.18)', 'rgba(249,115,22,0.18)'];
        waves.forEach((col, idx) => {
            ctx.fillStyle = col;
            ctx.beginPath();
            const baseY = h * (0.28 + idx * 0.22);
            ctx.moveTo(0, h);
            for (let x = 0; x <= w + 40; x += 40) {
                const wave = Math.sin(x * 0.005 + idx * 1.4 + timeSec * 0.35) * 38;
                ctx.lineTo(x, baseY + wave);
            }
            ctx.lineTo(w, h);
            ctx.closePath();
            ctx.fill();
        });
        return;
    }

    if (wallpaper === 'solstice') {
        // Rich liquid bokeh chromatic blobs behind floating glass chrome
        const baseGrad = ctx.createLinearGradient(0, 0, w, h);
        if (isDark) {
            baseGrad.addColorStop(0, '#0b1020');
            baseGrad.addColorStop(1, '#121829');
        } else {
            baseGrad.addColorStop(0, '#eef2ff');
            baseGrad.addColorStop(0.5, '#f5f3ff');
            baseGrad.addColorStop(1, '#fdf2f8');
        }
        ctx.fillStyle = baseGrad;
        ctx.fillRect(0, 0, w, h);

        const orbs = [
            { x: 0.14, y: 0.18, r: 0.36, c: isDark ? 'rgba(56, 189, 248, 0.42)' : 'rgba(59, 130, 246, 0.32)' },
            { x: 0.84, y: 0.24, r: 0.32, c: isDark ? 'rgba(236, 72, 153, 0.36)' : 'rgba(244, 63, 94, 0.26)' },
            { x: 0.22, y: 0.82, r: 0.38, c: isDark ? 'rgba(168, 85, 247, 0.36)' : 'rgba(139, 92, 246, 0.28)' },
            { x: 0.72, y: 0.76, r: 0.34, c: isDark ? 'rgba(251, 146, 60, 0.32)' : 'rgba(245, 158, 11, 0.24)' },
        ];
        for (let i = 0; i < orbs.length; i++) {
            const o = orbs[i];
            const ox = (o.x + Math.sin(timeSec * 0.4 + i) * 0.02) * w;
            const oy = (o.y + Math.cos(timeSec * 0.35 + i) * 0.02) * h;
            const rad = Math.max(w, h) * o.r;
            const g = ctx.createRadialGradient(ox, oy, 0, ox, oy, rad);
            g.addColorStop(0, o.c);
            g.addColorStop(1, 'rgba(0,0,0,0)');
            ctx.fillStyle = g;
            ctx.fillRect(0, 0, w, h);
        }
        return;
    }

    // Default 'system': clean Apple OS neutral canvas with subtle ambient tint
    const sys = ctx.createLinearGradient(0, 0, 0, h);
    if (isDark) {
        sys.addColorStop(0, '#0c0f16');
        sys.addColorStop(1, '#121620');
    } else {
        sys.addColorStop(0, '#efeff4');
        sys.addColorStop(1, '#e8eaf2');
    }
    ctx.fillStyle = sys;
    ctx.fillRect(0, 0, w, h);
}

/**
 * Draws procedural gallery artwork matching `.art-card` CSS gradients so scrolling
 * photos underneath the sidebar & top bar refract accurately.
 */
function paintArtFill(ctx, rect, artKind) {
    const { left: x, top: y, width: w, height: h } = rect;
    const g = ctx.createLinearGradient(x, y, x + w, y + h);
    if (artKind === 'sunset') {
        g.addColorStop(0, '#ff7e5f');
        g.addColorStop(0.5, '#feb47b');
        g.addColorStop(1, '#7c3aed');
    } else if (artKind === 'ocean') {
        g.addColorStop(0, '#0284c7');
        g.addColorStop(0.55, '#2dd4bf');
        g.addColorStop(1, '#1e3a8a');
    } else if (artKind === 'prism') {
        g.addColorStop(0, '#f43f5e');
        g.addColorStop(0.35, '#eab308');
        g.addColorStop(0.7, '#22c55e');
        g.addColorStop(1, '#3b82f6');
    } else if (artKind === 'emerald') {
        g.addColorStop(0, '#059669');
        g.addColorStop(0.5, '#34d399');
        g.addColorStop(1, '#065f46');
    } else if (artKind === 'graphite') {
        g.addColorStop(0, '#334155');
        g.addColorStop(0.5, '#0f172a');
        g.addColorStop(1, '#4f46e5');
    } else {
        g.addColorStop(0, '#6366f1');
        g.addColorStop(1, '#ec4899');
    }
    ctx.fillStyle = g;
    ctx.fill();

    // Bold high-contrast geometric ribbons inside cards for dramatic edge refraction
    ctx.save();
    ctx.clip();
    ctx.fillStyle = 'rgba(255,255,255,0.22)';
    ctx.beginPath();
    ctx.arc(x + w * 0.78, y + h * 0.25, Math.min(w, h) * 0.34, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = 'rgba(255,255,255,0.12)';
    ctx.fillRect(x, y + h * 0.62, w, 6);
    ctx.restore();
}

export class SceneMirror {
    constructor() {
        this.canvas = document.createElement('canvas');
        this.ctx = this.canvas.getContext('2d', { alpha: false });
        this.scale = 1;
    }

    /**
     * Synchronizes offscreen `sceneCanvas` with the live viewport DOM stage.
     */
    render({
        viewportCss,
        dpr,
        stageScroller,
        theme,
        wallpaper,
        formFactor,
        timeSec,
        tabDockRect,
        dropletActive,
    }) {
        // Mirror at capped scale <= 1.25 for crisp retina optics with minimal CPU upload cost
        const mirrorDpr = Math.min(dpr, 1.25);
        const pw = Math.max(16, Math.round(viewportCss.w * mirrorDpr));
        const ph = Math.max(16, Math.round(viewportCss.h * mirrorDpr));

        if (this.canvas.width !== pw || this.canvas.height !== ph) {
            this.canvas.width = pw;
            this.canvas.height = ph;
        }

        const ctx = this.ctx;
        ctx.save();
        ctx.setTransform(mirrorDpr, 0, 0, mirrorDpr, 0, 0);

        const isDark = theme === 'dark';

        // 1. Background: if in iPhone frame mode, paint clean studio stage around the device bezel
        if (formFactor === 'iphone') {
            ctx.fillStyle = isDark ? '#07090f' : '#dfe3ee';
            ctx.fillRect(0, 0, viewportCss.w, viewportCss.h);
        } else {
            paintWallpaperBackdrop(ctx, viewportCss.w, viewportCss.h, theme, wallpaper, timeSec);
        }

        if (stageScroller) {
            const clipRect = stageScroller.getBoundingClientRect();
            ctx.save();
            const clipRadius = formFactor === 'iphone' ? 44 : 0;
            roundRectPath(ctx, clipRect.left, clipRect.top, clipRect.width, clipRect.height, clipRadius);
            ctx.clip();

            // Paint wallpaper inside the scrolling container bounds too (matches iPhone wallpaper cleanly)
            if (formFactor === 'iphone') {
                ctx.save();
                ctx.translate(clipRect.left, clipRect.top);
                paintWallpaperBackdrop(ctx, clipRect.width, clipRect.height, theme, wallpaper, timeSec);
                ctx.restore();
            }

            // 2. Mirror Hero Banner (Messenger Profile colored banner or Settings hero)
            const heroes = stageScroller.querySelectorAll('[data-scene="hero"]');
            for (const el of heroes) {
                const r = el.getBoundingClientRect();
                if (r.bottom < clipRect.top - 40 || r.top > clipRect.bottom + 40) continue;
                const heroGrad = ctx.createLinearGradient(r.left, r.top, r.left, r.bottom);
                if (isDark) {
                    heroGrad.addColorStop(0, '#1e3a4c');
                    heroGrad.addColorStop(1, '#122230');
                } else {
                    heroGrad.addColorStop(0, '#44657a');
                    heroGrad.addColorStop(1, '#314c5f');
                }
                ctx.fillStyle = heroGrad;
                ctx.fillRect(r.left, r.top, r.width, r.height);

                // Subtle watermark badges across the banner matching ref_19
                ctx.fillStyle = 'rgba(255,255,255,0.10)';
                ctx.font = '700 18px system-ui, sans-serif';
                for (let bx = r.left + 24; bx < r.right - 20; bx += 92) {
                    for (let by = r.top + 38; by < r.bottom - 18; by += 64) {
                        ctx.fillText('LG', bx + ((by % 128 === 0) ? 24 : 0), by);
                    }
                }
            }

            // 3. Mirror Art / Stress-Test Photo Cards
            const arts = stageScroller.querySelectorAll('[data-scene="art"]');
            for (const el of arts) {
                const r = el.getBoundingClientRect();
                if (r.bottom < clipRect.top - 40 || r.top > clipRect.bottom + 40) continue;
                const rad = parseFloat(getComputedStyle(el).borderRadius) || 24;
                roundRectPath(ctx, r.left, r.top, r.width, r.height, rad);
                paintArtFill(ctx, r, el.dataset.art || 'sunset');

                const titleEl = el.querySelector('.art-title');
                if (titleEl) {
                    ctx.fillStyle = '#ffffff';
                    ctx.font = '700 20px system-ui, -apple-system, sans-serif';
                    ctx.fillText(titleEl.textContent || '', r.left + 18, r.bottom - 24);
                }
            }

            // 4. Mirror Inset Grouped Cards (Settings / Wi-Fi / Profile actions)
            const cards = stageScroller.querySelectorAll('[data-scene="card"]');
            for (const el of cards) {
                const r = el.getBoundingClientRect();
                if (r.bottom < clipRect.top - 40 || r.top > clipRect.bottom + 40) continue;
                const rad = parseFloat(getComputedStyle(el).borderRadius) || 24;
                roundRectPath(ctx, r.left, r.top, r.width, r.height, rad);
                ctx.fillStyle = isDark ? 'rgba(28, 32, 44, 0.94)' : 'rgba(255, 255, 255, 0.95)';
                ctx.fill();
                ctx.strokeStyle = isDark ? 'rgba(255,255,255,0.08)' : 'rgba(15, 23, 42, 0.06)';
                ctx.lineWidth = 1;
                ctx.stroke();
            }

            // 5. Mirror Rows, Icons, Labels, Badges & Active Toggles inside cards so refracting edges warp sharp typography!
            const rows = stageScroller.querySelectorAll('[data-scene="row"]');
            for (const el of rows) {
                const r = el.getBoundingClientRect();
                if (r.bottom < clipRect.top - 30 || r.top > clipRect.bottom + 30) continue;

                const iconEl = el.querySelector('.row-icon-badge');
                let textLeft = r.left + 18;
                if (iconEl) {
                    const ir = iconEl.getBoundingClientRect();
                    roundRectPath(ctx, ir.left, ir.top, ir.width, ir.height, 9);
                    ctx.fillStyle = iconEl.dataset.color || '#0a84ff';
                    ctx.fill();
                    textLeft = ir.right + 12;
                }

                const titleEl = el.querySelector('.row-title');
                if (titleEl) {
                    const tr = titleEl.getBoundingClientRect();
                    ctx.fillStyle = titleEl.classList.contains('is-accent')
                        ? (isDark ? '#60a5fa' : '#007aff')
                        : (isDark ? '#f8fafc' : '#111827');
                    ctx.font = '500 15px system-ui, -apple-system, sans-serif';
                    ctx.textBaseline = 'middle';
                    ctx.fillText((titleEl.textContent || '').trim(), Math.max(textLeft, tr.left), tr.top + tr.height * 0.5);
                }

                const valEl = el.querySelector('.row-value');
                if (valEl) {
                    const vr = valEl.getBoundingClientRect();
                    ctx.fillStyle = isDark ? '#94a3b8' : '#6b7280';
                    ctx.font = '400 14px system-ui, -apple-system, sans-serif';
                    ctx.textBaseline = 'middle';
                    ctx.fillText((valEl.textContent || '').trim(), vr.left, vr.top + vr.height * 0.5);
                }

                // Switch capsule state
                const sw = el.querySelector('input[type="checkbox"]');
                if (sw) {
                    const sr = sw.getBoundingClientRect();
                    roundRectPath(ctx, sr.left, sr.top, sr.width, sr.height, sr.height * 0.5);
                    ctx.fillStyle = sw.checked ? '#34c759' : (isDark ? '#3a3f4b' : '#cbd5e1');
                    ctx.fill();

                    const knobR = sr.height * 0.41;
                    const kx = sw.checked ? sr.right - sr.height * 0.5 : sr.left + sr.height * 0.5;
                    ctx.fillStyle = '#ffffff';
                    ctx.beginPath();
                    ctx.arc(kx, sr.top + sr.height * 0.5, knobR, 0, Math.PI * 2);
                    ctx.fill();
                }

                // Subtle bottom divider inside card
                if (el.nextElementSibling && el.nextElementSibling.matches('[data-scene="row"]')) {
                    ctx.fillStyle = isDark ? 'rgba(255,255,255,0.08)' : 'rgba(15, 23, 42, 0.08)';
                    ctx.fillRect(textLeft, r.bottom - 0.5, r.right - textLeft - 14, 0.75);
                }
            }

            // 6. Mirror Vertical Stepped Capsule Sliders & Headings in Control View
            const headings = stageScroller.querySelectorAll('[data-scene="heading"]');
            for (const el of headings) {
                const r = el.getBoundingClientRect();
                if (r.bottom < clipRect.top - 20 || r.top > clipRect.bottom + 20) continue;
                ctx.fillStyle = el.classList.contains('is-light') ? '#ffffff' : (isDark ? '#f8fafc' : '#0f172a');
                ctx.font = `700 ${parseFloat(getComputedStyle(el).fontSize) || 26}px system-ui, -apple-system, sans-serif`;
                ctx.textBaseline = 'alphabetic';
                ctx.fillText((el.textContent || '').trim(), r.left, r.bottom - 6);
            }

            const pillars = stageScroller.querySelectorAll('[data-scene="capsule-meter"]');
            for (const el of pillars) {
                const r = el.getBoundingClientRect();
                if (r.bottom < clipRect.top - 20 || r.top > clipRect.bottom + 20) continue;
                const fillFrac = parseFloat(el.dataset.fill || '0.6');
                roundRectPath(ctx, r.left, r.top, r.width, r.height, r.width * 0.5);
                ctx.save();
                ctx.clip();
                ctx.fillStyle = isDark ? 'rgba(255,255,255,0.14)' : 'rgba(15, 23, 42, 0.12)';
                ctx.fillRect(r.left, r.top, r.width, r.height);
                const fh = r.height * Math.max(0, Math.min(1, fillFrac));
                ctx.fillStyle = isDark ? '#f8fafc' : '#ffffff';
                ctx.fillRect(r.left, r.bottom - fh, r.width, fh);
                ctx.restore();
            }

            // 7. When dragging the Tab Bar Droplet, mirror the tab badge & labels right underneath
            // so the elevated liquid lens physically refracts badge `113` and icons along its bevel!
            if (dropletActive && tabDockRect && tabDockRect.width > 10) {
                const tr = tabDockRect;
                ctx.save();
                roundRectPath(ctx, tr.left, tr.top, tr.width, tr.height, tr.height * 0.5);
                ctx.fillStyle = isDark ? 'rgba(30, 35, 50, 0.72)' : 'rgba(255, 255, 255, 0.76)';
                ctx.fill();

                const tabs = stageScroller.closest('#device-shell')?.querySelectorAll('.tab-btn') || [];
                for (const btn of tabs) {
                    const br = btn.getBoundingClientRect();
                    const cx = br.left + br.width * 0.5;
                    const cy = br.top + br.height * 0.5;
                    ctx.fillStyle = btn.classList.contains('is-active') ? '#a855f7' : (isDark ? '#e2e8f0' : '#0f172a');
                    ctx.beginPath();
                    ctx.arc(cx, cy - 7, 8, 0, Math.PI * 2);
                    ctx.fill();

                    const badge = btn.querySelector('.tab-badge');
                    if (badge) {
                        const bgr = badge.getBoundingClientRect();
                        roundRectPath(ctx, bgr.left, bgr.top, bgr.width, bgr.height, 8);
                        ctx.fillStyle = '#ff3b30';
                        ctx.fill();
                        ctx.fillStyle = '#ffffff';
                        ctx.font = '700 10px system-ui, sans-serif';
                        ctx.textAlign = 'center';
                        ctx.textBaseline = 'middle';
                        ctx.fillText(badge.textContent || '113', bgr.left + bgr.width * 0.5, bgr.top + bgr.height * 0.5);
                        ctx.textAlign = 'left';
                    }
                }
                ctx.restore();
            }

            ctx.restore();
        }

        ctx.restore();
        return this.canvas;
    }
}
