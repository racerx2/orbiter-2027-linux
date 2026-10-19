#!/usr/bin/env bash

# Exit immediately if a command exits with a non-zero status
set -e

echo "=== 1. Updating System Repositories and Installing Dependencies ==="
# CachyOS native package configuration for Orbiter Linux
sudo pacman -Syu --needed base-devel cmake git gcc ninja \
    qt6-base vulkan-headers vulkan-validation-layers libpng pkgconf python

echo ""
echo "=== 2. Clearing out corrupted build caches and _deps ==="
# Hard wipe to bypass the Catch2 cloning blip
if [ -d "out" ]; then
    echo "Removing stale out/ directory..."
    rm -rf out
fi
if [ -d "build" ]; then
    echo "Removing stale build/ directory..."
    rm -rf build
fi

# Clean up top-level stray cache files if they exist
rm -f CMakeCache.txt
rm -rf CMakeFiles

echo ""
echo "=== 3. Configuring project with the Linux x64 Release Preset ==="
cmake --preset linux-x64-release

echo ""
echo "=== 4. Compiling the project ==="
cmake --build out/build/linux-x64-release

echo ""
echo "========================================="
echo " Build Completed Successfully! "
echo "========================================="
