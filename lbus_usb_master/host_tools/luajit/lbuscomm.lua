local ffi=require"ffi"

ffi.cdef[[
const static int LBUS_GENERIC_ERROR = -1;
const static int LBUS_MEMORY_ERROR = -2;
const static int LBUS_LIBUSB_INIT_ERROR = -3;
const static int LBUS_DEVICE_NOT_FOUND = -4;
const static int LBUS_INTERFACE_NOT_AVAILABLE = -5;
const static int LBUS_BUS_ERROR = -6;
const static int LBUS_NO_ANSWER = -7;
const static int LBUS_BROKEN_ANSWER = -8;
const static int LBUS_MISUSE_ERROR = -9;
const static int LBUS_CRC_ERROR = -10;
const static int LBUS_FIRMWARE_FILE_ERROR = -11;

struct lbus_ctx_s;
typedef struct lbus_ctx_s lbus_ctx;
int lbus_open(lbus_ctx **C);
void lbus_free(lbus_ctx* C);
int lbus_busmaster_echo(lbus_ctx* C);
const char* lbus_strerror(int ret);
int lbus_led_set_16bit(lbus_ctx* C, const int dst, const uint16_t led, const unsigned int vcount, const uint16_t values[]);
int lbus_led_commit(lbus_ctx* C, const int dst);
int lbus_ping(lbus_ctx* C, const int dst);
int lbus_reset_to_bootloader(lbus_ctx* C, const int dst);
int lbus_reset_to_firmware(lbus_ctx* C, const int dst);
int lbus_erase_config(lbus_ctx* C, const int dst);
int lbus_get_config(lbus_ctx* C, const int dst, const uint16_t type, const bool numeric, const int answer_bytes, void* data);
int lbus_set_address(lbus_ctx* C, const int dst, const uint8_t address);
int lbus_read_memory(lbus_ctx* C, const int dst, const uint32_t address, const int length, void *buf);
int lbus_flash_firmware(lbus_ctx* C, const int dst, const char *path);
int lbus_tx(lbus_ctx* C, const void* buf, const int size);
int lbus_rx(lbus_ctx* C, void* buf, const int size);
]]

local lbuscomm = ffi.load("./liblbuscomm.so")
local L = {}

function L.check(value)
	if value < 0 then
		error("lbus error: "..ffi.string(lbuscomm.lbus_strerror(value)))
	end
	return value
end

function L.open()
	local lbus_ctx = ffi.new("lbus_ctx*[2]")
	L.check(lbuscomm.lbus_open(lbus_ctx))
	ffi.metatype("struct lbus_ctx_s", {
		__index = function(_,f)
			return lbuscomm[f]
		end
	})
	local ctx = ffi.new("lbus_ctx*", lbus_ctx[0])
	return ctx
end

return L
