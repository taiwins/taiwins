#!/bin/bash
set -e

#get the lastest cmake, vulkan
wget -qO /tmp/key.asc http://packages.lunarg.com/lunarg-signing-key-pub.asc
sudo apt-key add /tmp/key.asc
sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-1.1.130-bionic.list http://packages.lunarg.com/vulkan/1.1.130/lunarg-vulkan-1.1.130-bionic.list

sudo apt-get update

sudo apt-get -y install vulkan-sdk gcc-8 g++-8

sudo apt-get -y --no-install-recommends install \
     autoconf \
     automake \
     build-essential \
     curl \
     git \
     libdbus-1-dev \
     libegl1-mesa-dev \
     libevdev-dev \
     libexpat1-dev \
     libffi-dev \
     libgbm-dev \
     libgles2-mesa-dev \
     libglu1-mesa-dev \
     libinput-dev \
     libpixman-1-dev \
     libpng-dev \
     libsystemd-dev \
     libtool \
     libudev-dev \
     libva-dev \
     libwayland-dev \
     libxkbcommon-dev \
     mesa-common-dev \
     ninja-build \
     pkg-config \
     python3-pip \
     python3-setuptools

sudo apt-get -y install \
     libglvnd-dev \
     libfontconfig1-dev \
     libfreetype6-dev \
     libpam0g-dev

#register currdir
export CURDIR=$(pwd)

#install meson
export PATH=~/.local/bin:$PATH
pip3 install --user git+https://github.com/mesonbuild/meson.git@0.54

#install upstream wayland
git clone --depth=1 -b 1.19.0 https://gitlab.freedesktop.org/wayland/wayland /tmp/wayland
export MAKEFLAGS="-j4"
cd /tmp/wayland
mkdir build
cd build
../autogen.sh --disable-documentation
sudo make install

#install wayland-protocols
git clone --depth=1 https://gitlab.freedesktop.org/wayland/wayland-protocols /tmp/wayland-protocols
cd /tmp/wayland-protocols
mkdir build
cd build
../autogen.sh
sudo make install

cd $(CURDIR)
