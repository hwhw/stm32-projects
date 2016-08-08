#!./comm
--[[
Switch off all channels on all controllers
--]]

led_set_16bit(c, 0xFF, {0,0,0,0,0,0,0,0,0,0,0,0})
led_commit(0xFF)
