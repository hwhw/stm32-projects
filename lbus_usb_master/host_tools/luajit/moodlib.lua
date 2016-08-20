--[[
Convenience library for controlling a LED matrix via LBUS
--]]
local lbus = require"lbuscomm"
local lbus_ctx = lbus.open()
local ffi = require"ffi"
ffi.cdef[[
const static int LED_SET_16BIT = 10;
const static int LED_COMMIT = 11;
struct lbus_hdr {
	uint16_t length;
	uint8_t addr;
	uint8_t cmd;
} __attribute__((packed));
struct led_cols {
	uint16_t r;
	uint16_t g;
	uint16_t b;
} __attribute__((packed));
struct led_set {
	struct lbus_hdr hdr;
	uint16_t led;
	struct led_cols v[4];
} __attribute__((packed));
struct led_all {
	struct led_set A;
	struct led_set B;
	struct led_set C;
	struct led_set D;
	struct led_set E;
	struct led_set F;
	struct lbus_hdr commit;
} __attribute__((packed));
int usleep(int useconds);
]]
local T = require"timelib"
local bit = require"bit"
local function le32(x) return x end 
local le16 = le32 
if ffi.abi("be") then 
	local shr = bit.rshift 
	le32 = bit.bswap 
	function le16(x) return shr(le32(x), 16) end 
end 

local cmd = ffi.new("struct led_all")
cmd.A.hdr.length = le16(ffi.sizeof("struct led_set"))
cmd.A.hdr.addr = 0x02
cmd.A.hdr.cmd = ffi.C.LED_SET_16BIT
cmd.B.hdr.length = cmd.A.hdr.length
cmd.B.hdr.addr = 0x03
cmd.B.hdr.cmd = ffi.C.LED_SET_16BIT
cmd.B.hdr.length = cmd.A.hdr.length
cmd.C.hdr.addr = 0x04
cmd.C.hdr.cmd = ffi.C.LED_SET_16BIT
cmd.C.hdr.length = cmd.A.hdr.length
cmd.D.hdr.addr = 0x05
cmd.D.hdr.cmd = ffi.C.LED_SET_16BIT
cmd.D.hdr.length = cmd.A.hdr.length
cmd.E.hdr.addr = 0x06
cmd.E.hdr.cmd = ffi.C.LED_SET_16BIT
cmd.E.hdr.length = cmd.A.hdr.length
cmd.F.hdr.addr = 0x07
cmd.F.hdr.cmd = ffi.C.LED_SET_16BIT
cmd.F.hdr.length = cmd.A.hdr.length
cmd.commit.length = le16(ffi.sizeof("struct lbus_hdr"))
cmd.commit.addr = 0xFF
cmd.commit.cmd = ffi.C.LED_COMMIT

local ledbuf = ffi.new("struct led_cols[4][6]")

--[[
	{{2,0},{2,3},{3,1},{3,0},{3,3},{3,2}},
	{{2,1},{2,2},{4,2},{4,0},{4,3},{4,1}},
	{{7,1},{7,0},{5,1},{5,0},{5,2},{5,3}},
	{{7,2},{7,3},{6,3},{6,2},{6,0},{6,1}}
]]

local L = {}

-- set LED to a certain color
function L.set(x, y, r, g, b)
	assert(x>=1)
	assert(x<=4)
	assert(y>=1)
	assert(y<=6)
	ledbuf[x-1][y-1].r = r
	ledbuf[x-1][y-1].g = g
	ledbuf[x-1][y-1].b = b
end

local function to_le16(dst, src)
	dst.r = le16(src.r)
	dst.g = le16(src.g)
	dst.b = le16(src.b)
end
-- commit all accumulated LED updates
function L.commit()
	to_le16(cmd.A.v[0], ledbuf[0][0])
	to_le16(cmd.A.v[3], ledbuf[0][1])
	to_le16(cmd.B.v[1], ledbuf[0][2])
	to_le16(cmd.B.v[0], ledbuf[0][3])
	to_le16(cmd.B.v[3], ledbuf[0][4])
	to_le16(cmd.B.v[2], ledbuf[0][5])
	to_le16(cmd.A.v[1], ledbuf[1][0])
	to_le16(cmd.A.v[2], ledbuf[1][1])
	to_le16(cmd.C.v[2], ledbuf[1][2])
	to_le16(cmd.C.v[0], ledbuf[1][3])
	to_le16(cmd.C.v[3], ledbuf[1][4])
	to_le16(cmd.C.v[1], ledbuf[1][5])
	to_le16(cmd.F.v[1], ledbuf[2][0])
	to_le16(cmd.F.v[0], ledbuf[2][1])
	to_le16(cmd.D.v[1], ledbuf[2][2])
	to_le16(cmd.D.v[0], ledbuf[2][3])
	to_le16(cmd.D.v[2], ledbuf[2][4])
	to_le16(cmd.D.v[3], ledbuf[2][5])
	to_le16(cmd.F.v[2], ledbuf[3][0])
	to_le16(cmd.F.v[3], ledbuf[3][1])
	to_le16(cmd.E.v[3], ledbuf[3][2])
	to_le16(cmd.E.v[2], ledbuf[3][3])
	to_le16(cmd.E.v[0], ledbuf[3][4])
	to_le16(cmd.E.v[1], ledbuf[3][5])
	-- keep data to send via USB <64 byte per transmit command
	-- otherwise, it will get sent just fine - however, bad USB
	-- controllers (I'm looking at you, Netgear WNDR3600!) with
	-- stupid drivers will send just 1 single bulk packet per 1ms
	-- which is bad for full speed USB, since that means 64byte per
	-- 1ms - and this will generate too long pauses on the light bus
	-- as a result, so controllers will drop packages as incomplete
	-- if those pauses happen in the middle of a packet.
	lbus.check(lbus_ctx:lbus_tx(cmd.A, ffi.sizeof(cmd.A)*2))
	lbus.check(lbus_ctx:lbus_tx(cmd.C, ffi.sizeof(cmd.C)*2))
	lbus.check(lbus_ctx:lbus_tx(cmd.E, ffi.sizeof(cmd.E)*2))
	lbus.check(lbus_ctx:lbus_tx(cmd.commit, ffi.sizeof(cmd.commit)))
end

L.usleep = ffi.C.usleep

return L
