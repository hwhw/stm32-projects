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

local default = {
  area = {
    { 1, 1, 1, 1 },
    { 1, 1, 1, 1 },
    { 3, 3, 3, 3 },
    { 3, 2, 2, 2 },
    { 4, 2, 2, 2 },
    { 4, 2, 2, 2 }},
  colors = {
    { H = 0, S = 0, L = 0 },
    { H = 60/360, S = 70/100, L = 10/100 },
    { H = 60/360, S = 70/100, L =  1/100 },
    { H = 0, S = 0, L = 3/100 }}
}

api:effect_add(1, "fix", function(t, data)
  if not data.active then return end
  for y=0,5 do
    for x=0,3 do
      local area_matrix = data.area or default.area
      local area_y = area_matrix[y+1] or default.area[y+1]
      local area = area_y[x+1] or default.area[y+1][x+1]
      local colors = data.colors or default.colors
      local color = colors[area] or default.colors[area]
      local H, S, L
      if color.H and color.S and color.L then
        H, S, L = color.H, color.S, color.L
      else
        -- dark/red
        H, S, L = 0, 40/100, 20/100
      end
      local r, g, b = hslToRgb(H, S, L)
      api:set(x,y,r,g,b)
    end
  end
end)
