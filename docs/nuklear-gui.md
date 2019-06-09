## It is not just GUI element

I've used the nuklear to draw full screen gui element, at the scale of entire
window. For all the gui application case, this is maybe a bad thing. Sure
nuklear is merely a simple gui library, Like QtGui, it draws labels and all
others. But just GUI won't be useful, since GUI application is only used for
controlling what you really want to display, like drawing images, videos. Or a
text memo. In our case, it is used to control the the compositor.

## We have two choice here, first is the foreground and background approach

- The foreground-background approach. Limit the application to draw only one
  thing (other than GUI elements) in particular as the background. The GUI
  elements are treated as foreground. This can be done with `nk_push_custom`, it
  gets excuted in `nk_convert` thus it becomes the background.

### But what can go wrong
	The major problem in the both approach is about buffer managing. If we
	choose this approach, we would need to draw GUI elements in a foreground
	buffer and background element in a seperated buffer. So you only need to
	update one of them at a time. But the price is that you have to blit them
	together, this would be support by different backend though. OpenGL can
	simply run blit with an additional framebuffer. but what about cairo?(It is
	heavy as well).

## `nk_wl_add_custom_window` approach.
- include `bool nk_wl_add_custom_window(app, x ,y ,w ,h)` function as an API,
  this can be used to draw whatever the custom drawing function you want
  (defered). Draw it onto a image is also a solution, we can create a window for
  that image. Image the case of

### But what can go wrong
	This approach has absolutely a disadvantage, if either of the GUI elements,
	or custom_window need to be redraw. Then you would have to redraw them
	all. Nuklear suggested using a `nk_image` and render it as a texture
	`nk_image_id()` and `nk_image_ptr()` then hook it to a nk_window. It is then
	like the foreground-background case. But you would save some time for
	rendering the heaving part all over again, right? For any 3D application, I
	suppose this only works with 3d graphics api like opengl. Managing 2D
	drawing library is difficult though
