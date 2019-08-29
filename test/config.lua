local os = require('os')
local compositor = require_compositor()

for v = 1,2,3,4,5 do
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

for tt = dummy:get_dummy_table() do
   print(tt)
   -- print(tt['dummy_field'])
end

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
-- compositor:set_outputs({
--       eDP1 = {
-- 	 mode = "3200x1800",
-- 	 scale = 2,
-- 	 clone = "HDMI1"
--       },
--       HDMI1 = {
-- 	 mode = "1920x1080",
-- 	 scale = 1,
--       },
-- })

