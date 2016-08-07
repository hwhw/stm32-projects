#!./comm
for c=2,7 do
	led_set_16bit(c, 0, {0,0,0,0,0,0,0,0,0,0,0,0})
end
led_commit(0xFF)
