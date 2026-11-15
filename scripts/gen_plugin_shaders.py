#!/usr/bin/env python3
"""
Clean-Room OpenGL ES 3.20 Shader Generator for Hyprland Plugin

Transforms the canonical `#version 440` clean-room GLSL sources under `shaders/`
into self-contained `#version 320 es` C++ raw-string literals so the native
Hyprland plugin executes identical math with zero duplicated shader logic.
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SHADERS_DIR = ROOT / "shaders"
OUT_HEADER = ROOT / "plugin" / "build" / "generated_shaders.hpp"

INCLUDE_RE = re.compile(r'^\s*#include\s+"([^"]+)"\s*$')
VERSION_RE = re.compile(r'^\s*#version\s+\d+.*$')
EXT_RE = re.compile(r'^\s*#extension\s+GL_GOOGLE_include_directive\b.*$')


# -----------------------------------------------------------------------------
# [1] Canonical Include Flattening & ES 3.20 Translation
# -----------------------------------------------------------------------------
def flatten_glsl(path: pathlib.Path, seen: set[pathlib.Path]) -> str:
    """Recursively inline local `#include` headers once per canonical file."""
    resolved = path.resolve()
    if resolved in seen:
        return ""
    seen.add(resolved)

    out_lines: list[str] = []
    for line in resolved.read_text(encoding="utf-8").splitlines():
        if VERSION_RE.match(line) or EXT_RE.match(line):
            continue
        m = INCLUDE_RE.match(line)
        if m:
            inc_path = (resolved.parent / m.group(1)).resolve()
            out_lines.append(flatten_glsl(inc_path, seen))
            continue
        out_lines.append(line)
    return "\n".join(out_lines) + "\n"


def to_es32_source(src_rel: str, post_process=None) -> str:
    """Build a standalone `#version 320 es` shader string from `shaders/<src_rel>`."""
    seen: set[pathlib.Path] = set()
    body = flatten_glsl(SHADERS_DIR / src_rel, seen)
    if post_process is not None:
        body = post_process(body)

    preamble = "\n".join([
        "#version 320 es",
        "precision highp float;",
        "precision highp int;",
        "precision highp sampler2D;",
        "",
    ])
    return preamble + body


def build_copy_prepare_es32() -> str:
    """
    Generate the ES 3.20 scene copy & linear-light normalization pass.

    Handles `TEXTURE_RGBX` alpha override (`forceOpaque`) and optional SDR
    sRGB-to-linear EOTF conversion before mip-pyramid downsampling.
    """
    seen: set[pathlib.Path] = set()
    common_inc = flatten_glsl(SHADERS_DIR / "include" / "glass_common.glsl", seen)

    stage = r"""
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform CopyBuf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float forceOpaque;
    float decodeSrgb;
    float _pad0;
    vec4 uvClampRect;
} u;

layout(binding = 1) uniform sampler2D srcTex;

vec3 srgbToLinearES(vec3 c) {
    vec3 safe = clamp(c, 0.0, 1.0);
    vec3 lo = safe / 12.92;
    vec3 hi = pow((safe + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), safe));
}

void main() {
    vec2 uv = clampUV(qt_TexCoord0, u.uvClampRect);
    vec4 s = textureLod(srcTex, uv, 0.0);
    float a = (u.forceOpaque > 0.5) ? 1.0 : clamp(s.a, 0.0, 1.0);
    vec3 rgb = (u.forceOpaque > 0.5) ? s.rgb : unpremultiplySafe(vec4(s.rgb, a), 1e-5);
    if (u.decodeSrgb > 0.5) {
        rgb = srgbToLinearES(rgb);
    }
    fragColor = vec4(rgb * a, a) * u.qt_Opacity;
}
""".lstrip("\n")

    preamble = "\n".join([
        "#version 320 es",
        "precision highp float;",
        "precision highp int;",
        "precision highp sampler2D;",
        "",
    ])
    return preamble + common_inc + "\n" + stage


def wrap_panel_es32(body: str) -> str:
    """
    Wrap `liquid_glass_panel.frag` to optionally re-encode linear light to sRGB
    before writing to an SDR Hyprland compositor framebuffer.
    """
    # Replace the fragment output declaration and references with an internal
    # vec4 accumulator so the entry wrapper applies linear->sRGB cleanly without
    # reading back an `out` interface variable.
    body = re.sub(
        r'layout\s*\(\s*location\s*=\s*0\s*\)\s*out\s+vec4\s+fragColor\s*;',
        'vec4 lg_core_fragColor;',
        body,
        count=1,
    )
    body = re.sub(r'\bfragColor\b', 'lg_core_fragColor', body)
    body = re.sub(r'\bvoid\s+main\s*\(\s*\)', 'void evalLiquidGlassPanelCore()', body, count=1)

    wrapper = r"""
layout(location = 0) uniform float u_encodeSrgb;
layout(location = 0) out vec4 fragColor;

vec3 linearToSrgbES(vec3 c) {
    vec3 safe = clamp(c, 0.0, 1.0);
    vec3 lo = safe * 12.92;
    vec3 hi = 1.055 * pow(safe, vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), safe));
}

void main() {
    evalLiquidGlassPanelCore();
    vec4 res = lg_core_fragColor;
    if (u_encodeSrgb > 0.5) {
        float a = max(res.a, 1e-5);
        vec3 straight = (res.a > 1e-5) ? (res.rgb / a) : vec3(0.0);
        res.rgb = linearToSrgbES(straight) * res.a;
    }
    fragColor = res;
}
""".lstrip("\n")
    return body + "\n" + wrapper


# -----------------------------------------------------------------------------
# [2] C++ Header Emission
# -----------------------------------------------------------------------------
def emit_raw_literal(name: str, text: str) -> str:
    """Emit a safe C++17 raw string literal using an unused delimiter."""
    delim = "LGGLSL"
    while f"){delim}\"" in text:
        delim += "X"
    return f'inline constexpr const char* {name} = R"{delim}({text}){delim}";\n'


def build_vert_es32() -> str:
    """
    Generate the ES 3.20 quad vertex shader with a distinct UBO block name (`VertBuf`)
    bound to slot 0 so OpenGL ES program linking never collides with differing fragment
    `buf` member definitions while still sharing the same 80-byte `qt_Matrix` UBO prefix.
    """
    return to_es32_source(
        "glass_quad.vert",
        post_process=lambda body: re.sub(
            r'\buniform\s+buf\b',
            'uniform VertBuf',
            body,
            count=1,
        ),
    )


def main() -> int:
    OUT_HEADER.parent.mkdir(parents=True, exist_ok=True)

    vert_es32 = build_vert_es32()
    copy_es32 = build_copy_prepare_es32()
    down_es32 = to_es32_source("downsample_clamped.frag")
    panel_es32 = to_es32_source("liquid_glass_panel.frag", post_process=wrap_panel_es32)

    parts = [
        "// Auto-generated by scripts/gen_plugin_shaders.py from canonical shaders/.",
        "// Do not edit by hand; modify shaders/*.{vert,frag} or shaders/include/*.glsl.",
        "#pragma once",
        "",
        "namespace LiquidGlassPlugin::Shaders {",
        emit_raw_literal("kQuadVertES32", vert_es32),
        emit_raw_literal("kCopyPrepareFragES32", copy_es32),
        emit_raw_literal("kDownsampleFragES32", down_es32),
        emit_raw_literal("kPanelFinishFragES32", panel_es32),
        "} // namespace LiquidGlassPlugin::Shaders",
        "",
    ]

    OUT_HEADER.write_text("\n".join(parts), encoding="utf-8")
    print(f"[gen_plugin_shaders] Wrote {OUT_HEADER}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
