local function rndcolor()
  local r, g, b = 0,0,0
  local no = math.random(0,2)    -- 0:r 1:g 2:b
  local p = math.random()
  if no == 0 then
    g = math.floor(p * 0xFFFF)
    b = 0xFFFF-g
  end
  if no == 1 then
    r = math.floor(p * 0xFFFF)
    b = 0xFFFF-r
  end
  if no == 2 then
    r = math.floor(p * 0xFFFF)
    g = 0xFFFF-r
  end
  return r, g, b
end

local stage = 0
api:effect_add(20, "red_alert", function(t, data)
	if not data.active then return end
	if stage > 200 then
		stage = 0
		for y=0,5 do
			for x=0,3 do
				local r, g, b = rndcolor()
				api:set(x,y,r,g,b)
			end
		end
		data.active = nil
		return
	end
	for y = 0,5 do
		for x = 0,3 do
			api:set(x,y,0xFFFF*math.abs(((stage/25)%2)-1),0,0)
		end
	end
	stage = stage + 1
end)
