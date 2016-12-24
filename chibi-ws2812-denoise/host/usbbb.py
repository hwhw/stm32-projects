import os
from cffi import FFI
ffi = FFI()
ffi.cdef("""
struct bb_ctx_s;
typedef struct bb_ctx_s bb_ctx;

/* run this to initialize a context and open the device
 *
 * returns 0 on success
 */
int bb_open(bb_ctx **C);
/* stop event handler thread, close device, free resources
 */
void bb_free(bb_ctx* C);

/* for a given LED number, return X/Y coordinates (in a 40x40 coordinate system) */
void bb_get_led_pos(bb_ctx* C, int led, int* x, int* y);
/* set a given LED - identified by number - to a certain color */
void bb_set_led(bb_ctx *C, const int led, const uint8_t r, const uint8_t g, const uint8_t b);
/* set a given LED bundle - identified by coordinates in a 10x10 system - to a certain color
 * will do nothing if coordinates have no associated LEDs
 */
void bb_set_led10(bb_ctx* C, const int x, const int y, const int r, const int g, const int b);
/* set a given LED - identified by coordinates in a 40x40 system - to a certain color
 * will do nothing if coordinates have no associated LED
 */
void bb_set_led40(bb_ctx* C, const int x, const int y, const int r, const int g, const int b);
/* send all changed data to the device
 * measure_row is a number 0..11 and specifies the sensor row to check as soon as the
 * changed data has become active; set to -1 to just iterate through all values
 * returns 0 when a transmit was successfully started
 * returns 1 when a transmit was already in flight. No new transmit is triggered in that case.
 * returns a value <0 upon error
 */
int bb_transmit(bb_ctx *C, int measure_row);

/* wait for a row of sensor data
 * returns the row number of freshly received data
 */
int bb_wait_measure(bb_ctx* C);

/* fill a 12x8 size array with the current sensor state */
void bb_get_sensordata(bb_ctx *C, uint16_t sensordata[]);
""")
lib = ffi.dlopen("./libusbbb-emu.so") if ("BBEMU" in os.environ) else ffi.dlopen("./libusbbb.so")

class BB:
    """BlackBox hardware access library"""
    sensors = ffi.new("uint16_t[96]")

    def __init__(self):
        self.bb = ffi.new("bb_ctx**")
        if 0 != lib.bb_open(self.bb):
            raise Exception("cannot open BlackBox USB device")

    def __del__(self):
        lib.bb_free(self.bb[0]);

    def get_led_pos(self, ledno):
        assert ledno >= 0
        assert ledno < 320
        x = ffi.new("int")
        y = ffi.new("int")
        lib.bb_get_led_pos(self.bb[0], ledno, x, y)
        return x, y

    def set_led(self, ledno, r, g, b):
        assert ledno >= 0
        assert ledno < 320
        assert r >= 0
        assert g >= 0
        assert b >= 0
        assert r < 256
        assert g < 256
        assert b < 256
        return lib.bb_set_led(self.bb[0], ledno, r, g, b)

    def set_led10(self, x, y, r, g, b):
        assert x >= 0
        assert y >= 0
        assert x < 10
        assert y < 10
        assert r >= 0
        assert g >= 0
        assert b >= 0
        assert r < 256
        assert g < 256
        assert b < 256
        return lib.bb_set_led10(self.bb[0], x, y, r, g, b)

    def set_led40(self, x, y, r, g, b):
        assert x >= 0
        assert y >= 0
        assert x < 40
        assert y < 40
        assert r >= 0
        assert g >= 0
        assert b >= 0
        assert r < 256
        assert g < 256
        assert b < 256
        return lib.bb_set_led40(self.bb[0], x, y, r, g, b)

    def transmit(self, measure_x):
        assert measure_x >= -1
        assert measure_x < 12
        return lib.bb_transmit(self.bb[0], measure_x)

    def wait_measure(self):
        return lib.bb_wait_measure(self.bb[0])

    def get_sensor_data(self):
        lib.bb_get_sensordata(self.bb[0], self.sensors)
        return self.sensors

    def get_sensor_field(self, x, y):
        assert x >= 1
        assert y >= 1
        assert x <= 8
        assert y <= 8
        self.get_sensor_data()
        return self.sensors[(x-1)*8 + (8-y)]
