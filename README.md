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

# PC Dashboard — Installation and Usage

## Requirements

- Python 3.8 or later
- Base node connected via USB serial
- Ganache running locally for blockchain features (optional)

## Dependencies

Install required Python packages:

```bash
pip install pyserial matplotlib web3
```

`tkinter` is included with Python on most systems. If missing on Linux:

```bash
sudo apt install python3-tk
```

## Files

```
dashboard/
├── gui.py              ← main dashboard application
├── air_quality_log.csv ← auto-generated data log
└── gui_config.json     ← auto-generated contract address config
```

## Running the Dashboard

Connect the base node via USB-C, then run:

```bash
python3 gui.py
```

## Connecting to the Base Node

1. In the dashboard, select the correct serial port from the dropdown (e.g. `/dev/ttyACM0` on Linux, `COM3` on Windows)
2. Click **Connect**
3. The dashboard will begin receiving JSON records from the base node

## Dashboard Features

**Live plots** — eCO2 and TVOC trends are plotted in real time for each node (`NODE_1` through `NODE_4`) and the Kalman filter estimate (`KALMAN`)

**Detail panel** — shows the latest readings per node including temperature, humidity, eCO2, TVOC, and air quality classification

**CSV logging** — all records are automatically logged to `air_quality_log.csv` with timestamps

**Blockchain integrity** — each record is hashed and stored on a local Ganache blockchain. On startup the dashboard verifies existing CSV records against blockchain hashes and flags any modified data as `DATA MODIFIED`

## Blockchain Setup

The dashboard uses a local Ethereum blockchain via Ganache for tamper-evident data logging.

**1. Install Ganache:**

Download from [https://trufflesuite.com/ganache](https://trufflesuite.com/ganache) or install the CLI:

```bash
npm install -g ganache
ganache
```

**2. Deploy the smart contract:**

The contract ABI is embedded in `gui.py`. Deploy using Remix IDE:

1. Open [https://remix.ethereum.org](https://remix.ethereum.org)
2. Create a new Solidity file with the following contract:

```solidity
// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

contract AirQualityLog {
    mapping(string => string) private hashes;

    function storeHash(string memory messageKey, string memory dataHash) public {
        hashes[messageKey] = dataHash;
    }

    function getHash(string memory messageKey) public view returns (string memory) {
        return hashes[messageKey];
    }

    function verifyHash(string memory messageKey, string memory dataHash) public view returns (bool) {
        return keccak256(bytes(hashes[messageKey])) == keccak256(bytes(dataHash));
    }
}
```

3. In Remix, set the environment to **Injected Web3** or **Web3 Provider** pointing to `http://127.0.0.1:7545`
4. Deploy the contract and copy the deployed contract address

**3. Set the contract address in the dashboard:**

Go to **Settings → Set Contract Address**, paste the deployed contract address and click **Save and Connect**. The address is saved to `gui_config.json` and persists across sessions.

## CSV Log Format

Records are logged to `air_quality_log.csv` with the following columns:

| Column | Description |
|---|---|
| `timestamp` | Wall clock time of record receipt (ISO format) |
| `type` | `node` or `kalman` |
| `record_id` | Unique record identifier |
| `node_id` | Node name e.g. `node_1` or `KALMAN` |
| `message_id` | Message sequence number |
| `timestamp_s` | Uptime timestamp from mobile node (seconds) |
| `temperature_c` | Temperature in centi-degrees C |
| `humidity_percent` | Humidity in centi-%RH |
| `eco2_ppm` | eCO2 in ppm |
| `tvoc_ppb` | TVOC in ppb |
| `eco2_class` | `good`, `warning`, or `poor` |
| `tvoc_class` | `good`, `warning`, or `poor` |
| `data_hash` | SHA256 hash of the record |
| `blockchain_status` | `VERIFIED`, `DATA MODIFIED`, `NO BLOCKCHAIN HASH`, or `BLOCKCHAIN ERROR` |
| `modification_detected` | `True` or `False` |

# Thingy:52 Sensor Node — Installation and Usage

## Hardware Requirements

- Nordic Thingy:52
- nRF52840-DK (required as external programmer/debugger)
- 10-pin SWD debug cable
- USB cable for nRF52840-DK
- PC running Linux with Zephyr development environment set up

## Why the nRF52840-DK is Required

The Thingy:52 does not have an onboard J-Link debug IC. An external programmer is required to flash firmware. The nRF52840-DK has a built-in J-Link that can be used as a debug-out programmer for external targets via its **Debug Out** connector.

## Hardware Setup

1. Connect the nRF52840-DK to the Thingy:52 using the 10-pin SWD cable:

| nRF52840-DK Debug Out | Thingy:52 P9 Programming Header |
|---|---|
| VTG | VDD |
| SWDIO | SWDIO |
| SWDCLK | SWDCLK |
| GND | GND |
| RESET | RESET |

2. On the nRF52840-DK set the **SW6** switch to **EXT** — this routes the J-Link to the external debug out connector rather than the onboard nRF52840
3. Connect the nRF52840-DK to the PC via USB
4. Power on the Thingy:52 using its slide switch

## Prerequisites

Ensure the following are installed and configured:

- Zephyr RTOS and west build tools
- nRF Connect SDK
- GNU Arm Embedded Toolchain
- J-Link software package from SEGGER

Verify J-Link is detected:
```bash
JLinkExe -device nRF52832_xxAA -if SWD -speed 4000 -autoconnect 1
```

Refer to the [Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) for environment setup.

## Repository Structure

```
thingy52_node/
├── build/
├── src/
│   ├── main.c
│   ├── node_helpers.c
│   └── node_helpers.h
├── CMakeLists.txt
├── prj.conf
└── README.md
```

## Configuration

All configurable parameters are in `src/node_helpers.h`.

**Node identity — must be unique per node:**
```c
#define NODE_ID       3          /* integer node ID — 1 through 5 */
#define DEVICE_NAME   "node_3"   /* must match NODE_ID */
```

Change both `NODE_ID` and `DEVICE_NAME` for each node. They must be consistent — `NODE_ID 1` must pair with `DEVICE_NAME "node_1"`.

## Compilation

Navigate to the project root directory and run:

```bash
west build -p -b thingy52/nrf52832 -d thingy52_node/build thingy52_node/
```

- `-p` — pristine build, cleans previous build artifacts
- `-b thingy52/nrf52832` — target board
- `-d thingy52_node/build` — build output directory
- `thingy52_node/` — source directory

A successful build produces:

```
thingy52_node/build/zephyr/zephyr.bin
thingy52_node/build/zephyr/zephyr.hex
```

## Flashing

With the nRF52840-DK connected and SW6 set to EXT:

```bash
west flash -d thingy52_node/build --runner jlink
```

If flashing fails, try specifying the device explicitly:

```bash
west flash -d thingy52_node/build --runner jlink \
    --device nRF52832_xxAA
```

## Verifying Operation

The Thingy:52 does not have a USB serial console. Verify operation using one of the following methods:

**Option 1 — nRF Connect for Desktop:**
1. Install nRF Connect for Desktop from [https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop)
2. Open the **Bluetooth Low Energy** app
3. Scan for `node_1` — the Thingy:52 should appear advertising with manufacturer data

**Option 2 — nRF Connect for Mobile:**
1. Install nRF Connect on iOS or Android
2. Scan for `node_1`
3. The advertisement should show manufacturer specific data with eCO2 and TVOC values

**Option 3 — Base node terminal:**
Once the full system is running, the base node will output JSON for each received record:

```json
{"type":"node","node_id":"node_1","timestamp_ms":117152,"temperature":2787,"humidity":6100,"eco2":572,"tvoc":26}
```

## Deploying Multiple Nodes

For each additional Thingy:52, update `src/node_helpers.h`:

| Node | `NODE_ID` | `DEVICE_NAME` |
|---|---|---|
| Node 1 | `1` | `"node_1"` |
| Node 2 | `2` | `"node_2"` |
| Node 3 | `3` | `"node_3"` |
| Node 4 | `4` | `"node_4"` |
| Node 5 | `5` | `"node_5"` |

Rebuild and flash each node individually using the nRF52840-DK.

## Operating Modes

The node supports two operating modes controlled by commands from the mobile node:

| Mode | Behaviour |
|---|---|
| Normal (`MODE_NORMAL`) | Continuously samples sensors and floods data into mesh |
| Low Power (`MODE_LOW_POWER`) | Sleeps for `LOW_POWER_SLEEP_MS`, wakes for `LOW_POWER_LISTEN_MS` to listen for commands |

Send `CMD_MEASURE` from the base node shell (`wake`) to enter Normal mode.
Send `CMD_SLEEP` from the base node shell (`sleep`) to enter Low Power mode.

