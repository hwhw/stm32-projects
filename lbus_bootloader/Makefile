BINARY = bootloader
OBJS = ../lbus_common/lbus.o ../lbus_common/config.o

LDSCRIPT = stm32loader.ld
VERSION = $(shell git rev-parse --short=8 HEAD)
CFLAGS += -fms-extensions -DVERSION=0x$(VERSION) -I../lbus_common

all: bootloader.bin

flash: bootloader.bin
	stm32flash -w bootloader.bin -S 0x08000000:0x1000 /dev/ttyUSB0

include ../Makefile.include

