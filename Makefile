# Clean-Room Liquid Glass Shader Suite Build & Verification Targets
ROOT_DIR := $(CURDIR)
BUILD_DIR := $(ROOT_DIR)/build
QSB_BIN   ?= /usr/lib/qt6/bin/qsb
CC        ?= gcc
CFLAGS    ?= -O2 -Wall -Wextra -Werror

.PHONY: all shaders test-invariants test-egl test clean

all: shaders test

# Compile and validate all GLSL 440 shaders -> Vulkan SPIR-V, GL SPIR-V, and Qt6 .qsb
shaders:
	@QSB_BIN="$(QSB_BIN)" bash "$(ROOT_DIR)/scripts/build_shaders.sh"

# Build headless EGL/OpenGL 4.6 SPIR-V GPU verification binary
$(BUILD_DIR)/egl_render_test: tests/egl_render_test.c
	@mkdir -p "$(BUILD_DIR)"
	$(CC) $(CFLAGS) "$<" -o "$@" -lEGL -lm

# CPU/NumPy mathematical invariant verification (JFA accuracy, dispersion, LOD, Snell/TIR, HDR clamp)
test-invariants:
	python3 "$(ROOT_DIR)/tests/verify_invariants.py"

# Execute compiled GL SPIR-V shaders on real GPU via headless EGL and export showcase PNG
test-egl: shaders $(BUILD_DIR)/egl_render_test
	"$(BUILD_DIR)/egl_render_test" "$(BUILD_DIR)/gl_spv" "$(BUILD_DIR)/render_preview.png"

test: shaders test-invariants test-egl

clean:
	rm -rf "$(BUILD_DIR)"
