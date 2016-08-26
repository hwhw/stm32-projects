local function hue2rgb(p, q, t)
	if t < 0   then t = t + 1 end
	if t > 1   then t = t - 1 end
	if t < 1/6 then return p + (q - p) * 6 * t end
	if t < 1/2 then return q end
	if t < 2/3 then return p + (q - p) * (2/3 - t) * 6 end
	return p
end

local function hslToRgb(h, s, l)
	local r, g, b

	if s == 0 then
		r, g, b = l, l, l -- achromatic
	else

		local q
		if l < 0.5 then q = l * (1 + s) else q = l + s - l * s end
		local p = 2 * l - q

		r = hue2rgb(p, q, h + 1/3)
		g = hue2rgb(p, q, h)
		b = hue2rgb(p, q, h - 1/3)
	end

	return math.floor(r * 0xFFFF), math.floor(g * 0xFFFF), math.floor(b * 0xFFFF)
end

local function clamp(v)
	return v % 1
end

local offs = 0
local last_t = 0
api:effect_add(0, "hsl_saturated", function(t, data)
	local t_diff = t - last_t
	last_t = t
	local s = data.s or 1.0
	local l = data.l or 0.5
	for y=0,5 do
		for x=0,3 do
			local r, g, b = hslToRgb(clamp(offs+y*.15+x*0.25), s, l)
			api:set(x,y,r,g,b)
		end
	end
	offs = clamp(offs + t_diff/40)
end)
