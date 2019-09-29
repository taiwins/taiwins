os = require('os')
widgets = require('twwidgets')

-- we would also need to
function sample_anchor(wig)
   return "sample0"
end

function sample_anchor1(wig)
   return "sample1"
end

local comboText = 'Combo 1'

function combo(ui)
	ui:layoutRow('dynamic', 30, 1)
	if ui:comboboxItem('Combo 1') then
		print 'Closure: Combo 1'
		comboText = 'Combo 1'
	end
	if ui:comboboxItem('Combo 2') then
		print 'Closure: Combo 2'
		comboText = 'Combo 2'
	end
	if ui:comboboxItem('Combo 3') then
		print 'Closure: Combo 3'
		comboText = 'Combo 3'
	end
end


function sample_drawfunc(ui, wig)
	ui:layoutRow('dynamic', 30, 1)
	ui:combobox(comboText, combo)
end

-- register method 1
widgets.new_widget({
      name = "sample",
      brief = sample_anchor,
      draw = sample_drawfunc,
      --file_watch = "/proc/uptime",
      timer = 40,
      width = 100,
      height = 100,
      --device_watch = "/sys/class/backlight",
})

-- register method 2
w = widgets.new_widget()
w:brief(function (wig) return 'aaa' end)
w:watch_file('/proc/cpuinfo')
w:width(200)
w:height(100)
w:register()

widgets.add_builtin('clock')
