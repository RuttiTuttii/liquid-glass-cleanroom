/**
 * Clean-Room Liquid Glass — Interactive Apple-Style UI Testbed Application
 *
 * SPDX-License-Identifier: MIT
 *
 * Orchestrates multiple realistic Apple-style floating UI layouts (`Desktop Split`,
 * `iPhone 26 Mockup`, `Settings -> Wi-Fi`, `Messenger Profile + Tab Bar Droplet`,
 * `Control Center Stepped Pills`, and `Spatial Gallery`) backed by our WebGL2
 * Web Lite Liquid Glass shader & DOM-synced scene mirror.
 */

import { LiquidGlassEngine } from './engine/liquid_glass_gl.js';
import { SceneMirror } from './engine/scene_mirror.js';

// -----------------------------------------------------------------------------
// [1] Optical Presets & Initial Testbed State
// -----------------------------------------------------------------------------
const OPTICS_PRESETS = {
    apple: {
        refraction: 24,
        bevel: 24,
        dispersion: 4.2,
        frostLod: 1.95,
        edgeLod: 0.20,
        specular: 0.68,
        rimGlow: 0.55,
        tintOpacity: 0.18,
        refractionMode: 1.0,
    },
    crystal: {
        refraction: 30,
        bevel: 28,
        dispersion: 3.0,
        frostLod: 0.85,
        edgeLod: 0.08,
        specular: 0.86,
        rimGlow: 0.72,
        tintOpacity: 0.07,
        refractionMode: 1.0,
    },
    prism: {
        refraction: 36,
        bevel: 22,
        dispersion: 9.6,
        frostLod: 1.65,
        edgeLod: 0.12,
        specular: 0.78,
        rimGlow: 0.78,
        tintOpacity: 0.12,
        refractionMode: -1.0,
    },
    smoked: {
        refraction: 22,
        bevel: 26,
        dispersion: 3.4,
        frostLod: 2.35,
        edgeLod: 0.28,
        specular: 0.58,
        rimGlow: 0.48,
        tintOpacity: 0.34,
        refractionMode: 1.0,
    },
};

const WALLPAPERS = [
    { id: 'solstice', label: 'Solstice' },
    { id: 'alpine', label: 'Alpine' },
    { id: 'grid', label: 'Optical Grid' },
    { id: 'system', label: 'OS Neutral' },
];

const state = {
    view: 'gallery',          // 'gallery' | 'settings' | 'wifi-detail' | 'profile' | 'controls'
    selectedNetwork: 'Studio-5G',
    wifiEnabled: true,
    autoJoin: true,
    lowDataMode: false,
    personalHotspot: false,
    vpnActive: true,
    activeAccount: 'Alex Mercer',
    controlTextSize: 135,
    controlBrightness: 76,
    controlScopeIndex: 0,

    formFactor: 'desktop',    // 'desktop' | 'iphone'
    theme: 'light',           // 'light' | 'dark'
    wallpaperIndex: 0,
    engineMode: 'webgl2',     // 'webgl2' | 'css'
    animateWave: false,
    searchQuery: '',
    sidebarVisible: true,
    opticsVisible: true,

    optics: {
        ...OPTICS_PRESETS.apple,
        sevenTap: false,
        shadow: 1.0,
    },

    // Spring-driven Tab Bar Droplet Physics (ref_15 / ref_16 / ref_19)
    droplet: {
        x: 4,
        w: 86,
        targetX: 4,
        targetW: 86,
        velX: 0,
        lift: 0,
        targetLift: 0,
        dragging: false,
        pointerOffsetX: 0,
    },

    // Scheduler & Animation Timing
    dirtyScene: true,
    dirtyPanels: true,
    keepAliveUntil: 0,
    rafId: 0,
    lastFrameMs: 0,
    smoothedFps: 60,
    startMs: performance.now(),
};

// -----------------------------------------------------------------------------
// [2] DOM References & Engine Initialization
// -----------------------------------------------------------------------------
const bodyEl = document.body;
const glassCanvas = document.getElementById('glass-canvas');
const stageScroller = document.getElementById('stage-scroller');
const stageContent = document.getElementById('stage-content');

const sidebarEl = document.getElementById('app-sidebar');
const opticsPanelEl = document.getElementById('optics-panel');
const btnShowSidebar = document.getElementById('btn-show-sidebar');
const btnCollapseSidebar = document.getElementById('btn-collapse-sidebar');
const btnToggleOptics = document.getElementById('btn-toggle-optics');
const btnCloseOptics = document.getElementById('btn-close-optics');
const btnNavBack = document.getElementById('btn-nav-back');
const crumbTitle = document.getElementById('crumb-title');

const btnFormFactor = document.getElementById('btn-form-factor');
const labelFormFactor = document.getElementById('label-form-factor');
const btnWallpaper = document.getElementById('btn-wallpaper');
const labelWallpaper = document.getElementById('label-wallpaper');
const btnTheme = document.getElementById('btn-theme');
const labelTheme = document.getElementById('label-theme');
const btnAnimateBg = document.getElementById('btn-animate-bg');

const searchPillEl = document.getElementById('search-pill');
const bottomSearchInput = document.getElementById('bottom-search-input');
const sidebarFilterInput = document.getElementById('sidebar-filter');

const tabDockEl = document.getElementById('tab-dock');
const tabDropletEl = document.getElementById('tab-droplet');
const toastPopover = document.getElementById('toast-popover');
const accountPopover = document.getElementById('account-popover');
const telemetryLine = document.getElementById('telemetry-line');

const glEngine = new LiquidGlassEngine(glassCanvas);
const sceneMirror = new SceneMirror();

if (!glEngine.isSupported()) {
    state.engineMode = 'css';
    bodyEl.dataset.engine = 'css';
    const engSwitch = document.getElementById('toggle-engine');
    if (engSwitch) {
        engSwitch.checked = false;
        engSwitch.disabled = true;
    }
}

// -----------------------------------------------------------------------------
// [3] View Templates (Apple-Style Floating Cards, Wi-Fi Settings, Profile, Controls)
// -----------------------------------------------------------------------------
const WIFI_NETWORKS = [
    { name: 'Studio-5G', detail: 'Connected \u2022 Wi-Fi 7', locked: true, active: true },
    { name: 'Nordic-Mesh', detail: '5 GHz', locked: true, active: false },
    { name: 'Glass-Lab-Internal', detail: '6 GHz', locked: true, active: false },
    { name: 'Cafe-Public', detail: 'Open', locked: false, active: false },
];

function escapeHtml(str) {
    return String(str).replace(/[&<>"']/g, (ch) => ({
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        "'": '&#39;',
    }[ch]));
}

function renderGalleryView() {
    const tiles = [
        { art: 'sunset', title: 'Sunset Refraction', sub: 'Scroll behind floating sidebar edge' },
        { art: 'ocean', title: 'Ocean Caustic', sub: '3-mip linear pyramid depth test' },
        { art: 'prism', title: 'Chromatic Dispersion', sub: 'High-contrast edge bevel test' },
        { art: 'emerald', title: 'Emerald Scattering', sub: 'Rec.709 adaptive specular check' },
        { art: 'graphite', title: 'Graphite Slate', sub: 'Dark-on-light rim legibility' },
        { art: 'aurora', title: 'Aurora Gradient', sub: 'Smooth C1 squircle corners' },
    ];
    const q = state.searchQuery.trim().toLowerCase();
    const filtered = q ? tiles.filter((t) => `${t.title} ${t.sub}`.toLowerCase().includes(q)) : tiles;

    return `
        <div class="hero-intro">
            <h1 id="view-heading" tabindex="-1" data-scene="heading">Spatial Liquid Glass Kit</h1>
            <p>Scroll content behind the floating sidebar, header pills, and bottom tab bar. Drag the Optics sliders on the right or flip <strong>WebGL2 Shader Engine</strong> off to compare directly against CSS <code>backdrop-filter</code>.</p>
        </div>

        <div class="gallery-grid">
            ${filtered.map((t) => `
                <article class="art-card" data-scene="art" data-art="${t.art}">
                    <span class="art-title">${escapeHtml(t.title)}</span>
                    <span class="art-sub">${escapeHtml(t.sub)}</span>
                </article>
            `).join('')}
        </div>

        <div class="section-header-label">Architecture Highlights</div>
        <div class="grouped-card" data-scene="card">
            <button type="button" class="grouped-row" data-route="settings">
                <span class="row-icon-badge" data-color="#0a84ff" style="background:#0a84ff">W</span>
                <span class="row-title">Open System Settings &amp; Wi-Fi Subpage</span>
                <span class="row-value">Grouped UI</span>
                <span class="row-chevron" aria-hidden="true">&#8250;</span>
            </button>
            <button type="button" class="grouped-row" data-route="profile">
                <span class="row-icon-badge" data-color="#8b5cf6" style="background:#8b5cf6">T</span>
                <span class="row-title">Open Messenger Profile &amp; Drag Tab Lens</span>
                <span class="row-value">113 badge</span>
                <span class="row-chevron" aria-hidden="true">&#8250;</span>
            </button>
            <button type="button" class="grouped-row" data-route="controls">
                <span class="row-icon-badge" data-color="#f97316" style="background:#f97316">C</span>
                <span class="row-title">Test Vertical Stepped Capsule Sliders</span>
                <span class="row-value">${state.controlTextSize}%</span>
                <span class="row-chevron" aria-hidden="true">&#8250;</span>
            </button>
        </div>
    `;
}

function renderSettingsView() {
    const q = state.searchQuery.trim().toLowerCase();
    const visibleNets = q
        ? WIFI_NETWORKS.filter((n) => n.name.toLowerCase().includes(q))
        : WIFI_NETWORKS;

    return `
        <h1 id="view-heading" tabindex="-1" class="ios-page-title" data-scene="heading">Wi-Fi</h1>

        <div class="grouped-card" data-scene="card">
            <div class="grouped-row" data-scene="row">
                <span class="row-icon-badge" data-color="#0a84ff" style="background:#0a84ff">W</span>
                <span class="row-title">Wi-Fi</span>
                <input id="sw-wifi-master" type="checkbox" switch class="native-switch" ${state.wifiEnabled ? 'checked' : ''} aria-label="Toggle Wi-Fi">
            </div>
            ${state.wifiEnabled ? `
                <button type="button" class="grouped-row" data-scene="row" data-open-network="Studio-5G">
                    <span class="row-icon-badge" data-color="#34c759" style="background:#34c759">&#10003;</span>
                    <span class="row-title">Studio-5G</span>
                    <span class="row-value">Wi-Fi 7</span>
                    <span class="row-chevron" aria-hidden="true">&#9432; &#8250;</span>
                </button>
            ` : ''}
        </div>
        <p class="section-caption">Known networks connect automatically. Notice how cards scroll continuously behind floating top and bottom glass pills.</p>

        ${state.wifiEnabled ? `
            <div class="section-header-label">Other Networks</div>
            <div class="grouped-card" data-scene="card">
                ${visibleNets.map((net) => `
                    <button type="button" class="grouped-row" data-scene="row" data-open-network="${escapeHtml(net.name)}">
                        <span class="row-icon-badge" data-color="#64748b" style="background:#64748b">${net.locked ? 'L' : 'O'}</span>
                        <span class="row-title">${escapeHtml(net.name)}</span>
                        <span class="row-value">${escapeHtml(net.detail)}</span>
                        <span class="row-chevron" aria-hidden="true">&#8250;</span>
                    </button>
                `).join('')}
            </div>
        ` : ''}

        <div class="section-header-label">Connectivity</div>
        <div class="grouped-card" data-scene="card">
            <div class="grouped-row" data-scene="row">
                <span class="row-icon-badge" data-color="#10b981" style="background:#10b981">H</span>
                <span class="row-title">Personal Hotspot</span>
                <input id="sw-hotspot" type="checkbox" switch class="native-switch" ${state.personalHotspot ? 'checked' : ''} aria-label="Personal Hotspot">
            </div>
            <div class="grouped-row" data-scene="row">
                <span class="row-icon-badge" data-color="#6366f1" style="background:#6366f1">V</span>
                <span class="row-title">Encrypted Tunnel (VPN)</span>
                <input id="sw-vpn" type="checkbox" switch class="native-switch" ${state.vpnActive ? 'checked' : ''} aria-label="Encrypted Tunnel">
            </div>
        </div>
    `;
}

function renderWifiDetailView() {
    const net = state.selectedNetwork;
    return `
        <h1 id="view-heading" tabindex="-1" class="ios-page-title" data-scene="heading">${escapeHtml(net)}</h1>

        <div class="grouped-card" data-scene="card">
            <button type="button" id="btn-forget-network" class="grouped-row no-icon" data-scene="row">
                <span class="row-title is-accent">Forget This Network</span>
            </button>
        </div>

        <div class="grouped-card" data-scene="card">
            <div class="grouped-row no-icon" data-scene="row">
                <span class="row-title">Auto-Join</span>
                <input id="sw-autojoin" type="checkbox" switch class="native-switch" ${state.autoJoin ? 'checked' : ''} aria-label="Auto-Join">
            </div>
            <div class="grouped-row no-icon" data-scene="row">
                <span class="row-title">Password</span>
                <span class="row-value">&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;</span>
            </div>
        </div>

        <div class="grouped-card" data-scene="card">
            <div class="grouped-row no-icon" data-scene="row">
                <span class="row-title">Low Data Mode</span>
                <input id="sw-lowdata" type="checkbox" switch class="native-switch" ${state.lowDataMode ? 'checked' : ''} aria-label="Low Data Mode">
            </div>
        </div>
        <p class="section-caption">Low Data Mode helps reduce background network traffic and pauses automatic large asset prefetches over this network.</p>

        <div class="grouped-card" data-scene="card">
            <div class="grouped-row no-icon" data-scene="row">
                <span class="row-title">Private Wi-Fi Address</span>
                <span class="row-value">Rotating</span>
                <span class="row-chevron" aria-hidden="true">&#8250;</span>
            </div>
            <div class="grouped-row no-icon" data-scene="row">
                <span class="row-title">Hardware Address</span>
                <span class="row-value">62:92:EA:D2:FA:84</span>
            </div>
        </div>
    `;
}

function renderProfileView() {
    const rows = [
        { color: '#3b82f6', icon: 'S', title: 'Saved Items', val: '14' },
        { color: '#22c55e', icon: 'R', title: 'Recent Calls', val: '' },
        { color: '#f59e0b', icon: 'D', title: 'Linked Devices', val: '7' },
        { color: '#06b6d4', icon: 'F', title: 'Workspace Folders', val: '4' },
        { color: '#ef4444', icon: 'N', title: 'Notifications & Sounds', val: '' },
        { color: '#64748b', icon: 'P', title: 'Privacy & Security', val: '' },
        { color: '#10b981', icon: 'M', title: 'Data & Storage', val: '2.4 GB' },
        { color: '#6366f1', icon: 'A', title: 'Appearance & Glass', val: 'WebGL2' },
    ];

    return `
        <section class="profile-hero" data-scene="hero" aria-labelledby="view-heading">
            <div class="hero-avatar" aria-hidden="true"></div>
            <h2 id="view-heading" tabindex="-1" data-scene="heading" class="is-light">${escapeHtml(state.activeAccount)}</h2>
            <p>@liquid_glass &bull; Drag bottom tab bar pills to inspect lens droplet refraction</p>
        </section>

        <div class="grouped-card" data-scene="card">
            <button type="button" class="grouped-row no-icon" data-scene="row" commandfor="toast-popover" command="toggle-popover">
                <span class="row-title is-accent">Trigger Floating Notification Toast</span>
                <span class="row-value">Popover API</span>
            </button>
            <button type="button" class="grouped-row no-icon" data-scene="row" commandfor="account-popover" command="toggle-popover">
                <span class="row-title is-accent">Switch Active Account Menu</span>
                <span class="row-value">${escapeHtml(state.activeAccount)}</span>
            </button>
        </div>

        <div class="grouped-card" data-scene="card">
            ${rows.map((r) => `
                <div class="grouped-row" data-scene="row">
                    <span class="row-icon-badge" data-color="${r.color}" style="background:${r.color}">${r.icon}</span>
                    <span class="row-title">${escapeHtml(r.title)}</span>
                    ${r.val ? `<span class="row-value">${escapeHtml(r.val)}</span>` : ''}
                    <span class="row-chevron" aria-hidden="true">&#8250;</span>
                </div>
            `).join('')}
        </div>
    `;
}

function renderControlsView() {
    const textFill = (state.controlTextSize - 80) / 100; // 80%..180%
    const brightFill = state.controlBrightness / 100;

    return `
        <h1 id="view-heading" tabindex="-1" class="ios-page-title" data-scene="heading">Control Surfaces</h1>

        <section class="control-stage">
            <div class="control-sliders-row">
                <div class="capsule-slider-col">
                    <span class="cap-title">Text Size</span>
                    <span class="cap-val" id="cap-text-val">${state.controlTextSize} %</span>
                    <div class="vertical-capsule-meter"
                         data-scene="capsule-meter"
                         data-meter="text"
                         data-fill="${textFill.toFixed(2)}"
                         role="slider"
                         tabindex="0"
                         aria-label="Text Size"
                         aria-valuemin="80"
                         aria-valuemax="180"
                         aria-valuenow="${state.controlTextSize}">
                        <div class="vertical-capsule-fill" style="block-size:${(textFill * 100).toFixed(1)}%"></div>
                        <div class="stepped-lines" aria-hidden="true">
                            <span></span><span></span><span></span><span></span><span></span>
                        </div>
                    </div>
                </div>

                <div class="capsule-slider-col">
                    <span class="cap-title">Brightness</span>
                    <span class="cap-val" id="cap-bright-val">${state.controlBrightness} %</span>
                    <div class="vertical-capsule-meter"
                         data-scene="capsule-meter"
                         data-meter="brightness"
                         data-fill="${brightFill.toFixed(2)}"
                         role="slider"
                         tabindex="0"
                         aria-label="Display Brightness"
                         aria-valuemin="10"
                         aria-valuemax="100"
                         aria-valuenow="${state.controlBrightness}">
                        <div class="vertical-capsule-fill" style="block-size:${(brightFill * 100).toFixed(1)}%"></div>
                    </div>
                </div>
            </div>

            <div class="segmented-pill-bar" data-active-index="${state.controlScopeIndex}" role="group" aria-label="Control Target Scope">
                <div class="segmented-knob" aria-hidden="true"></div>
                <button type="button" class="segmented-opt ${state.controlScopeIndex === 0 ? 'is-selected' : ''}" data-scope="0">This App Only</button>
                <button type="button" class="segmented-opt ${state.controlScopeIndex === 1 ? 'is-selected' : ''}" data-scope="1">All Workspaces</button>
            </div>
        </section>

        <div class="grouped-card" data-scene="card">
            <div class="grouped-row" data-scene="row">
                <span class="row-icon-badge" data-color="#3b82f6" style="background:#3b82f6">A</span>
                <span class="row-title">Adaptive Capsule Stepped Snapping</span>
                <span class="row-value">6 Steps</span>
            </div>
            <div class="grouped-row" data-scene="row">
                <span class="row-icon-badge" data-color="#a855f7" style="background:#a855f7">L</span>
                <span class="row-title">Drag Tab Bar Droplet Below</span>
                <span class="row-value">Spring Lens</span>
            </div>
        </div>
    `;
}

function renderActiveView(focusHeading = false) {
    let html = '';
    let crumb = 'Spatial Gallery';
    let showBack = false;
    let showSearch = false;

    if (state.view === 'gallery') {
        html = renderGalleryView();
        crumb = 'Spatial Gallery';
    } else if (state.view === 'settings') {
        html = renderSettingsView();
        crumb = 'Settings \u2022 Wi-Fi';
        showSearch = true;
    } else if (state.view === 'wifi-detail') {
        html = renderWifiDetailView();
        crumb = state.selectedNetwork;
        showBack = true;
    } else if (state.view === 'profile') {
        html = renderProfileView();
        crumb = 'Messenger Profile';
    } else if (state.view === 'controls') {
        html = renderControlsView();
        crumb = 'Control Surfaces';
    }

    stageContent.innerHTML = html;
    crumbTitle.textContent = crumb;
    btnNavBack.classList.toggle('is-hidden', !showBack);
    searchPillEl.classList.toggle('is-hidden', !showSearch);

    // Sync active highlights across sidebar & tab dock
    const logicalTab = state.view === 'wifi-detail' ? 'settings' : state.view;
    document.querySelectorAll('.sidebar-item[data-nav]').forEach((el) => {
        el.classList.toggle('is-active', el.dataset.nav === logicalTab);
    });
    document.querySelectorAll('.tab-btn[data-tab]').forEach((el) => {
        el.classList.toggle('is-active', el.dataset.tab === logicalTab);
    });

    bindDynamicStageHandlers();
    snapDropletTargetToActiveTab(false);
    keepAliveFor(340);
    markDirty(true, true);

    if (focusHeading) {
        document.getElementById('view-heading')?.focus({ preventScroll: true });
    }
}

function navigateTo(nextView, { useTransition = true, focusHeading = true } = {}) {
    if (state.view === nextView) return;
    const commit = () => {
        state.view = nextView;
        renderActiveView(focusHeading);
        stageScroller.scrollTop = 0;
    };

    if (useTransition && document.startViewTransition && !window.matchMedia('(prefers-reduced-motion: reduce)').matches) {
        const vt = document.startViewTransition(commit);
        vt.finished.finally(() => {
            if (focusHeading) {
                document.getElementById('view-heading')?.focus({ preventScroll: true });
            }
            markDirty(true, true);
        });
    } else {
        commit();
    }
}

function bindDynamicStageHandlers() {
    stageContent.querySelectorAll('[data-route]').forEach((btn) => {
        btn.addEventListener('click', () => navigateTo(btn.dataset.route));
    });

    stageContent.querySelectorAll('[data-open-network]').forEach((btn) => {
        btn.addEventListener('click', () => {
            state.selectedNetwork = btn.dataset.openNetwork;
            navigateTo('wifi-detail');
        });
    });

    const swWifi = document.getElementById('sw-wifi-master');
    if (swWifi) {
        swWifi.addEventListener('change', () => {
            state.wifiEnabled = swWifi.checked;
            renderActiveView(false);
        });
    }
    const swHotspot = document.getElementById('sw-hotspot');
    if (swHotspot) swHotspot.addEventListener('change', () => { state.personalHotspot = swHotspot.checked; markDirty(true, false); });
    const swVpn = document.getElementById('sw-vpn');
    if (swVpn) swVpn.addEventListener('change', () => { state.vpnActive = swVpn.checked; markDirty(true, false); });
    const swAutoJoin = document.getElementById('sw-autojoin');
    if (swAutoJoin) swAutoJoin.addEventListener('change', () => { state.autoJoin = swAutoJoin.checked; markDirty(true, false); });
    const swLowData = document.getElementById('sw-lowdata');
    if (swLowData) swLowData.addEventListener('change', () => { state.lowDataMode = swLowData.checked; markDirty(true, false); });

    const forgetBtn = document.getElementById('btn-forget-network');
    if (forgetBtn) {
        forgetBtn.addEventListener('click', () => {
            showToastNotification('Network Reset', `Removed auto-join credentials for ${state.selectedNetwork}.`);
            navigateTo('settings');
        });
    }

    // Bind interactive vertical capsule meters (ref_02)
    stageContent.querySelectorAll('.vertical-capsule-meter').forEach((meter) => {
        const updateFromY = (clientY) => {
            const rect = meter.getBoundingClientRect();
            const frac = Math.max(0, Math.min(1, (rect.bottom - clientY) / rect.height));
            if (meter.dataset.meter === 'text') {
                // 6 stepped snap levels matching ref_01/ref_02
                const steps = [85, 100, 115, 125, 135, 150, 170];
                const idx = Math.round(frac * (steps.length - 1));
                state.controlTextSize = steps[idx];
                const snappedFrac = (state.controlTextSize - 80) / 100;
                meter.dataset.fill = snappedFrac.toFixed(2);
                meter.querySelector('.vertical-capsule-fill').style.blockSize = `${(snappedFrac * 100).toFixed(1)}%`;
                const valEl = document.getElementById('cap-text-val');
                if (valEl) valEl.textContent = `${state.controlTextSize} %`;
            } else {
                state.controlBrightness = Math.round(10 + frac * 90);
                const cf = state.controlBrightness / 100;
                meter.dataset.fill = cf.toFixed(2);
                meter.querySelector('.vertical-capsule-fill').style.blockSize = `${(cf * 100).toFixed(1)}%`;
                const valEl = document.getElementById('cap-bright-val');
                if (valEl) valEl.textContent = `${state.controlBrightness} %`;
            }
            markDirty(true, false);
        };

        meter.addEventListener('pointerdown', (e) => {
            meter.setPointerCapture(e.pointerId);
            updateFromY(e.clientY);
        });
        meter.addEventListener('pointermove', (e) => {
            if (meter.hasPointerCapture(e.pointerId)) {
                updateFromY(e.clientY);
            }
        });
    });

    stageContent.querySelectorAll('.segmented-opt').forEach((btn) => {
        btn.addEventListener('click', () => {
            state.controlScopeIndex = Number(btn.dataset.scope || 0);
            const bar = btn.closest('.segmented-pill-bar');
            if (bar) bar.dataset.activeIndex = String(state.controlScopeIndex);
            bar?.querySelectorAll('.segmented-opt').forEach((o) => {
                o.classList.toggle('is-selected', Number(o.dataset.scope) === state.controlScopeIndex);
            });
            markDirty(true, false);
        });
    });

    // Re-bind fallback command buttons dynamically inserted in stageContent
    wireCommandFallback(stageContent);
}

// -----------------------------------------------------------------------------
// [4] Popover Helpers & Feature-Gated Invoker Fallback
// -----------------------------------------------------------------------------
function togglePopoverSafe(popoverEl, forceShow) {
    if (!popoverEl) return;
    if (typeof popoverEl.showPopover === 'function') {
        const isOpen = popoverEl.matches(':popover-open');
        if (forceShow === true || (!isOpen && forceShow === undefined)) {
            if (!isOpen) popoverEl.showPopover();
        } else if (forceShow === false || isOpen) {
            if (isOpen) popoverEl.hidePopover();
        }
    } else {
        const shouldOpen = forceShow ?? !popoverEl.classList.contains(':popover-open');
        popoverEl.classList.toggle(':popover-open', shouldOpen);
    }
    keepAliveFor(340);
    markDirty(false, true);
}

function showToastNotification(title, message) {
    const titleEl = toastPopover.querySelector('.toast-title-row strong');
    const bodyElMsg = toastPopover.querySelector('.toast-body p');
    if (titleEl) titleEl.textContent = title;
    if (bodyElMsg) bodyElMsg.textContent = message;
    togglePopoverSafe(toastPopover, true);
    clearTimeout(showToastNotification._timer);
    showToastNotification._timer = setTimeout(() => {
        togglePopoverSafe(toastPopover, false);
    }, 3800);
}

function wireCommandFallback(root = document) {
    if ('commandForElement' in HTMLButtonElement.prototype) return;
    root.querySelectorAll('button[commandfor][command]').forEach((btn) => {
        if (btn.dataset.cmdBound) return;
        btn.dataset.cmdBound = '1';
        btn.addEventListener('click', () => {
            const target = document.getElementById(btn.getAttribute('commandfor'));
            const cmd = btn.getAttribute('command');
            if (!target) return;
            if (cmd === 'toggle-popover') togglePopoverSafe(target);
            else if (cmd === 'show-popover') togglePopoverSafe(target, true);
            else if (cmd === 'hide-popover') togglePopoverSafe(target, false);
        });
    });
}
wireCommandFallback(document);

for (const pop of [toastPopover, accountPopover]) {
    pop.addEventListener('toggle', () => {
        keepAliveFor(340);
        markDirty(false, true);
    });
}

// -----------------------------------------------------------------------------
// [5] Interactive Physics Tab Bar Droplet (ref_15 / ref_16 / ref_19)
// -----------------------------------------------------------------------------
function getTabRectInDock(tabName) {
    const dockRect = tabDockEl.getBoundingClientRect();
    const btn = tabDockEl.querySelector(`.tab-btn[data-tab="${tabName}"]`);
    if (!btn || dockRect.width < 10) return { x: 4, w: 86 };
    const r = btn.getBoundingClientRect();
    return {
        x: r.left - dockRect.left,
        w: r.width,
    };
}

function snapDropletTargetToActiveTab(immediate = false) {
    const logicalTab = state.view === 'wifi-detail' ? 'settings' : state.view;
    const target = getTabRectInDock(logicalTab);
    state.droplet.targetX = target.x;
    state.droplet.targetW = target.w;
    if (immediate) {
        state.droplet.x = target.x;
        state.droplet.w = target.w;
        state.droplet.velX = 0;
    }
    updateDropletDom();
}

function updateDropletDom() {
    const d = state.droplet;
    const dockRect = tabDockEl.getBoundingClientRect();
    const scaleBoost = 1 + d.lift * 0.16;
    const extraY = -d.lift * 4;
    tabDropletEl.style.inlineSize = `${Math.max(54, d.w).toFixed(1)}px`;
    tabDropletEl.style.transform = `translate3d(${d.x.toFixed(2)}px, ${extraY.toFixed(2)}px, 0) scale(${scaleBoost.toFixed(3)})`;
    tabDropletEl.classList.toggle('is-lifted', d.lift > 0.08);

    // Highlight whichever tab center the droplet currently hovers
    const dropCenter = d.x + d.w * 0.5;
    tabDockEl.querySelectorAll('.tab-btn').forEach((btn) => {
        const br = btn.getBoundingClientRect();
        const cx = br.left - dockRect.left + br.width * 0.5;
        const near = Math.abs(dropCenter - cx) < br.width * 0.48 && d.lift > 0.15;
        btn.classList.toggle('is-lifted', near);
    });
}

function tabAtDockCenterX(cxLocal) {
    const dockRect = tabDockEl.getBoundingClientRect();
    let best = 'gallery';
    let bestDist = Infinity;
    tabDockEl.querySelectorAll('.tab-btn').forEach((btn) => {
        const br = btn.getBoundingClientRect();
        const center = br.left - dockRect.left + br.width * 0.5;
        const dist = Math.abs(center - cxLocal);
        if (dist < bestDist) {
            bestDist = dist;
            best = btn.dataset.tab;
        }
    });
    return best;
}

tabDockEl.addEventListener('pointerdown', (e) => {
    const btn = e.target.closest('.tab-btn');
    if (!btn) return;
    tabDockEl.setPointerCapture(e.pointerId);
    const dockRect = tabDockEl.getBoundingClientRect();
    state.droplet.dragging = true;
    state.droplet.targetLift = 1.0;
    const localX = e.clientX - dockRect.left;
    state.droplet.pointerOffsetX = localX - state.droplet.x;
    // If clicking far from current thumb, allow snapping smoothly toward pressed point
    if (localX < state.droplet.x || localX > state.droplet.x + state.droplet.w) {
        state.droplet.targetX = Math.max(4, Math.min(dockRect.width - state.droplet.w - 4, localX - state.droplet.w * 0.5));
        state.droplet.pointerOffsetX = state.droplet.w * 0.5;
    }
    keepAliveFor(250);
    markDirty(true, true);
});

tabDockEl.addEventListener('pointermove', (e) => {
    if (!state.droplet.dragging) return;
    const dockRect = tabDockEl.getBoundingClientRect();
    const minX = 4;
    const maxX = Math.max(4, dockRect.width - state.droplet.w - 4);
    const rawX = e.clientX - dockRect.left - state.droplet.pointerOffsetX;
    state.droplet.targetX = Math.max(minX, Math.min(maxX, rawX));
    markDirty(true, true);
});

function releaseTabDroplet(e) {
    if (!state.droplet.dragging) return;
    if (tabDockEl.hasPointerCapture(e.pointerId)) {
        tabDockEl.releasePointerCapture(e.pointerId);
    }
    state.droplet.dragging = false;
    state.droplet.targetLift = 0.0;
    const dropCenter = state.droplet.targetX + state.droplet.w * 0.5;
    const targetTab = tabAtDockCenterX(dropCenter);
    if (targetTab && targetTab !== state.view) {
        navigateTo(targetTab, { useTransition: true, focusHeading: false });
    } else {
        snapDropletTargetToActiveTab(false);
    }
    keepAliveFor(420);
    markDirty(true, true);
}

tabDockEl.addEventListener('pointerup', releaseTabDroplet);
tabDockEl.addEventListener('pointercancel', releaseTabDroplet);

// -----------------------------------------------------------------------------
// [6] Optics Tuner Controls, Presets & Global Event Bindings
// -----------------------------------------------------------------------------
function syncRangeSliderVisual(input) {
    input.style.setProperty('--attr-min', input.min || '0');
    input.style.setProperty('--attr-max', input.max || '100');
    input.style.setProperty('--control-value', input.value);
    const outId = input.id.replace('opt-', 'val-');
    const out = document.getElementById(outId);
    if (out) {
        const unit = input.dataset.unit ? ` ${input.dataset.unit}` : '';
        out.textContent = `${Number(input.value).toFixed(input.step.includes('.') ? 2 : 0)}${unit}`;
    }
}

const SLIDER_KEY_MAP = {
    'opt-refraction': 'refraction',
    'opt-bevel': 'bevel',
    'opt-dispersion': 'dispersion',
    'opt-frost': 'frostLod',
    'opt-edge': 'edgeLod',
    'opt-specular': 'specular',
    'opt-rimglow': 'rimGlow',
    'opt-tint': 'tintOpacity',
};

function applyPreset(presetKey) {
    const preset = OPTICS_PRESETS[presetKey];
    if (!preset) return;
    Object.assign(state.optics, preset);

    // Clear active chip highlight
    document.querySelectorAll('.preset-chip').forEach((chip) => {
        chip.classList.toggle('is-active', chip.dataset.preset === presetKey);
    });

    // Update dark theme suggestion for smoked preset cleanly if chosen explicitly
    if (presetKey === 'smoked' && state.theme !== 'dark') {
        setTheme('dark');
    } else if (presetKey === 'apple' && state.theme !== 'light') {
        setTheme('light');
    }

    const dirSwitch = document.getElementById('toggle-lens-dir');
    if (dirSwitch) dirSwitch.checked = state.optics.refractionMode < 0;

    for (const [id, key] of Object.entries(SLIDER_KEY_MAP)) {
        const el = document.getElementById(id);
        if (el) {
            el.value = String(state.optics[key]);
            syncRangeSliderVisual(el);
        }
    }
    markDirty(false, true);
}

for (const [id, key] of Object.entries(SLIDER_KEY_MAP)) {
    const el = document.getElementById(id);
    if (!el) continue;
    syncRangeSliderVisual(el);
    el.addEventListener('input', () => {
        state.optics[key] = parseFloat(el.value);
        syncRangeSliderVisual(el);
        document.querySelectorAll('.preset-chip').forEach((c) => c.classList.remove('is-active'));
        markDirty(false, true);
    });
}

document.querySelectorAll('.preset-chip[data-preset]').forEach((chip) => {
    chip.addEventListener('click', () => applyPreset(chip.dataset.preset));
});

const toggleEngine = document.getElementById('toggle-engine');
toggleEngine.addEventListener('change', () => {
    state.engineMode = (toggleEngine.checked && glEngine.isSupported()) ? 'webgl2' : 'css';
    bodyEl.dataset.engine = state.engineMode;
    markDirty(true, true);
});

const toggleLensDir = document.getElementById('toggle-lens-dir');
toggleLensDir.addEventListener('change', () => {
    state.optics.refractionMode = toggleLensDir.checked ? -1.0 : 1.0;
    markDirty(false, true);
});

const toggle7Tap = document.getElementById('toggle-7tap');
toggle7Tap.addEventListener('change', () => {
    state.optics.sevenTap = toggle7Tap.checked;
    markDirty(false, true);
});

function setTheme(themeName) {
    state.theme = themeName;
    document.documentElement.dataset.theme = themeName;
    bodyEl.dataset.theme = themeName;
    labelTheme.textContent = themeName === 'dark' ? 'Dark' : 'Light';
    markDirty(true, true);
}

btnTheme.addEventListener('click', () => {
    setTheme(state.theme === 'light' ? 'dark' : 'light');
});

btnFormFactor.addEventListener('click', () => {
    state.formFactor = state.formFactor === 'desktop' ? 'iphone' : 'desktop';
    bodyEl.dataset.formFactor = state.formFactor;
    labelFormFactor.textContent = state.formFactor === 'iphone' ? 'iPhone 26' : 'Desktop';
    setTimeout(() => {
        snapDropletTargetToActiveTab(true);
        markDirty(true, true);
    }, 30);
    keepAliveFor(380);
});

btnWallpaper.addEventListener('click', () => {
    state.wallpaperIndex = (state.wallpaperIndex + 1) % WALLPAPERS.length;
    labelWallpaper.textContent = WALLPAPERS[state.wallpaperIndex].label;
    markDirty(true, false);
});

btnAnimateBg.addEventListener('click', () => {
    state.animateWave = !state.animateWave;
    btnAnimateBg.setAttribute('aria-pressed', String(state.animateWave));
    btnAnimateBg.textContent = state.animateWave ? 'Live' : 'Wave';
    if (state.animateWave && WALLPAPERS[state.wallpaperIndex].id === 'system') {
        state.wallpaperIndex = 0; // Switch to solstice animated orbs automatically so user sees live wave motion
        labelWallpaper.textContent = WALLPAPERS[0].label;
    }
    markDirty(true, true);
});

function setSidebarVisible(show) {
    state.sidebarVisible = show;
    sidebarEl.classList.toggle('is-collapsed', !show);
    bodyEl.classList.toggle('sidebar-is-hidden', !show);
    btnShowSidebar.classList.toggle('is-hidden', show);
    keepAliveFor(360);
    markDirty(true, true);
}

btnCollapseSidebar.addEventListener('click', () => setSidebarVisible(false));
btnShowSidebar.addEventListener('click', () => setSidebarVisible(true));

function setOpticsVisible(show) {
    state.opticsVisible = show;
    opticsPanelEl.classList.toggle('is-collapsed', !show);
    bodyEl.classList.toggle('optics-is-hidden', !show);
    btnToggleOptics.setAttribute('aria-expanded', String(show));
    keepAliveFor(360);
    markDirty(true, true);
}

btnToggleOptics.addEventListener('click', () => setOpticsVisible(!state.opticsVisible));
btnCloseOptics.addEventListener('click', () => setOpticsVisible(false));

btnNavBack.addEventListener('click', () => {
    if (state.view === 'wifi-detail') {
        navigateTo('settings');
    }
});

document.querySelectorAll('.sidebar-item[data-nav]').forEach((btn) => {
    btn.addEventListener('click', () => navigateTo(btn.dataset.nav));
});

const onSearch = (val) => {
    state.searchQuery = val;
    if (sidebarFilterInput.value !== val) sidebarFilterInput.value = val;
    if (bottomSearchInput.value !== val) bottomSearchInput.value = val;
    renderActiveView(false);
};
sidebarFilterInput.addEventListener('input', () => onSearch(sidebarFilterInput.value));
bottomSearchInput.addEventListener('input', () => onSearch(bottomSearchInput.value));

accountPopover.querySelectorAll('[data-account-select]').forEach((btn) => {
    btn.addEventListener('click', () => {
        state.activeAccount = btn.dataset.accountSelect;
        togglePopoverSafe(accountPopover, false);
        renderActiveView(false);
        showToastNotification('Account Switched', `Active workspace set to ${state.activeAccount}.`);
    });
});

stageScroller.addEventListener('scroll', () => {
    markDirty(true, false);
}, { passive: true });

function syncResponsiveOverlayModes(initial = false) {
    const w = window.innerWidth;
    const compactOptics = w < 1200;
    const compactSidebar = w < 860;
    bodyEl.classList.toggle('optics-overlay-mode', compactOptics);
    bodyEl.classList.toggle('sidebar-overlay-mode', compactSidebar);
    if (initial) {
        if (compactOptics) setOpticsVisible(false);
        if (compactSidebar) setSidebarVisible(false);
    }
}

window.addEventListener('resize', () => {
    syncResponsiveOverlayModes(false);
    snapDropletTargetToActiveTab(true);
    keepAliveFor(200);
    markDirty(true, true);
});

// -----------------------------------------------------------------------------
// [7] DOM Glass Panel Collector & Compositor Loop (Idle-Zero Dirty Scheduler)
// -----------------------------------------------------------------------------
function getGlassMaterialForRole(role, isDark) {
    const t = state.optics.tintOpacity;
    if (role === 'sidebar' || role === 'optics-panel') {
        // Large navigation panels use creamy readable frosted glass with gentle rim bevel
        return {
            tint: isDark
                ? [0.13, 0.16, 0.23, Math.max(0.54, t + 0.26)]
                : [0.97, 0.98, 1.00, Math.max(0.52, t + 0.28)],
            panelOptics: [0.62, 2.55, 0.72, isDark ? 0.02 : 0.06],
        };
    }
    if (role === 'tab-dock' || role === 'search-pill') {
        return {
            tint: isDark
                ? [0.16, 0.19, 0.26, Math.max(0.46, t + 0.20)]
                : [0.98, 0.985, 1.00, Math.max(0.48, t + 0.26)],
            panelOptics: [0.90, 0.0, 0.65, isDark ? 0.02 : 0.08],
        };
    }
    if (role === 'tab-droplet') {
        return {
            tint: isDark ? [0.88, 0.93, 1.0, 0.08] : [1.0, 1.0, 1.0, 0.10],
            panelOptics: [1.35, 0.0, 0.85, isDark ? 0.01 : 0.06],
        };
    }
    if (role === 'toast' || role === 'account-popover') {
        return {
            tint: isDark
                ? [0.15, 0.18, 0.26, Math.max(0.42, t + 0.18)]
                : [0.98, 0.985, 1.00, Math.max(0.46, t + 0.24)],
            panelOptics: [1.0, 1.4, 0.80, isDark ? 0.02 : 0.08],
        };
    }
    // Floating pill buttons & circular navigation controls: pure responsive Crystal Liquid Glass
    return {
        tint: isDark
            ? [0.15, 0.18, 0.25, Math.max(0.32, t + 0.10)]
            : [0.98, 0.985, 1.00, Math.max(0.28, t + 0.10)],
        panelOptics: [1.0, 0.0, 0.65, isDark ? 0.02 : 0.07],
    };
}

function collectVisibleGlassPanels() {
    const panels = [];
    const isDark = state.theme === 'dark';
    const els = document.querySelectorAll('[data-glass]');

    for (const el of els) {
        if (el.classList.contains('is-hidden') || el.classList.contains('is-collapsed')) continue;

        // Top-layer Popover visibility test
        if (el.hasAttribute('popover')) {
            const isOpen = el.matches(':popover-open') || el.classList.contains(':popover-open');
            if (!isOpen) continue;
        }

        const r = el.getBoundingClientRect();
        if (r.width < 2 || r.height < 2) continue;

        const role = el.dataset.glass;
        const radius = parseFloat(el.dataset.glassRadius || '22');
        const layer = parseFloat(el.dataset.glassLayer || '1');

        const isDroplet = role === 'tab-droplet';
        const d = state.droplet;
        // At rest, CSS draws the sunken soft capsule indicator inside #tab-dock cleanly;
        // WebGL takes over the droplet dynamically as it inflates on press/drag!
        if (isDroplet && d.lift <= 0.08) continue;

        const mat = getGlassMaterialForRole(role, isDark);

        panels.push({
            id: role,
            x: r.left,
            y: r.top,
            w: r.width,
            h: r.height,
            radius,
            smoothness: isDroplet ? 0.88 : 0.64,
            ellipseMorph: isDroplet ? d.lift * 0.58 : 0.0,
            lensBoost: isDroplet ? (d.lift * 0.92) : 0.0,
            opacity: isDroplet ? Math.min(1.0, (d.lift - 0.08) / 0.28) : 1.0,
            zLayer: layer,
            tint: mat.tint,
            panelOptics: mat.panelOptics,
        });
    }
    return panels;
}

function keepAliveFor(ms) {
    state.keepAliveUntil = Math.max(state.keepAliveUntil, performance.now() + ms);
    scheduleTick();
}

function markDirty(scene = false, panels = false) {
    if (scene) state.dirtyScene = true;
    if (panels) state.dirtyPanels = true;
    scheduleTick();
}

function scheduleTick() {
    if (state.rafId) return;
    state.rafId = requestAnimationFrame(frameStep);
}

function frameStep(nowMs) {
    state.rafId = 0;
    const dt = state.lastFrameMs ? Math.min(0.05, (nowMs - state.lastFrameMs) * 0.001) : 0.016;
    state.lastFrameMs = nowMs;
    if (dt > 0.001) {
        state.smoothedFps = state.smoothedFps * 0.88 + (1.0 / dt) * 0.12;
    }

    // Advance spring physics for the interactive Tab Bar Droplet
    const dr = state.droplet;
    const dx = dr.targetX - dr.x;
    const force = dx * 280 - dr.velX * 26;
    dr.velX += force * dt;
    dr.x += dr.velX * dt;
    dr.w += (dr.targetW - dr.w) * Math.min(1, dt * 16);
    dr.lift += (dr.targetLift - dr.lift) * Math.min(1, dt * 15);

    const springMoving = Math.abs(dx) > 0.15 || Math.abs(dr.velX) > 0.25 || Math.abs(dr.targetLift - dr.lift) > 0.008;
    if (springMoving) {
        updateDropletDom();
        state.dirtyPanels = true;
        if (dr.lift > 0.05) state.dirtyScene = true;
    }

    const keepAnimating = state.animateWave || nowMs < state.keepAliveUntil || springMoving;
    const viewportCss = { w: window.innerWidth, h: window.innerHeight };
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const timeSec = (nowMs - state.startMs) * 0.001;

    if (state.engineMode === 'webgl2' && glEngine.isSupported()) {
        if (state.dirtyScene || state.animateWave) {
            const tabDockRect = tabDockEl.getBoundingClientRect();
            const mirrorCanvas = sceneMirror.render({
                viewportCss,
                dpr,
                stageScroller,
                theme: state.theme,
                wallpaper: WALLPAPERS[state.wallpaperIndex].id,
                formFactor: state.formFactor,
                timeSec,
                tabDockRect,
                dropletActive: dr.lift > 0.12,
            });
            glEngine.uploadSceneAndBuildPyramid(mirrorCanvas);
            state.dirtyScene = false;
            state.dirtyPanels = true;
        }

        if (state.dirtyPanels || state.animateWave || keepAnimating) {
            const panels = collectVisibleGlassPanels();
            glEngine.renderPanels(panels, state.optics, viewportCss, dpr, timeSec);
            state.dirtyPanels = false;

            if (telemetryLine) {
                const status = keepAnimating ? `${Math.round(state.smoothedFps)} fps` : 'Idle (0% GPU)';
                const tapMode = state.optics.sevenTap ? '7-tap' : '3-tap';
                telemetryLine.textContent = `${glEngine.tier.toUpperCase()} \u2022 ${panels.length} pills \u2022 ${tapMode} \u2022 ${status}`;
            }
        }
    } else if (telemetryLine) {
        telemetryLine.textContent = 'CSS backdrop-filter Fallback (A/B Compare)';
    }

    if (keepAnimating || state.dirtyScene || state.dirtyPanels) {
        scheduleTick();
    }
}

// -----------------------------------------------------------------------------
// [8] Boot Testbed
// -----------------------------------------------------------------------------
syncResponsiveOverlayModes(true);
renderActiveView(false);
requestAnimationFrame(() => {
    snapDropletTargetToActiveTab(true);
    markDirty(true, true);
});
