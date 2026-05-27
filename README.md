# CSSE4011_Project

# Mobile Node — Installation and Usage

## Hardware Requirements

- Seeed XIAO nRF52840 (Sense)
- USB-C cable

## Prerequisites

Ensure the following are installed and configured:

- Zephyr RTOS and west build tools

Refer to the [Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) for environment setup.

## Repository Structure

```
mobile_node/
├── src/
│   ├── main.c
│   ├── nus_peripheral.c
│   └── ble.c
├── include/
│   ├── nus_peripheral.h
│   ├── ble.h
│   └── env_packet.h
├── boards/
│   └── xiao_ble_nrf52840_sense.overlay
├── CMakeLists.txt
└── prj.conf
```

## Compilation

Navigate to the project root directory and run:

```bash
west build -p -b xiao_ble/nrf52840/sense -d mobile_node/build mobile_node/
```

A successful build produces:
```
mobile_node/build/zephyr/zephyr.bin
mobile_node/build/zephyr/zephyr.hex
```

## Flashing

Connect the XIAO nRF52840 via USB-C, put it into bootloader mode and flash:

```bash
west flash -d mobile_node/build
```

If `west flash` fails, the XIAO can be flashed manually using UF2:

1. Double-press the reset button on the XIAO — the board enters UF2 bootloader mode and appears as a USB drive
2. Copy `mobile_node/build/zephyr/zephyr.uf2` to the USB drive
3. The board reboots automatically after flashing

## Verifying Operation

Open a serial terminal at 115200 baud:

```bash
screen /dev/ttyACM0 115200
```

On successful boot you should see:

```
*** Booting Zephyr OS ***
Bluetooth initialised
Advertising as mobile_node...
Sniffer started, listening for sensor nodes...
```

Once the base node connects:

```
Connected to base_node (XX:XX:XX:XX:XX:XX)
NUS notify enabled
```

When sensor node data is received and forwarded:

```
Sent record for node_1
EOW sent
```

# Base Node — Installation and Usage

## Hardware Requirements

Seeed XIAO nRF52840 (Sense)
USB-C cable connected to PC
PAM8302 mono amplifier with speaker signal pin connected to pin P0.03 (A1)

## Prerequisites
Ensure the following are installed and configured:

Zephyr RTOS and west build tools
nRF Connect SDK

## Repository Structure
```
base_node/
├── src/
│   ├── main.c
│   ├── nus_central.c
│   ├── json_serial.c
│   ├── kalman.c
│   ├── air_quality.c
│   ├── buzzer.c
│   ├── watchdog.c
│   └── node_buffer.c
├── include/
│   ├── nus_central.h
│   ├── json_serial.h
│   ├── kalman.h
│   ├── air_quality.h
│   ├── buzzer.h
│   ├── watchdog.h
│   ├── node_buffer.h
│   └── env_packet.h
├── boards/
│   └── xiao_ble_nrf52840_sense.overlay
├── CMakeLists.txt
└── prj.conf
```

## Compilation

Navigate to the project root directory and run:

```bash
west build -p -b xiao_ble/nrf52840/sense -d base_node/build base_node/
```

- `-p` — pristine build, cleans previous build artifacts
- `-b xiao_ble/nrf52840/sense` — target board
- `-d base_node/build` — build output directory
- `base_node/` — source directory

A successful build produces:

```
base_node/build/zephyr/zephyr.bin
base_node/build/zephyr/zephyr.hex
```

## Flashing

Connect the XIAO nRF52840 via USB-C and flash:

```bash
west flash -d base_node/build
```

If `west flash` fails, use UF2:

1. Double-press the reset button — board enters UF2 bootloader mode
2. Copy `base_node/build/zephyr/zephyr.uf2` to the USB drive
3. Board reboots automatically after flashing

## Verifying Operation

Open a serial terminal at 115200 baud:

```bash
screen /dev/ttyACM0 115200
```

On successful boot you should see:

```
*** Booting Zephyr OS ***
Bluetooth initialised
Scanning for mobile_node...
```

Once the mobile node is found and connected:

```
Connected to mobile_node (XX:XX:XX:XX:XX:XX)
```

Once sensor data arrives and the Kalman filter updates, JSON output appears
over serial:

```json
{"type":"node","node_id":"node_1","timestamp_ms":117152,"temperature":2787,"humidity":6100,"eco2":572,"tvoc":26}
{"type":"node","node_id":"node_2","timestamp_ms":119478,"temperature":2825,"humidity":5800,"eco2":614,"tvoc":32}
{"type":"kalman","timestamp_ms":158437,"eco2_estimate":602,"tvoc_estimate":30,"eco2_class":"good","tvoc_class":"good"}
```

## Shell Commands

The base node exposes two shell commands over USB serial:

| Command | Description |
|---|---|
| `wake` | Sends `CMD_MEASURE` to the sensor mesh — nodes begin sampling |
| `sleep` | Sends `CMD_SLEEP` to the sensor mesh — nodes stop sampling |

Example usage:

```
uart:~$ wake
CMD_MEASURE sent to mobile_node

uart:~$ sleep
CMD_SLEEP sent to mobile_node
```

Classification values: `"good"`, `"warning"`, `"poor"`.

## Buzzer Behaviour

| Air Quality State | Buzzer Behaviour |
|---|---|
| Good | Silent |
| Warning | Short chirp (3 × 200ms at 1000 Hz) |
| Poor | Continuous alarm (2000 Hz) until air quality improves |

The alarm stops automatically when air quality returns to Warning or Good.
Sending `CMD_SLEEP` also stops the alarm and resets the classification state.
