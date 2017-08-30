/* LBUS USB bus master host library
 *
 * Copyright (c) 2016 Hans-Werner Hilse <hwhilse@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <libusb.h>

#include "../../lbus_common/lbus_data.h"

#include "lbuscomm.h"

#define MAX_FW_SIZE (44*1024)
#define FW_START_PAGE 4

#define LIBUSB_TIMEOUT 1000
#define LIBUSB_TXTIMEOUT 20

struct lbus_ctx_s {
	libusb_context *ctx;
	libusb_device_handle *dev;
};

static void DumpHex(const char* info, const void* data, size_t size) {
#ifdef DEBUG
	char ascii[17];
	size_t i, j;
	ascii[16] = '\0';
	for (i = 0; i < size; ++i) {
		printf("%02X ", ((unsigned char*)data)[i]);
		if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') {
			ascii[i % 16] = ((unsigned char*)data)[i];
		} else {
			ascii[i % 16] = '.';
		}
		if ((i+1) % 8 == 0 || i+1 == size) {
			printf(" ");
			if ((i+1) % 16 == 0) {
				printf("|  %s \n", ascii);
			} else if (i+1 == size) {
				ascii[(i+1) % 16] = '\0';
				if ((i+1) % 16 <= 8) {
					printf(" ");
				}
				for (j = (i+1) % 16; j < 16; ++j) {
					printf("   ");
				}
				printf("|  %s \n", ascii);
			}
		}
	}
#endif
}

static inline uint16_t tole16(const uint16_t x)
{
	union {
		uint8_t  b8[2];
		uint16_t b16;
	} _tmp;
	_tmp.b8[1] = (uint8_t) (x >> 8);
	_tmp.b8[0] = (uint8_t) (x & 0xff);
	return _tmp.b16;
}
#define le16 tole16
static inline uint32_t tole32(const uint32_t x)
{
	union {
		uint8_t  b8[4];
		uint32_t b32;
	} _tmp;
	_tmp.b8[3] = (uint8_t) (x >> 24);
	_tmp.b8[2] = (uint8_t) ((x >> 16) & 0xff);
	_tmp.b8[1] = (uint8_t) ((x >> 8) & 0xff);
	_tmp.b8[0] = (uint8_t) (x & 0xff);
	return _tmp.b32;
}
#define le32 tole32

static uint32_t crc32(uint32_t Crc, uint32_t Size, void *Buffer) {
	Size = Size >> 2; // /4

	while(Size--) {
		static const uint32_t CrcTable[16] = { // Nibble lookup table for 0x04C11DB7 polynomial
		0x00000000,0x04C11DB7,0x09823B6E,0x0D4326D9,0x130476DC,0x17C56B6B,0x1A864DB2,0x1E475005,
		0x2608EDB8,0x22C9F00F,0x2F8AD6D6,0x2B4BCB61,0x350C9B64,0x31CD86D3,0x3C8EA00A,0x384FBDBD };

		Crc = Crc ^ *((uint32_t *)Buffer); // Apply all 32-bits

		Buffer += 4;

		// Process 32-bits, 4 at a time, or 8 rounds

		Crc = (Crc << 4) ^ CrcTable[Crc >> 28]; // Assumes 32-bit reg, masking index to 4-bits
		Crc = (Crc << 4) ^ CrcTable[Crc >> 28]; //  0x04C11DB7 Polynomial used in STM32
		Crc = (Crc << 4) ^ CrcTable[Crc >> 28];
		Crc = (Crc << 4) ^ CrcTable[Crc >> 28];
		Crc = (Crc << 4) ^ CrcTable[Crc >> 28];
		Crc = (Crc << 4) ^ CrcTable[Crc >> 28];
		Crc = (Crc << 4) ^ CrcTable[Crc >> 28];
		Crc = (Crc << 4) ^ CrcTable[Crc >> 28];
	}

	return(Crc);
}

static int readall(int fd, void *buf, size_t count) {
	int res = 0;
	int c = 0;
	while(c < count) {
		int res = read(fd, buf, count - c);
		if(res == -1) return -1;
		if(res == 0) return c;
		c += res;
		buf += res;
	}
	return c;
}

LBUS_API
int lbus_tx(lbus_ctx* C, const void* buf, const int size) {
	int done = 0;
	int transferred = 0;
	uint8_t tbuf[64];
	while(done < size) {
		const int chunk_size = (size-done) > 63 ? 63 : (size-done);
		tbuf[0] = 1; // XMIT
		memcpy(tbuf+1, buf+done, chunk_size);
		int res = libusb_bulk_transfer(C->dev, 0x01, tbuf, chunk_size+1, &transferred, LIBUSB_TXTIMEOUT);
		if(res == 0 || res == LIBUSB_ERROR_TIMEOUT) {
			if(transferred > 0) {
				DumpHex("USB TX:\n", tbuf, transferred);
				done += transferred-1;
			}
		} else {
			return LBUS_BUS_ERROR;
		}
	}
	return done;
}

LBUS_API
int lbus_rx(lbus_ctx* C, void* buf, const int size) {
	int done = 0;
	int transferred = 0;
	uint8_t tbuf[2] = { 2 /* RECV */, 0 };
	while(done < size) {
		tbuf[1] = size > 64 ? 64 : size;
		int res = libusb_bulk_transfer(C->dev, 0x01, tbuf, 2, &transferred, LIBUSB_TXTIMEOUT);
		if(res != 0) {
			return LBUS_BUS_ERROR;
		}
		DumpHex("USB TX (receive command):\n", tbuf, transferred);
		transferred = 0;
		res = libusb_bulk_transfer(C->dev, 0x82, buf + done, tbuf[1], &transferred, LIBUSB_TIMEOUT);
		done += transferred;
		if(res == LIBUSB_ERROR_TIMEOUT || res == 0) {
			DumpHex("USB RX:\n", buf+done, transferred);
			if(transferred < tbuf[1])
				break;
		} else {
			return LBUS_BUS_ERROR;
		}
	}
	return done;
}

LBUS_API
int lbus_busmaster_echo(lbus_ctx* C) {
	uint8_t tbuf[64] = { 3 /* ECHO */, 1,2,3,4,5,6,7,8,9,10 };
	uint8_t rbuf[63];
	int transferred = 0;
	for(int i=0; i<16384; i++) {
		transferred = 0;
		int res = libusb_bulk_transfer(C->dev, 0x01, tbuf, 64, &transferred, LIBUSB_TXTIMEOUT);
		if(res != 0 || transferred != 64) {
			return LBUS_BUS_ERROR;
		}
		transferred = 0;
		res = libusb_bulk_transfer(C->dev, 0x82, rbuf, 63, &transferred, LIBUSB_TIMEOUT);
		if(res != 0 || transferred != 63) {
			return LBUS_BUS_ERROR;
		}
	}
	return 0;
}

/* convenience wrapper for single-value receives */
static int lbus_simple_recv(lbus_ctx* C, const int answer_bytes, uint32_t* d32) {
	int ret;
	if(answer_bytes == 1) {
		uint8_t reply;
		ret = lbus_rx(C, &reply, 1);
		if(ret < 0) return ret;
		if(ret == 0) return LBUS_NO_ANSWER;
		return reply;
	} else if(answer_bytes == 2) {
		uint16_t reply;
		ret = lbus_rx(C, &reply, 2);
		if(ret < 0) return ret;
		if(ret == 0) return LBUS_NO_ANSWER;
		return le16(reply);
	} else if(answer_bytes == 4) {
		uint32_t reply;
		ret = lbus_rx(C, &reply, 4);
		if(ret < 0) return ret;
		if(ret == 0) return LBUS_NO_ANSWER;
		*d32 = le32(reply);
		return 1;
	}
	return 0;
}

/* convenience wrapper for no-payload commands and single-value receives */
static int lbus_simple_cmd(lbus_ctx* C, const int dst, const uint8_t command, const int answer_bytes, uint32_t* d32) {
	struct lbus_hdr pkg = {
		.length = tole16(sizeof(struct lbus_hdr)+answer_bytes),
		.addr = dst,
		.cmd = command,
	};
	int ret = lbus_tx(C, &pkg, sizeof(pkg));
	if(ret < 0) return ret;

	return lbus_simple_recv(C, answer_bytes, d32);
}

LBUS_API
int lbus_ping(lbus_ctx* C, const int dst) {
	return lbus_simple_cmd(C, dst, PING, 1, NULL);
}

LBUS_API
int lbus_reset_to_bootloader(lbus_ctx* C, const int dst) {
	return lbus_simple_cmd(C, dst, RESET_TO_BOOTLOADER, 0, NULL);
}

LBUS_API
int lbus_reset_to_firmware(lbus_ctx* C, const int dst) {
	return lbus_simple_cmd(C, dst, RESET_TO_FIRMWARE, 0, NULL);
}

LBUS_API
int lbus_erase_config(lbus_ctx* C, const int dst) {
	return lbus_simple_cmd(C, dst, ERASE_CONFIG, 0, NULL);
}

LBUS_API
int lbus_get_config(lbus_ctx* C, const int dst, const uint16_t type, const bool numeric, const int answer_bytes, void* data) {
	struct lbus_pkg pkg = {
		.hdr = {
			.length = tole16(sizeof(struct lbus_hdr)+sizeof(struct lbus_GET_DATA)+answer_bytes),
			.addr = dst,
			.cmd = GET_DATA,
		},
		.GET_DATA = {
			.type = tole16(type)
		}
	};
	int ret = lbus_tx(C, &pkg, sizeof(struct lbus_hdr)+sizeof(struct lbus_GET_DATA));
	if(ret < 0) return ret;

	if(numeric) {
		return lbus_simple_recv(C, answer_bytes, (uint32_t*)data);
	} else {
		return lbus_rx(C, data, answer_bytes);
	}
}

LBUS_API
int lbus_set_address(lbus_ctx* C, const int dst, const uint8_t address) {
	if(address < 1 || address > 127) {
		return LBUS_MISUSE_ERROR;
	}
	struct lbus_pkg pkg = {
		.hdr = {
			.length = tole16(sizeof(struct lbus_hdr)+sizeof(struct lbus_SET_ADDRESS)+1),
			.addr = dst,
			.cmd = SET_ADDRESS,
		},
		.SET_ADDRESS = {
			.naddr = address
		}
	};
	int ret = lbus_tx(C, &pkg, sizeof(pkg));
	if(ret < 0) return ret;

	return lbus_simple_recv(C, 1, NULL);
}

LBUS_API
int lbus_set_polarity(lbus_ctx* C, const int dst, const uint8_t polarity) {
	struct lbus_pkg pkg = {
		.hdr = {
			.length = tole16(sizeof(struct lbus_hdr)+sizeof(struct lbus_SET_POLARITY)+1),
			.addr = dst,
			.cmd = SET_POLARITY,
		},
		.SET_POLARITY = {
			.polarity = polarity
		}
	};
	int ret = lbus_tx(C, &pkg, sizeof(pkg));
	if(ret < 0) return ret;

	return lbus_simple_recv(C, 1, NULL);
}

LBUS_API
int lbus_read_memory(lbus_ctx* C, const int dst, const uint32_t address, const int length, void *buf) {
	if(length < 0 || length > 1024) {
		return LBUS_MISUSE_ERROR;
	}
	struct lbus_pkg pkg = {
		.hdr = {
			.length = tole16(sizeof(struct lbus_hdr)+sizeof(struct lbus_READ_MEMORY)+length+4),
			.addr = dst,
			.cmd = READ_MEMORY,
		},
		.READ_MEMORY = {
			.address = address,
		}
	};
	int ret = lbus_tx(C, &pkg, sizeof(struct lbus_hdr)+sizeof(struct lbus_READ_MEMORY));
	if(ret < 0) return ret;

	ret = lbus_rx(C, buf, length);
	if(ret < 0) return ret;
	if(ret < length) return LBUS_BROKEN_ANSWER;

	uint32_t crc_test;
	ret = lbus_rx(C, &crc_test, 4);
	if(ret < 0) return ret;
	if(ret < 4) return LBUS_BROKEN_ANSWER;
	if(le32(crc_test) != crc32(0xFFFFFFFF, length, buf)) {
		return LBUS_CRC_ERROR;
	}
	return length;
}

#define LBUS_LED_MAX_VCOUNT 1024
LBUS_API
int lbus_led_set_16bit(lbus_ctx* C, const int dst, const uint16_t led, const unsigned int vcount, const uint16_t values[]) {
	if(vcount > LBUS_LED_MAX_VCOUNT) {
		return LBUS_MISUSE_ERROR;
	}
	struct {
		struct lbus_hdr hdr;
		uint16_t led;
		uint16_t values[LBUS_LED_MAX_VCOUNT];
	} __attribute__((packed)) pkg = {
		.hdr = {
			.length = tole16(sizeof(struct lbus_hdr)+sizeof(uint16_t)*(vcount+1)),
			.addr = dst,
			.cmd = LED_SET_16BIT,
		},
		.led = tole16(led)
	};
	for(int i=0; i<vcount; i++)
		pkg.values[i] = tole16(values[i]);

	return lbus_tx(C, &pkg, sizeof(struct lbus_hdr)+sizeof(uint16_t)*(vcount+1));
}

LBUS_API
int lbus_led_commit(lbus_ctx* C, const int dst) {
	return lbus_simple_cmd(C, dst, LED_COMMIT, 0, NULL);
}

LBUS_API
int lbus_flash_firmware(lbus_ctx* C, const int dst, const char *path) {
	struct stat st;
	int ffd = open(path, O_RDONLY);
	if(ffd == -1)
		return LBUS_FIRMWARE_FILE_ERROR;

	if(fstat(ffd, &st) == -1) {
		close(ffd);
		return LBUS_FIRMWARE_FILE_ERROR;
	}
	if(st.st_size > MAX_FW_SIZE) {
		close(ffd);
		return LBUS_FIRMWARE_FILE_ERROR;
	}
	int fw_pg_size = (st.st_size+(PAGE_SIZE-1)) & (~(PAGE_SIZE-1));
	void *fwbuf = calloc(1, fw_pg_size);
	if(fwbuf == NULL) {
		close(ffd);
		return LBUS_GENERIC_ERROR;
	}
	int res = readall(ffd, fwbuf, st.st_size);
	if(res != st.st_size) {
		close(ffd);
		return LBUS_FIRMWARE_FILE_ERROR;
	}
	*((uint32_t*)(fwbuf + 0x150 + 4)) = tole32(fw_pg_size);
	*((uint32_t*)(fwbuf + 0x150 + 8)) = tole32(crc32(0xFFFFFFFF, fw_pg_size, fwbuf));
	void *p = fwbuf;
	uint16_t pg = FW_START_PAGE;
	for(void *p = fwbuf; p < (fwbuf+fw_pg_size); p+=PAGE_SIZE) {
		struct {
			struct lbus_hdr hdr;
			uint16_t page;
			uint8_t data[PAGE_SIZE];
			uint32_t crc;
		} __attribute__((packed)) pkg = {
			.hdr = {
				.length = tole16(sizeof(struct lbus_hdr) + sizeof(uint16_t) + PAGE_SIZE + sizeof(uint32_t) + 1),
				.addr = dst,
				.cmd = FLASH_FIRMWARE
			},
			.page = tole16(pg),
		};
		memcpy(pkg.data, p, PAGE_SIZE);
		pkg.crc = tole32(crc32(0xFFFFFFFF, PAGE_SIZE, p));
		int ret = lbus_tx(C, &pkg, sizeof(pkg));
		if(ret < 0) {
			close(ffd);
			return ret;
		}

		uint8_t reply=0x7F;
		for(int i=0; i<10; i++) {
			if(lbus_rx(C, &reply, 1) == 1) {
				break;
			} else {
				fprintf(stderr, ".");
				usleep(100000);
			}
		}
		if(reply == 0) {
			fprintf(stderr, "<%02X>", pg);
		} else {
			fprintf(stderr, " failure %d!\n", reply);
			close(ffd);
			return LBUS_GENERIC_ERROR;
		}
		pg++;
	}
	fprintf(stderr, " done!\n");
	close(ffd);
	return 0;
}

LBUS_API
const char* lbus_strerror(int ret) {
	switch(ret) {
		case LBUS_BUS_ERROR:
			return "bus error";
		case LBUS_NO_ANSWER:
			return "no answer";
		case LBUS_MISUSE_ERROR:
			return "request is forbidden";
		case LBUS_BROKEN_ANSWER:
			return "broken answer";
		case LBUS_CRC_ERROR:
			return "CRC error";
		case LBUS_FIRMWARE_FILE_ERROR:
			return "bad firmware file";
		case LBUS_GENERIC_ERROR:
			return "unspecified error";
		case LBUS_LIBUSB_INIT_ERROR:
			return "cannot initialize libusb";
		case LBUS_MEMORY_ERROR:
			return "not enough memory";
		case LBUS_DEVICE_NOT_FOUND:
			return "lbus master device not found";
		case LBUS_INTERFACE_NOT_AVAILABLE:
			return "cannot claim interface - might be in use";
	}
	return NULL;
}

LBUS_API
int lbus_open(lbus_ctx **C) {
	*C = calloc(1, sizeof(lbus_ctx));
	if(*C == NULL)
		return LBUS_MEMORY_ERROR;

	if(libusb_init(&((*C)->ctx))) {
		lbus_free(*C);
		*C = NULL;
		return LBUS_LIBUSB_INIT_ERROR;
	}
	if(NULL == ((*C)->dev = libusb_open_device_with_vid_pid((*C)->ctx, 0xdead, 0xcafe))) {
		lbus_free(*C);
		*C = NULL;
		return LBUS_DEVICE_NOT_FOUND;
	}
	if(libusb_claim_interface((*C)->dev, 0)) {
		lbus_free(*C);
		*C = NULL;
		return LBUS_INTERFACE_NOT_AVAILABLE;
	}
	return 0;
}

LBUS_API
void lbus_free(lbus_ctx* C) {
	if(C != NULL) {
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
