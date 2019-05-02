local x = 0
local y = 0
local stage = 0
api:effect_add(14, "spread", function(t, data)
  if not data.active then return end
  if y >= 5 and x >= 3 then
    x,y,stage = 0,0,0
    data.active = nil
    return
  end
  stage = stage + 1
  if not (stage%10 == 0) then return end
  local r,g,b = api:get(x, y)
  if y == 0 then
	  y = x+1
	  x = 0
  else
	  y = y-1
	  x = x+1
  end
  if x >= 4 then
	  x = y
	  y = 5
  end
  api:set(x,y,r,g,b)
end)
