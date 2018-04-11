# Weston-desktop API

The API essentially implements the `xdg_shell_protocols`, avec ceci, on as les
puissance de décider quoi faire à le moment de crée l'application. On peut
faire les décisions à les dimension des fenêtres, les positions ou priorités.

## weston_layer
`weston_layer`s are stored from top to bottom in the compositor, in that case
`compositor->layer_header->top_layer->...bottom_layer`. The same priority
pattern applies within the layer. So the top view is the first view in the
layer's `view_link`. When compositor starts painting, it will build the
view_list from all the layers(priority top to down). With the information, the
compositor can figure out which parts of the `weston_plane` (a `pixman_region`)
are damaged.

## weston_plane
I am actually not sure what exactly it is, my guess is that this plane is the
entire rendering output. I guess the plane is kind of organized in a smart
way. Also it seems both `gl_renderer` and `pixman_render` do the same
thing. So once you insert the layer to compositor, it becomes part of the
rendering pipeline, so we have to optimize the layer_list!

	wl_list_for_each_reverse(view, compositor->view_list, link)
		if (view->plane == &compositor->primary_plane)
			draw_view(view);

### the pixman_region_t
`pixman_region16_t` or `pixman_region32_t` are both regions, because it contains
boxes(x,y,w,h).


### weston-renderer implementation
