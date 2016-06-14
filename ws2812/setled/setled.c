#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <arpa/inet.h>


int
set_interface_attribs (int fd, int speed, int parity)
{
        struct termios tty;
        memset (&tty, 0, sizeof tty);
        if (tcgetattr (fd, &tty) != 0)
        {
                fprintf(stderr, "error %d from tcgetattr\n", errno);
                return -1;
        }

        cfsetospeed (&tty, speed);
        cfsetispeed (&tty, speed);

        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
        // disable IGNBRK for mismatched speed tests; otherwise receive break
        // as \000 chars
        tty.c_iflag &= ~IGNBRK;         // disable break processing
        tty.c_lflag = 0;                // no signaling chars, no echo,
                                        // no canonical processing
        tty.c_oflag = 0;                // no remapping, no delays
        tty.c_cc[VMIN]  = 0;            // read doesn't block
        tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

        tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

        tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
                                        // enable reading
        tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
        tty.c_cflag |= parity;
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CRTSCTS;

        if (tcsetattr (fd, TCSANOW, &tty) != 0)
        {
                fprintf(stderr, "error %d from tcsetattr\n", errno);
                return -1;
        }
        return 0;
}

int
set_blocking (int fd, int should_block)
{
        struct termios tty;
        memset (&tty, 0, sizeof tty);
        if (tcgetattr (fd, &tty) != 0)
        {
                fprintf(stderr, "error %d from tggetattr\n", errno);
                return -1;
        }

        tty.c_cc[VMIN]  = should_block ? 1 : 0;
        tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

        if (tcsetattr (fd, TCSANOW, &tty) != 0) {
                fprintf(stderr, "error %d setting term attributes\n", errno);
		return -1;
	}
	return 0;
}

int main(int argc, char* argv[]) {
	uint16_t id;
	uint8_t r;
	uint8_t g;
	uint8_t b;

	if(argc != 6) {
		fprintf(stderr, "usage: %s <tty device> <led id> <G> <R> <B>\n", argv[0]);
		return -1;
	}
	int fd = open(argv[1], O_RDWR | O_NOCTTY | O_SYNC);
	if(fd < 0) {
		fprintf(stderr, "error %d opening %s: %s\n", errno, argv[1], strerror(errno));
		return -1;
	}
	if(set_interface_attribs(fd, B115200, 0))
		return -1;

	id = strtoul(argv[2], NULL, 0);
	r = strtoul(argv[3], NULL, 0);
	g = strtoul(argv[4], NULL, 0);
	b = strtoul(argv[5], NULL, 0);
	
	id = htons(id);
	int err = write(fd, &id, 2);
	if(err == -1) goto wrfailure;
	err = write(fd, &r, 1);
	if(err == -1) goto wrfailure;
	err = write(fd, &g, 1);
	if(err == -1) goto wrfailure;
	err = write(fd, &b, 1);
	if(err == -1) goto wrfailure;

	return 0;
wrfailure:
	fprintf(stderr, "error %d writing data: %s\n", errno, strerror(errno));
	return -1;
}
