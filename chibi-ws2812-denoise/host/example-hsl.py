#!/usr/bin/env pypy
import usbbb
import colorsys
import math

bb = usbbb.BB()

h = 0

while True:
    for y in range(0,40):
        for x in range(0,40):
            c = colorsys.hls_to_rgb(h+math.sqrt((x-19.5)**2 + (y-19.5)**2)/40,0.5,1.0)
            bb.set_led40(x,y,int(c[0]*255), int(c[1]*255), int(c[2]*255))

    h = (h + .005) % 1.0
    bb.wait_measure()
    bb.transmit(-1)
