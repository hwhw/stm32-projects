CC:=$(ARCH)gcc

all: read libusbbb.so

clean:
	rm -f *.o *.so read

read.o: read.c
	$(CC) $(CFLAGS) -std=c99 $(shell pkg-config --cflags libusb-1.0) -c $< -o $@

read: read.o libusbbb.so
	$(CC) $(LDFLAGS) -L. -lusbbb -lm $< -o $@

#	$(CC) $(LDFLAGS) $(shell pkg-config --libs libusb-1.0) $< -o $@

usbbb.o: usbbb.c
	$(CC) $(CFLAGS) -std=c99 -shared -fPIC -pthread -fvisibility=hidden $(shell pkg-config --cflags libusb-1.0) -c $< -o $@

libusbbb.so: usbbb.o
	$(CC) $(LDFLAGS) -shared -fPIC -pthread $(shell pkg-config --libs libusb-1.0) $< -o $@
