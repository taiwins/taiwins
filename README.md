## Taiwins, a morden window manager based on libweston

Taiwins is a wayland tiling window manager project based on **libweston**. It is
designed to be mordern and modular. It has built-in shell and widget
implementation thus it is extensible, it supports popular tiling window manager
features like gapping.

It is currently usable but still under developement. You can refer to
[progress](docs/progress.org) for current progress.


## How to build
you will need following dependencies
- Pixman
- libweston(if you have an nvidia graphics card you may need weston-eglstream)
- xkbcommon
- libinput
- wayland
- wayland protocols
- cairo
- lua
- librsvg
- opengl>=3.3
- cmake

build steps:

	git clone https://github.com/xeechou/taiwins-weston taiwins && cd taiwins
	git submodule init
	git submodule update
	mkdir build && cd build
	cmake ..
	make -j8

## How to run

Here is currently how I run the compositor, it has no configuration now, will be
added as lua script later.

	cd build
	./bin/taiwins ./bin/shell-taiwins ./bin/shell-console


### Screenshots
- widget example
![current progress](imgs/with-nuklear.png)

- opening application in floating mode
![use-emacs](imgs/use-emacs.png)

- opening application in tiling mode
![tiling](imgs/resizing.png)
