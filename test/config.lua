local os = require('os')
local compositor = require_compositor()

for _,v in ipairs({1,2,3,4,5}) do
   print(v)
end


compositor:bind_key("TW_OPEN_CONSOLE", "C-x,C-c")

-- allow user define functions
function random_function()
   compositor:do_weird_stuff()
end
--this works
compositor:bind_btn(random_function, "C-M-btn_l")

-- compositor:option("bigmac", "#ffffff")
-- compositor:option("bigmac", 88, 87, 86)

dummy = compositor:get_dummy_interface()

for _, tt in ipairs(dummy:get_dummy_table()) do
   print(tt['haha'])
   tt['adfasf'] = "bababa"
end

compositor:set_menus({
      {
	 {'bbbb','cccc'},
	 {'dddd','ffff'}
      },
      {'aaaa', 'bbbb'},
      {'aaa2', 'bbbb2'}
})

-- -- general setup
-- compositor:keyboard_layout("ctrl:swap_lalt_lctl")
-- compositor:set_kb_delay(400)
-- compositor:set_kb_repeat(40)
-- compositor:set_color_format("xrgb8888")


-- -- setup themes,
-- compositor:init_theme('path_to_theme')
-- compositor:text_font("font name/path")

-- -- setup widget
-- compositor:init_widgets('path_to_another_luascript')

-- -- output we have.
-- for output in compositor:get_outputs do
--    if output["type"] == "HDMI" then
--       output:set_scale(1)
--    end
-- end

-- desktop = compositor:get_desktop()

-- for ws in desktop:get_workspace() do
--    ws:set_layout('floating')
-- end

-- -- now more on the usability part
-- compositor:set_default_layout("floating")
-- -- set the environment variables
-- compositor:set_envar("name", "value")

-- -- Then we need to actual set the output by config.

-- weston do output settings by output sections. It finds output by heads names?
-- and then configure with it. So different backends may be able to setup
-- outputs differently.

-- For example, if we have a X11/Wayland windowed output. x11/wayland backends
-- will search for output config starts with X/WL.

-- if we have loaded drm backend. drm backends will setup output for heads like
-- VGA, LVDS.

-- if compositor:under_x11() then
--    -- we know that whether we need to setup the transform
--    output = compositor:x11_output()
--    output:set_scale(2)
--    output:set_transform(270)
-- elseif compositor:under_wayland() then
--    output:set_scale(2)
--    output:set_transform(270)
-- else -- we are having drm output. For now
--    -- we need to know how many output we want.
--    for _, heads in compositor:available_heads() do
--    end
-- end
-- compositor:set_outputs({
--       eDP1 = {
--	 mode = "3200x1800",
--	 scale = 2,
--	 clone = "HDMI1"
--       },
--       HDMI1 = {
--	 mode = "1920x1080",
--	 scale = 1,
--       },
-- })
