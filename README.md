## Taiwins, a morden wayland window manager(currently under migration to wlroots backend, stay tuned)

Taiwins is a dynamic wayland window manager, supports both tiling and floating
layout. It is designed to be mordern and modular. It is extensible through lua
script and it has built-in **shell** and **widgets** implementation through
[nuklear GUI](https://github.com/vurtun/nuklear). It also supports popular
tiling window manager features like gapping.

Taiwins is usable and currently under heavy developement. If you would like to
contribute to the project, You can refer to [contributing](CONTRIBUTING.md) for
getting started.

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
- opengl or opengles
- meson
- ninja
- pam
- fontconfig
- freetype2

## build steps:
with source code, you can easily compile and try out:

	git clone https://github.com/taiwins/taiwins --recursive taiwins && cd taiwins

	meson wrap promote subprojects/twclient/subprojects/ctypes
	
	meson build && ninja -C build
	
For those who use Archlinux, there is an
[aur](https://aur.archlinux.org/packages/taiwins-git) package you can simply install.

## How to run

Taiwins starts with default shell and default console they are found. You can
also specifiy the shell application and console application through command line
options.

	cd build
	./bin/taiwins -s ./bin/taiwins-shell -c ./bin/taiwins-console
	
Or if you install systemwisely, you can simply use

	taiwins 

If you prefer not to have the shell, try `taiwins -n` which will make taiwins
run without shell, user can start a shell later.

The default configuration is `$XDG_CONFIG_PATH/taiwins/config.lua`, see the
[sample config](docs/config.lua) for example.

### key-bindings
Though it is configurable, by default available bindings are

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
