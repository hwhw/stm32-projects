local function clip(x)
	if x < 0.0 then return 0.0 end
	if x > 1.0 then return 1.0 end
	return x
end

local phase   = 0      -- 0 = filter does nothing, 1 = filter blocks light
local fadein  = 0.003  -- speed of turning light on
local fadeout = 0.01   -- speed of turning light off

api:effect_add(9000, "vortragsmodus", function(t, data)
	if not data.active then 
		if phase <= 0 then return end
		phase = clip(phase - fadein)
	else
		phase = clip(phase + fadeout)
	end

	-- compute factor for alpha
	local amul = phase^2 * (3 - 2 * phase)  -- smooth curve
	amul = 1 - amul                         -- negate
	amul = amul + (amul^2 - amul) * 0.9     -- gamma correction
	amul = 1 - amul                         -- negate

	-- darken some pixels...
	api:set(3,0, 0,0,0, amul)
	api:set(2,0, 0,0,0, amul)
	api:set(1,0, 0,0,0, 0.66*amul)
	api:set(3,1, 0,0,0, amul)
	api:set(2,1, 0,0,0, amul)
	api:set(1,1, 0,0,0, 0.66*amul)
	api:set(3,2, 0,0,0, amul)
	api:set(2,2, 0,0,0, amul)
	api:set(1,2, 0,0,0, 0.66*amul)
end)
