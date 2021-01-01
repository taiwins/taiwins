# Progress and feature track

This is a public list of Taiwins features(finished or not), I have been using
org-mode for tracking taiwins development, the feature list was never known
other than myself. This should be helpful for knowing what taiwins can do.


## libtaiwins feature
libtaiwins is the compositor library tailored for Taiwins compositor, much like
[libweston](https://gitlab.freedesktop.org/wayland/weston/tree/master/libweston).

* [x] Headless backend.
* [x] X11 backend.
* [x] Wayland backend.
  - [ ] Frame re-scheduling on switching workspaces.
  - [ ] Frame time measurement in wayland backend.
  - [ ] Fullscreen surface.
* [ ] DRM backend.
  - login service.
	+ [x] systemd-logind/elogind service.
	+ [x] direct login service.
  - [x] GPU resource enumeration.
  - [x] atomic modesetting.
  - [x] gbm platform.
  - [ ] EGLStream platform.
  - [x] Multi-GPU support.
  - [x] GPU hotplug.
   + [ ] per-GPU render context.
  - [ ] writeback connectors.
* [ ] XWayland.
* [ ] Pipewire screenrecording.
* rendering pipeline.
  - [x] EGL render context.
	- [ ] EGLStream functions. 
  - [ ] Vulkan render context.
* desktop logic.
  - [x] switchable workspace.
  - [x] xdg-shell protocol.
  - layouts support.
	+ [x] floating layout.
	+ [x] tiling layout.
	+ [x] maximized layout.
	+ [x] fullscreend layout.
	+ [x] minimized layout.
	+ [ ] smart layout (auto-select which layout to use).
* desktop shell.
  - [x] taiwins-shell protocol.
  - [x] taiwins-console protocol.
  - [x] taiwins-theme protocol (loading different theme/fonts).
  - [x] wlr-layer-shell protocol(NOT TESTED).

## Compositor
	Taiwins is an instance of a libtaiwins compositor.
	
* [x] lua configuration.
* [ ] DBus configuration service.

## Taiwins Shell
	Taiwins shell provides desktop environment(cursor, wallpaper, panel).
	
* [x] lua configuration.
* [x] taiwins-shell protocol.
* [x] taiwins-theme protocol.
* [x] wallpaper.
* [x] GUI system using twclient-gui.
* [x] panel/task bar.
* [x] dropdown widget.
  - [x] clock widget.
  - [x] battery widget.
  - [x] lua widget environment.
  - [ ] other widgets like (WiFi Connection, alsa sound control).

## Taiwins Console
	Taiwins Console is the application launcher for taiwins, designed like
    ulauncher.

* [x] taiwins-console protocol.
* [x] taiwins-theme protocol.
* [x] Runners architecture.
  - [x] lua runners(writing a runner using lua script).
  - [x] xdg desktop entry runner (loading desktop application)
  - [x] CMD runner.
* [ ] runner feedback.
* [ ] limiting runner threads.

## Taiwins Cache Updater.
* [x] Loading icons.
  - [x] limiting app icons.
* [x] building atlas textures.
* [x] custom cache data format.
