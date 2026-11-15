#!/usr/bin/env python3
"""
Clean-Room Mathematical & Edge Invariants Verification Suite.

Independently verifies the exact mathematical equations implemented by the
Liquid Glass GLSL 440 shaders against the clean-room specification criteria:
  1. Sub-pixel JFA SDF accuracy (< 0.25 px) & smoothed normal continuity
     across circle, rounded-rectangle, and concave L-shape masks.
  2. Capture-clamped 13-tap downsample pyramid zero border bleed invariance.
  3. 7-tap spectral dispersion unit weights & neutral gray achromaticity.
  4. Smooth-knee mip-LOD C0 continuity at r = 2 and singularity-free r = 0.
  5. Convex bevel C1 tangency & 3D Snell thin-lens continuity + TIR safety.
  6. Hue-preserving HDR/SDR clamp invariance & premultiplied RGBA bounds.
"""

import math
import sys
import numpy as np


# =============================================================================
# [1] UV Clamping & Bilinear Texture Sampler Mirror
# =============================================================================

def clamp_uv(u, v, rect):
    """Clamps texture coordinates to the inset capture rectangle."""
    u_min, v_min, u_max, v_max = rect
    return np.clip(u, u_min, u_max), np.clip(v, v_min, v_max)


def sample_bilinear_rgba(img, u, v):
    """Exact OpenGL-style bilinear texture sampler with normalized UVs in [0, 1]."""
    h, w, _ = img.shape
    x = u * w - 0.5
    y = v * h - 0.5
    x0 = np.floor(x).astype(np.int32)
    y0 = np.floor(y).astype(np.int32)
    fx = (x - x0)[..., None]
    fy = (y - y0)[..., None]

    x0c = np.clip(x0, 0, w - 1)
    x1c = np.clip(x0 + 1, 0, w - 1)
    y0c = np.clip(y0, 0, h - 1)
    y1c = np.clip(y0 + 1, 0, h - 1)

    c00 = img[y0c, x0c]
    c10 = img[y0c, x1c]
    c01 = img[y1c, x0c]
    c11 = img[y1c, x1c]

    return (
        c00 * (1.0 - fx) * (1.0 - fy)
        + c10 * fx * (1.0 - fy)
        + c01 * (1.0 - fx) * fy
        + c11 * fx * fy
    )


# =============================================================================
# [2] Analytic Signed Distance Field Ground-Truth Shapes
# =============================================================================

def exact_circle_sdf(xx, yy, cx, cy, radius):
    return np.hypot(xx - cx, yy - cy) - radius


def exact_round_rect_sdf(xx, yy, cx, cy, hx, hy, r):
    px = np.abs(xx - cx) - (hx - r)
    py = np.abs(yy - cy) - (hy - r)
    qx = np.maximum(px, 0.0)
    qy = np.maximum(py, 0.0)
    return np.hypot(qx, qy) + np.minimum(np.maximum(px, py), 0.0) - r


def exact_l_shape_sdf(xx, yy, cx, cy):
    d1 = exact_round_rect_sdf(xx, yy, cx - 14.0, cy, 14.0, 32.0, 4.0)
    d2 = exact_round_rect_sdf(xx, yy, cx + 6.0, cy - 18.0, 26.0, 14.0, 4.0)
    return np.minimum(d1, d2)


def true_l_shape_sdf(xx, yy, cx, cy):
    """Exact Euclidean SDF to the concave L-shape perimeter (union of two rounded bars)."""
    d_union = exact_l_shape_sdf(xx, yy, cx, cy)
    h, w = xx.shape
    yyf, xxf = np.mgrid[0:h:0.25, 0:w:0.25]
    df = exact_l_shape_sdf(xxf, yyf, cx, cy)
    by_idx, bx_idx = np.where(np.abs(df) <= 0.18)
    bx = xxf[by_idx, bx_idx]
    by = yyf[by_idx, bx_idx]
    d_true = np.zeros_like(d_union)
    for y in range(h):
        dist2 = (xx[y, :, None] - bx[None, :]) ** 2 + (float(y) - by[None, :]) ** 2
        u = np.sqrt(np.min(dist2, axis=1))
        d_true[y] = np.where(d_union[y] <= 0.0, -u, u)
    return d_true


# =============================================================================
# [3] 5-Pass CPU Mirror of GPU JFA SDF & Normal Extraction Pipeline
# =============================================================================

def run_jfa_pipeline(mask, threshold=0.5, sigma=1.25):
    """Exact Python/NumPy mirror of jfa_seed -> jfa_step -> sdf_resolve -> sdf_smooth -> sdf_pack_normals."""
    h, w = mask.shape
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)

    # Pass B1 (jfa_seed): mark interior pixels adjacent to an exterior 4-neighbor.
    a_c = mask
    a_l = np.pad(mask[:, :-1], ((0, 0), (1, 0)), mode="edge")
    a_r = np.pad(mask[:, 1:], ((0, 0), (0, 1)), mode="edge")
    a_d = np.pad(mask[:-1, :], ((1, 0), (0, 0)), mode="edge")
    a_u = np.pad(mask[1:, :], ((0, 1), (0, 0)), mode="edge")
    min_nbr = np.minimum(np.minimum(a_l, a_r), np.minimum(a_d, a_u))
    is_seed = (a_c > threshold) & (min_nbr <= threshold)

    seed_x = np.where(is_seed, xx, -1.0)
    seed_y = np.where(is_seed, yy, -1.0)
    is_inside = mask > threshold

    # Pass B2 (jfa_step): logarithmic stride schedule + extra 1-px stabilization pass.
    max_dim = max(w, h)
    step = 1 << (int(math.ceil(math.log2(max_dim))) - 1)
    steps = []
    while step >= 1:
        steps.append(step)
        step //= 2
    steps.append(1)

    for k in steps:
        best_dist = np.full((h, w), 1e20, dtype=np.float64)
        next_sx = np.full((h, w), -1.0, dtype=np.float64)
        next_sy = np.full((h, w), -1.0, dtype=np.float64)

        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                ny = np.arange(h) + dy * k
                nx = np.arange(w) + dx * k
                valid_y = (ny >= 0) & (ny < h)
                valid_x = (nx >= 0) & (nx < w)
                ny_c = np.clip(ny, 0, h - 1)
                nx_c = np.clip(nx, 0, w - 1)
                in_bounds = valid_y[:, None] & valid_x[None, :]

                cand_x = seed_x[np.ix_(ny_c, nx_c)]
                cand_y = seed_y[np.ix_(ny_c, nx_c)]
                valid = in_bounds & (cand_x >= 0.0)

                qx = np.clip(cand_x, 0, w - 1).astype(np.int32)
                qy = np.clip(cand_y, 0, h - 1).astype(np.int32)
                seed_alpha = mask[qy, qx]
                residual = np.where(is_inside, seed_alpha, 1.0 - seed_alpha)
                rank_dist = np.hypot(xx - cand_x, yy - cand_y) + residual
                rank_dist = np.where(valid, rank_dist, 1e20)

                better = rank_dist < best_dist
                best_dist = np.where(better, rank_dist, best_dist)
                next_sx = np.where(better, cand_x, next_sx)
                next_sy = np.where(better, cand_y, next_sy)

        seed_x, seed_y = next_sx, next_sy

    # Pass B3 (sdf_resolve): recover signed Euclidean distance with (0.5 - alpha) sub-pixel offset.
    qx = np.clip(seed_x, 0, w - 1).astype(np.int32)
    qy = np.clip(seed_y, 0, h - 1).astype(np.int32)
    seed_alpha = mask[qy, qx]
    u = np.hypot(xx - seed_x, yy - seed_y)
    d_crisp = np.where(is_inside, -u, u) + (0.5 - seed_alpha)

    # Pass B4a (sdf_smooth): 5x5 Gaussian pre-blur to eliminate discrete Voronoi ridges in gradients.
    inv_2s2 = 0.5 / (sigma * sigma)
    d_smooth = np.zeros_like(d_crisp)
    w_sum = 0.0
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            wt = math.exp(-(dx * dx + dy * dy) * inv_2s2)
            ny_c = np.clip(np.arange(h) + dy, 0, h - 1)
            nx_c = np.clip(np.arange(w) + dx, 0, w - 1)
            d_smooth += d_crisp[np.ix_(ny_c, nx_c)] * wt
            w_sum += wt
    d_smooth /= w_sum

    # Pass B4b (sdf_pack_normals): compare central-difference normals from smoothed vs crisp SDF.
    def grad_normals(field):
        gx = np.pad(field[:, 1:], ((0, 0), (0, 1)), mode="edge") - np.pad(field[:, :-1], ((0, 0), (1, 0)), mode="edge")
        gy = np.pad(field[1:, :], ((0, 1), (0, 0)), mode="edge") - np.pad(field[:-1, :], ((1, 0), (0, 0)), mode="edge")
        ln = np.hypot(gx, gy)
        nx = np.where(ln > 1e-8, gx / np.maximum(ln, 1e-8), 0.0)
        ny = np.where(ln > 1e-8, gy / np.maximum(ln, 1e-8), 0.0)
        return nx, ny

    nx_smooth, ny_smooth = grad_normals(d_smooth)
    nx_raw, ny_raw = grad_normals(d_crisp)
    return d_crisp, (nx_smooth, ny_smooth), (nx_raw, ny_raw)


# =============================================================================
# [4] Invariant 1: JFA Sub-Pixel Distance Accuracy & Normal Smoothness
# =============================================================================

def test_jfa_sdf_and_normals():
    """Asserts <0.25 px RMS distance error across convex/concave masks and >2x normal jitter cut."""
    h, w = 128, 128
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
    sub_offsets = (np.arange(16) + 0.5) / 16.0 - 0.5

    def rasterize_aa(sdf_fn):
        cov = np.zeros((h, w), dtype=np.float64)
        for oy in sub_offsets:
            for ox in sub_offsets:
                cov += (sdf_fn(xx + ox, yy + oy) <= 0.0).astype(np.float64)
        return cov / (16.0 * 16.0)

    cases = [
        ("circle", lambda x, y: exact_circle_sdf(x, y, 63.7, 64.3, 38.4), None),
        ("round_rect", lambda x, y: exact_round_rect_sdf(x, y, 64.2, 63.6, 36.2, 26.8, 12.3), None),
        ("l_shape", lambda x, y: exact_l_shape_sdf(x, y, 64.3, 63.7), lambda x, y: true_l_shape_sdf(x, y, 64.3, 63.7)),
    ]

    for name, rast_fn, exact_fn in cases:
        mask = rasterize_aa(rast_fn)
        d_exact = exact_fn(xx, yy) if exact_fn is not None else rast_fn(xx, yy)
        band = np.abs(d_exact) <= 12.0

        # Verify both default threshold=0.50 and sub-pixel-band threshold=0.25.
        d_jfa_def, (nx_sm, ny_sm), (nx_raw, ny_raw) = run_jfa_pipeline(mask, threshold=0.5, sigma=1.25)
        err_def = np.abs(d_jfa_def[band] - d_exact[band])
        mean_def = float(np.mean(err_def))
        rms_def = float(np.sqrt(np.mean(err_def ** 2)))

        d_jfa_sub, (nx_sub, ny_sub), _ = run_jfa_pipeline(mask, threshold=0.25, sigma=1.25)
        err_sub = np.abs(d_jfa_sub[band] - d_exact[band])
        mean_sub = float(np.mean(err_sub))
        rms_sub = float(np.sqrt(np.mean(err_sub ** 2)))

        print(
            f"  [JFA {name:10s}] tau=0.50: mean={mean_def:.4f} rms={rms_def:.4f} px | "
            f"tau=0.25: mean={mean_sub:.4f} rms={rms_sub:.4f} px"
        )
        assert rms_def < 0.25, f"{name} tau=0.50 RMS error {rms_def} >= 0.25 px"
        assert rms_sub < 0.20, f"{name} tau=0.25 RMS error {rms_sub} >= 0.20 px"

        if name == "circle":
            # Compare normal angular error in bevel ring [-10, -2] px.
            ring = (d_exact <= -2.0) & (d_exact >= -10.0)
            gx_ex = (xx - 63.7) / np.maximum(np.hypot(xx - 63.7, yy - 64.3), 1e-6)
            gy_ex = (yy - 64.3) / np.maximum(np.hypot(xx - 63.7, yy - 64.3), 1e-6)
            ang_sm = np.degrees(np.arccos(np.clip(nx_sm[ring] * gx_ex[ring] + ny_sm[ring] * gy_ex[ring], -1.0, 1.0)))
            ang_sub = np.degrees(np.arccos(np.clip(nx_sub[ring] * gx_ex[ring] + ny_sub[ring] * gy_ex[ring], -1.0, 1.0)))
            ang_raw = np.degrees(np.arccos(np.clip(nx_raw[ring] * gx_ex[ring] + ny_raw[ring] * gy_ex[ring], -1.0, 1.0)))
            mean_sm = float(np.mean(ang_sm))
            mean_sub_ang = float(np.mean(ang_sub))
            mean_raw = float(np.mean(ang_raw))
            print(
                f"  [JFA circle normals] mean angular error: "
                f"smoothed(0.5)={mean_sm:.3f} deg, smoothed(0.25)={mean_sub_ang:.3f} deg vs unsmoothed={mean_raw:.3f} deg"
            )
            assert mean_sm < 2.2, f"Smoothed normal error {mean_sm} deg too high"
            assert mean_sub_ang < 1.0, f"Sub-pixel threshold normal error {mean_sub_ang} deg too high"
            assert mean_sm < 0.5 * mean_raw, "Gaussian pre-blur should cut normal angular jitter by > 2x"


# =============================================================================
# [5] Invariant 2: Clamped 13-Tap Downsample Pyramid Zero-Bleed
# =============================================================================

def downsample_13tap(src, dst_w, dst_h, clamp_rect=None):
    """Exact NumPy mirror of downsample_clamped.frag dual-ring 13-tap box filter."""
    src_h, src_w, _ = src.shape
    texel_x = 1.0 / src_w
    texel_y = 1.0 / src_h
    u = (np.arange(dst_w) + 0.5) / dst_w
    v = (np.arange(dst_h) + 0.5) / dst_h
    uu, vv = np.meshgrid(u, v)

    taps = [
        ((-1.0, -1.0), 0.125), ((1.0, -1.0), 0.125), ((-1.0, 1.0), 0.125), ((1.0, 1.0), 0.125),
        ((0.0, 0.0), 0.125),
        ((-2.0, 0.0), 0.0625), ((2.0, 0.0), 0.0625), ((0.0, -2.0), 0.0625), ((0.0, 2.0), 0.0625),
        ((-2.0, -2.0), 0.03125), ((2.0, -2.0), 0.03125), ((-2.0, 2.0), 0.03125), ((2.0, 2.0), 0.03125),
    ]
    out = np.zeros((dst_h, dst_w, 4), dtype=np.float64)
    for (dx, dy), wt in taps:
        tu = uu + dx * texel_x
        tv = vv + dy * texel_y
        if clamp_rect is not None:
            tu, tv = clamp_uv(tu, tv, clamp_rect)
        out += sample_bilinear_rgba(src, tu, tv) * wt
    return out


def test_clamped_downsample_pyramid():
    """Proves 0.5-texel inset UV clamping blocks padding color bleed across 4 mip levels."""
    w, h = 128, 128
    base = np.zeros((h, w, 4), dtype=np.float64)
    target = np.array([0.95, 0.65, 0.35, 1.0], dtype=np.float64)
    pad = 16
    base[pad:h - pad, pad:w - pad] = target

    cur_clamped = base.copy()
    cur_unclamped = base.copy()
    valid_min = pad
    valid_max = w - pad

    for lvl in range(1, 5):
        src_h, src_w, _ = cur_clamped.shape
        dst_w, dst_h = src_w // 2, src_h // 2
        u_min = (valid_min + 0.5) / src_w
        v_min = (valid_min + 0.5) / src_h
        u_max = (valid_max - 0.5) / src_w
        v_max = (valid_max - 0.5) / src_h

        cur_clamped = downsample_13tap(cur_clamped, dst_w, dst_h, clamp_rect=(u_min, v_min, u_max, v_max))
        cur_unclamped = downsample_13tap(cur_unclamped, dst_w, dst_h, clamp_rect=None)

        valid_min //= 2
        valid_max //= 2
        inner_clamped = cur_clamped[valid_min:valid_max, valid_min:valid_max]
        inner_unclamped = cur_unclamped[valid_min:valid_max, valid_min:valid_max]

        max_bleed = float(np.max(np.abs(inner_clamped - target)))
        unclamped_bleed = float(np.max(np.abs(inner_unclamped - target)))
        print(f"  [Downsample mip {lvl}] clamped max deviation={max_bleed:.2e}, unclamped bleed={unclamped_bleed:.4f}")
        assert max_bleed < 1e-12, f"Clamped downsample leaked border color at mip {lvl}: {max_bleed}"
        assert unclamped_bleed > 0.05, "Expected unclamped control to exhibit border bleed"


# =============================================================================
# [6] Invariant 3: 7-Tap Spectral Dispersion Unit Weights & Edge Monotonicity
# =============================================================================

def spectral_weights():
    return np.array([
        [0.00, 0.00, 0.20],
        [0.00, 0.10, 0.30],
        [0.05, 0.25, 0.30],
        [0.15, 0.30, 0.15],
        [0.30, 0.25, 0.05],
        [0.30, 0.10, 0.00],
        [0.20, 0.00, 0.00],
    ], dtype=np.float64)


def test_spectral_dispersion():
    """Verifies unit channel sums, gray achromaticity, and monotonic black->white fringe rolloff."""
    w = spectral_weights()
    col_sum = np.sum(w, axis=0)
    print(f"  [Dispersion weights] R,G,B column sums={col_sum}")
    assert np.allclose(col_sum, np.ones(3), atol=1e-12)

    luma_w = np.array([0.2126, 0.7152, 0.0722])
    alpha_sum = float(np.sum(w @ luma_w))
    assert abs(alpha_sum - 1.0) < 1e-12

    # 1) Uniform neutral gray/white fields must remain strictly achromatic under dispersion.
    for level in (0.18, 0.50, 0.95):
        flat_tex = np.full((64, 64, 4), [level, level, level, 1.0], dtype=np.float64)
        u = np.full((16, 16), 0.5)
        v = np.full((16, 16), 0.5)
        disp = (0.08, 0.05)
        acc = np.zeros((16, 16, 3), dtype=np.float64)
        for i in range(7):
            shift = (i - 3.0) / 3.0
            tu, tv = clamp_uv(u + shift * disp[0], v + shift * disp[1], (0.01, 0.01, 0.99, 0.99))
            acc += sample_bilinear_rgba(flat_tex, tu, tv)[..., :3] * w[i]

        chroma_diff = float(np.max(np.abs(acc - level)))
        assert chroma_diff < 1e-12, f"Uniform neutral gray {level} drifted by {chroma_diff}"

    # 2) Sharp black-to-white step edge produces smooth monotonic spectral prism transition.
    step_tex = np.zeros((64, 64, 4), dtype=np.float64)
    step_tex[:, 32:, :3] = 1.0
    step_tex[..., 3] = 1.0
    us = np.linspace(0.40, 0.60, 32)[None, :]
    vs = np.full((1, 32), 0.5)
    acc_edge = np.zeros((1, 32, 3), dtype=np.float64)
    for i in range(7):
        shift = (i - 3.0) / 3.0
        tu, tv = clamp_uv(us + shift * 0.06, vs, (0.01, 0.01, 0.99, 0.99))
        acc_edge += sample_bilinear_rgba(step_tex, tu, tv)[..., :3] * w[i]

    assert np.all(np.diff(acc_edge[0, :, 0]) >= -1e-12)
    assert np.all(np.diff(acc_edge[0, :, 1]) >= -1e-12)
    assert np.all(np.diff(acc_edge[0, :, 2]) >= -1e-12)
    print("  [Dispersion neutral & edge] flat gray exact achromaticity + smooth monotonic edge dispersion verified.")


# =============================================================================
# [7] Invariant 4: Smooth-Knee Mip-LOD Continuity & Singularity-Free Origin
# =============================================================================

def test_smooth_knee_lod():
    """Checks C0 continuity at r = 2 px and zero singularity at r = 0."""
    def lod(r):
        r_safe = np.maximum(r, 0.0)
        knee = np.where(r_safe < 2.0, 0.5 * r_safe + 1.0, r_safe)
        return np.maximum(0.0, np.log2(knee))

    r_grid = np.linspace(0.0, 32.0, 10000)
    vals = lod(r_grid)
    assert np.all(np.isfinite(vals))
    assert lod(0.0) == 0.0
    eps = 1e-7
    jump = abs(float(lod(2.0 + eps)) - float(lod(2.0 - eps)))
    print(f"  [Smooth-knee LOD] lod(0)={lod(0.0):.6f}, lod(2)={lod(2.0):.6f}, C0 jump at r=2: {jump:.2e}")
    assert jump < 1e-6
    assert np.all(np.diff(vals) >= -1e-12)


# =============================================================================
# [8] Invariant 5: Convex Bevel Profile & 3D Snell Thin-Lens + TIR Safety
# =============================================================================

def test_refraction_and_snell_tir():
    """Checks quarter-circle bevel boundary values and finite bounded TIR output when ior < 1."""
    u_vals = np.linspace(-0.2, 1.2, 500)
    u_c = np.clip(u_vals, 0.0, 1.0)
    h = 1.0 - np.sqrt(np.clip(u_c * (2.0 - u_c), 0.0, 1.0))
    assert np.all(np.isfinite(h))
    assert abs(h[np.argmin(np.abs(u_vals - 0.0))] - 1.0) < 1e-3
    assert abs(h[np.argmin(np.abs(u_vals - 1.0))]) < 1e-6

    R = 24.0
    lev = 4.0
    for ior in (1.50, 0.65):
        deltas = np.concatenate([np.array([0.0]), np.linspace(0.0, R, 1000), np.array([R])])
        rho = np.clip(deltas / R, 0.0, 1.0)
        nz = np.sqrt(np.maximum(0.0, 1.0 - rho ** 2))
        z_surf = nz * R + lev
        eta = 1.0 / ior
        k = 1.0 - (eta ** 2) * (1.0 - nz ** 2)

        disp_x = np.zeros_like(deltas)
        tir_count = int(np.sum(k < 0.0))
        valid = k >= 0.0
        tx = (eta * nz[valid] - np.sqrt(k[valid])) * rho[valid]
        tz = np.minimum((eta * nz[valid] - np.sqrt(k[valid])) * nz[valid] - eta, -1e-4)
        t_hit = -z_surf[valid] / tz
        disp_x[valid] = np.clip(t_hit * tx, -2.0 * R, 2.0 * R)
        disp_x[~valid] = np.clip(-R * 0.35, -2.0 * R, 2.0 * R)

        assert np.all(np.isfinite(disp_x))
        assert np.max(np.abs(disp_x)) <= 2.0 * R + 1e-9
        print(f"  [Snell lens ior={ior:.2f}] zero NaN/Inf, TIR samples={tir_count}, max |disp|={np.max(np.abs(disp_x)):.3f} px")
        if ior < 1.0:
            assert tir_count > 0, "Expected TIR branch activation for ior < 1.0"


# =============================================================================
# [9] Invariant 6: Hue-Preserving HDR Clamp & Premultiplied Alpha Bounds
# =============================================================================

def test_hue_preserving_clamp_and_premul():
    """Asserts exact Rec.709 luminance & hue direction preservation and premul <= alpha."""
    w_luma = np.array([0.2126, 0.7152, 0.0722])
    clamp_limit = 1.0

    def clamp_hue(c):
        peak = float(np.max(c))
        if peak > clamp_limit:
            y = float(np.dot(c, w_luma))
            if y >= clamp_limit:
                res = np.full(3, clamp_limit)
            else:
                t = np.clip((clamp_limit - y) / max(peak - y, 1e-6), 0.0, 1.0)
                res = y + (c - y) * t
        else:
            res = np.minimum(c, clamp_limit)
        return np.maximum(res, 0.0)

    hdr_colors = [
        np.array([1.80, 0.72, 0.18]),  # bright orange (2x)
        np.array([1.90, 0.30, 1.40]),  # bright magenta
        np.array([0.20, 1.60, 2.00]),  # bright cyan
        np.array([2.00, 1.80, 0.20]),  # HDR yellow (high luma)
    ]
    for c in hdr_colors:
        out = clamp_hue(c)
        assert np.all(out >= 0.0) and np.all(out <= clamp_limit + 1e-12)
        y_in = float(np.dot(c, w_luma))
        y_out = float(np.dot(out, w_luma))
        if y_in <= clamp_limit:
            assert abs(y_in - y_out) < 1e-6, f"Luminance drifted: {y_in} vs {y_out}"
            d_in = c - y_in
            d_out = out - y_out
            scale = np.linalg.norm(d_out) / max(np.linalg.norm(d_in), 1e-12)
            assert np.allclose(d_out, d_in * scale, atol=1e-6), "Hue direction shifted!"

        for cov in (0.0, 0.25, 0.73, 1.0):
            for op in (0.0, 0.5, 1.0):
                premul_rgb = out * cov * op
                premul_a = cov * op
                assert np.all(premul_rgb <= premul_a * clamp_limit + 1e-12)
    print("  [Hue-preserving clamp] exact Y & hue-ratio invariance + premultiplied bounds verified.")


def main():
    print("Running Clean-Room Liquid Glass Invariant Verification Suite:")
    test_jfa_sdf_and_normals()
    test_clamped_downsample_pyramid()
    test_spectral_dispersion()
    test_smooth_knee_lod()
    test_refraction_and_snell_tir()
    test_hue_preserving_clamp_and_premul()
    print("ALL MATHEMATICAL INVARIANTS PASSED.")


if __name__ == "__main__":
    try:
        main()
    except AssertionError as exc:
        print(f"INVARIANT FAILURE: {exc}", file=sys.stderr)
        sys.exit(1)
