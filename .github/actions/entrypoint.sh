#!/bin/bash

pacman -Syu --needed --noconfirm weston libxkbcommon pixman mesa wayland  wayland-protocols fontconfig freetype2 librsvg lua cairo pam doxygen
pacman -Syu --needed --noconfirm libglvnd vulkan-headers vulkan-icd-loader
pacman -Syu --needed --noconfirm gstreamer gst-plugins-base gst-plugins-base-libs
pacman -Syu --needed --noconfirm cmake make gcc clang musl git meson ninja rsync pkgconfig
# build step
rm -rf build && mkdir -p build && cd build && cmake .. && make
