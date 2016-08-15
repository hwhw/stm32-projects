CC:=$(ARCH)gcc
LUA:=lua51

all: liblbuscomm.so lbus-tool lualbuscomm.so

clean:
	rm -f *.o *.so lbus-tool

lbuscomm.o: lbuscomm.c
	$(CC) $(CFLAGS) -std=c99 -shared -fPIC -fvisibility=hidden $(shell pkg-config --cflags libusb-1.0) -c $< -o $@

liblbuscomm.so: lbuscomm.o
	$(CC) $(LDFLAGS) -shared -fPIC $(shell pkg-config --libs libusb-1.0) $< -o $@

lualbuscomm.o: lualbuscomm.c
	$(CC) $(CFLAGS) -std=c99 -shared -fPIC $(shell pkg-config --cflags $(LUA)) -c $< -o $@

lualbuscomm.so: lualbuscomm.o lbuscomm.o
	$(CC) $(LDFLAGS) -shared -fPIC $(shell pkg-config --libs $(LUA)) $< -o $@

lbus-tool: lbus-tool.o
	$(CC) $(LDFLAGS) -L. -llbuscomm $< -o $@

lbus-tool.o: lbus-tool.c
	$(CC) $(CFLAGS) -std=c99 -c $< -o $@
