# Meshtastic XIAO nRF52840 Sense + Wio-SX1262 + ATGM336H

This repository is a hardware-specific Meshtastic firmware fork for the following build:

- Seeed Studio XIAO nRF52840 Sense
- Seeed Wio-SX1262 radio
- ATGM336H GPS
- HLK-LD2410C presence radar

It is based on the upstream Meshtastic firmware repository:

- https://github.com/meshtastic/firmware

This branch keeps the Meshtastic node support focused on this hardware. Experimental gyro mouse and microphone work is not included in this baseline.

## What This Fork Changes

- Adds a dedicated PlatformIO target for this hardware: `seeed_xiao_nrf52840_sense_atgm336h`
- Uses the real direct-wired ATGM336H UART pinout
- Skips the broad startup I2C scan that can hang this hardware combination
- Leaves startup I2C probing disabled in the production radar target because the
  onboard IMU bus and the LD2410C UART compete for nRF52840 peripheral instance 1
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

The current combined radar build deliberately does not start this bus. Live
hardware testing showed that enabling the previous targeted `Wire1` probe stops
the Meshtastic USB API from becoming ready when the LD2410C uses UARTE1. A
nonconflicting IMU bridge is still required before the IMU can be enabled in
this combined image. The separately archived `xiao-sense-meshtastic.uf2` image
remains the proven IMU/microphone bridge image, but it does not include radar.

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
onboard IMU is electrically on a separate internal bus but cannot currently run
at the same time because both drivers require peripheral instance 1.

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
- Internal pins `17` and `16` are reserved for the onboard Sense IMU bus; that
  bus is disabled in the current combined radar image pending a nonconflicting
  driver.

## Build

Use PlatformIO from the repository root:

```powershell
pio run -e seeed_xiao_nrf52840_sense_atgm336h
```

The root `platformio.ini` already defaults to this environment.

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
- The onboard IMU is not enabled in the current combined radar image. Do not add
  `XIAO_SENSE_IMU_TARGETED_SCAN` back to this target: it was isolated as the
  cause of a live USB/API boot regression with radar enabled.
- The GPS is supported over UART only; no dedicated GPS power control pin is configured.

## Tracking Upstream

A clean way to keep pulling Meshtastic updates is to keep the upstream remote:

```powershell
git remote add upstream https://github.com/meshtastic/firmware
git fetch upstream
```

## License

This repository remains under the same license as the upstream Meshtastic firmware project. See [LICENSE](LICENSE).
