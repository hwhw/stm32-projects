LBUS device oriented programming library


config.*:
	not LBUS specific, manages a configuration data store in a
	part of the STM32's flash memory

lbus.*:
	main LBUS handling code
	LBUS is handled on the following pins:

	- MAX485 DE:  PB12
	- MAX485 ~RE: PB13
	- MAX485 DI:  PB10 (USART3 TX)
	- MAX485 RO:  PB11 (USART3 RX)

	Note that USART3 is used because it uses a 5V tolerant RX pin.

lbus_data.h:
	Just the LBUS data structures, might get extended with future
	extensions/modifications of the LBUS protocol

platform.h:
	Some general STM32 platform configuration defines for use
	in LBUS oriented projects

stm32firmware.ld:
	Linker script for firmware compiled for use in combination
	with the LBUS bootloader
