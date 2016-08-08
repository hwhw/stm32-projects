#!./comm
local M=require"moodlib"

local offs = 1
while true do
	for x=1,4 do
		for y=1,6 do
			local col = (offs + x + y) % 3
			if col == 0 then
				M.set(x,y,0xFFFF,0,0)
			elseif col == 1 then
				M.set(x,y,0,0xFFFF,0)
			else
				M.set(x,y,0,0,0xFFFF)
			end

		end
	end
	M.commit()
	offs = (offs + 1) % 3
	usleep(1000000)
end
