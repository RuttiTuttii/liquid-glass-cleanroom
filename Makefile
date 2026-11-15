# Clean-Room Liquid Glass Shader Suite Build & Verification Targets
ROOT_DIR := $(CURDIR)
BUILD_DIR := $(ROOT_DIR)/build
QSB_BIN   ?= /usr/lib/qt6/bin/qsb
CC        ?= gcc
CFLAGS    ?= -O2 -Wall -Wextra -Werror

.PHONY: all shaders test-invariants test-egl plugin test-plugin test web-serve clean

all: shaders plugin test

# Compile and validate all GLSL 440 shaders -> Vulkan SPIR-V, GL SPIR-V, and Qt6 .qsb
shaders:
	@QSB_BIN="$(QSB_BIN)" bash "$(ROOT_DIR)/scripts/build_shaders.sh"

# Build the native Hyprland C++ OpenGL ES 3.2 plugin (plugin/build/liquid-glass.so)
plugin:
	$(MAKE) -C "$(ROOT_DIR)/plugin" plugin

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

# Execute standalone OpenGL ES 3.20 plugin GPU pipeline & GL state guard tests
test-plugin:
	$(MAKE) -C "$(ROOT_DIR)/plugin" test

test: shaders test-invariants test-egl test-plugin

# Serve interactive Clean-Room Apple-style Liquid Glass Web UI testbed locally
web-serve:
	python3 -m http.server 18426 --bind 127.0.0.1 --directory "$(ROOT_DIR)/web"

clean:
	rm -rf "$(BUILD_DIR)"
	$(MAKE) -C "$(ROOT_DIR)/plugin" clean
