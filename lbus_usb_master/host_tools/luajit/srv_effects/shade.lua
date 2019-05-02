-- clamp value x to range [mi,ma]
local function clip(x,mi,ma)
    if x < mi then return mi end
    if x > ma then return ma end
    return x
end

local function approach(a,b,delta)
	if math.abs(a-b) < delta then
		return b
	else
		if a < b then
			return a + delta
		else
			return a - delta
		end
	end
end

local sat = 1    -- default saturation
local white = 0  
local gain_r = 1
local gain_g = 1
local gain_b = 1

api:effect_add(6677, "shade", function(t, data)
    if data.active == nil then
        data.active = true
    end
    if not data.active then return end
    sat = approach( sat, clip(data.sat or 1.0, 0, 3), 0.01 )
    white = approach( white, clip(data.white or 0.0, 0, 1), 0.01 )
    gain_r = approach( gain_r, clip(data.r or 1.0, 0, 3), 0.01 )
    gain_g = approach( gain_g, clip(data.g or 1.0, 0, 3), 0.01 )
    gain_b = approach( gain_b, clip(data.b or 1.0, 0, 3), 0.01 )

    local gwhite = white * (0.5 + 0.5 * white)
    local keep = 1 - gwhite
    for x = 0,3 do
        for y = 0,5 do
            local r,g,b = api:get(x,y)
            -- normalized colorspace similar to sRGB...
            local wr = math.sqrt(clip(r / 0xFFFF, 0, 1))
            local wg = math.sqrt(clip(g / 0xFFFF, 0, 1))
            local wb = math.sqrt(clip(b / 0xFFFF, 0, 1))
            -- approximate luminosity...
            local lumi = 0.3 * wr + 0.5 * wg + 0.2 * wb
            -- apply saturation...
            wr = clip(lumi + (wr - lumi) * sat, 0, 2)
            wg = clip(lumi + (wg - lumi) * sat, 0, 2)
            wb = clip(lumi + (wb - lumi) * sat, 0, 2)
            -- convert to linear RGB, apply whiteness & gains...
            r = clip((keep * wr^2 + gwhite) * gain_r, 0, 1) * 0xFFFF
            g = clip((keep * wg^2 + gwhite) * gain_g, 0, 1) * 0xFFFF
            b = clip((keep * wb^2 + gwhite) * gain_b, 0, 1) * 0xFFFF
            api:set(x,y, r, g, b)
        end
    end

    error(string.format( "sat=%s, white=%s, r=%s, g=%s, b=%s",
                         tostring(sat), tostring(white),
                         tostring(gain_r), tostring(gain_g), tostring(gain_b) ))
end)
