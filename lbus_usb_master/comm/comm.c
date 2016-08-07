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

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "../../lbus_common/lbus_data.h"

void DumpHex(const char* info, const void* data, size_t size) {
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


static libusb_context *ctx = NULL;
static libusb_device_handle *dev = NULL;

#define MAX_FW_SIZE (44*1024)
#define FW_START_PAGE 4

#define LIBUSB_TIMEOUT 1000
#define LIBUSB_TXTIMEOUT 20

#define LBUS_BUS_ERROR -1
#define LBUS_NO_ANSWER -2
#define LBUS_MISUSE_ERROR -3
#define LBUS_BROKEN_ANSWER -4
#define LBUS_CRC_ERROR -5
#define LBUS_FIRMWARE_FILE_ERROR -6
#define LBUS_GENERIC_ERROR -7

static int tx(void* buf, int size) {
	int done = 0;
	int transferred = 0;
	uint8_t tbuf[64];
	while(size > 0) {
		const int chunk_size = size > 63 ? 63 : size;
		tbuf[0] = 1; // XMIT
		memcpy(tbuf+1, buf+done, chunk_size);
		int res = libusb_bulk_transfer(dev, 0x01, tbuf, chunk_size+1, &transferred, LIBUSB_TXTIMEOUT);
		if(res == 0 || res == LIBUSB_ERROR_TIMEOUT) {
			if(transferred > 0) {
				DumpHex("USB TX:\n", tbuf, transferred);
				done += transferred-1;
				size -= transferred-1;
			}
		} else {
			return LBUS_BUS_ERROR;
		}
	}
	return done;
}

static int rx(void* buf, int size) {
	int done = 0;
	int transferred = 0;
	uint8_t tbuf[2] = { 2 /* RECV */, 0 };
	while(done < size) {
		tbuf[1] = size > 64 ? 64 : size;
		int res = libusb_bulk_transfer(dev, 0x01, tbuf, 2, &transferred, LIBUSB_TXTIMEOUT);
		if(res != 0) {
			return LBUS_BUS_ERROR;
		}
		DumpHex("USB TX (receive command):\n", tbuf, transferred);
		transferred = 0;
		res = libusb_bulk_transfer(dev, 0x82, buf + done, tbuf[1], &transferred, LIBUSB_TIMEOUT);
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

static int echo() {
	uint8_t tbuf[64] = { 3 /* ECHO */, 1,2,3,4,5,6,7,8,9,10 };
	uint8_t rbuf[63];
	int transferred = 0;
	for(int i=0; i<16384; i++) {
		transferred = 0;
		int res = libusb_bulk_transfer(dev, 0x01, tbuf, 64, &transferred, LIBUSB_TXTIMEOUT);
		if(res != 0 || transferred != 64) {
			return LBUS_BUS_ERROR;
		}
		transferred = 0;
		res = libusb_bulk_transfer(dev, 0x82, rbuf, 63, &transferred, LIBUSB_TIMEOUT);
		if(res != 0 || transferred != 63) {
			return LBUS_BUS_ERROR;
		}
	}
}

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

int lbus_simple_recv(const int answer_bytes, uint32_t* d32) {
	int ret;
	if(answer_bytes == 1) {
		uint8_t reply;
		ret = rx(&reply, 1);
		if(ret < 0) return ret;
		if(ret == 0) return LBUS_NO_ANSWER;
		return reply;
	} else if(answer_bytes == 2) {
		uint16_t reply;
		ret = rx(&reply, 2);
		if(ret < 0) return ret;
		if(ret == 0) return LBUS_NO_ANSWER;
		return le16(reply);
	} else if(answer_bytes == 4) {
		uint32_t reply;
		ret = rx(&reply, 4);
		if(ret < 0) return ret;
		if(ret == 0) return LBUS_NO_ANSWER;
		*d32 = le32(reply);
		return 1;
	}
	return 0;
}

int lbus_simple_cmd(const int dst, const uint8_t command, const int answer_bytes, uint32_t* d32) {
	struct lbus_hdr pkg = {
		.length = tole16(sizeof(struct lbus_hdr)+answer_bytes),
		.addr = dst,
		.cmd = command,
	};
	int ret = tx(&pkg, sizeof(pkg));
	if(ret < 0) return ret;

	return lbus_simple_recv(answer_bytes, d32);
}

int lbus_ping(const int dst) {
	return lbus_simple_cmd(dst, PING, 1, NULL);
}

int lbus_reset_to_bootloader(const int dst) {
	return lbus_simple_cmd(dst, RESET_TO_BOOTLOADER, 0, NULL);
}

int lbus_reset_to_firmware(const int dst) {
	return lbus_simple_cmd(dst, RESET_TO_FIRMWARE, 0, NULL);
}

int lbus_erase_config(const int dst) {
	return lbus_simple_cmd(dst, ERASE_CONFIG, 0, NULL);
}

int lbus_get_config(const int dst, const uint16_t type, const bool numeric, const int answer_bytes, void* data) {
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
	int ret = tx(&pkg, sizeof(struct lbus_hdr)+sizeof(struct lbus_GET_DATA));
	if(ret < 0) return ret;

	if(numeric) {
		return lbus_simple_recv(answer_bytes, (uint32_t*)data);
	} else {
		return rx(data, answer_bytes);
	}
}

int lbus_set_address(const int dst, const uint8_t address) {
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
	int ret = tx(&pkg, sizeof(pkg));
	if(ret < 0) return ret;

	return lbus_simple_recv(1, NULL);
}

int lbus_read_memory(const int dst, const uint32_t address, const int length, void *buf) {
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
	int ret = tx(&pkg, sizeof(struct lbus_hdr)+sizeof(struct lbus_READ_MEMORY));
	if(ret < 0) return ret;

	ret = rx(buf, length);
    if(ret < 0) return ret;
	if(ret < length) return LBUS_BROKEN_ANSWER;

	uint32_t crc_test;
	ret = rx(&crc_test, 4);
	if(ret < 0) return ret;
	if(ret < 4) return LBUS_BROKEN_ANSWER;
	if(le32(crc_test) != crc32(0xFFFFFFFF, length, buf)) {
		return LBUS_CRC_ERROR;
	}
	return length;
}

#define LBUS_LED_MAX_VCOUNT 1024
int lbus_led_set_16bit(const int dst, const uint16_t led, const unsigned int vcount, const uint16_t values[]) {
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

	return tx(&pkg, sizeof(struct lbus_hdr)+sizeof(uint16_t)*(vcount+1));
}

int lbus_led_commit(const int dst) {
	return lbus_simple_cmd(dst, LED_COMMIT, 0, NULL);
}

int lbus_flash_firmware(const int dst, const char *path) {
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
		int ret = tx(&pkg, sizeof(pkg));
		if(ret < 0) {
			close(ffd);
			return ret;
		}

		uint8_t reply=0x7F;
		for(int i=0; i<10; i++) {
			if(rx(&reply, 1) == 1) {
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

static int lled_set_16bit(lua_State *L) {
	int dst = luaL_checkinteger(L, 1);
	int offset = luaL_checkinteger(L, 2);
	if(!lua_istable(L, 3))
		return luaL_argerror(L, 2, "no value table given");
	int len = 0;
	uint16_t values[1024];
	lua_pushnil(L);
	while(len < 1024 && lua_next(L, 3) != 0) {
		values[len] = lua_tointeger(L, -1);
		len++;
		lua_pop(L, 1);
	}
	lua_pushinteger(L, lbus_led_set_16bit(dst, offset, len, values));
	return 1;
}

static int lled_commit(lua_State *L) {
	int dst = luaL_checkinteger(L, 1);
	lua_pushinteger(L, lbus_led_commit(dst));
	return 1;
}

static int lusleep(lua_State *L) {
	int usecs = luaL_checkinteger(L, 1);
	usleep(usecs);
	return 0;
}

static int test_error(int ret) {
	switch(ret) {
		case LBUS_BUS_ERROR:
			fprintf(stderr, "error: bus error.\n");
			break;
		case LBUS_NO_ANSWER:
			fprintf(stderr, "error: no answer.\n");
			break;
		case LBUS_MISUSE_ERROR:
			fprintf(stderr, "error: request is forbidden.\n");
			break;
		case LBUS_BROKEN_ANSWER:
			fprintf(stderr, "error: broken answer.\n");
			break;
		case LBUS_CRC_ERROR:
			fprintf(stderr, "error: CRC error.\n");
			break;
		case LBUS_FIRMWARE_FILE_ERROR:
			fprintf(stderr, "error: bad firmware file.\n");
			break;
		case LBUS_GENERIC_ERROR:
			fprintf(stderr, "error: unspecified error.\n");
			break;
		default:
			return ret;
	}

	libusb_release_interface(dev, 0);
	libusb_close(dev);
	libusb_exit(ctx);

	exit(ret);
}

static void usage(void) {
	fprintf(stderr, "Usage: comm <dest_addr> <command> [parameters...]\n");
}

int main(int argc, char* argv[]) {
	int ret = 0;
	uint8_t buf[4096];
	int optind = 1;

	if(libusb_init(&ctx)) {
		fprintf(stderr, "cannot initialize libusb.\n");
		return -1;
	}
	if(NULL == (dev = libusb_open_device_with_vid_pid(ctx, 0xdead, 0xcafe))) {
		fprintf(stderr, "lbus master device not found.\n");
		goto error;
	}
	if(libusb_claim_interface(dev, 0)) {
		fprintf(stderr, "cannot claim interface - might be in use.\n");
		goto error;
	}

	if(argc == 2) {
		if(!strcasecmp("echo", argv[1])) {
			test_error(echo());
		} else {
			lua_State* L;
			L = luaL_newstate();
			luaL_openlibs(L);
			lua_register(L, "led_commit", lled_commit);
			lua_register(L, "led_set_16bit", lled_set_16bit);
			lua_register(L, "usleep", lusleep);
			if(luaL_dofile(L, argv[1])) {
				fprintf(stderr, "error: %s\n", lua_tostring(L, -1));
				ret = -1;
			}
			lua_close(L);
		}
	} else if(argc >= 3) {
		int dst = strtol(argv[optind++], NULL, 0);
		if(dst < 0 || dst > 0xFF) {
			fprintf(stderr, "bad address given.\n");
			goto error;
		}
		char *cmd = argv[optind++];
		if(!strcasecmp("ping", cmd)) {
			test_error(lbus_ping(dst));
			fprintf(stderr, "got reply\n");
		} else if(!strcasecmp("reset_to_bootloader", cmd)) {
			test_error(lbus_reset_to_bootloader(dst));
		} else if(!strcasecmp("reset_to_firmware", cmd)) {
			test_error(lbus_reset_to_firmware(dst));
		} else if(!strcasecmp("erase_config", cmd)) {
			test_error(lbus_erase_config(dst));
		} else if(!strcasecmp("get_data", cmd)) {
			if(optind == argc-1) {
				if(!strcasecmp("status", argv[optind])) {
					int reply = test_error(lbus_get_config(dst, LBUS_DATA_STATUS, true, 1, NULL));
					printf("%d\n", reply);
				} else if(!strcasecmp("address", argv[optind])) {
					int reply = test_error(lbus_get_config(dst, LBUS_DATA_ADDRESS, true, 1, NULL));
					printf("%d\n", reply);
				} else if(!strcasecmp("firmware_version", argv[optind])) {
					uint32_t version = 0;
					test_error(lbus_get_config(dst, LBUS_DATA_FIRMWARE_VERSION, true, 4, &version));
					printf("%x\n", version);
				} else if(!strcasecmp("bootloader_version", argv[optind])) {
					uint32_t version = 0;
					test_error(lbus_get_config(dst, LBUS_DATA_BOOTLOADER_VERSION, true, 4, &version));
					printf("%x\n", version);
				} else if(!strcasecmp("firmware_name", argv[optind])) {
					int reply = test_error(lbus_get_config(dst, LBUS_DATA_FIRMWARE_NAME_LENGTH, true, 1, NULL));
					if(reply <= 0) {
						printf("<not assigned>\n");
					} else {
						uint8_t nbuf[256];
						int l = test_error(lbus_get_config(dst, LBUS_DATA_FIRMWARE_NAME, false, reply, nbuf));
						nbuf[l] = '\0';
						printf("%s\n", nbuf);
					}
				} else {
					fprintf(stderr, "unknown data type name.\n");
					goto error;
				}
			} else if(optind == argc) {
				int item = strtol(argv[optind++], NULL, 0);
				if(item < 1 || item > 0xFFFF) {
					fprintf(stderr, "bad type_id.\n");
					goto error;
				}
				int reply_size = strtol(argv[optind++], NULL, 0);
				if(reply_size < 0 || reply_size > sizeof(buf)) {
					fprintf(stderr, "bad reply_size.\n");
					goto error;
				}
				int l = test_error(lbus_get_config(dst, LBUS_DATA_FIRMWARE_NAME, false, reply_size, buf));
				fprintf(stderr, "got answer:\n");
				write(1, buf, l);
			} else {
				fprintf(stderr, "usage: ... %s <type_id> <reply_size>\nor     ... %s <type_name>\n", cmd, cmd);
				goto error;
			}
		} else if(!strcasecmp("set_address", cmd)) {
			if(optind >= argc) {
				fprintf(stderr, "usage: ... %s <address>\n", cmd);
				goto error;
			}
			int address = strtol(argv[optind++], NULL, 0);
			test_error(lbus_set_address(dst, address));
			fprintf(stderr, "success\n");
		} else if(!strcasecmp("read_memory", cmd)) {
			if(optind + 1 >= argc) {
				fprintf(stderr, "usage: ... %s <address> <length>\n", cmd);
				goto error;
			}
			uint32_t address = strtoul(argv[optind++], NULL, 0);
			int length = (strtol(argv[optind++], NULL, 0)+3)&(~(3));
			test_error(lbus_read_memory(dst, address, length, buf));
			write(1, buf, length);
		} else if(!strcasecmp("led_set_16bit", cmd)) {
			if(optind + 1 >= argc) {
				fprintf(stderr, "usage: ... %s <led> <value> [<value> ...]\n", cmd);
				goto error;
			}
			int c = argc - (optind + 1);
			int led = strtoul(argv[optind++], NULL, 0);
			uint16_t values[1024];
			if(c>1024) c=1024;
			for(int i=0; i<c; i++)
				values[i] = tole16(strtoul(argv[optind++], NULL, 0));
			test_error(lbus_led_set_16bit(dst, led, c, values));
		} else if(!strcasecmp("led_commit", cmd)) {
			test_error(lbus_led_commit(dst));
		} else if(!strcasecmp("flash_firmware", cmd)) {
			if(optind >= argc) {
				fprintf(stderr, "usage: ... %s <firmware.bin>\n", cmd);
				goto error;
			}
			test_error(lbus_flash_firmware(dst, argv[optind]));
		} else {
			fprintf(stderr, "unknown command given.\n");
			goto error;
		}
	} else {
		usage();
		goto error;
	}
	goto done;

error:
	if(ret == 0)
		ret = 1;

done:
	if(dev != NULL) {
		libusb_release_interface(dev, 0);
		libusb_close(dev);
	}
	if(ctx != NULL)
		libusb_exit(ctx);
	return ret;
}
