api:effect_add(20, "lightning", function(t, data)
	if not data.active then return end
	if math.random() < (data.probability or 0.01) then
		for y = 0,5 do
			for x = 0,3 do api:set(x,y,0xFFFF,0xFFFF,0xFFFF) end
		end
	end
end)

