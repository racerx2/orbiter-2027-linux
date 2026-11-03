#!/bin/bash
# not upstream: the pinned Vulkan pieces of the portable build, built hardened with GCC 14 into $1 (setup.sh)
set -euo pipefail
P=${1:-/opt/deps}
# tag and the commit it must be (a tag can move)
VK=(v1.4.341 b5c8f996196ba4aa6d8f97e52b5d3b6e70f7e4e2)
GLSLANG=(16.2.0 f0bd0257c308b9a26562c1a30c4748a0219cc951)
EXTLAYER=(v1.4.341 38092a9e23a086d4eba8657dc49f2ab71320e91f) # its UPDATE_DEPS takes the commits its known_good.json names
source /opt/rh/gcc-toolset-14/enable
export CFLAGS="-O2 -fstack-protector-strong -fstack-clash-protection -fcf-protection -D_FORTIFY_SOURCE=3" CXXFLAGS="-O2 -fstack-protector-strong -fstack-clash-protection -fcf-protection -D_FORTIFY_SOURCE=3"
export LDFLAGS="-Wl,-z,relro,-z,now,-z,noexecstack"
W=$(mktemp -d)
cd "$W"
clone() { # $1 repository, $2 tag, $3 commit
	git clone -q --depth 1 -b "$2" "https://github.com/KhronosGroup/$1.git"
	[ "$(git -C "$1" rev-parse HEAD)" = "$3" ] || { echo "deps.sh: $1 $2 is not $3" >&2; exit 1; }
}
clone Vulkan-Headers "${VK[@]}"
cmake -S Vulkan-Headers -B vh -G Ninja -DCMAKE_INSTALL_PREFIX="$P"
cmake --install vh
clone glslang "${GLSLANG[@]}"
cmake -S glslang -B gl -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" -DBUILD_SHARED_LIBS=OFF -DENABLE_OPT=OFF \
	-DGLSLANG_TESTS=OFF -DENABLE_GLSLANG_BINARIES=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build gl
cmake --install gl
clone Vulkan-ExtensionLayer "${EXTLAYER[@]}"
cmake -S Vulkan-ExtensionLayer -B el -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" -DUPDATE_DEPS=ON -DBUILD_TESTS=OFF
cmake --build el --target VkLayer_khronos_shader_object
mkdir -p "$P/share/vulkan/explicit_layer.d" "$P/lib64"
find el -name 'libVkLayer_khronos_shader_object.so' -exec cp {} "$P/lib64/" \;
find el -name 'VkLayer_khronos_shader_object.json' -exec cp {} "$P/share/vulkan/explicit_layer.d/" \;
ls -l "$P/lib64/libVkLayer_khronos_shader_object.so" "$P/share/vulkan/explicit_layer.d/VkLayer_khronos_shader_object.json"
cd / && rm -rf "$W"
