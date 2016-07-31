#ifndef _LCOMM_H_
#define _LCOMM_H_
#include <stdint.h>
#include <libusb.h>

typedef struct lcomm_ctx_s {
	libusb_context *ctx;
	libusb_device_handle *dev;
} lcomm_ctx;

int lcomm_open_usb(lcomm_ctx* c);
void lcomm_close(lcomm_ctx* ctx);
int lcomm_echo(const lcomm_ctx* ctx);
int lcomm_tx(const lcomm_ctx* ctx, const uint32_t size, const void *buf);
int lcomm_txf(const lcomm_ctx* c, const char *fmt, ...);
int lcomm_rx(const lcomm_ctx* ctx, const uint32_t size, void *buf);
uint32_t lcomm_crc32(uint32_t crc, uint32_t size, void *buf);

#endif // _LCOMM_H_
