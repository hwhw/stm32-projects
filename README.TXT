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
  * or visit a friend who will flash a USB bootloader for you
    (see below/in usb_dfu)

* initialize libopencm3 submodule:

# submodule init
# submodule update

* build libopencm3

* optional: set up default linker script (defaults to USB bootloader
  linker configuration for now, so you need to change it when you
  are using a flash tool and don't have a bootloader):

# cp stm32-full.ld stm32-default.ld

# cd libopencm3 && make

* build projects you're interested in

# cd ws2812 && make
(or some other project folder)

Flashing:
---------

The Makefile is prepared with a few targets that will trigger flashing
using the various possible ways. Look into Makefile.rules for details.

When using the USB bootloader, just run

# make blinky.dfu-flash

(this will build blinky.bin and runs dfu-flash with config for the
USB bootloader)

When you're going to use stm32flash (and it's easy and nice!), do it
like this:

* set BOOT0 to 1, BOOT1 to 0 (boot from internal memory - ROM bootloader)
* connect UART to UART1 pins (GND, on STM32F103: TX on PA9, RX on PA10)
* power the development board
* run stm32flash:

# stm32flash -w mybinary.bin /dev/ttyUSB0


List of projects:
-----------------

usb_dfu:

  USB Bootloader (99% copied from libopencm3-examples)
  Adapted for STM32F103 China development board:
  - bootloader is triggered by setting BOOT1 to 1 (since no other button
    is available)
  - a Makefile target for projects exists: use the suffix .dfu-flash
    to run dfu-util for flashing

blinky:

  Just blink the onboard LED. Start with this!

ws2812:

  this implements a controller for WS2812(b) LEDs using PWM capabilities.
  It presents itself to the host PC as a USB CDC (serial) device.
  You can send data to that USB device to set LED colors:
  <16bit MSB LED number> <16bit red> <16bit green> <16bit blue>
  (only topmost 8bit of color values are used)
  Also accepts the same command syntax on UART3 (RX only), 921600 baud, 8N1.
  A tool that does this with a CLI interface is also present in a subfolder.

moodlight:

  generate 16bit PWM outputs for up to 4 RGB LEDs (12 channels).
  same protocol as for ws2812 above.
  See the README in the project folder for details.

tvbgone:

  An implementation of the TV-B-Gone software for the STM32.


Your own work?
--------------

See libopencm3-examples (https://github.com/libopencm3/libopencm3-examples/)
to see how to get things going. You'll probably need to adapt GPIO pins
(at least) for your target platform.


If you happen to have an el-cheapo STM32F103 from China, too, these
documentation links might come handy:
* stm32f103 datasheet: http://www.st.com/content/ccc/resource/technical/document/datasheet/33/d4/6f/1d/df/0b/4c/6d/CD00161566.pdf/files/CD00161566.pdf/jcr:content/translations/en.CD00161566.pdf
* stm32f1xx manual: http://www.st.com/content/ccc/resource/technical/document/reference_manual/59/b9/ba/7f/11/af/43/d5/CD00171190.pdf/files/CD00171190.pdf/jcr:content/translations/en.CD00171190.pdf
* STM32 Cortex-M3 programming manual: http://www.st.com/st-web-ui/static/active/en/resource/technical/document/programming_manual/CD00228163.pdf
* http://www.mikrocontroller.net/articles/STM32 (german, KB article)
