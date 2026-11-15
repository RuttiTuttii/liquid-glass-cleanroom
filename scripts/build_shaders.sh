#!/usr/bin/env bash
set -euo pipefail

# -----------------------------------------------------------------------------
# Clean-Room Shader Toolchain Matrix
# Compiles and validates all GLSL #version 440 shaders across four independent
# frontends/validators:
#   1. glslangValidator (Vulkan SPIR-V) + spirv-val
#   2. glslc            (Vulkan SPIR-V) + spirv-val
#   3. glslangValidator (OpenGL 4.5 SPIR-V for EGL testbench) + spirv-val
#   4. Qt6 qsb          (cross-API RHI shader bundle: 100 es / 120 / 330 / 440)
# -----------------------------------------------------------------------------

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SHADER_DIR="${ROOT_DIR}/shaders"
BUILD_DIR="${ROOT_DIR}/build"
SPV_VULKAN_DIR="${BUILD_DIR}/spv"
SPV_GLSLC_DIR="${BUILD_DIR}/spv_glslc"
SPV_GL_DIR="${BUILD_DIR}/gl_spv"
QSB_DIR="${BUILD_DIR}/qsb"

QSB_BIN="${QSB_BIN:-/usr/lib/qt6/bin/qsb}"

mkdir -p "${SPV_VULKAN_DIR}" "${SPV_GLSLC_DIR}" "${SPV_GL_DIR}" "${QSB_DIR}"

compile_shader() {
    local src="$1"
    local base
    base="$(basename "${src}")"
    echo "  [compile/validate] ${base}"

    # 1. glslangValidator (Vulkan SPIR-V) + spirv-val
    glslangValidator -V "${src}" -o "${SPV_VULKAN_DIR}/${base}.spv"
    spirv-val "${SPV_VULKAN_DIR}/${base}.spv"

    # 2. glslc (Shaderc Vulkan SPIR-V) + spirv-val
    glslc "${src}" -o "${SPV_GLSLC_DIR}/${base}.spv"
    spirv-val "${SPV_GLSLC_DIR}/${base}.spv"

    # 3. glslangValidator (OpenGL 4.5+ SPIR-V for headless EGL/GL testbench) + spirv-val
    glslangValidator -G "${src}" -o "${SPV_GL_DIR}/${base}.spv"
    spirv-val --target-env opengl4.5 "${SPV_GL_DIR}/${base}.spv"

    # 4. Qt6 Shader Baker (.qsb) targeting GLSL 100 es / 120 / 330 / 440 + SPIR-V
    "${QSB_BIN}" --glsl "100 es,120,330,440" -o "${QSB_DIR}/${base}.qsb" "${src}"
}

shopt -s nullglob
SHADERS=("${SHADER_DIR}"/*.vert "${SHADER_DIR}"/*.frag)

if [ "${#SHADERS[@]}" -eq 0 ]; then
    echo "No shaders found in ${SHADER_DIR}" >&2
    exit 1
fi

echo "Building ${#SHADERS[@]} shaders from ${SHADER_DIR}..."
for src in "${SHADERS[@]}"; do
    compile_shader "${src}"
done

echo "All ${#SHADERS[@]} shaders compiled, SPIR-V validated, and QSB-baked cleanly."
