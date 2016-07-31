CFLAGS += $(shell pkg-config --cflags libusb-1.0)
LDFLAGS += $(shell pkg-config --libs libusb-1.0)

comm: comm.o
comm.o: comm.c

lcomm.o: lcomm.c lcomm.h
	$(CC) $(CFLAGS) -fPIC -o $@ -c $<

lcomm.so: lcomm.o
	$(CC) -shared $(LDFLAGS) -fPIC -o $@ $<

clean:
	rm comm.o comm
