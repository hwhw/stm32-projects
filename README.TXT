HW's STM32 projects
-------------------

I bought a bunch of STM32F103 development boards in China for about
2 EUR per item (containing the microcontroller, a 8MHz Quarz, 32kHz
oscillator for RTC, USB jack, LTO, jumpers for BOOT0/1 and a reset
button. They make a great experimental platform. Here I collect
projects I created with it.

For now I settled on using libopencm3 for interfacing with the core
for when I'm too lazy to write such stuff myself. In case of the USB
code, for example, I hope you can relate.

First steps:
------------

* install an ARM-none-eabi toolchain
* optional: install ARM-none-eabi GDB
* install software that allows you to put your firmware onto the
  controller:
  * st-link (if you have an st-link programmer)
  * openocd (if you have an st-link programmer - as an alternative -
    or some other more obscure tool)
  * stm32flash (when you're going to flash via UART)

* initialize libopencm3 submodule:

# submodule init
# submodule update

* build libopencm3

# cd libopencm3 && make

* build projects you're interested in

# cd ws2812 && make

Flashing:
---------

The Makefile is prepared with a few targets that will trigger flashing
using the various possible ways. Look into Makefile.rules for details.

When you're going to use stm32flash (and it's easy and nice!), do it
like this:

* set BOOT0 to 1, BOOT1 to 0 (boot from internal memory - ROM bootloader)
* connect UART to UART1 pins (GND, on STM32F103: TX on PA9, RX on PA10)
* power the development board
* run stm32flash:

# stm32flash -w mybinary.bin /dev/ttyUSB0

List of projects:
-----------------

ws2812:

  this implements a controller for WS2812(b) LEDs using PWM capabilities.
  It presents itself to the host PC as a USB CDC (serial) device.
  You can send data to that USB device to set LED colors:
  <16bit MSB LED number> <8bit green> <8bit red> <8bit blue>
  A tool that does this with a CLI interface is also present in a subfolder.

