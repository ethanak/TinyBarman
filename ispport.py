#!/usr/bin/env python
#coding: utf-8

import serial
from serial.tools.list_ports import comports


def detect_avrisp(port):
    try:
        if '067B:2303' not in port[2]:
            return
    except:
        return
    try:
        ser = serial.Serial(port[0], 19200, timeout=1)
    except:
        return
    ser.write('1 ')
    a=ser.read(9)
    if a == '\x14AVR ISP\x10':
        return port[0]
    ser.close()
    return None

ports = comports()
avr = []
for port in ports:
    a = detect_avrisp(port)
    if a:
        avr.append(a)
import sys
if len(avr) == 1:
    sys.stdout.write(avr[0])
    exit(0)
elif len(avr) == 0:
    sys.stderr.write("No port\n")
else:
    sys.stderr.write("Too many ports\n")
exit(1)
