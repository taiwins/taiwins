os = require "os"
compositor = require "taiwins"

compositor:bind_key("TW_OPEN_CONSOLE", "C-x,C-p")
compositor:lock_in(5)
compositor:wake() --this wakes up the compositor

compositor:panel_position("bottom")

desktop = compositor:desktop()
for _,ws in ipairs(desktop:workspaces()) do
   ws:set_layout('floating')
end
