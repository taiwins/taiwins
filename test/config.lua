-- okay, let us write a sample configuration
-- refuse to stop if configuration has errors
local os = require('os')
local compositor = taiwins_require_compositor()
local shell = compositor:require_shell()

-- do you really want to bind all the functions into lua functions?
compositor:bind_key(compositor.close, kbd("C-xC-c"))
-- or we do this
compositor:bind_key("close_taiwins", kbd("C-xC-c"))

-- allow user define functions
function random_function()
   compositor:do_weird_stuff()
end
-- then we can do this
compositor:bind_axis(random_function, kbd("C-up"))

-- general setup
compositor:set_key_layout("ctrl:swap_lalt_lctl")
compositor:set_kb_delay(400)
compositor:set_kb_repeat(40)
compositor:set_color_format("xrgb8888")

-- now more on the usability part
compositor:set_default_layout("floating")

-- outputs, anythins
compositor
-- try to choose a theme. This theme does not

-- we can have the compositor to read about themes and verify its
-- existence. then send it to the shell later.
-- or you can implement the same set of the api, for client you can simply
-- ignore a bunch of stuff
shell:set_theme("light_blue")
shell:set_position("top")

-- again, this is
shell:set_wallpaper(os.getenv("XDG_CONFIG_DIR") .. "/taiwins/wallpaper.png")
-- set a group of wallpapers
shell:set_wallpapers(os.getenv("XDG_CONFIG_DIR") .. "/taiwins/wallpapers")
shell:set_wallpaper_chang_interval(300) -- 300s
shell:set_widget_dir(os.getenv("XDG_CONFIG_DIR") .."/taiwins/widgets")
shell:set_cursor_theme("highcolor")
shell:set_cursor_size(24)

-- set menus
shell:set_menu({
      theme = {
	 width = 200,
      },
      items = {
	 {
	    "log out", function () compositor:quit() end
	 },
      },
})

-- this is really nothing, mostly about setting up keybindings and define some
-- small functions. We are not utilizing something really worth the effort of
-- lua actually
