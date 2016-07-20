Implements a bootloader on the LBUS protocol specification

See https://cccgoe.de/wiki/LightBus for specification and further
documentation.

LBUS is handled on the following pins:

- MAX485 DE:  PB12
- MAX485 ~RE: PB13
- MAX485 DI:  PB10 (USART3 TX)
- MAX485 RO:  PB11 (USART3 RX)

Note that USART3 is used because it uses a 5V tolerant RX pin.
