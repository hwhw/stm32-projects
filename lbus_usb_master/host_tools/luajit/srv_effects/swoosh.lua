local y = -1
api:effect_add(10, "swoosh", function(t, data)
	y = y + .02
	local start = math.floor(y)
	if start >= 6 then return true end
	local s1 = y % 1
	local s2 = 1.0 - s1
	if start+1 < 6 then
		for x = 0,3 do api:set(x, start+1, 0, 0, 0, s1) end
	end
	if start >= 0 then
		for x = 0,3 do api:set(x, start, 0, 0, 0, s2) end
	end
end)
