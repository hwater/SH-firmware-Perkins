# Perkins Engine Monitor (SH-ESP32 / SensESP)

SensESP firmware that monitors a **Perkins 4.236** marine engine
([engine reference](docs/perkins-4236.md)) on an **SH-ESP32 Engine
Hat** (ESP32 + ADS1115 analog frontend + NMEA 2000 transceiver + SSD1306 OLED +
1-Wire). It publishes engine data over **NMEA 2000** and **Signal K** and serves
a live web dashboard. Derived from the
[HALMET example firmware](https://github.com/hatlabs/halmet-example).

## I/O configuration

### Digital inputs (Engine Hat terminals D1–D4)
| Terminal | GPIO | Function | NMEA 2000 |
|----------|------|----------|-----------|
| D1 | 15 | **Fuel-flow** sensor ([flowTrecs FS-40-10-AL, Version S](docs/fuel-sensor-fs-40-10-al.md)) — pulses → L/h via a configurable curve; also drives the engine hour meter | 127489 (fuel rate, engine hours) |
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
- **Motorstunden** — "Zaehlerstand (h)" sets the hour meter to exactly the value
  entered, so read the analogue gauge and type it in; the runtime accumulated
  since the last set is reset. "Laufschwelle (L/h)" is the fuel rate above which
  the engine counts as running (default 0.1).
- **Kraftstoff-Plausibilitaet** — "Max. plausibler Verbrauch (L/h)" (default 35):
  readings above this are treated as interference on the D1 pulse input and
  dropped, so they reach neither the hour meter nor NMEA 2000 / Signal K.
  **Keep it above the flow curve's highest output** (currently 30 L/h at 200 Hz)
  or genuine full-load readings get rejected. `0` disables the check. Dropped
  spikes are counted as `fuel_spikes` on `/dash`, the Status page and
  `/api/data` — a rising count means the D1 wiring needs attention.

## Engine hour meter
The engine counts as running while the fuel rate exceeds the configured
threshold. The reading is the configured base ("Zaehlerstand") plus the runtime
accumulated since, published as **PGN 127489** *Engine Total Hours of Operation*
(seconds) and Signal K `propulsion.main.runTime` (seconds), and exposed on
`/dash`, the Status page and `/api/data` (`engine_h` in hours, `engine_run`).

The accumulator is written to flash when the engine stops and every 5 minutes of
runtime — frequent enough that a power cut while running loses at most 0.08 h
(below the gauge's 0.1 h step), rare enough not to wear the flash out.

> ⚠️ This board owns **engine instance 0** (Signal K `propulsion.port`) and is
> the sole authority for engine hours. The **AchternSensorik** board sends the
> same engine PGNs and used to share instance 0, which made the two overwrite
> each other; it now reports NA for engine hours and sits on engine instance 1
> (`propulsion.starboard`). Keep it there — see `SH-firmware-Achtern`.

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
