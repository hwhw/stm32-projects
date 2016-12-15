#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <libusb.h>
#include "usbbb.h"

#define LIBUSB_TIMEOUT 1000
#define LIBUSB_TXTIMEOUT 20

#define BB_USB_VID 0x0483
#define BB_USB_PID 0xffff

#define POLL_MAX_EVENTS 1

#define LED_COUNT 320
#define SENSOR_ROWS 8
#define SENSOR_COLS 12

struct bb_ctx_s {
	libusb_context *ctx;
	libusb_device_handle *dev;

  pthread_t event_thread;

  uint8_t fb[LED_COUNT*3];
  uint8_t xmit_fb[2+LED_COUNT*3];
  uint16_t sensors[SENSOR_COLS*SENSOR_ROWS];

  bool running;
  bool transmitting;
  int measure_row;
  int xmit_offs;
  int sensor_last_row;
  pthread_mutex_t mutex_transmitting;
  pthread_mutex_t mutex_wait_sensor;
  pthread_cond_t cond_wait_sensor;

  uint8_t rbuf[1+SENSOR_ROWS*sizeof(uint16_t)];

  int16_t pos_led[40][40];
  int16_t pos_leds10[10][10][4];
};

struct timeval zero_tv = {0, 0};

void LIBUSB_CALL onReceiveComplete(struct libusb_transfer *transfer) {
  bb_ctx *C = (bb_ctx *) transfer->user_data;
  if(transfer->status == LIBUSB_TRANSFER_COMPLETED) {
    if(C->rbuf[0] < SENSOR_COLS) {
      pthread_mutex_lock(&C->mutex_wait_sensor);
      C->sensor_last_row = C->rbuf[0];
      int offs = C->rbuf[0]*SENSOR_ROWS;
      for(int i=0; i<SENSOR_ROWS; i++)
        C->sensors[offs+i] = (C->rbuf[i*2+2] << 8) | (C->rbuf[i*2+1]);
      pthread_cond_signal(&C->cond_wait_sensor);
      pthread_mutex_unlock(&C->mutex_wait_sensor);
    }
  }
  if(C->running) {
    libusb_fill_bulk_transfer(transfer, C->dev, 0x81, C->rbuf, 1+SENSOR_ROWS*2, onReceiveComplete, (void*) C, LIBUSB_TIMEOUT);
    libusb_submit_transfer(transfer);
  } else {
    libusb_free_transfer(transfer);
  }
}

BB_API
void bb_get_sensordata(bb_ctx *C, uint16_t sensordata[]) {
  pthread_mutex_lock(&C->mutex_wait_sensor);
  memcpy(sensordata, C->sensors, sizeof(uint16_t)*SENSOR_COLS*SENSOR_ROWS);
  pthread_mutex_unlock(&C->mutex_wait_sensor);
}

void* bb_event_thread(void *d) {
  bb_ctx *C = (bb_ctx*)d;
  C->running = true;
  struct libusb_transfer *xfr_rx = libusb_alloc_transfer(0);
  libusb_fill_bulk_transfer(xfr_rx, C->dev, 0x81, C->rbuf, 1+SENSOR_ROWS*2, onReceiveComplete, (void*) C, LIBUSB_TIMEOUT);
  libusb_submit_transfer(xfr_rx);
  while(C->running) {
    libusb_handle_events(C->ctx);
  }
  libusb_cancel_transfer(xfr_rx);
  pthread_exit(NULL);
}

void bb_init_pos_led(bb_ctx* C) {
  memset(C->pos_led, 0xFF, 40*40*sizeof(int16_t));
  memset(C->pos_leds10, 0xFF, 10*10*4*sizeof(int16_t));
  for(int i=0; i<LED_COUNT; i++) {
    int x, y;
    bb_get_led_pos(C, i, &x, &y);

    C->pos_led[y][x] = i;

    int16_t *p10 = C->pos_leds10[y>>2][x>>2];
    while(*p10 != -1) p10++;
    *p10 = i;
  }
}

BB_API
int bb_open(bb_ctx **C) {
	*C = calloc(1, sizeof(bb_ctx));
	if(*C == NULL)
		return BB_MEMORY_ERROR;

	if(libusb_init(&((*C)->ctx))) {
		bb_free(*C);
		*C = NULL;
		return BB_LIBUSB_INIT_ERROR;
	}

  libusb_set_debug((*C)->ctx, LIBUSB_LOG_LEVEL_WARNING); /* alternatively: LIBUSB_LOG_LEVEL_NOTHING */

	if(NULL == ((*C)->dev = libusb_open_device_with_vid_pid((*C)->ctx, BB_USB_VID, BB_USB_PID))) {
		bb_free(*C);
		*C = NULL;
		return BB_DEVICE_NOT_FOUND;
	}
	if(libusb_claim_interface((*C)->dev, 0)) {
		bb_free(*C);
		*C = NULL;
		return BB_INTERFACE_NOT_AVAILABLE;
	}

  pthread_mutex_init(&(*C)->mutex_wait_sensor, NULL);
  pthread_cond_init(&(*C)->cond_wait_sensor, NULL);
  pthread_mutex_init(&(*C)->mutex_transmitting, NULL);

  bb_init_pos_led(*C);

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  if(pthread_create(&(*C)->event_thread, &attr, bb_event_thread, (void*) *C)) {
    return BB_POLL_ERROR;
  }
	return 0;
}

BB_API
void bb_free(bb_ctx* C) {
	if(C != NULL) {
    if(C->running) {
      C->running = false;
      void *status;
      pthread_join(C->event_thread, &status);
      (void)status;
      pthread_mutex_destroy(&C->mutex_transmitting);
      pthread_mutex_destroy(&C->mutex_wait_sensor);
    }
		if(C->dev != NULL) {
			libusb_release_interface(C->dev, 0);
			libusb_close(C->dev);
		}
		if(C->ctx != NULL) {
			libusb_exit(C->ctx);
		}
		free(C);
	}
}

void LIBUSB_CALL onTransmitComplete(struct libusb_transfer *transfer) {
  bb_ctx *C = (bb_ctx*)transfer->user_data;
  C->xmit_offs += 20;
  if(transfer->status == LIBUSB_TRANSFER_COMPLETED && C->xmit_offs < LED_COUNT) {
    C->xmit_fb[3*C->xmit_offs] = C->xmit_offs >> 8;
    if((C->xmit_offs+20) >= LED_COUNT) {
      /* last package will trigger refresh on device side */
      C->xmit_fb[3*C->xmit_offs] |= (C->measure_row + 1) << 4;
    }
    C->xmit_fb[3*C->xmit_offs+1] = C->xmit_offs & 0xFF;
    int size = (LED_COUNT - C->xmit_offs > 20) ? 20*3 : (LED_COUNT - C->xmit_offs)*3;
    libusb_fill_bulk_transfer(transfer, C->dev, 0x02, C->xmit_fb + 3*C->xmit_offs, size+2, onTransmitComplete, (void*) C, LIBUSB_TXTIMEOUT);
    if(libusb_submit_transfer(transfer) == 0) {
      /* next chunk of data in flight */
      return;
    }
  }
  /* error or completed: we free up the transfer */
  libusb_free_transfer(transfer);
  pthread_mutex_lock(&C->mutex_transmitting);
  C->transmitting = false;
  pthread_mutex_unlock(&C->mutex_transmitting);
}

BB_API
int bb_transmit(bb_ctx *C, int measure_row) {
  pthread_mutex_lock(&C->mutex_transmitting);
  if(C->transmitting) {
    /* transmit in progress, return error */
    pthread_mutex_unlock(&C->mutex_transmitting);
    return 1;
  }
  struct libusb_transfer *xfr = libusb_alloc_transfer(0);
  if(xfr == NULL) {
    pthread_mutex_unlock(&C->mutex_transmitting);
    return -1;
  }
  C->xmit_offs = 0;
  C->xmit_fb[0] = 0;
  C->xmit_fb[1] = 0;
  memcpy(C->xmit_fb+2, C->fb, LED_COUNT*3);
  libusb_fill_bulk_transfer(xfr, C->dev, 0x02, C->xmit_fb, 62, onTransmitComplete, (void*) C, LIBUSB_TXTIMEOUT);
  if(libusb_submit_transfer(xfr) != 0) {
    libusb_free_transfer(xfr);
    pthread_mutex_unlock(&C->mutex_transmitting);
    return -2;
  }
  C->measure_row = (measure_row == -1) ? ((C->measure_row + 1)%12) : measure_row;
  C->transmitting = true;
  pthread_mutex_unlock(&C->mutex_transmitting);
  return 0;
}

BB_API
void bb_get_led_pos(bb_ctx* C, int led, int* x, int* y) {
  if(led < 160) {
    // left-right
    *y = 5 + 4 * (led / 20);
    *x = 2 * (led % 20);
    if(((led / 20) % 2) == 1)
      *x = 38 - *x;
  } else {
    // down-up
    led -= 160;
    *x = 5 + 4 * (led / 20);
    *y = 2 * (led % 20);
    if(((led / 20) % 2) == 0)
      *y = 38 - *y;
  }
}

BB_API
void bb_set_led(bb_ctx *C, const int led, const uint8_t r, const uint8_t g, const uint8_t b) {
  uint8_t *p = C->fb + led*3;
  *p++ = g;
  *p++ = r;
  *p = b;
}

BB_API
void bb_set_led10(bb_ctx* C, const int x, const int y, const int r, const int g, const int b) {
  for(int i=0; i<4; i++) {
    int led = C->pos_leds10[y][x][i];
    if(led == -1) break;
    bb_set_led(C, led, r, g, b);
  }
}

BB_API
void bb_set_led40(bb_ctx* C, const int x, const int y, const int r, const int g, const int b) {
  int led = C->pos_led[y][x];
  if(led != -1)
    bb_set_led(C, led, r, g, b);
}

BB_API
int bb_wait_measure(bb_ctx* C) {
  pthread_mutex_lock(&C->mutex_wait_sensor);
  pthread_cond_wait(&C->cond_wait_sensor, &C->mutex_wait_sensor);
  int r = C->sensor_last_row;
  pthread_mutex_unlock(&C->mutex_wait_sensor);
  return r;
}
