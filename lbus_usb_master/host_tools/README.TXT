Tool for issuing LBUS requests using the USB busmaster

This is still very much work in progress. Expect APIs to break
at any point in time. There will be dragons.


Use the "comm" tool to issue queries/commands via LBUS.


Requirements for compiling:

- libusb >= 1.0
- lua (tested for 5.2)


Lua API:

The tool is prepared to launch Lua scripts. For now, only LED
setting/committing is available for the Lua scripts. This is meant
for implementing animations, scripted in Lua.

  led_set_16bit(controller, led, {values...})
    Sets one or more PWM duty cycle registers

    controller:
      ID of the LBUS controller to send command to
    led:
      LED ID to start setting values for
    values:
      a table containing one or more values to be set
      for the PWM duty cycle of the LED in question


  led_commit(controller)
    Make new values effective

    controller:
      ID of the LBUS controller to send command to


  usleep(useconds)
    Interrupt program flow for given number of microseconds

    useconds:
      number of microseconds
