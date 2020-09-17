#!/bin/bash

pacman -Syu --needed --noconfirm weston libxkbcommon pixman mesa wayland  wayland-protocols fontconfig freetype2 librsvg lua cairo pam doxygen
pacman -Syu --needed --noconfirm libglvnd vulkan-headers vulkan-icd-loader
pacman -Syu --needed --noconfirm gstreamer gst-plugins-base gst-plugins-base-libs
pacman -Syu --needed --noconfirm cmake make gcc clang musl git meson ninja rsync pkgconfig
# build step
meson wrap promote subprojects/twclient/subprojects/ctypes
rm -rf build && meson build && ninja -C build
