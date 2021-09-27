# Taiwins, a modern wayland window manager (Discontinued)

I no longer actively mantain this project due to personal reasons, anyone is
free to fork and take control of it or simply take it as a study
reference as long as you respect the GPL license agreements. Ciao.

[![Gitter](https://img.shields.io/badge/gitter-taiwins--project-purple?logo=gitter-white)](https://gitter.im/taiwins/)
![example workflow](https://github.com/taiwins/taiwins/actions/workflows/ccpp.yml/badge.svg)

Taiwins is a dynamic wayland window manager, supports both tiling and floating
layout. It is designed to be modern and modular. It is extensible through lua
script and it has built-in **shell** and **widgets** implementation through
[nuklear GUI](https://github.com/Immediate-Mode-UI/Nuklear). It also supports 
popular tiling window manager features like gapping.

The name of the project pronounces as ['taiwinz], it is inspired by the
philosophy of Taichi as I hope it would be dynamic and balanced.

Taiwins is usable now with potential bugs and some missing features. Continues
developement in progress and helps are wanted. If you like to join, I drafted
some pages of [notes](docs/apidoc.md) to guide you through the starting
steps. You can also join the chat on [Gitter](https://gitter.im/taiwins) for
any questions and disscussions. There is a [feature list](docs/progress.md)
available if you want to know more about what taiwins can do.

## Dependencies
you will need following dependencies

- Pixman
- xkbcommon
- xwayland
- libx11, libxcb (if you want X11 backen support)
- libinput
- wayland
- wayland protocols
- cairo
- lua
- librsvg
- opengl or opengles
- meson
- ninja
- pam
- fontconfig
- freetype2

## Build steps:
with source code, you can easily compile and try out:

	git clone https://github.com/taiwins/taiwins --recursive taiwins && cd taiwins
	
	meson build && ninja -C build
	
For those who use Archlinux, there is an
[aur](https://aur.archlinux.org/packages/taiwins) package you can simply
install.

## How to run

Taiwins starts with default shell and default console they are found. You can
also specifiy the shell application and console application through command line
options.

	cd build
	./bin/taiwins -s ./bin/taiwins-shell -c ./bin/taiwins-console
	
Or if you install systemwisely, you can simply use

	taiwins -s taiwins-shell -c taiwins-console

If you prefer not to have the shell, try `taiwins -n` which will make taiwins
run without shell, user can start a shell later.

The default configuration is `$XDG_CONFIG_PATH/taiwins/config.lua`, see the
[sample config](docs/config.lua) for an example.

### Key-bindings
Taiwins has a versatile binding system, you can chain key-presses like in
Emacs(up to 5) and add custom bindings through lua functions. The bindings is
configurable, by default available bindings are

- `F12` : quit taiwins
- `Super+Shift+c` close current application
- `Ctrl+LEFT/RIGHT` switch to previous/next workspace
- `Alt+Super+b` switch to last workspace
- `Alt+LEFT` resize window to the left (only in tiling mode)
- `Alt+RIGHT` resize window to the right (only in tiling mode)
- `Super+Space` toggle vertical/horizental layout (only in tiling mode)
- `Alt+Shift+Space` toggle window floating/tiling
- `Alt+Shift+j` cycle through applications
- `Super+v` creating vertical sub-layout (only in tiling mode)
- `Super+h` creating horizontal sub-layout (only in tiling mode)
- `Super+m` merge current application to parent layout
- `Super+p` calling **shell-console** to launch application

### Documentation
Currently documentation is generated through doxygen. enable `build_doc` option
to enable building documentation. We also host a online themed
[documentation](https://taiwins.org/page_doc.html) which you can access.


### Screenshots
There are some example screen shots of taiwins, check out the [screenshot
page](docs/screenshots.rst) for more details.
