#!/bin/bash
set -e

#get the lastest cmake, vulkan
wget -qO /tmp/key.asc http://packages.lunarg.com/lunarg-signing-key-pub.asc
sudo apt-key add /tmp/key.asc
sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-1.1.130-bionic.list http://packages.lunarg.com/vulkan/1.1.130/lunarg-vulkan-1.1.130-bionic.list

wget -qO /tmp/key1.asc https://apt.kitware.com/keys/kitware-archive-latest.asc
sudo apt-key add /tmp/key1.asc
echo 'deb https://apt.kitware.com/ubuntu/ bionic main' > /tmp/cmake-kitware.list
sudo mv /tmp/cmake-kitware.list /etc/apt/sources.list.d/

sudo apt-get update

sudo apt-get -y install vulkan-sdk cmake gcc-8 g++-8

sudo apt-get -y --no-install-recommends install \
     autoconf \
     automake \
     build-essential \
     curl \
     doxygen \
     freerdp2-dev \
     git \
     libcairo2-dev \
     libcolord-dev \
     libdbus-1-dev \
     libegl1-mesa-dev \
     libevdev-dev \
     libexpat1-dev \
     libffi-dev \
     libgbm-dev \
     libgdk-pixbuf2.0-dev \
     libgles2-mesa-dev \
     libglu1-mesa-dev \
     libgstreamer1.0-dev \
     libgstreamer-plugins-base1.0-dev \
     libinput-dev \
     libjpeg-dev \
     liblcms2-dev \
     libmtdev-dev \
     libpam0g-dev \
     libpango1.0-dev \
     libpixman-1-dev \
     libpng-dev \
     libsystemd-dev \
     libtool \
     libudev-dev \
     libva-dev \
     libvpx-dev \
     libwayland-dev \
     libwebp-dev \
     libx11-dev \
     libx11-xcb-dev \
     libxcb1-dev \
     libxcb-composite0-dev \
     libxcb-xfixes0-dev \
     libxcb-xinput-dev \
     libxcb-xkb-dev \
     libxcursor-dev \
     libxkbcommon-dev \
     libasound2-dev \
     libxml2-dev \
     mesa-common-dev \
     ninja-build \
     pkg-config \
     python3-pip \
     python3-setuptools \
     xwayland

sudo apt-get -y install \
     libglvnd-dev \
     libfontconfig1-dev \
     libfreetype6-dev \
     librsvg2-dev \
     liblua5.3-dev \
     libpam0g-dev

#register currdir
export CURDIR=$(pwd)

#install meson
export PATH=~/.local/bin:$PATH
pip3 install --user git+https://github.com/mesonbuild/meson.git@0.54

#install upstream wayland
git clone --depth=1 https://gitlab.freedesktop.org/wayland/wayland /tmp/wayland
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

#install pipewire that is not available on ubuntu
git clone -b 0.2 --depth=1 https://gitlab.freedesktop.org/pipewire/pipewire /tmp/pipewire
cd /tmp/pipewire
mkdir build
./autogen.sh
sudo make install

cd $(CURDIR)
