/**
 * Clean-Room Liquid Glass — WebGL2 Web Lite Runtime Engine
 *
 * SPDX-License-Identifier: MIT
 *
 * Implements the scene-backed 3-level mip pyramid (`1/2`, `1/4`, `1/8`) with
 * energy-preserving 13-tap clamped downsampling, Tier 1 (`RGBA16F`) / Tier 2
 * (`RGBA8`) framebuffer auto-selection, and batched per-panel bounding quads.
 */

import {
    VERTEX_FULLSCREEN,
    FRAGMENT_DOWNSAMPLE_CLAMPED,
    VERTEX_PANEL_QUAD,
    FRAGMENT_LIQUID_GLASS,
} from '../shaders/glass_es300.js';

function compileShader(gl, type, source) {
    const shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
        const log = gl.getShaderInfoLog(shader) || 'Unknown shader compile failure';
        gl.deleteShader(shader);
        throw new Error(`Shader compile error: ${log}`);
    }
    return shader;
}

function linkProgram(gl, vsSource, fsSource) {
    const vs = compileShader(gl, gl.VERTEX_SHADER, vsSource);
    const fs = compileShader(gl, gl.FRAGMENT_SHADER, fsSource);
    const prog = gl.createProgram();
    gl.attachShader(prog, vs);
    gl.attachShader(prog, fs);
    gl.linkProgram(prog);
    gl.deleteShader(vs);
    gl.deleteShader(fs);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
        const log = gl.getProgramInfoLog(prog) || 'Unknown program link failure';
        gl.deleteProgram(prog);
        throw new Error(`Program link error: ${log}`);
    }
    return prog;
}

function createMipTexture(gl, width, height, internalFormat, format, type) {
    const tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(gl.TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, null);
    return tex;
}

export class LiquidGlassEngine {
    /**
     * @param {HTMLCanvasElement} canvas
     */
    constructor(canvas) {
        this.canvas = canvas;
        this.gl = null;
        this.tier = 'css-fallback'; // 'webgl2-rgba16f' | 'webgl2-rgba8' | 'css-fallback'
        this.lastError = '';

        this.quadVao = null;
        this.quadVbo = null;

        this.downsampleProg = null;
        this.dsLocs = null;

        this.glassProg = null;
        this.glLocs = null;

        this.useHalfFloat = false;
        this.mipLevels = []; // [{ tex, fbo, w, h }, ...] length 4 (0 = full scene)
        this.sceneWidth = 0;
        this.sceneHeight = 0;

        this.init();
    }

    init() {
        const gl = this.canvas.getContext('webgl2', {
            alpha: true,
            premultipliedAlpha: true,
            antialias: false,
            depth: false,
            stencil: false,
            preserveDrawingBuffer: false,
            powerPreference: 'high-performance',
        });

        if (!gl) {
            this.tier = 'css-fallback';
            this.lastError = 'WebGL2 context unavailable; using CSS backdrop-filter fallback.';
            return;
        }

        this.gl = gl;

        try {
            // Check Tier 1 RGBA16F half-float pyramid support cleanly
            const extHalfFloat = gl.getExtension('EXT_color_buffer_half_float');
            const extFloat = gl.getExtension('EXT_color_buffer_float');
            this.useHalfFloat = Boolean(extHalfFloat || extFloat);

            // Compile Downsample + Liquid Glass programs
            this.downsampleProg = linkProgram(gl, VERTEX_FULLSCREEN, FRAGMENT_DOWNSAMPLE_CLAMPED);
            this.dsLocs = {
                uSrcTex: gl.getUniformLocation(this.downsampleProg, 'uSrcTex'),
                uSrcTexelSize: gl.getUniformLocation(this.downsampleProg, 'uSrcTexelSize'),
                uUvClampRect: gl.getUniformLocation(this.downsampleProg, 'uUvClampRect'),
            };

            this.glassProg = linkProgram(gl, VERTEX_PANEL_QUAD, FRAGMENT_LIQUID_GLASS);
            const uNames = [
                'uMip0', 'uMip1', 'uMip2', 'uMip3',
                'uViewportCss', 'uDpr', 'uUvClampRect',
                'uPanelBoundsCss', 'uRectCss', 'uCornerRadii',
                'uSmoothness', 'uEllipseMorph',
                'uHasSecondary', 'uSecondaryRectCss', 'uSecondaryParams',
                'uRefractionPx', 'uBevelWidthPx', 'uDispersionPx',
                'uEdgeLod', 'uFrostLod', 'uSpecularStrength', 'uRimGlowStrength',
                'uRefractionMode', 'uQualityTaps', 'uShadowStrength',
                'uPanelTint', 'uPanelOptics', 'uPanelOpacity', 'uLensBoost', 'uTime',
            ];
            this.glLocs = Object.create(null);
            for (const name of uNames) {
                this.glLocs[name] = gl.getUniformLocation(this.glassProg, name);
            }

            // Shared [-1, +1] triangle-strip quad VBO + VAO
            this.quadVao = gl.createVertexArray();
            gl.bindVertexArray(this.quadVao);
            this.quadVbo = gl.createBuffer();
            gl.bindBuffer(gl.ARRAY_BUFFER, this.quadVbo);
            gl.bufferData(
                gl.ARRAY_BUFFER,
                new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]),
                gl.STATIC_DRAW
            );
            gl.enableVertexAttribArray(0);
            gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
            gl.bindVertexArray(null);

            this.tier = this.useHalfFloat ? 'webgl2-rgba16f' : 'webgl2-rgba8';
            this.lastError = '';
        } catch (err) {
            this.destroy();
            this.tier = 'css-fallback';
            this.lastError = err instanceof Error ? err.message : String(err);
        }
    }

    isSupported() {
        return Boolean(this.gl && this.glassProg && this.downsampleProg);
    }

    /**
     * Reallocates the 4-tier scene pyramid (`1, 1/2, 1/4, 1/8`) when pixel size changes.
     */
    ensurePyramid(width, height) {
        const gl = this.gl;
        if (!gl) return false;
        const w = Math.max(16, Math.round(width));
        const h = Math.max(16, Math.round(height));

        if (this.sceneWidth === w && this.sceneHeight === h && this.mipLevels.length === 4) {
            return true;
        }

        this.freePyramid();

        const buildChain = (allowHalfFloat) => {
            const chain = [];
            for (let level = 0; level < 4; level++) {
                const lw = Math.max(4, w >> level);
                const lh = Math.max(4, h >> level);
                // Level 0 stores direct Canvas2D sRGB bytes cleanly; levels 1..3 use RGBA16F when supported
                const isF16 = allowHalfFloat && level > 0;
                const internalFmt = isF16 ? gl.RGBA16F : gl.RGBA8;
                const type = isF16 ? gl.HALF_FLOAT : gl.UNSIGNED_BYTE;
                const tex = createMipTexture(gl, lw, lh, internalFmt, gl.RGBA, type);
                let fbo = null;
                if (level > 0) {
                    fbo = gl.createFramebuffer();
                    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
                    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
                    const status = gl.checkFramebufferStatus(gl.FRAMEBUFFER);
                    if (status !== gl.FRAMEBUFFER_COMPLETE) {
                        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
                        for (const item of chain) {
                            if (item.fbo) gl.deleteFramebuffer(item.fbo);
                            gl.deleteTexture(item.tex);
                        }
                        if (fbo) gl.deleteFramebuffer(fbo);
                        gl.deleteTexture(tex);
                        return null;
                    }
                }
                chain.push({ tex, fbo, w: lw, h: lh });
            }
            gl.bindFramebuffer(gl.FRAMEBUFFER, null);
            return chain;
        };

        let chain = this.useHalfFloat ? buildChain(true) : null;
        if (!chain) {
            this.useHalfFloat = false;
            this.tier = 'webgl2-rgba8';
            chain = buildChain(false);
        } else {
            this.tier = 'webgl2-rgba16f';
        }

        if (!chain) {
            this.tier = 'css-fallback';
            this.lastError = 'Framebuffer pyramid allocation failed cleanly; fell back to CSS.';
            return false;
        }

        this.mipLevels = chain;
        this.sceneWidth = w;
        this.sceneHeight = h;
        return true;
    }

    /**
     * Uploads `sceneCanvas` into mip0 and executes 3 clamped 13-tap downsample passes.
     */
    uploadSceneAndBuildPyramid(sceneCanvas) {
        const gl = this.gl;
        if (!gl || !this.isSupported()) return false;

        if (!this.ensurePyramid(sceneCanvas.width, sceneCanvas.height)) {
            return false;
        }

        const base = this.mipLevels[0];
        gl.disable(gl.BLEND);
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, base.tex);
        // Flip Y on upload so v=1 aligns with Canvas2D top row y=0 and v=0 with bottom
        gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 1);
        gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, 0);
        gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, gl.RGBA, gl.UNSIGNED_BYTE, sceneCanvas);
        gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 0);

        gl.useProgram(this.downsampleProg);
        gl.bindVertexArray(this.quadVao);
        gl.uniform1i(this.dsLocs.uSrcTex, 0);

        for (let i = 1; i < 4; i++) {
            const src = this.mipLevels[i - 1];
            const dst = this.mipLevels[i];
            gl.bindFramebuffer(gl.FRAMEBUFFER, dst.fbo);
            gl.viewport(0, 0, dst.w, dst.h);

            const hx = 0.5 / src.w;
            const hy = 0.5 / src.h;
            gl.uniform2f(this.dsLocs.uSrcTexelSize, 1.0 / src.w, 1.0 / src.h);
            gl.uniform4f(this.dsLocs.uUvClampRect, hx, hy, 1.0 - hx, 1.0 - hy);

            gl.bindTexture(gl.TEXTURE_2D, src.tex);
            gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
        }

        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        return true;
    }

    /**
     * Renders every active glass pill/panel inside its dilated bounding quad.
     */
    renderPanels(panels, optics, viewportCss, dpr, timeSec = 0) {
        const gl = this.gl;
        if (!gl || !this.isSupported() || this.mipLevels.length < 4) return;

        const targetW = Math.max(1, Math.round(viewportCss.w * dpr));
        const targetH = Math.max(1, Math.round(viewportCss.h * dpr));
        if (this.canvas.width !== targetW || this.canvas.height !== targetH) {
            this.canvas.width = targetW;
            this.canvas.height = targetH;
        }

        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.viewport(0, 0, targetW, targetH);
        gl.clearColor(0, 0, 0, 0);
        gl.clear(gl.COLOR_BUFFER_BIT);

        if (!panels.length) return;

        gl.enable(gl.BLEND);
        gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);

        gl.useProgram(this.glassProg);
        gl.bindVertexArray(this.quadVao);

        for (let i = 0; i < 4; i++) {
            gl.activeTexture(gl.TEXTURE0 + i);
            gl.bindTexture(gl.TEXTURE_2D, this.mipLevels[i].tex);
        }
        const loc = this.glLocs;
        gl.uniform1i(loc.uMip0, 0);
        gl.uniform1i(loc.uMip1, 1);
        gl.uniform1i(loc.uMip2, 2);
        gl.uniform1i(loc.uMip3, 3);

        gl.uniform2f(loc.uViewportCss, viewportCss.w, viewportCss.h);
        gl.uniform1f(loc.uDpr, dpr);

        const clampHx = 0.5 / this.mipLevels[0].w;
        const clampHy = 0.5 / this.mipLevels[0].h;
        gl.uniform4f(loc.uUvClampRect, clampHx, clampHy, 1.0 - clampHx, 1.0 - clampHy);

        // Shared optical parameters
        gl.uniform1f(loc.uRefractionPx, optics.refraction);
        gl.uniform1f(loc.uBevelWidthPx, optics.bevel);
        gl.uniform1f(loc.uDispersionPx, optics.dispersion);
        gl.uniform1f(loc.uEdgeLod, optics.edgeLod);
        gl.uniform1f(loc.uFrostLod, optics.frostLod);
        gl.uniform1f(loc.uSpecularStrength, optics.specular);
        gl.uniform1f(loc.uRimGlowStrength, optics.rimGlow);
        gl.uniform1f(loc.uRefractionMode, optics.refractionMode);
        gl.uniform1f(loc.uQualityTaps, optics.sevenTap ? 1.0 : 0.0);
        gl.uniform1f(loc.uShadowStrength, optics.shadow);
        gl.uniform1f(loc.uTime, timeSec);

        // Sort panels back-to-front by zLayer so higher layers (popovers, droplet lens) sit cleanly above
        const sorted = panels.slice().sort((a, b) => (a.zLayer || 0) - (b.zLayer || 0));

        for (const p of sorted) {
            if (!p || p.w <= 1 || p.h <= 1 || p.opacity <= 0.005) continue;

            const hw = p.w * 0.5;
            const hh = p.h * 0.5;
            const cx = p.x + hw;
            const cy = p.y + hh;

            // Compute quad bounds covering both primary and optional secondary coalesced rect + shadow margin
            const margin = 24;
            let minX = p.x - margin;
            let maxX = p.x + p.w + margin;
            let minY = p.y - margin;
            let maxY = p.y + p.h + margin;

            if (p.secondary) {
                minX = Math.min(minX, p.secondary.x - margin);
                maxX = Math.max(maxX, p.secondary.x + p.secondary.w + margin);
                minY = Math.min(minY, p.secondary.y - margin);
                maxY = Math.max(maxY, p.secondary.y + p.secondary.h + margin);
            }

            const qCx = (minX + maxX) * 0.5;
            const qCy = (minY + maxY) * 0.5;
            const qHw = (maxX - minX) * 0.5;
            const qHh = (maxY - minY) * 0.5;

            gl.uniform4f(loc.uPanelBoundsCss, qCx, qCy, qHw, qHh);
            gl.uniform4f(loc.uRectCss, cx, cy, hw, hh);

            const r = p.radii || [p.radius ?? 22, p.radius ?? 22, p.radius ?? 22, p.radius ?? 22];
            gl.uniform4f(loc.uCornerRadii, r[0], r[1], r[2], r[3]);
            gl.uniform1f(loc.uSmoothness, p.smoothness ?? 0.6);
            gl.uniform1f(loc.uEllipseMorph, p.ellipseMorph ?? 0.0);

            if (p.secondary) {
                const s = p.secondary;
                gl.uniform1f(loc.uHasSecondary, 1.0);
                gl.uniform4f(loc.uSecondaryRectCss, s.x + s.w * 0.5, s.y + s.h * 0.5, s.w * 0.5, s.h * 0.5);
                gl.uniform2f(loc.uSecondaryParams, s.radius ?? 24, s.smoothK ?? 14);
            } else {
                gl.uniform1f(loc.uHasSecondary, 0.0);
            }

            const tint = p.tint || [1.0, 1.0, 1.0, optics.tintOpacity];
            const po = p.panelOptics || [1.0, 0.0, 1.0, 0.04];
            gl.uniform4f(loc.uPanelTint, tint[0], tint[1], tint[2], tint[3]);
            gl.uniform4f(loc.uPanelOptics, po[0], po[1], po[2], po[3]);
            gl.uniform1f(loc.uPanelOpacity, p.opacity ?? 1.0);
            gl.uniform1f(loc.uLensBoost, p.lensBoost ?? 0.0);

            gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
        }

        gl.bindVertexArray(null);
    }

    freePyramid() {
        const gl = this.gl;
        if (!gl) return;
        for (const item of this.mipLevels) {
            if (item.fbo) gl.deleteFramebuffer(item.fbo);
            if (item.tex) gl.deleteTexture(item.tex);
        }
        this.mipLevels = [];
        this.sceneWidth = 0;
        this.sceneHeight = 0;
    }

    destroy() {
        const gl = this.gl;
        if (!gl) return;
        this.freePyramid();
        if (this.quadVbo) gl.deleteBuffer(this.quadVbo);
        if (this.quadVao) gl.deleteVertexArray(this.quadVao);
        if (this.downsampleProg) gl.deleteProgram(this.downsampleProg);
        if (this.glassProg) gl.deleteProgram(this.glassProg);
        this.gl = null;
    }
}
