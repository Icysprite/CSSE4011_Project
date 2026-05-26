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
