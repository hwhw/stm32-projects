# README

This example implements a USB Device Firmware Upgrade (DFU) bootloader
to demonstrate the use of the USB device stack.

It has been copied over from libopencm3-examples (examples/stm32/f1/stm32-h103)
and slightly modified to work with the China development boards I got.

As the device only has a reset button, I use the BOOT1 jumper for toggling
bootloader on/off (as it is basically unused: as long as BOOT0 is set to 0,
the device will boot from internal flash).

See http://libopencm3.org/wiki/USB_DFU for documentation on how to use
the bootloader for flashing.
