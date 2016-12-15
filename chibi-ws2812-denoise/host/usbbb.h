#ifndef USBBB_H
#define USBBB_H

#include <stdint.h>

#define BB_GENERIC_ERROR -1
#define BB_MEMORY_ERROR -2
#define BB_LIBUSB_INIT_ERROR -3
#define BB_DEVICE_NOT_FOUND -4
#define BB_INTERFACE_NOT_AVAILABLE -5
#define BB_MISUSE_ERROR -9
#define BB_TX_ERROR -10
#define BB_POLL_ERROR -11

#ifndef BB_API
#  if __GNUC__ >= 4
#    define BB_API __attribute__((visibility("default")))
#  else
#    define BB_API
#  endif
#endif

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
#endif
