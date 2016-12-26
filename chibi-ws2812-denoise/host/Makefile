CC:=$(ARCH)gcc

all: libusbbb.so libusbbb-emu.so

clean:
	rm -f *.o *.so read

read.o: read.c
	$(CC) $(CFLAGS) -std=c11 -c $< -o $@

read: read.o libusbbb.so
	$(CC) $(LDFLAGS) -L. -lusbbb -lm $< -o $@

usbbb.o: usbbb.c
	$(CC) $(CFLAGS) -std=c11 -shared -fPIC -pthread -fvisibility=hidden $(shell pkg-config --cflags libusb-1.0) -c $< -o $@

usbbb-emu.o: usbbb-emu.c
	$(CC) $(CFLAGS) -std=c11 -shared -fPIC -pthread -fvisibility=hidden $(shell pkg-config --cflags sdl2) -c $< -o $@

libusbbb.so: usbbb.o
	$(CC) $(LDFLAGS) -shared -fPIC -pthread $(shell pkg-config --libs libusb-1.0) $< -o $@

libusbbb-emu.so: usbbb-emu.o
	$(CC) $(LDFLAGS) -shared -fPIC -pthread $(shell pkg-config --libs sdl2) $< -o $@
