#!/bin/bash
# not upstream: the pinned Vulkan pieces of the portable build, built with GCC 14 into $1 (Dockerfile)
set -euo pipefail
P=${1:-/opt/deps}
VK=v1.4.341
GLSLANG=16.2.0
EXTLAYER=v1.4.341
source /opt/rh/gcc-toolset-14/enable
W=$(mktemp -d)
cd "$W"
git clone -q --depth 1 -b "$VK" https://github.com/KhronosGroup/Vulkan-Headers.git
cmake -S Vulkan-Headers -B vh -G Ninja -DCMAKE_INSTALL_PREFIX="$P"
cmake --install vh
git clone -q --depth 1 -b "$GLSLANG" https://github.com/KhronosGroup/glslang.git
cmake -S glslang -B gl -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" -DBUILD_SHARED_LIBS=OFF -DENABLE_OPT=OFF \
	-DGLSLANG_TESTS=OFF -DENABLE_GLSLANG_BINARIES=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build gl
cmake --install gl
git clone -q --depth 1 -b "$EXTLAYER" https://github.com/KhronosGroup/Vulkan-ExtensionLayer.git
cmake -S Vulkan-ExtensionLayer -B el -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" -DUPDATE_DEPS=ON -DBUILD_TESTS=OFF
cmake --build el --target VkLayer_khronos_shader_object
mkdir -p "$P/share/vulkan/explicit_layer.d" "$P/lib64"
find el -name 'libVkLayer_khronos_shader_object.so' -exec cp {} "$P/lib64/" \;
find el -name 'VkLayer_khronos_shader_object.json' -exec cp {} "$P/share/vulkan/explicit_layer.d/" \;
ls -l "$P/lib64/libVkLayer_khronos_shader_object.so" "$P/share/vulkan/explicit_layer.d/VkLayer_khronos_shader_object.json"
cd / && rm -rf "$W"
