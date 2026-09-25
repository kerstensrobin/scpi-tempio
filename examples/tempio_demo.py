#!/usr/bin/env python3
"""Run every SCPI command of the Nacho.works SCPI-TempIO once.

Usage:
    pip install pyserial
    python3 tempio_demo.py [PORT]

Without PORT, the first connected SCPI-TempIO is used (Linux/macOS), e.g.
    python3 tempio_demo.py COM5          (Windows)
    python3 tempio_demo.py /dev/ttyACM0  (Linux)
"""
import glob
import sys

import serial


def find_port():
    ports = glob.glob("/dev/serial/by-id/usb-Nacho.works_SCPI-TempIO*")
    if not ports:
        sys.exit("No SCPI-TempIO found, pass the port as argument")
    return ports[0]


class TempIO:
    def __init__(self, port):
        self.s = serial.Serial(port, timeout=1)
        self.s.reset_input_buffer()

    def write(self, cmd):
        self.s.write((cmd + "\n").encode())

    def query(self, cmd):
        self.write(cmd)
        return self.s.readline().decode().strip()

    def check_errors(self):
        while True:
            err = self.query("SYST:ERR?")
            if err.startswith("0,"):
                return
            print("  error:", err)


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_port()
    dev = TempIO(port)

    print("--- identification")
    print("*IDN?        ", dev.query("*IDN?"))
    print("SYST:VERS?   ", dev.query("SYST:VERS?"))
    dev.write("*RST")
    dev.write("*CLS")
    print("*OPC?        ", dev.query("*OPC?"))
    print("*TST?        ", dev.query("*TST?"), "(0 = sensor OK)")

    print("--- measurements")
    print("MEAS:TEMP?   ", dev.query("MEAS:TEMP?"), "degC")
    print("MEAS:HUM?    ", dev.query("MEAS:HUM?"), "%RH")
    temp, hum = dev.query("MEAS:ALL?").split(",")
    print(f"MEAS:ALL?     {temp} degC, {hum} %RH")

    print("--- GPIO")
    dev.write("DIG:PIN0 1")         # latch the level first,
    dev.write("DIG:PIN0:MODE OUT")  # then enable the output: no glitch
    print("DIG:PIN0:MODE?", dev.query("DIG:PIN0:MODE?"))
    print("DIG:PIN0?     ", dev.query("DIG:PIN0?"))
    dev.write("DIG:PIN1:MODE OD")
    dev.write("DIG:PIN1 0")
    print("DIG:PIN1:MODE?", dev.query("DIG:PIN1:MODE?"))
    dev.write("DIG:PIN2:MODE IN")
    print("DIG:PIN2?     ", dev.query("DIG:PIN2?"))
    print("DIG:PORT?     ", dev.query("DIG:PORT?"), "(DIO7..DIO0)")
    dev.write("DIG:PIN0 0")

    print("--- chained commands")
    print("MEAS:TEMP?;MEAS:HUM?;DIG:PORT?  ->", dev.query("MEAS:TEMP?;MEAS:HUM?;DIG:PORT?"))

    print("--- error handling")
    dev.write("DIG:PIN9 1")  # pin out of range
    dev.write("FOO:BAR")     # unknown command
    dev.check_errors()

    dev.write("*RST")  # all pins back to input
    print("done")


if __name__ == "__main__":
    main()
