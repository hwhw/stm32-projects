#include <termios.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "../../lbus_common/lbus_data.h"

#define MAX_FW_SIZE (44*1024)
#define FW_START_PAGE 4

uint32_t crc32(uint32_t Crc, uint32_t Size, void *Buffer) {
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

void usage(void) {
	fprintf(stderr, "Usage: comm [-b baudrate] <ttydev> <dest_addr> <command> [parameters...]\n");
	exit(-1);
}

int readall(int fd, void *buf, size_t count) {
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

int main(int argc, char* argv[]) {
	int opt;
	int ret = -1;
	uint32_t br = B921600;
	struct termios config;
	uint8_t buf[4096];

	while((opt = getopt(argc, argv, "b:")) != -1) {
		switch(opt) {
			case 'b':
				switch(atoi(optarg)) {
					case 9600: br = B9600; break;
					case 19200: br = B19200; break;
					case 38400: br = B38400; break;
					case 57600: br = B57600; break;
					case 115200: br = B115200; break;
					case 230400: br = B230400; break;
					case 460800: br = B460800; break;
					case 500000: br = B500000; break;
					case 576000: br = B576000; break;
					case 921600: br = B921600; break;
					case 1000000: br = B1000000; break;
					case 1152000: br = B1152000; break;
					case 1500000: br = B1500000; break;
					case 2000000: br = B2000000; break;
					case 2500000: br = B2500000; break;
					case 3000000: br = B3000000; break;
					case 3500000: br = B3500000; break;
					case 4000000: br = B4000000; break;
					default:
						fprintf(stderr, "invalid baud rate given, aborting.\n");
						exit(-1);
				}
				break;
			default:
				usage();
		}
	}
	if(optind + 2 >= argc) {
		usage();
	}
	char *ttydev = argv[optind++];

	int fd = open(ttydev, O_RDWR | O_NOCTTY);
	if(fd == -1) {
		perror("open tty");
		exit(-1);
	}
	if(!isatty(fd)) {
		fprintf(stderr, "device is not a TTY.\n");
		goto done;
	}
	if(tcgetattr(fd, &config) < 0) {
		fprintf(stderr, "cannot get TTY config.\n");
		goto done;
	}
	config.c_iflag &= ~(IGNBRK|BRKINT|ICRNL|INLCR|PARMRK|INPCK|ISTRIP|IXON);
	config.c_oflag = 0;
	config.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN | ISIG);
	config.c_cflag &= ~(CSIZE | PARENB);
	config.c_cflag |= CS8 | CLOCAL | CREAD | br;
	config.c_cc[VMIN] = 0;
	config.c_cc[VTIME] = 10;
	if(tcsetattr(fd, TCSAFLUSH, &config) < 0) {
		close(fd);
		fprintf(stderr, "cannot set TTY config.\n");
		goto done;
	}

	int dst = strtol(argv[optind++], NULL, 0);
	if(dst < 0 || dst > 0xFF) usage();
	char *cmd = argv[optind++];
	if(!strcasecmp("ping", cmd)) {
		uint8_t pkg[] = { 5, 0, dst, PING };
		write(fd, pkg, sizeof(pkg));
		uint8_t reply;
		if(read(fd, &reply, 1) == 1) {
			fprintf(stderr, "got reply\n");
			ret = 0;
		} else {
			fprintf(stderr, "miss\n");
		}
	} else if(!strcasecmp("reset_to_bootloader", cmd)) {
		uint8_t pkg[] = { 4, 0, dst, RESET_TO_BOOTLOADER };
		write(fd, pkg, sizeof(pkg));
		ret = 0;
	} else if(!strcasecmp("reset_to_firmware", cmd)) {
		uint8_t pkg[] = { 4, 0, dst, RESET_TO_FIRMWARE };
		write(fd, pkg, sizeof(pkg));
		ret = 0;
	} else if(!strcasecmp("erase_config", cmd)) {
		uint8_t pkg[] = { 4, 0, dst, ERASE_CONFIG };
		write(fd, pkg, sizeof(pkg));
		ret = 0;
	} else if(!strcasecmp("get_data", cmd)) {
		if(optind + 1 >= argc) {
			fprintf(stderr, "usage: ... %s <type_id> <reply_size>\n", cmd);
			goto done;
		}
		int item = strtol(argv[optind++], NULL, 0);
		if(item < 1 || item > 0xFFFF) {
			fprintf(stderr, "bad type_id.\n");
			goto done;
		}
		int reply_size = strtol(argv[optind++], NULL, 0);
		if(reply_size < 0 || reply_size > sizeof(buf)) {
			fprintf(stderr, "bad reply_size.\n");
			goto done;
		}
		uint16_t d16 = 6 + reply_size;
		write(fd, &d16, 2);
		uint8_t d8[] = { dst, GET_DATA };
		write(fd, d8, 2);
		d16 = item;
		write(fd, &d16, 2);
		if(readall(fd, &buf, reply_size) == reply_size) {
			fprintf(stderr, "got answer:\n");
			write(1, buf, reply_size);
			ret = 0;
		} else {
			fprintf(stderr, "error or no reply\n");
		}
	} else if(!strcasecmp("set_address", cmd)) {
		if(optind >= argc) {
			fprintf(stderr, "usage: ... %s <address>\n", cmd);
			goto done;
		}
		int address = strtol(argv[optind++], NULL, 0);
		if(address < 1 || address > 254) {
			fprintf(stderr, "bad address given.\n");
			goto done;
		}
		uint8_t pkg[] = { 6, 0, dst, SET_ADDRESS, address };
		write(fd, pkg, sizeof(pkg));
		uint8_t reply;
		if(read(fd, &reply, 1) == 1 && reply == 0) {
			fprintf(stderr, "success\n");
			ret = 0;
		} else {
			fprintf(stderr, "failure\n");
		}
	} else if(!strcasecmp("read_memory", cmd)) {
		if(optind + 1 >= argc) {
			fprintf(stderr, "usage: ... %s <address> <length>\n", cmd);
			goto done;
		}
		uint32_t address = strtoul(argv[optind++], NULL, 0);
		int length = (strtol(argv[optind++], NULL, 0)+3)&(~(3));
		if(length < 0 || length > sizeof(buf)) {
			fprintf(stderr, "bad reply_size.\n");
			goto done;
		}
		uint16_t d16 = 8 + length + 4;
		write(fd, &d16, 2);
		uint8_t d8[] = { dst, READ_MEMORY };
		write(fd, d8, 2);
		write(fd, &address, 4);
		uint32_t crc_test = 0;
		if(readall(fd, buf, length) == length && read(fd, &crc_test, 4) == 4) {
			fprintf(stderr, "got answer (crc is %08X - ", crc_test);
			if(crc_test == crc32(0xFFFFFFFF, length, buf)) {
				fprintf(stderr, "OK)\n");
				write(1, buf, length);
				ret = 0;
			} else {
				fprintf(stderr, "WRONG)\n");
			}
		} else {
			fprintf(stderr, "error or no reply\n");
		}
	} else if(!strcasecmp("flash_firmware", cmd)) {
		if(optind >= argc) {
			fprintf(stderr, "usage: ... %s <firmware.bin>\n", cmd);
			goto done;
		}
		struct stat st;
		int ffd = open(argv[optind], O_RDONLY);
		if(ffd == -1) {
			perror("open firmware file");
			goto done;
		}
		if(fstat(ffd, &st) == -1) {
			close(ffd);
			perror("stat() firmware file");
			goto done;
		}
		if(st.st_size > MAX_FW_SIZE) {
			close(ffd);
			fprintf(stderr, "firmware file too large.\n");
			goto done;
		}
		int fw_pg_size = (st.st_size+(PAGE_SIZE-1)) & (~(PAGE_SIZE-1));
		void *fwbuf = calloc(1, fw_pg_size);
		int res = readall(ffd, fwbuf, st.st_size);
		if(res != st.st_size) {
			close(ffd);
			fprintf(stderr, "error reading firmware file.\n");
			goto done;
		}
		*((uint32_t*)(fwbuf + 0x150 + 4)) = fw_pg_size;
		*((uint32_t*)(fwbuf + 0x150 + 8)) = crc32(0xFFFFFFFF, fw_pg_size, fwbuf);
		void *p = fwbuf;
		uint16_t pg = FW_START_PAGE;
		for(void *p = fwbuf; p < (fwbuf+fw_pg_size); p+=PAGE_SIZE) {
			uint16_t d16 = 6 + PAGE_SIZE + 4 + 1;
			write(fd, &d16, 2);
			uint8_t d8[] = { dst, FLASH_FIRMWARE };
			write(fd, d8, 2);
			write(fd, &pg, 2);
			write(fd, p, PAGE_SIZE);
			uint32_t crc = crc32(0xFFFFFFFF, PAGE_SIZE, p);
			write(fd, &crc, 4);
			uint8_t reply;
			if(read(fd, &reply, 1) == 1 && reply == 0) {
				fprintf(stderr, "<%02X>", pg);
			} else {
				close(ffd);
				fprintf(stderr, " failure!\n");
				goto done;
			}
			pg++;
		}
		fprintf(stderr, " done!\n");
		close(ffd);
		ret = 0;
	} else {
		fprintf(stderr, "unknown command given.\n");
	}
done:
	close(fd);
	return ret;
}
