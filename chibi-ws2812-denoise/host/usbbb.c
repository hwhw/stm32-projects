#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "usbbb.h"

#define LIBUSB_TIMEOUT 1000
#define LIBUSB_TXTIMEOUT 20

#define BB_USB_VID 0x0483
#define BB_USB_PID 0xffff

#define POLL_MAX_EVENTS 1

struct timeval zero_tv = {0, 0};

struct fb_tx_s {
  struct bb_ctx_s *C;
  uint8_t data[2+320*3];
  int offs;
  struct libusb_transfer *xfr;
};

void LIBUSB_CALL onReceiveComplete(struct libusb_transfer *transfer) {
  bb_ctx *C = (bb_ctx *) transfer->user_data;
  if(transfer->status == LIBUSB_TRANSFER_COMPLETED) {
    if(C->rbuf[0] < 12) {
      int offs = C->rbuf[0]*8;
      for(int i=0; i<8; i++)
        C->sensors[offs+i] = (C->rbuf[i*2+2] << 8) | (C->rbuf[i*2+1]);
    }
  }
  if(C->running) {
    libusb_fill_bulk_transfer(transfer, C->dev, 0x81, C->rbuf, 1+8*2, onReceiveComplete, (void*) C, LIBUSB_TIMEOUT);
    libusb_submit_transfer(transfer);
  } else {
    libusb_free_transfer(transfer);
  }
}

void* bb_event_thread(void *d) {
  bb_ctx *C = (bb_ctx*)d;
  C->running = true;
  struct libusb_transfer *xfr_rx = libusb_alloc_transfer(0);
  libusb_fill_bulk_transfer(xfr_rx, C->dev, 0x81, C->rbuf, 1+8*2, onReceiveComplete, (void*) C, LIBUSB_TIMEOUT);
  libusb_submit_transfer(xfr_rx);
  while(C->running) {
    libusb_handle_events(C->ctx);
  }
  libusb_cancel_transfer(xfr_rx);
  pthread_exit(NULL);
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
  if(pthread_create(&(*C)->event_thread, NULL, bb_event_thread, (void*) *C)) {
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

BB_API
void bb_set_led(bb_ctx *C, const int led, const uint8_t r, const uint8_t g, const uint8_t b) {
  uint8_t *p = C->fb + led*3;
  *p++ = g;
  *p++ = r;
  *p = b;
  C->dirty = true;
}

void LIBUSB_CALL onTransmitComplete(struct libusb_transfer *transfer) {
  struct fb_tx_s *tx = (struct fb_tx_s *) transfer->user_data;
  struct bb_ctx_s *C = tx->C;
  //fprintf(stdout, "%d/%d, %d, txoffs=%d\n", transfer->actual_length, transfer->length, transfer->status, tx->offs);
  tx->offs += 20;
  if(transfer->status == LIBUSB_TRANSFER_COMPLETED && tx->offs < 320) {
    tx->data[3*tx->offs] = tx->offs >> 8;
    tx->data[3*tx->offs+1] = tx->offs & 0xFF;
    int size = (320 - tx->offs > 20) ? 60 : (320 - tx->offs)*3;
    libusb_fill_bulk_transfer(tx->xfr, C->dev, 0x02, tx->data + 3*tx->offs, size+2, onTransmitComplete, (void*) tx, LIBUSB_TXTIMEOUT);
    if(libusb_submit_transfer(tx->xfr) == 0)
      return;
  }
  libusb_free_transfer(tx->xfr);
  C->transmitting = false;
  free(tx);
}

BB_API
int bb_transmit(bb_ctx *C) {
  if(!C->dirty)
    return 0;
  if(C->transmitting)
    return 1;
  C->transmitting = true;
  struct fb_tx_s *tx = calloc(1, sizeof(struct fb_tx_s));
  if(tx == NULL) return -1;
  tx->C = C;
  tx->xfr = libusb_alloc_transfer(0);
  if(tx->xfr == NULL) return -2;
  memcpy(tx->data+2, C->fb, 320*3);
  libusb_fill_bulk_transfer(tx->xfr, C->dev, 0x02, tx->data, 62, onTransmitComplete, (void*) tx, LIBUSB_TXTIMEOUT);
  if(libusb_submit_transfer(tx->xfr) != 0) {
    libusb_free_transfer(tx->xfr);
    free(tx);
    return -3;
  }
  C->dirty = false;
  return 0;
}

