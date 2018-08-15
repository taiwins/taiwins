## Taiwins, a morden window manager based on libweston

### project under development.
![current progress](imgs/with-nuklear.png)

### finally moved to desktop implementation, showing windows is seamless
![use-emacs](imgs/use-emacs.png)

currently on [desktop implementation](docs/weston-desktop.md)


## how to build
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

Here is currently how I test the program

	cd build
	./bin/taiwins ./bin/shell-taiwins ./bin/shell-launcher
