# Meshtastic XIAO nRF52840 Sense + Wio-SX1262 + ATGM336H

This repository is a hardware-specific Meshtastic firmware fork for the following build:

- Seeed Studio XIAO nRF52840 Sense
- Seeed Wio-SX1262 radio
- ATGM336H GPS
- HLK-LD2410C presence radar

It is based on the upstream Meshtastic firmware repository:

- https://github.com/meshtastic/firmware

This branch keeps the Meshtastic node support focused on this hardware. The
onboard IMU and microphone provide local telemetry to TalleySoft Vision; it
does not implement a gyro mouse or transmit raw audio.

## What This Fork Changes

- Adds a dedicated PlatformIO target for this hardware: `seeed_xiao_nrf52840_sense_atgm336h`
- Uses the real direct-wired ATGM336H UART pinout
- Skips the broad startup I2C scan that can hang this hardware combination
- Runs the onboard IMU through a small software-I2C driver so the LD2410C can
  retain the nRF52840 UARTE1 peripheral
- Samples the onboard microphone at 16 kHz but exports only RMS, peak, and
  activity metadata
- Delivers IMU, microphone, GPS, and presence reports only to the connected
  USB/BLE PhoneAPI client; these reports are never handed to the LoRa router
- Powers the IMU and microphone down when no PhoneAPI client is connected
- Reads the LD2410C hardware-presence output on `D0`
- Decodes LD2410C range/energy reports over the NFC pads at 256000 baud
- Delivers presence reports only to the connected USB/BLE client; this module
  never broadcasts presence over LoRa
- Broadcasts a custom firmware identity:
  - `firmwareEdition = DIY_EDITION`
  - `firmwareVersion = 2.7.20-sriracha`
  - `hwModel = PRIVATE_HW`

## Wiring

### Wio-SX1262

This fork assumes the Seeed Wio-SX1262 is connected directly using the XIAO-compatible wiring used by the `seeed_xiao_nrf52840_kit` variant.

### ATGM336H

Wire the GPS module like this:

- `3V3` -> `3V3`
- `GND` -> `GND`
- `ATGM336H TX` -> `D6` (MCU RX)
- `ATGM336H RX` -> `D7` (MCU TX)

Do not use `D8`, `D9`, or `D10` for the GPS. Those pins are used by the SX1262 SPI bus.

### XIAO Sense IMU

The onboard LSM6DS3TR-C is wired to the internal XIAO Sense I2C bus:

- `SDA1` -> internal pin `17`
- `SCL1` -> internal pin `16`
- IMU address -> `0x6A`

The combined image uses software I2C on these pins instead of `Wire1` because
the nRF52840 hardware instance used by `Wire1` conflicts with the LD2410C
UARTE1. The firmware drives the IMU power switch on `P1.08` in high-drive mode,
then samples the accelerometer and gyroscope at 52 Hz and publishes the newest
complete sample to the local PhoneAPI client at a 25 Hz target rate. This path
has been verified simultaneously with the radar and microphone.

### HLK-LD2410C presence radar

The presence sensor uses both its immediate hardware output and its UART data.
The hardware output keeps basic presence functional if UART data is temporarily
unavailable; UART adds moving/static classification, distance, and energy.

- `LD2410C OUT` -> `D0`
- `LD2410C TX` -> `NFC1` / internal pin `30` (XIAO RX)
- `LD2410C RX` -> `NFC2` / internal pin `31` (XIAO TX)
- `LD2410C GND` -> `GND`
- Power the LD2410C from the supply used by the assembled sensor board; do not
  feed a 5 V signal into XIAO GPIO

The UART runs at the LD2410C default `256000 8N1`. The target releases the NFC
pads as GPIO with `CONFIG_NFCT_PINS_AS_GPIOS`. The Adafruit nRF52 core does not
natively select 256000 baud, so the module applies the nRF52840 custom UARTE
baud register after initializing UARTE1. NFC functionality and the
external NFC-pad I2C bus are unavailable while this sensor is installed. The
onboard IMU is electrically on a separate internal bus and runs through
software I2C, so the UART and IMU remain independent.

The firmware emits a compact `TSV_RADAR_V1` report on Meshtastic port 10 to the
currently connected PhoneAPI client only. Reports are sent on state changes,
meaningful distance changes, and a five-second keepalive. They are never sent
to the mesh by this module. The existing generic Meshtastic Detection Sensor
module remains disabled unless explicitly configured.

## Pin Availability

Current external XIAO pin usage in this hardware stack:

- `D1`, `D2`, `D3`, `D4`, `D5`, `D8`, `D9`, `D10` are used by the Wio-SX1262 radio.
- `D6` and `D7` are used by the ATGM336H GPS UART.
- `D0` is used by the LD2410C hardware presence output.
- NFC pads `30` and `31` are used by the LD2410C UART and are not available as
  the external I2C bus in this target.
- Internal pins `17` and `16` are reserved for the active onboard Sense IMU
  software-I2C bus.
- Internal microphone pins `19`, `20`, and `21` are reserved for PDM power,
  clock, and data.

## Build

Use PlatformIO from the repository root:

```powershell
pio run -e seeed_xiao_nrf52840_sense_atgm336h
```

The root `platformio.ini` already defaults to this environment.

## Test

The LD2410C streaming parser has a minimal native test environment that avoids
the Linux-only Portduino and graphics dependencies:

```powershell
pio test -e native-xiao-parser -f test_xiao_ld2410_parser
```

On Windows, install a native C++ compiler before running it. The BlackBox test
host uses these Winget packages:

```powershell
winget install --id BrechtSanders.WinLibs.POSIX.UCRT --exact
winget install --id bloodrock.pkg-config-lite --exact
```

The optional pkg-config calls used by the broader native environments are
routed through `bin/optional-pkg-config.py`, so absent optional libraries no
longer make configuration fail under Windows `cmd.exe`.

The connected-board acceptance helper validates the local payload families and
reports observed end-to-end rates:

```powershell
python tools/verify_tsv_local_sensors.py --port COM5 --seconds 12
```

## Live hardware verification

Verified August 14, 2026 on the assembled board, not only with parser fixtures:

- XIAO runtime USB identity `239A:810B`, serial `3CDB69C117E1D98C`
- Meshtastic PhoneAPI connected to local node `!a23f8829`
- LD2410C reports received with `uart=1`, range, energy, and hardware-presence
  fallback state
- IMU reports observed with changing acceleration/gyro values and a 25 Hz
  payload target
- Microphone RMS/peak reports observed with a 10 Hz payload target; no raw
  audio leaves the board
- D0 remained available as the presence fallback
- Presence, IMU, microphone, and exact GPS packets stayed local to the
  connected PhoneAPI client
- The generic Meshtastic position sender is disabled in this target so exact
  GPS cannot be broadcast through normal position packets
- Firmware build passed at 41.3% RAM and 87.5% flash utilization
- Native parser suite passed both normal-frame decoding and bad-tail
  resynchronization cases
- A 13.26-second PhoneAPI acceptance run received 234 IMU, 95 microphone, and
  two radar reports. GPS did not have an indoor fix during that run.

When a PhoneAPI client disconnects, the IMU and microphone are stopped and the
poll interval backs off from 20 ms to 250 ms. While connected, the low-latency
policy is newest-sample delivery: 25 Hz for IMU, 10 Hz for microphone level,
one-second GPS updates/keepalives, and radar state-change/distance updates.

## Flash

Flash to the connected board:

```powershell
pio run -e seeed_xiao_nrf52840_sense_atgm336h -t upload --upload-port COM23
```

Replace `COM23` with the board's current serial port if Windows assigns a different one.

## First Boot

After flashing:

1. Open the Meshtastic app and connect over USB or Bluetooth.
2. Set the LoRa region before using the radio on air.
3. Confirm GPS fixes are being reported.

## Notes

- This fork is meant for a XIAO nRF52840 Sense used as a normal Meshtastic node.
- Do not add `XIAO_SENSE_IMU_TARGETED_SCAN` or `Wire1` back to this target. The
  software-I2C implementation is the nonconflicting production path.
- The GPS is supported over UART only; no dedicated GPS power control pin is configured.

## Tracking Upstream

A clean way to keep pulling Meshtastic updates is to keep the upstream remote:

```powershell
git remote add upstream https://github.com/meshtastic/firmware
git fetch upstream
```

## License

This repository remains under the same license as the upstream Meshtastic firmware project. See [LICENSE](LICENSE).
