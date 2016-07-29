CFLAGS += $(shell pkg-config --cflags libusb-1.0)
LDFLAGS += $(shell pkg-config --libs libusb-1.0)

comm: comm.o

comm.o: comm.c

clean:
	rm comm.o comm
