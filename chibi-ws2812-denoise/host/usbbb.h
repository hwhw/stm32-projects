#ifndef USBBB_H
#define USBBB_H

#include <stdbool.h>
#include <libusb.h>
#include <pthread.h>

#define BB_GENERIC_ERROR -1
#define BB_MEMORY_ERROR -2
#define BB_LIBUSB_INIT_ERROR -3
#define BB_DEVICE_NOT_FOUND -4
#define BB_INTERFACE_NOT_AVAILABLE -5
#define BB_BUS_ERROR -6
#define BB_NO_ANSWER -7
#define BB_BROKEN_ANSWER -8
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

struct bb_ctx_s {
	libusb_context *ctx;
	libusb_device_handle *dev;

  pthread_t event_thread;

  uint8_t fb[320*3];
  uint16_t sensors[12*8];

  bool running;
  bool dirty;
  bool transmitting;

  uint8_t rbuf[1+8*2];
};

typedef struct bb_ctx_s bb_ctx;

int bb_open(bb_ctx **C);
void bb_free(bb_ctx* C);
void bb_set_led(bb_ctx *C, const int led, const uint8_t r, const uint8_t g, const uint8_t b);
int bb_transmit(bb_ctx *C);
#endif
