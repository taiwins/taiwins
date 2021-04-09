local os = require "os"
local taiwins = require "taiwins"
-- get the compositor object from taiwins
local compositor = taiwins.compositor()

-- general setup
compositor:panel_pos("bottom")
compositor:set_gaps(20, 20)
compositor:repeat_info(20, 500)

-- matching display add setting mode
compositor:config_display("X11-0", {
			     ["enable"] = true,
			     ["position"] = {0, 0},
			     ["mode"] = "1000x600"})

-- compositor:lock_in(5) -- onlonger available at the moment
-- compositor:wake() --this wakes up the compositor --on longer available at the moment

-- bind builtin keybindings
compositor:bind_key("TW_TOGGLE_FLOATING", "s-t")
compositor:bind_key("TW_OPEN_CONSOLE", "C-x,C-p")

-- bind user defined lua functions
function random_function()
   os.execute('weston-terminal&')
end
compositor:bind_key(random_function, "C-M-enter")

for _,ws in ipairs(compositor:workspaces()) do
   ws:set_layout('floating')
end
