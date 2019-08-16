-- okay, let us write a sample configuration
-- refuse to stop if configuration has errors
local os = require('os')
local compositor = require_compositor()

-- or we do this
compositor:bind_key("TW_OPEN_CONSOLE", "C-x,C-c")

-- allow user define functions
function random_function()
   compositor:do_weird_stuff()
end
--this works
compositor:bind_btn(random_function, "C-M-btn_l")

-- general setup
compositor:keyboard_layout("ctrl:swap_lalt_lctl")
compositor:set_kb_delay(400)
compositor:set_kb_repeat(40)
compositor:set_color_format("xrgb8888")

-- more advanced use: we can have dynamic configuration that takes input and
-- produce output. For example we can setup the weston_output based on whatever
-- output we have.
-- for output in compositor:get_outputs do
--     if output["type"] == "HDMI"
--        compositor:set_scale(output, 1)
--     end
-- end

-- now more on the usability part
compositor:set_default_layout("floating")
-- set the environment variables
compositor:set_envar("name", "value")

-- outputs, you can setup the default rules for creating output. Or
-- you can actually make a function for that. Default rules could be like
compositor:set_output_rule("do_nothing")
-- or you can do this
compositor[outputs] = {
   eDP1 = {
      mode =  "3200x1800",
      scale = 2,
      clone = "HDMI1"
   },
   HDMI1 = {
      mode = "1920x1080",
      scale = 1,
   }
}

-- Then we need to actual set the output by config.
compositor:set_outputs({
      eDP1 = {
	 mode = "3200x1800",
	 scale = 2,
	 clone = "HDMI1"
      },
      HDMI1 = {
	 mode = "1920x1080",
	 scale = 1,
      },
})

-- try to choose a theme. This theme does not
-- we can have the compositor to read about themes and verify its
-- existence. then send it to the shell later.
-- or you can implement the same set of the api, for client you can simply
-- ignore a bunch of stuff
shell:set_theme_dir("")
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

-- set right click menus
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
