local function convertcolor(r, g, b)
  rg = 4*math.floor(r/7)
  rb = 2*math.floor(r/7)
  rr = math.floor(r/7)
  -- move red value to green and blue
  g = math.min(g+rg, 0xFFFF)
  b = math.min(b+rb, 0xFFFF)
  r = rr
  return b, g, r
end

api:effect_add(19, "congress", function(t, data)
  if data.active == nil then
    data.active = false
  end
  if not data.active then return end
  local r,g,b
  for x=0,3 do
    for y=0,5 do
      r,g,b = api:get(x, y)
      r,g,b = convertcolor(r,g,b)
      api:set(x,y,r,g,b)
    end
  end
end)
