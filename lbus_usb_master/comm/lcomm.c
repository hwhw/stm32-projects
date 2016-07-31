#include <stdint.h>
#include <libusb.h>
#include <string.h>
#include <stdarg.h>

#include "lcomm.h"

#define LIBUSB_TIMEOUT 1000
#define LIBUSB_TXTIMEOUT 20

int lcomm_open_usb(lcomm_ctx* c) {
	int ret;
	if(0 != (ret = libusb_init(&c->ctx))) {
		return ret;
	}
	if(NULL == (c->dev = libusb_open_device_with_vid_pid(c->ctx, 0xdead, 0xcafe))) {
		libusb_exit(c->ctx);
		c->ctx = NULL;
		return 1;
	}
	if(0 != (ret = libusb_claim_interface(c->dev, 0))) {
		libusb_close(c->dev);
		c->dev = NULL;
		libusb_exit(c->ctx);
		c->ctx = NULL;
		return ret;
	}
	return 0;
}

void lcomm_close(lcomm_ctx* c) {
	if(c->dev != NULL) {
		libusb_release_interface(c->dev, 0);
		libusb_close(c->dev);
		c->dev = NULL;
	}
	if(c->ctx != NULL) {
		libusb_exit(c->ctx);
		c->ctx = NULL;
	}
}

int lcomm_echo(const lcomm_ctx* c) {
	uint8_t tbuf[64] = { 3 /* ECHO */, 1,2,3,4,5,6,7,8,9,10 };
	uint8_t rbuf[63];
	int transferred = 0;
	for(int i=0; i<16384; i++) {
		transferred = 0;
		int res = libusb_bulk_transfer(c->dev, 0x01, tbuf, 64, &transferred, LIBUSB_TXTIMEOUT);
		if(res != 0)
			return res;
		if(transferred != 64)
			return 1;
		transferred = 0;
		res = libusb_bulk_transfer(c->dev, 0x82, rbuf, 63, &transferred, LIBUSB_TIMEOUT);
		if(res != 0)
			return res;
		if(transferred != 63)
			return 2;
	}
}

int lcomm_tx(const lcomm_ctx* c, const uint32_t size, const void *buf) {
	int done = 0;
	int transferred;
	uint8_t tbuf[64];
	while(done < size) {
		const int chunk_size = (size-done) > 63 ? 63 : (size-done);
		tbuf[0] = 1; // XMIT
		memcpy(tbuf+1, buf+done, chunk_size);
		transferred = 0;
		int res = libusb_bulk_transfer(c->dev, 0x01, tbuf, chunk_size+1, &transferred, LIBUSB_TXTIMEOUT);
		if(res == 0 || res == LIBUSB_ERROR_TIMEOUT) {
			done += transferred;
		} else {
			return res;
		}
	}
	return done;
}

int lcomm_rx(const lcomm_ctx* c, const uint32_t size, void *buf) {
	int done = 0;
	int transferred;
	uint8_t tbuf[2] = { 2 /* RECV */, 0 };
	while(done < size) {
		tbuf[1] = (size-done) > 64 ? 64 : (size-done);
		int res = libusb_bulk_transfer(c->dev, 0x01, tbuf, 2, &transferred, LIBUSB_TXTIMEOUT);
		if(res != 0) {
			return res;
		}
		if(transferred != 2) {
			return -1024;
		}
		transferred = 0;
		res = libusb_bulk_transfer(c->dev, 0x82, buf + done, tbuf[1], &transferred, LIBUSB_TIMEOUT);
		done += transferred;
		if(res == LIBUSB_ERROR_TIMEOUT || res == 0) {
			if(transferred < tbuf[1])
				break;
		} else {
			return res;
		}
	}
	return done;
}

int lcomm_txf(const lcomm_ctx* c, const char *fmt, ...) {
	uint8_t buf[4096];
	uint8_t *b = buf;
	int c = 0;

	va_list vl;
	va_start(vl, fmt);

	while(*fmt) {
		switch(*fmt) {
			case 'b': /* byte */
				*b++ = va_arg(vl, uint8_t);
				break;
			case 'h': /* halfword = 16bit */
				*(uint16_t*)b++ = va_arg(vl, uint16_t);
				break;
			case 'w': /* word = 32bit */
				*(uint32_t*)b++ = va_arg(vl, uint32_t);
				break;
			case 's':
				const char *s = va_arg(vl, const char*);
				while(*s) *b++ = *s++;
				break;
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				c = c*10 + (*fmt - '0');
				break;
			case 'S':
				const void *s = va_arg(vl, const void*);
				memcpy(b, s, c);
				b += c;
				c = 0;
				break;
		}
		fmt++;
	}
	va_end(vl);
	if(b != buf) {
		return lcomm_tx(c, b-buf, buf);
	}
	return 0;
}

uint32_t lcomm_crc32(uint32_t crc, uint32_t size, void *buf) {
	size = size >> 2; // /4

	while(size--) {
		static const uint32_t CrcTable[16] = { // Nibble lookup table for 0x04C11DB7 polynomial
		0x00000000,0x04C11DB7,0x09823B6E,0x0D4326D9,0x130476DC,0x17C56B6B,0x1A864DB2,0x1E475005,
		0x2608EDB8,0x22C9F00F,0x2F8AD6D6,0x2B4BCB61,0x350C9B64,0x31CD86D3,0x3C8EA00A,0x384FBDBD };

		crc = crc ^ *((uint32_t *)buf); // Apply all 32-bits

		buf += 4;

		// Process 32-bits, 4 at a time, or 8 rounds

		crc = (crc << 4) ^ CrcTable[crc >> 28]; // Assumes 32-bit reg, masking index to 4-bits
		crc = (crc << 4) ^ CrcTable[crc >> 28]; //  0x04C11DB7 Polynomial used in STM32
		crc = (crc << 4) ^ CrcTable[crc >> 28];
		crc = (crc << 4) ^ CrcTable[crc >> 28];
		crc = (crc << 4) ^ CrcTable[crc >> 28];
		crc = (crc << 4) ^ CrcTable[crc >> 28];
		crc = (crc << 4) ^ CrcTable[crc >> 28];
		crc = (crc << 4) ^ CrcTable[crc >> 28];
	}

	return crc;
}
