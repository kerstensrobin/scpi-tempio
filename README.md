# STM32G070 SCPI GPIO + Environmental Sensor Module

> USB-connected environmental sensor and GPIO control module based on the STM32G070KBTx.

---

## Repository Contents

* Firmware
* Hardware design files
* SCPI command reference
* Example host-side scripts
* Hardware documentation

---

## Overview

This module is a small USB-connected development and test interface based on the STM32G070KBTx microcontroller.

It provides:

* USB serial SCPI interface
* Temperature measurement
* Humidity measurement
* 8 configurable GPIO pins
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

* SHT41 temperature + humidity sensor

Typical measurements:

| Parameter   | Typical Accuracy |
| ----------- | ---------------- |
| Temperature | ±0.2 °C          |
| Humidity    | ±1.8 %RH         |

---

## USB interface

The module connects to a host PC through USB.

Interface type:

```text
USB CDC Serial Device
```

The device appears as:

* COMx on Windows
* /dev/ttyACMx on Linux
* /dev/cu.* on macOS

Default serial settings:

| Setting   | Value  |
| --------- | ------ |
| Baud rate | 115200 |
| Data bits | 8      |
| Parity    | None   |
| Stop bits | 1      |

---

## GPIO Interface

The module exposes 8 configurable GPIO pins.

## GPIO capabilities

Each pin can be configured as:

* Digital input
* Digital output
* Open-drain output
* PWM output (optional firmware support)

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

## GPIO Header

| Pin | Function |
| --- | -------- |
| 1   | 3V3      |
| 2   | GND      |
| 3   | DIO0     |
| 4   | DIO1     |
| 5   | DIO2     |
| 6   | DIO3     |
| 7   | DIO4     |
| 8   | DIO5     |
| 9   | DIO6     |
| 10  | DIO7     |

Optional:

| Pin | Function    |
| --- | ----------- |
| 11  | USB VBUS 5V |

---

## SCPI Command Interface

## General

Commands are ASCII text terminated by:\n or \r\n.

Examples:

```text
*IDN?
MEAS:TEMP?
DIG:PIN0 1
```

---

## Supported Commands

## Identification

### Query device identification

```text
*IDN?
```

Example response:

```text
Robin,STM32G0-SCPI-IO,0001,1.0
```

---

# Environmental Measurements

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

---

## Read GPIO state

```text
DIG:PIN0?
```

Example response:

```text
1
```

---

## Read all GPIO states

```text
DIG:PORT?
```

Example response:

```text
10100110
```

---

## Programming and Debugging

## SWD Interface

The module includes an SWD programming/debug interface.

Signals:

| Signal | Description    |
| ------ | -------------- |
| SWDIO  | Debug data     |
| SWCLK  | Debug clock    |
| NRST   | Reset          |
| 3V3    | Target voltage |
| GND    | Ground         |

Compatible programmers:

* ST-Link V2
* ST-Link V3

---

## Firmware Development

Recommended development environments:

* STM32CubeIDE
* STM32CubeProgrammer

Recommended libraries:

* STM32 HAL
* TinyUSB

---

## Electrical Specifications

| Parameter          | Value         |
| ------------------ | ------------- |
| USB input voltage  | 5V            |
| Logic voltage      | 3.3V          |
| GPIO voltage range | 0–3.3V        |
| GPIO direction     | Configurable  |
| MCU                | STM32G070KBTx |
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

```python
import serial

s = serial.Serial('/dev/ttyACM0', 115200, timeout=1)

s.write(b'*IDN?\n')
print(s.readline().decode())

s.write(b'MEAS:TEMP?\n')
print(s.readline().decode())

s.write(b'DIG:PIN0:MODE OUT\n')
s.write(b'DIG:PIN0 1\n')
```

---

## Revision

| Revision | Description   |
| -------- | ------------- |
| 1.0      | Initial draft |

