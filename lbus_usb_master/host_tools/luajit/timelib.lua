local ffi=require"ffi"
ffi.cdef[[
typedef long time_t;
typedef struct timeval {
	time_t tv_sec;
	time_t tv_usec;
} timeval;
int gettimeofday(struct timeval* t, void* tzp);
]]
local T = {}
local Tf = {}
local t = ffi.new("timeval")
function T.gettimeofday()
	ffi.C.gettimeofday(t, nil)
	return tonumber(t.tv_sec)*1000000 + tonumber(t.tv_usec)
end

function T.benchmark(f)
	local start = T.gettimeofday()
	f()
	local dur = T.gettimeofday() - start
	return dur
end

function T.every(usecs, fn)
	local last = 0
	return function(now)
		if (now - last) > usecs then
			last = now
			fn()
		end
	end
end

return T
