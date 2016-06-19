This is a PWM controller for 4 RGB LEDs

outputs are active low and are meant to control a constant current
source for the LEDs.

LED1:
 r:PA0 g:PA6 b:PB6

LED2:
 r:PA1 g:PA7 b:PB7

LED3:
 r:PA2 g:PB0 b:PB8

LED4:
 r:PA3 g:PB1 b:PB9

you can send commands via USB (CDC-ACM) or UART:

UART:
 rx:PB11 (5V tolerant), tx:PB10 (unused)
 921600 Baud, 8N1

Command:

 <16bit ID> <16bit R> <16bit G> <16bit B>

ID is the number of the LED to toggle.
IDs 0x0000..0x00FF are reserved.

Command state machine will stay waiting for an ID when it sees
an ID from this range. Thus, you can send a bunch of 0x00 bytes
to bring the state machine into a known state.

All 16bit values are in network byte order.
