# Clean-Room Liquid Glass Native Hyprland 0.57.x Plugin Build
ROOT_DIR   := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
PLUGIN_DIR := $(ROOT_DIR)/plugin
SRC_DIR    := $(PLUGIN_DIR)/src
TEST_DIR   := $(PLUGIN_DIR)/tests
BUILD_DIR  := $(PLUGIN_DIR)/build

CXX       ?= g++
PYTHON    ?= python3
STD_FLAG  ?= -std=c++23
WARNFLAGS ?= -Wall -Wextra -Werror
PLUGIN_FLAGS ?= -fPIC -fno-gnu-unique

HYPR_CFLAGS := $(shell pkg-config --cflags hyprland pixman-1 libdrm egl glesv2)
GLES_LIBS   := $(shell pkg-config --libs egl glesv2) -lm

PLUGIN_SO   := $(BUILD_DIR)/liquid-glass.so
TEST_BIN    := $(BUILD_DIR)/gles_plugin_test
GEN_HEADER  := $(BUILD_DIR)/generated_shaders.hpp

SHADER_DEPS := \
	$(ROOT_DIR)/scripts/gen_plugin_shaders.py \
	$(ROOT_DIR)/shaders/include/glass_common.glsl \
	$(ROOT_DIR)/shaders/include/glass_sdf.glsl \
	$(ROOT_DIR)/shaders/include/glass_optics.glsl \
	$(ROOT_DIR)/shaders/glass_quad.vert \
	$(ROOT_DIR)/shaders/downsample_clamped.frag \
	$(ROOT_DIR)/shaders/liquid_glass_panel.frag

.PHONY: all plugin test clean

all: plugin test

shaders-header: $(GEN_HEADER)

$(GEN_HEADER): $(SHADER_DEPS)
	@mkdir -p "$(BUILD_DIR)"
	$(PYTHON) "$(ROOT_DIR)/scripts/gen_plugin_shaders.py"

plugin: $(PLUGIN_SO)

$(BUILD_DIR)/gl_resources.o: $(SRC_DIR)/gl_resources.cpp $(SRC_DIR)/gl_resources.hpp $(SRC_DIR)/glass_uniforms.hpp $(GEN_HEADER)
	@mkdir -p "$(BUILD_DIR)"
	$(CXX) $(STD_FLAG) -O2 $(PLUGIN_FLAGS) $(WARNFLAGS) -I"$(SRC_DIR)" -I"$(BUILD_DIR)" -c "$<" -o "$@"

$(BUILD_DIR)/config.o: $(SRC_DIR)/config.cpp $(SRC_DIR)/config.hpp $(SRC_DIR)/glass_uniforms.hpp
	@mkdir -p "$(BUILD_DIR)"
	$(CXX) $(STD_FLAG) -O2 $(PLUGIN_FLAGS) $(WARNFLAGS) $(HYPR_CFLAGS) -I"$(SRC_DIR)" -c "$<" -o "$@"

$(BUILD_DIR)/glass_provider.o: $(SRC_DIR)/glass_provider.cpp $(SRC_DIR)/glass_provider.hpp $(SRC_DIR)/gl_resources.hpp $(SRC_DIR)/config.hpp $(SRC_DIR)/glass_uniforms.hpp
	@mkdir -p "$(BUILD_DIR)"
	$(CXX) $(STD_FLAG) -O2 $(PLUGIN_FLAGS) $(WARNFLAGS) $(HYPR_CFLAGS) -I"$(SRC_DIR)" -I"$(BUILD_DIR)" -c "$<" -o "$@"

$(BUILD_DIR)/main.o: $(SRC_DIR)/main.cpp $(SRC_DIR)/config.hpp $(SRC_DIR)/glass_provider.hpp $(SRC_DIR)/gl_resources.hpp $(SRC_DIR)/glass_uniforms.hpp
	@mkdir -p "$(BUILD_DIR)"
	$(CXX) $(STD_FLAG) -O2 $(PLUGIN_FLAGS) $(WARNFLAGS) $(HYPR_CFLAGS) -I"$(SRC_DIR)" -I"$(BUILD_DIR)" -c "$<" -o "$@"

$(PLUGIN_SO): $(BUILD_DIR)/gl_resources.o $(BUILD_DIR)/config.o $(BUILD_DIR)/glass_provider.o $(BUILD_DIR)/main.o
	$(CXX) -shared -o "$@" $^ $(GLES_LIBS)

$(TEST_BIN): $(TEST_DIR)/gles_plugin_test.cpp $(BUILD_DIR)/gl_resources.o $(SRC_DIR)/gl_resources.hpp $(SRC_DIR)/glass_uniforms.hpp $(GEN_HEADER)
	@mkdir -p "$(BUILD_DIR)"
	$(CXX) $(STD_FLAG) -O2 $(WARNFLAGS) -I"$(SRC_DIR)" -I"$(BUILD_DIR)" "$<" "$(BUILD_DIR)/gl_resources.o" -o "$@" $(GLES_LIBS)

test: $(TEST_BIN)
	"$(TEST_BIN)"

clean:
	rm -rf "$(BUILD_DIR)"
