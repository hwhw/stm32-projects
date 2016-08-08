/* LBUS USB bus master: Host tool
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
#include <unistd.h>
#include <string.h>

#include "lbuscomm.h"

static lbus_ctx* C;

static int test_error(int ret) {
	const char* errstr = lbus_strerror(ret);
	if(errstr == NULL)
		return ret;

	fprintf(stderr, "error: %s.\n", errstr);
	lbus_free(C);
	exit(ret);
}

static void usage(void) {
	fprintf(stderr, "Usage: comm <dest_addr> <command> [parameters...]\n");
}

int main(int argc, char* argv[]) {
	int ret = 0;
	uint8_t buf[4096];
	int optind = 1;

	test_error(lbus_open(&C));

	if(argc == 2) {
		if(!strcasecmp("echo", argv[1])) {
			test_error(lbus_busmaster_echo(C));
		} else {
			fprintf(stderr, "bad short command.\n");
			goto error;
		}
	} else if(argc >= 3) {
		int dst = strtol(argv[optind++], NULL, 0);
		if(dst < 0 || dst > 0xFF) {
			fprintf(stderr, "bad address given.\n");
			goto error;
		}
		char *cmd = argv[optind++];
		if(!strcasecmp("ping", cmd)) {
			test_error(lbus_ping(C, dst));
			fprintf(stderr, "got reply\n");
		} else if(!strcasecmp("reset_to_bootloader", cmd)) {
			test_error(lbus_reset_to_bootloader(C, dst));
		} else if(!strcasecmp("reset_to_firmware", cmd)) {
			test_error(lbus_reset_to_firmware(C, dst));
		} else if(!strcasecmp("erase_config", cmd)) {
			test_error(lbus_erase_config(C, dst));
		} else if(!strcasecmp("get_data", cmd)) {
			if(optind == argc-1) {
				if(!strcasecmp("status", argv[optind])) {
					int reply = test_error(lbus_get_config(C, dst, LBUS_DATA_STATUS, true, 1, NULL));
					printf("%d\n", reply);
				} else if(!strcasecmp("address", argv[optind])) {
					int reply = test_error(lbus_get_config(C, dst, LBUS_DATA_ADDRESS, true, 1, NULL));
					printf("%d\n", reply);
				} else if(!strcasecmp("firmware_version", argv[optind])) {
					uint32_t version = 0;
					test_error(lbus_get_config(C, dst, LBUS_DATA_FIRMWARE_VERSION, true, 4, &version));
					printf("%x\n", version);
				} else if(!strcasecmp("bootloader_version", argv[optind])) {
					uint32_t version = 0;
					test_error(lbus_get_config(C, dst, LBUS_DATA_BOOTLOADER_VERSION, true, 4, &version));
					printf("%x\n", version);
				} else if(!strcasecmp("firmware_name", argv[optind])) {
					int reply = test_error(lbus_get_config(C, dst, LBUS_DATA_FIRMWARE_NAME_LENGTH, true, 1, NULL));
					if(reply <= 0) {
						printf("<not assigned>\n");
					} else {
						uint8_t nbuf[256];
						int l = test_error(lbus_get_config(C, dst, LBUS_DATA_FIRMWARE_NAME, false, reply, nbuf));
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
				int l = test_error(lbus_get_config(C, dst, LBUS_DATA_FIRMWARE_NAME, false, reply_size, buf));
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
			test_error(lbus_set_address(C, dst, address));
			fprintf(stderr, "success\n");
		} else if(!strcasecmp("read_memory", cmd)) {
			if(optind + 1 >= argc) {
				fprintf(stderr, "usage: ... %s <address> <length>\n", cmd);
				goto error;
			}
			uint32_t address = strtoul(argv[optind++], NULL, 0);
			int length = (strtol(argv[optind++], NULL, 0)+3)&(~(3));
			test_error(lbus_read_memory(C, dst, address, length, buf));
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
				values[i] = strtoul(argv[optind++], NULL, 0);
			test_error(lbus_led_set_16bit(C, dst, led, c, values));
		} else if(!strcasecmp("led_commit", cmd)) {
			test_error(lbus_led_commit(C, dst));
		} else if(!strcasecmp("flash_firmware", cmd)) {
			if(optind >= argc) {
				fprintf(stderr, "usage: ... %s <firmware.bin>\n", cmd);
				goto error;
			}
			test_error(lbus_flash_firmware(C, dst, argv[optind]));
		} else {
			fprintf(stderr, "unknown command given.\n");
			goto error;
		}
	} else {
		usage();
		goto error;
	}
	lbus_free(C);
	exit(0);

error:
	lbus_free(C);
	exit(1);
}
