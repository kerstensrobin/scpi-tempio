# Nacho.works SCPI-TempIO

> USB-connected environmental sensor and GPIO control module based on the STM32G0B1KBTx.

---

## Repository Contents

| Path          | Contents                                                        |
| ------------- | --------------------------------------------------------------- |
| `hardware/`   | KiCad project; `SCPI_tempIO-rounded.kicad_pcb` is the fabricated board, `production/` holds the JLC outputs |
| `firmware/`   | STM32CubeMX + CMake project, TinyUSB submodule in `firmware/lib/tinyusb` |
| `examples/`   | Host-side example scripts (`tempio_demo.py` runs every command) |

---

## Overview

This module is a small USB-connected development and test interface based on the STM32G0B1KBTx microcontroller.

It provides:

* USB serial SCPI interface
* Temperature measurement
* Humidity measurement
* 8 configurable GPIO pins
* I2C expansion connectors
* Breadboard-friendly operation
* SWD programming/debug interface

The module is intended for:

* breadboard prototyping
* embedded testing
* automation experiments
* SCPI/VISA software integration
* GPIO scripting from a PC

---

## Features

## Environmental sensing

Integrated sensor:

* SHT41 temperature + humidity sensor (I2C1, address 0x44)

Typical measurements:

| Parameter   | Typical Accuracy |
| ----------- | ---------------- |
| Temperature | ±0.2 °C          |
| Humidity    | ±1.8 %RH         |

---

## USB interface

The module connects to a host PC through USB-C and shows up as two interfaces at once. Both run the same SCPI commands and share one error queue:

| Interface          | Use it for                                                         |
| ------------------ | ------------------------------------------------------------------ |
| USBTMC (USB488)    | VISA / instrument software: `USB0::0x1209::0x0001::<serial>::INSTR` |
| CDC serial port    | Terminals, pyserial, anything without VISA; no driver needed       |

USB identification:

| Field        | Value                                   |
| ------------ | --------------------------------------- |
| VID:PID      | 1209:0001 (pid.codes test ID, dev only) |
| Manufacturer | Nacho.works                             |
| Product      | SCPI-TempIO v01                         |
| Serial       | 96-bit MCU unique ID, 24 hex characters |

The device appears as:

* COMx on Windows
* /dev/ttyACMx on Linux, also as `/dev/serial/by-id/usb-Nacho.works_SCPI-TempIO_v01_<serial>-if00`
* /dev/cu.usbmodem* on macOS

It is a native USB device, so the baud rate and other serial settings are ignored; any value works.

## VISA (USBTMC)

VISA resource string:

```text
USB0::0x1209::0x0001::<serial>::INSTR
```

The serial is the one returned by `*IDN?`. Some tools print the IDs in decimal: `USB0::4617::1::<serial>::0::INSTR` is the same instrument.

What USBTMC adds over the serial port:

* Found by VID/PID/serial, independent of COM port or ttyACM numbering
* Explicit message boundaries, no line-termination guessing
* Device clear (`viClear`): drops a half-sent command and any unread response
* Status byte (`viReadSTB`): bit 4 MAV = response waiting, bit 2 EAV = error queue not empty
* Indicator pulse ("identify" in NI MAX): the LED blinks fast for 2 s

Host setup:

* **Windows / macOS:** install NI-VISA or Keysight IO Libraries, they include the USBTMC driver.
* **Linux:** works with pyvisa + pyvisa-py (`pip install pyvisa pyvisa-py pyusb`). Install the udev rule once so a normal user can open the device and the kernel `usbtmc` driver does not claim it:

  ```sh
  sudo cp examples/99-scpi-tempio.rules /etc/udev/rules.d/
  sudo udevadm control --reload
  ```

  Then replug the unit. Without the rule, pyvisa lists the device but fails with `Resource busy`.

pyvisa-py (0.8) implements read/write/query on USB, but not `read_stb()` or `clear()`; those need NI-VISA or Keysight VISA. The firmware supports both.

The USBTMC interface is deliberately interface 0: pyvisa-py detaches the kernel driver from interface 0 when it opens a USB instrument, and would otherwise disconnect the serial port.

---

## GPIO Interface

The module exposes 8 configurable GPIO pins, DIO0–DIO7 (MCU pins PA0–PA7).

## GPIO capabilities

Each pin can be configured as:

* Digital input (default after power-up and `*RST`)
* Digital output (push-pull)
* Open-drain output

## GPIO voltage levels

IMPORTANT:

```text
GPIO pins are 3.3V logic only.
```

Do not apply voltages above 3.3V.

## GPIO protection

Each GPIO pin includes a series protection resistor.

This improves tolerance against:

* accidental shorts
* output contention
* breadboard wiring mistakes

---

## Connector Pinout

## GPIO Header (J2)

2×5 box header, 2.54 mm pitch. Top view, pin 1 top-left:

```text
          ┌──────────────┐
  3V3   ■ 1            2 ●  DIO0
  DIO1  ● 3            4 ●  DIO2
  DIO3  ● 5            6 ●  DIO4
  DIO5  ● 7            8 ●  DIO6
  DIO7  ● 9           10 ●  GND
          └──────────────┘
```

Pin 1 (3V3) has the square pad on the bottom side and the triangle mark on the header housing. The pins alternate between the two rows: odd pins in one row, even pins in the other. DIO0 sits right next to 3V3, and GND is in the opposite corner.

| Pin | Function |
| --- | -------- |
| 1   | 3V3      |
| 2   | DIO0     |
| 3   | DIO1     |
| 4   | DIO2     |
| 5   | DIO3     |
| 6   | DIO4     |
| 7   | DIO5     |
| 8   | DIO6     |
| 9   | DIO7     |
| 10  | GND      |

## I2C Expansion (J4, J5)

Shared with the on-board SHT41 bus (pull-ups on board).

| Pin | Function |
| --- | -------- |
| 1   | 3V3      |
| 2   | SCL      |
| 3   | SDA      |
| 4   | GND      |

---

## SCPI Command Interface

## General

Commands are ASCII text terminated by `\n` or `\r\n`. Responses end with `\n`.

Headers are case-insensitive and accept both the short and the long form, for example `MEAS:TEMP?` and `measure:temperature?` are the same query.

Several commands can be sent on one line, separated by `;`. Each command must be written out in full (`MEAS:TEMP?;MEAS:HUM?`); the responses come back on one line, also separated by `;`.

Examples:

```text
*IDN?
MEAS:TEMP?
DIG:PIN0 1
```

---

## Supported Commands

| Command                       | Description                                  |
| ----------------------------- | -------------------------------------------- |
| `*IDN?`                       | Identification                               |
| `*RST`                        | Reset: all GPIO pins back to input           |
| `*CLS`                        | Clear the error queue                        |
| `*OPC?`                       | Always returns `1`                           |
| `*TST?`                       | Self test: `0` = SHT41 found, `1` = not found |
| `SYSTem:ERRor[:NEXT]?`        | Read and remove the oldest error             |
| `SYSTem:VERSion?`             | SCPI version, `1999.0`                       |
| `MEASure:TEMPerature?`        | Temperature in °C                            |
| `MEASure:HUMidity?`           | Relative humidity in %RH                     |
| `MEASure:ALL?`                | `temperature,humidity`                       |
| `DIGital:PIN<n>:MODE IN\|OUT\|OD` | Set pin mode                             |
| `DIGital:PIN<n>:MODE?`        | Read pin mode                                |
| `DIGital:PIN<n> 0\|1\|OFF\|ON` | Set output level                            |
| `DIGital:PIN<n>?`             | Read pin level                               |
| `DIGital:PORT?`               | Read all 8 pins                              |

`<n>` is 0–7.

---

## Identification

### Query device identification

```text
*IDN?
```

Example response:

```text
Nacho.works,SCPI-TempIO v01,2038393741565017002D005E,0.2
```

Format:

```text
MANUFACTURER,MODEL,SERIAL,FIRMWARE_VERSION
```

---

# Environmental Measurements

Every query starts a new high-precision measurement (~10 ms).

If the sensor does not respond, the value is returned as `9.91E37` (SCPI "not a number") and error `-240` is queued.

## Temperature measurement

```text
MEAS:TEMP?
```

Example response:

```text
23.41
```

Units:

```text
°C
```

---

## Humidity measurement

```text
MEAS:HUM?
```

Example response:

```text
45.72
```

Units:

```text
%RH
```

---

## Combined measurement

```text
MEAS:ALL?
```

Example response:

```text
23.41,45.72
```

Format:

```text
TEMPERATURE,HUMIDITY
```

Both values come from the same measurement.

---

# GPIO Commands

## Configure GPIO mode

### Input mode

```text
DIG:PIN0:MODE IN
```

### Output mode

```text
DIG:PIN0:MODE OUT
```

### Open-drain mode

```text
DIG:PIN0:MODE OD
```

### Read mode

```text
DIG:PIN0:MODE?
```

Example response:

```text
OUT
```

---

## Set GPIO output state

### Set HIGH

```text
DIG:PIN0 1
```

### Set LOW

```text
DIG:PIN0 0
```

The level is remembered while a pin is an input, and is applied when the pin is switched to OUT or OD. This lets you set the level first and then enable the output without a glitch.

---

## Read GPIO state

```text
DIG:PIN0?
```

Example response:

```text
1
```

This reads the actual pin level, in every mode.

---

## Read all GPIO states

```text
DIG:PORT?
```

Example response:

```text
10100110
```

The first character is DIO7 and the last is DIO0. In the example above, DIO1, DIO2, DIO5 and DIO7 are high.

---

## Errors

Errors are queued (up to 8) and read one at a time with `SYST:ERR?`. `0,"No error"` means the queue is empty.

| Code | Meaning                                               |
| ---- | ----------------------------------------------------- |
| -108 | Parameter not allowed                                 |
| -109 | Missing parameter                                     |
| -113 | Undefined header (unknown command)                    |
| -114 | Header suffix out of range (pin number not 0–7)       |
| -223 | Too much data (response line too long)                |
| -224 | Illegal parameter value                               |
| -240 | Hardware error (SHT41 not responding)                 |
| -350 | Queue overflow                                        |
| -363 | Input buffer overrun (command line over 127 characters) |

---

## Status LED

| LED               | Meaning                                  |
| ----------------- | ---------------------------------------- |
| Blinking (2 Hz)   | Not enumerated by a USB host             |
| On                | Enumerated, ready                        |
| Short off-flick   | A command was received                   |
| Fast blink (2 s)  | Identify request (USBTMC indicator pulse) |

---

## Programming and Debugging

## SWD Interface (J1)

The module includes a 10-pin 1.27 mm Cortex debug connector.

Signals:

| Signal | Description    |
| ------ | -------------- |
| SWDIO  | Debug data     |
| SWCLK  | Debug clock    |
| NRST   | Reset          |
| VTref  | Target voltage |
| GND    | Ground         |

Compatible programmers:

* ST-Link V2
* ST-Link V3

Notes:

* The SWD cable does not power the board. Power it from USB.
* With a Treedix JTAG/SWD adapter board, install the VCC→VREF jumper. Otherwise the ST-Link reads about 1.7 V (floating VTref) and will not connect.
* On the first PCB batch, VTref (J1 pin 1) was routed to VBUS (5 V) instead of 3V3. This is fixed in the schematic. Check J1 pin 1 on older boards before connecting a programmer.

---

## Firmware Development

Toolchain:

* STM32CubeMX for pin and clock configuration (`firmware/tempio.ioc`, toolchain set to CMake)
* CMake + arm-none-eabi-gcc (the one bundled with STM32CubeIDE works)
* STM32 HAL
* TinyUSB 0.21.0 (git submodule in `firmware/lib/tinyusb`)
* STM32CubeProgrammer CLI or any SWD tool for flashing

Source layout:

| Path                             | Contents                                   |
| -------------------------------- | ------------------------------------------ |
| `firmware/App/`                  | Application code (SCPI, SHT41, DIO, USB CDC + USBTMC) |
| `firmware/App/Inc/version.h`     | Manufacturer, model and firmware version   |
| `firmware/Src`, `firmware/Inc`   | CubeMX generated code                      |

Application code lives in `App/` and is called from `main.c` inside the `USER CODE` sections, so regenerating from CubeMX keeps it.

USB is deliberately left disabled in CubeMX. TinyUSB drives the peripheral, and `App/Src/usb_hw.c` sets up the USB clock (HSI48 trimmed by the CRS, no crystal needed) and the interrupt handler.

## Build

```sh
git submodule update --init
cd firmware
cmake -S . -B build/Debug -G "Unix Makefiles" \
      -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Debug
```

`arm-none-eabi-gcc` must be on the `PATH`. With Ninja installed, `cmake --preset Debug` also works.

## Flash

```sh
STM32_Programmer_CLI -c port=SWD mode=UR -w build/Debug/tempio.elf -v -rst
```

---

## Electrical Specifications

| Parameter          | Value         |
| ------------------ | ------------- |
| USB input voltage  | 5V            |
| Logic voltage      | 3.3V          |
| GPIO voltage range | 0–3.3V        |
| GPIO direction     | Configurable  |
| MCU                | STM32G0B1KBTx |
| Sensor             | SHT41         |

---

## Safety Notes

## IMPORTANT

Do not:

* connect GPIO pins directly to mains voltage
* exceed 3.3V on GPIO pins
* drive motors directly from GPIO pins
* short outputs together intentionally

For inductive loads:

* use transistors or MOSFETs
* use flyback diodes

---

## Example Python Usage

`examples/tempio_demo.py` connects to the unit and runs every command once:

```sh
pip install pyserial
python3 examples/tempio_demo.py            # auto-detect on Linux/macOS
python3 examples/tempio_demo.py COM5       # or give the port
```

Minimal VISA version:

```python
import pyvisa

rm = pyvisa.ResourceManager()          # '@py' for pyvisa-py
print(rm.list_resources())

inst = rm.open_resource('USB0::0x1209::0x0001::2038393741565017002D005E::INSTR')
print(inst.query('*IDN?'))
print(inst.query('MEAS:ALL?'))
inst.write('DIG:PIN0:MODE OUT')
inst.write('DIG:PIN0 1')
```

Minimal serial version:

```python
import serial

s = serial.Serial('/dev/ttyACM0', timeout=1)

s.write(b'*IDN?\n')
print(s.readline().decode())

s.write(b'MEAS:TEMP?\n')
print(s.readline().decode())

s.write(b'DIG:PIN0:MODE OUT\n')
s.write(b'DIG:PIN0 1\n')
```

---

## Revision

| Revision | Description                                          |
| -------- | ---------------------------------------------------- |
| 1.0      | Initial draft                                        |
| 1.1      | Firmware 0.1: USB CDC SCPI, SHT41, GPIO; MCU corrected to STM32G0B1 |
| 1.2      | Firmware 0.2: added USBTMC/USB488 interface (VISA) next to CDC |
