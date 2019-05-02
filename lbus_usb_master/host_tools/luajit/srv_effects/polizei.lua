local stage = 0
api:effect_add(20, "polizei", function(t, data)
 if not data.active then return end
 if stage > 200 then
  stage = 0
  data.active = nil
 end
 for y = 0,5 do
  for x = 0,3 do
   api:set(x,y,0,0,0xFFFF*math.abs(((stage/25)%2)-1))
  end
 end
 stage = stage + 1
end)


