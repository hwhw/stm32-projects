#!/usr/bin/env pypy
import usbbb
import time

bb = usbbb.BB()

for y in range(0,40):
    for x in range(0,40):
        bb.set_led40(x,y,0,0,0)

bb.transmit(1)
time.sleep(0.1)
