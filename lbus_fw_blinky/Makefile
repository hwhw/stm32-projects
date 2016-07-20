BINARY = firmware
OBJS = ../lbus_common/lbus.o ../lbus_common/config.o

LDSCRIPT = ../lbus_common/stm32firmware.ld
VERSION = $(shell git rev-parse --short=8 HEAD)
CFLAGS += -fms-extensions -DVERSION=0x$(VERSION) -I../lbus_common

all: firmware.bin

flash: firmware.bin
	@echo "Firmware must be flashed using the LBUS bootloader's"
	@echo "dedicated tools in order to set a correct checksum."

include ../Makefile.include

