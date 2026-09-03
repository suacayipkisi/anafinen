#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"

if [[ -f .gitmodules ]] && command -v git >/dev/null 2>&1 && git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git submodule update --init --recursive
fi

if [ -f /etc/fedora-release ]; then
    echo "Detected Fedora. Generating RPM..."
    sudo dnf install -y rpm-build ninja-build cmake gcc-c++ eigen3-devel libpng-devel mesa-libGL-devel gmsh-devel glfw-devel spectra-devel
    cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR"
    cpack --config "$BUILD_DIR/CPackConfig.cmake" -G RPM -B "$BUILD_DIR"
    echo "RPM created successfully in $BUILD_DIR/"
elif [ -f /etc/cachyos-release ] || [ -f /etc/arch-release ]; then
    echo "Detected Arch/Arch-based. Generating native pacman package..."
    makepkg -f
    echo "Package (.pkg.tar.zst) created successfully."
elif [ -f /etc/debian_version ]; then
    echo "Detected Debian-based system. Generating DEB..."
    sudo apt-get update
    sudo apt-get install -y cmake ninja-build build-essential pkg-config libeigen3-dev libpng-dev libglfw3-dev libgmsh-dev libspectra-dev libgl1-mesa-dev
    cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR"
    cpack --config "$BUILD_DIR/CPackConfig.cmake" -G DEB -B "$BUILD_DIR"
    echo "DEB created successfully in $BUILD_DIR/"
else
    echo "Unsupported distribution for native packaging. Generating tarball..."
    cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR"
    cpack --config "$BUILD_DIR/CPackConfig.cmake" -G TGZ -B "$BUILD_DIR"
fi
