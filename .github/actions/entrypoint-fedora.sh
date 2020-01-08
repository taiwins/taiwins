#!/bin/bash

dnf install -y libxkbcommon-devel libxkbcommon pixman pixman-devel
dnf install -y libglvnd-devel libglvnd-egl libglvnd-gles libglvnd-opengl
dnf install -y wayland-protocols-devel wayland-devel libwayland-server libwayland-client libwayland-egl libwayland-cursor
dnf install -y weston-devel libinput-devel libudev-devel

dnf install -y mesa-libGL-devel mesa-libEGL-devel mesa-libGLES-devel vulkan-headers mesa-vulkan-devel mesa-vulkan-drivers
dnf install -y fontconfig fontconfig-devel freetype freetype-devel librsvg2 librsvg2-devel
dnf install -y lua-devel cairo-devel pam-devel
dnf install -y cmake make glibc-headers glibc-devel gcc gcc-c++ git meson ninja-build pkgconf-pkg-config
# build step
rm -rf build && mkdir -p build && cd build && cmake -DCMAKE_C_COMPILER=gcc .. && make
