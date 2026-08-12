# Meshtastic XIAO nRF52840 Sense + Wio-SX1262 + ATGM336H

This repository is a hardware-specific Meshtastic firmware fork for the following build:

- Seeed Studio XIAO nRF52840 Sense
- Seeed Wio-SX1262 radio
- ATGM336H GPS

It is based on the upstream Meshtastic firmware repository:

- https://github.com/meshtastic/firmware

This branch keeps the Meshtastic node support focused on this hardware. Experimental gyro mouse and microphone work is not included in this baseline.

## What This Fork Changes

- Adds a dedicated PlatformIO target for this hardware: `seeed_xiao_nrf52840_sense_atgm336h`
- Uses the real direct-wired ATGM336H UART pinout
- Skips the broad startup I2C scan that can hang this hardware combination
- Probes only the XIAO Sense onboard LSM6DS3TR-C IMU address on the internal I2C bus
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

The onboard LSM6DS3TR-C IMU is enabled with a targeted probe on the internal XIAO Sense I2C bus:

- `SDA1` -> internal pin `17`
- `SCL1` -> internal pin `16`
- IMU address -> `0x6A`

The firmware still skips the broad startup I2C scan. This keeps the previous hang avoidance while allowing Meshtastic to discover the onboard IMU.

## Pin Availability

Current external XIAO pin usage in this hardware stack:

- `D1`, `D2`, `D3`, `D4`, `D5`, `D8`, `D9`, `D10` are used by the Wio-SX1262 radio.
- `D6` and `D7` are used by the ATGM336H GPS UART.
- `D0` is the only unassigned normal external XIAO `D` pin in this firmware target.
- NFC pads `30` and `31` remain configured as the external I2C bus when physically accessible.
- Internal pins `17` and `16` are used for the onboard Sense IMU bus.

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
- The onboard IMU is enabled by a targeted internal-bus probe; the full startup I2C scan remains disabled.
- The GPS is supported over UART only; no dedicated GPS power control pin is configured.

## Tracking Upstream

A clean way to keep pulling Meshtastic updates is to keep the upstream remote:

```powershell
git remote add upstream https://github.com/meshtastic/firmware
git fetch upstream
```

## License

This repository remains under the same license as the upstream Meshtastic firmware project. See [LICENSE](LICENSE).
