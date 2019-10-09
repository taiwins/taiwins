os = require "os"
compositor = require_compositor()

compositor:bind_key("TW_OPEN_CONSOLE", "C-x,C-p")
compositor:lock_in(5)

shell = compositor:shell()
shell:panel_position("bottom")
shell:set_wallpaper("/tmp/wallpaper.png")
shell:set_menus({
      {
	 {'bbbb','cccc'},
	 {'dddd','ffff'}
      },
      {'aaaa', 'bbbb'},
      {'aaa2', 'bbbb2'}
})
-- shell:init_widgets("safjklksajfla")

desktop = compositor:desktop()
for _,ws in ipairs(desktop:workspaces()) do
   ws:set_layout('floating')
end
