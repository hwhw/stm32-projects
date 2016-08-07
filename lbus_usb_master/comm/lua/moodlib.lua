local leds = {
	{{2,0},{2,3},{3,1},{3,0},{3,3},{3,2}},
	{{2,1},{2,2},{4,2},{4,0},{4,3},{4,1}},
	{{7,1},{7,0},{5,1},{5,0},{5,2},{5,3}},
	{{7,2},{7,3},{6,3},{6,2},{6,0},{6,1}}
}

local L = {}

local dirty = {}

function L.set(x, y, r, g, b)
	local controller = leds[x][y][1]
	dirty[controller] = true
	led_set_16bit(controller, leds[x][y][2]*3, {r, g, b})
end

function L.commit()
	for c, _ in pairs(dirty) do
		led_commit(c)
	end
	dirty = {}
end

return L
