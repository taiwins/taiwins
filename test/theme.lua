local style = {
	['text'] = {
		['color'] = '#000000'
	},
	['button'] = {
		['normal'] = buttonNormal,
		['hover'] = buttonHover,
		['active'] = buttonActive,
		['text background'] = '#00000000',
		['text normal'] = '#000000',
		['text hover'] = '#000000',
		['text active'] = '#ffffff'
	},
	['checkbox'] = {
		['normal'] = checkboxOff,
		['hover'] = checkboxOff,
		['active'] = checkboxOff,
		['cursor normal'] = checkboxOn,
		['cursor hover'] = checkboxOn,
		['text normal'] = '#000000',
		['text hover'] = '#000000',
		['text active'] = '#000000',
		['text background'] = '#d3ceaa'
	},
}
local dummy = {}
read_theme(dummy, style)
