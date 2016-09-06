-- LuaJIT builtin libs:
local ffi = require"ffi"
local bit = require"bit"
-- LJSyscall
local S = require "syscall"
local t, c = S.t, S.c
-- lbuscomm.lua
local lbus
if arg[1] and arg[1] == "emu" then
	lbus = {
		open = function() return {
			lbus_tx = function() end
		} end,
		check = function() end
	}
else
	lbus = require"lbuscomm"
end
local lbus_ctx = lbus.open()
-- some datastructures for busmaster communication and LED data
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
]]
-- endianess conversion:
local function le32(x) return x end 
local le16 = le32 
if ffi.abi("be") then 
	local shr = bit.rshift 
	le32 = bit.bswap 
	function le16(x) return shr(le32(x), 16) end 
end 

-- prepare buffers for busmaster communication
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

-- convenience wrapper to do endianess conversion on all the values
-- of an LED at once
local function to_le16(dst, src)
	dst.r = le16(src.r)
	dst.g = le16(src.g)
	dst.b = le16(src.b)
end

-- assert wrapper for proper error messages - about the same as
-- the normal Lua assert, except that does not do the tostring()
-- step
local function assert(cond, s, ...)
	if cond == nil then error(tostring(s)) end
	return cond, s, ...
end

-- wrapper for epoll()
local maxevents = 1024
local function nilf() return nil end
local poll = {
	init = function(this)
		return setmetatable({fd = assert(S.epoll_create())}, {__index = this})
	end,
	event = t.epoll_event(),
	add = function(this, s)
		local event = this.event
		event.events = c.EPOLL.IN
		event.data.fd = s:getfd()
		assert(this.fd:epoll_ctl("add", s, event))
	end,
	del = function(this, s)
		local event = this.event
		event.events = c.EPOLL.IN
		event.data.fd = s:getfd()
		assert(this.fd:epoll_ctl("del", s, event))
	end,
	events = t.epoll_events(maxevents),
	get = function(this)
		local f, a, r = this.fd:epoll_wait(this.events)
		if not f then
			print("error on fd", a)
			return nilf
		else
			return f, a, r
		end
	end,
	eof = function(ev) return ev.HUP or ev.ERR or ev.RDHUP end,
}

assert(S.signal("pipe", "ign"))

local ep = poll:init()

-- set up TCP socket to listen on
local s = assert(S.socket("inet", "stream, nonblock"))
s:setsockopt("socket", "reuseaddr", true)
local sa = assert(t.sockaddr_in(1167, "0.0.0.0"))
assert(s:bind(sa))
assert(s:listen(128))

-- add socket to sockets that epoll() checks
ep:add(s)

-- set up timerfd for LED updates
local tfd = assert(S.timerfd_create("MONOTONIC", 0))
local interval = {0,20000000} -- 20ms = 50 Hz
S.timerfd_settime(tfd, 0, {interval, interval}, nil)

-- also handle this socket with epoll()
ep:add(tfd)


-- the API we present via the network socket
-- and towards effect functions registered there:
local ledapi = {
	effectstack = {},
	io = nil,
	ledbuf = ffi.new("struct led_cols[4][6]"),
}

-- set LED to color
function ledapi:set(x, y, r, g, b, a_r, a_g, a_b)
	assert(x>=0)
	assert(x<=3)
	assert(y>=0)
	assert(y<=5)
	local a_r = a_r or 1.0
	local a_g = a_g or a_r
	local a_b = a_b or a_g
	self.ledbuf[x][y].r = self.ledbuf[x][y].r * (1.0-a_r) + r*a_r
	self.ledbuf[x][y].g = self.ledbuf[x][y].g * (1.0-a_g) + g*a_g
	self.ledbuf[x][y].b = self.ledbuf[x][y].b * (1.0-a_b) + b*a_b
end

-- get LED color
function ledapi:get(x, y)
	assert(x>=0)
	assert(x<=3)
	assert(y>=0)
	assert(y<=5)
	return self.ledbuf[x][y].r, self.ledbuf[x][y].g, self.ledbuf[x][y].b
end

-- this handles a timer tick: run all the effects
function ledapi:tick()
	local start = S.clock_gettime("MONOTONIC").time
	local tstart = start
	local delete = {}
	for offs, effect in pairs(self.effectstack) do
		local ok, err = pcall(effect.func, tstart, effect.data)
		local now = S.clock_gettime("MONOTONIC").time
		local dur = now - start
		start = now
		if not ok then
			effect.lasterr = err
		else
			if err then table.insert(delete, 1, offs) end
			effect.lastdur = dur
		end
	end
	for _, offs in pairs(delete) do
		self.effectstack[offs] = nil
	end
end

-- output an effect list
function ledapi:effect_list()
	if self.io then
		self.io:write("PRIO\tLOAD\tNAME\tLASTERR\n")
		for prio, effect in pairs(self.effectstack) do
			self.io:write(string.format("%d\t%02.2f%%\t%s\t%s\n", effect.prio, 100*effect.lastdur*50, effect.name, effect.lasterr))
		end
	end
end

-- add an effect to the stack
function ledapi:effect_add(prio, name, func)
	local pos = 1
	for offs, effect in pairs(self.effectstack) do
		if effect.prio < prio then pos = offs + 1 end
	end
	table.insert(self.effectstack, pos, { prio=prio, name=name, func=func, lastdur=-1, lasterr="", data={} })
	if self.io then
		self.io:write("EFFECT ADDED\n")
	end
end

-- remove an effect from the stack
function ledapi:effect_del(name)
	if name ~= "COMMIT" then
		local delete = nil
		for offs, effect in pairs(self.effectstack) do
			if effect.name == name then
				delete = offs
			end
		end
		if delete then
			self.effectstack[delete] = nil
			self.io:write("EFFECT REMOVED\n")
		end
	end
end

-- return an effect for data manipulation
function ledapi:effect_set(name, dataitem, value)
	for _, effect in pairs(self.effectstack) do
		if effect.name == name then
			effect.data[dataitem] = value
		end
	end
end

-- fixed "effect" on the effectstack is the commit, i.e. pushing all the
-- data to the busmaster in order to send it out to the controllers
-- We make this an "effect" so it is measured properly and diagnostic
-- data is shown when listing the effects
ledapi:effect_add(-1, "COMMIT", function(t)
--[[ LEDs on the roof: {controller, no}
	{{2,0},{2,3},{3,1},{3,0},{3,3},{3,2}},
	{{2,1},{2,2},{4,2},{4,0},{4,3},{4,1}},
	{{7,1},{7,0},{5,1},{5,0},{5,2},{5,3}},
	{{7,2},{7,3},{6,3},{6,2},{6,0},{6,1}}
]]
	to_le16(cmd.A.v[0], ledapi.ledbuf[0][0])
	to_le16(cmd.A.v[3], ledapi.ledbuf[0][1])
	to_le16(cmd.B.v[1], ledapi.ledbuf[0][2])
	to_le16(cmd.B.v[0], ledapi.ledbuf[0][3])
	to_le16(cmd.B.v[3], ledapi.ledbuf[0][4])
	to_le16(cmd.B.v[2], ledapi.ledbuf[0][5])
	to_le16(cmd.A.v[1], ledapi.ledbuf[1][0])
	to_le16(cmd.A.v[2], ledapi.ledbuf[1][1])
	to_le16(cmd.C.v[2], ledapi.ledbuf[1][2])
	to_le16(cmd.C.v[0], ledapi.ledbuf[1][3])
	to_le16(cmd.C.v[3], ledapi.ledbuf[1][4])
	to_le16(cmd.C.v[1], ledapi.ledbuf[1][5])
	to_le16(cmd.F.v[1], ledapi.ledbuf[2][0])
	to_le16(cmd.F.v[0], ledapi.ledbuf[2][1])
	to_le16(cmd.D.v[1], ledapi.ledbuf[2][2])
	to_le16(cmd.D.v[0], ledapi.ledbuf[2][3])
	to_le16(cmd.D.v[2], ledapi.ledbuf[2][4])
	to_le16(cmd.D.v[3], ledapi.ledbuf[2][5])
	to_le16(cmd.F.v[2], ledapi.ledbuf[3][0])
	to_le16(cmd.F.v[3], ledapi.ledbuf[3][1])
	to_le16(cmd.E.v[3], ledapi.ledbuf[3][2])
	to_le16(cmd.E.v[2], ledapi.ledbuf[3][3])
	to_le16(cmd.E.v[0], ledapi.ledbuf[3][4])
	to_le16(cmd.E.v[1], ledapi.ledbuf[3][5])
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
end)


-- socket management for epoll()
local w = {}
local bufsize = 4096
local buffer = t.buffer(bufsize)
local ss = t.sockaddr_storage()

local conn_mt = {}
function conn_mt:read()
	local n = self.fd:read(buffer, bufsize)
	if n>0 then
		-- accumulate Lua code from socket
		table.insert(self.code, ffi.string(buffer, n))
	else
		-- other end closed connection, now run the Lua code
		-- that was sent
		ep:del(self.fd)
		local i = 0
		local c = self.code
		local fn, err = load(function() i = i + 1; return c[i] end, nil)
		self.code = nil
		if fn == nil then
			self.fd:write("ERROR LOAD:\n")
			self.fd:write(err)
			self.fd:write("\nBYE\n")
		else
			ledapi.io = self.fd
			local ok, err = pcall(fn, ledapi)
			if ok then
				self.fd:write("OK\nBYE\n")
			else
				self.fd:write("ERROR RUN:")
				self.fd:write(err)
				self.fd:write("\nBYE\n")
			end
			ledapi.io = nil
		end
		self.fd:close()
	end
end

-- helper to set up a new connection object
local function new_connection(fd)
	local conn = {
		fd = fd,
		code = {"local api=...\n"},
	}
	setmetatable(conn, { __index = conn_mt })
	return conn
end

function poll()
	for i, ev in ep:get() do

		if ep.eof(ev) then
			w[ev.fd].fd:close()
			w[ev.fd] = nil
		end

		if ev.IN then
			if ev.fd == s:getfd() then -- server socket, accept
				repeat
					local a, err = s:accept(ss, nil, "nonblock")
					if a then
						ep:add(a)
						w[a:getfd()] = new_connection(a)
					end
				until not a
			elseif ev.fd == tfd:getfd() then
				tfd:read(buffer, bufsize)
				ledapi:tick()
			else
				w[ev.fd]:read()
			end
		end
	end
end

-- main loop
while true do poll() end
