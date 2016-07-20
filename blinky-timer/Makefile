BINARY = blinky-timer

LDSCRIPT = ../stm32-default.ld

all: $(BINARY).bin

flash: $(BINARY).bin
	stm32flash -w $(BINARY).bin -g 0 /dev/ttyUSB0

include ../Makefile.include

