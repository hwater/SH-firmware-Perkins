# Perkins Engine Monitor (SH-ESP32 / SensESP)

SensESP firmware that monitors a Perkins marine engine on an **SH-ESP32 Engine
Hat** (ESP32 + ADS1115 analog frontend + NMEA 2000 transceiver + SSD1306 OLED +
1-Wire). It publishes engine data over **NMEA 2000** and **Signal K** and serves
a live web dashboard. Derived from the
[HALMET example firmware](https://github.com/hatlabs/halmet-example).

## I/O configuration

### Digital inputs (Engine Hat terminals D1–D4)
| Terminal | GPIO | Function | NMEA 2000 |
|----------|------|----------|-----------|
| D1 | 15 | **Fuel-flow** sensor — pulses → L/h via a configurable curve | 127489 (fuel rate) |
| D2 | 13 | **Engine RPM** from the alternator **W** terminal — frequency → RPM | 127488 (engine rapid) |
| D3 | 14 | **Over-temperature** alarm | 127489 |
| D4 | 12 | **Low-oil-pressure** alarm | 127489 |

> ⚠️ **D4 = GPIO 12 is a strapping pin.** If it is HIGH at boot the ESP32 can
> fail to start. A low-oil switch that closes to GND when pressure is low keeps
> D4 LOW at boot (engine off), which is safe.

### Analog inputs (ADS1115 — terminals A–D = channels 0–3)
| Terminal | Channel | Function | NMEA 2000 |
|----------|---------|----------|-----------|
| A | 0 | free | — |
| B | 1 | "Analog Voltage B" (generic voltage, Signal K only) | — |
| C | 2 | **Fuel tank** level sender — resistance → level via a configurable curve | 127505 (fluid level) |
| D | 3 | pressure sender | — |

### Other peripherals
- **1-Wire (GPIO 4):** DS18B20 temperatures — coolant, exhaust, alternator.
- **OLED (I²C SDA 16 / SCL 17):** two auto-cycling pages.
- **NMEA 2000 CAN:** RX = GPIO 34, TX = GPIO 32.

## Web interface (port 80, `http://perkins.local/`)
- `/` — SensESP configuration UI (with a **Dash** item added to the navbar).
- `/dash` — live dashboard: Drehzahl, Verbrauch / Durchfluss, Temperaturen,
  Tank & Alarme (incl. Öldruck D4), NMEA 2000, System (incl. Analog B).
- `/status` — SensESP device status page (Sensoren + CAN-Bus groups).
- `/api/data` — live JSON.

## Calibration (web UI → Configuration; persisted in the device's flash)
- **Fuel flow** — "Kraftstoff-Durchfluss Kurve": pulse frequency (Hz) → L/h.
- **Tank level** — "Fuel Tank Level Curve": sender resistance (Ω) → level (0–1).
- **RPM** — "Tacho main Multiplier" = 1 / (W-pulses per engine revolution)
  = 1 / (alternator pole-pairs × pulley ratio).

## Build & upload (PlatformIO, env `esp32dev`)
```
pio run -e esp32dev
```
**OTA** — the PlatformIO espota wrapper can stall at 0% (it binds the host to
`0.0.0.0`); call `espota.py` directly with an explicit host IP (`-I`):
```
python3 ~/.platformio/packages/framework-arduinoespressif32/tools/espota.py \
  -i perkins.local -I <your-host-ip> -p 3232 \
  -a <ota_password> -f .pio/build/esp32dev/firmware.bin
```
The OTA password and Wi-Fi/Signal K credentials come from the gitignored
`secrets.ini` (copy `secrets.example.ini`).

A pre-build script (`scripts/patch_sensesp_navbar.py`) injects the **Dash** entry
into the SensESP web-UI navbar (the library route list is hardcoded and can't be
extended from the sketch).

To customize, edit `src/main.cpp`; customizable parts are marked `EDIT:`.
