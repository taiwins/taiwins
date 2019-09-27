os = require('os')
widgets = require('twwidgets')

-- we would also need to
function sample_anchor(wig)
   c = wig:file_content()
   symbols = ''
   if tonumber(c) > 75 then
      symbols = symbols .. theme.symbol('battery_full')
   elseif tonumber(c) > 25 then
      symbols = symbols .. theme.symbol('battery_half')
   end
   return symbols
end

function sample_drawfunc(wig, ui)
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

-- register method 1
widgets.new_widget({
      name = "sample",
      brief = sample_anchor,
      draw = sample_drawfunc,
      --file_watch = "/proc/uptime",
      timer = 40,
      --device_watch = "/sys/class/backlight",
})


-- register method 2
w = widgets.new_widget()
w:brief(function () return 'aaa' end)
w:watch_file('/proc/cpuinfo')
-- w:add_timer('3s')
-- w:watch_device('/sys/class/ata_device/')
w:register()

widgets.add_builti('clock')
