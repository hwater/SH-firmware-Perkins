# Changelog — SH-firmware-Perkins

Engine‑monitor firmware for a Perkins marine engine on an **SH‑ESP32 Engine Hat**
(ESP32 + ADS1115 + NMEA 2000 + SSD1306 OLED), built on SensESP. Most recent first.

## I/O & sensors
- **Fuel flow on D1** (GPIO 15) — pulse counter → Hz → configurable curve → L/h,
  published as NMEA 2000 **PGN 127489** (fuel rate). (`e7ed24a`, `5c20a87`)
- **Engine RPM from the alternator W terminal on D2** (GPIO 13) — frequency → RPM,
  **PGN 127488**, with a configurable revolution multiplier. (`5c20a87`)
- **Fuel tank on analog C** (ADS1115 ch 2) — sender resistance → level via a
  configurable curve, **PGN 127505**. (`5c20a87`)
- **Low‑oil‑pressure alarm on D4** (GPIO 12); over‑temperature alarm on D3. (`5c20a87`)
- **Analog Voltage B** (ADS1115 ch 1) shown on `/dash` (Tank & Alarme card), the
  Status page, the OLED, and `/api/data`. (`fa06b6f`, `60bce53`)
- DS18B20 temperatures (coolant, exhaust, alternator) via SensESP `OneWireTemperature`.

## Stability
- **HTTP server no longer hangs after a few days uptime**: SensESP starts the
  ESP‑IDF `httpd` with `HTTPD_DEFAULT_CONFIG()` (`max_open_sockets = 7`,
  `lru_purge_enable = false`). Stale keep‑alive sockets from sleeping/departed
  browsers pile up until the listener can no longer accept connections and the
  web UI/API appears frozen. A pre‑build patch sets `lru_purge_enable = true` so
  the oldest session is purged to admit a new one — the server self‑heals.
  (`scripts/patch_sensesp_navbar.py`, Patch 4)

## Web interface
- **`/dash`** live dashboard (dark card grid): Drehzahl, Verbrauch/Durchfluss,
  Temperaturen, Tank & Alarme, NMEA 2000, System. (`3eae96b`, `1fb9854`)
- **`/api/data`** live JSON, hardened against invalid output (NaN/Inf sanitised). (`ed43989`)
- All sensor values added to the SensESP **Status** page (Sensoren group). (`1fb9854`)
- **Dash** entry injected into the SensESP web‑UI navbar via the pre‑build script
  `scripts/patch_sensesp_navbar.py`; the SensESP logo on `/dash` links to the config UI.
- Stale config labels fixed: fuel‑flow input D4→D1, tank A1→C, voltage A2→B,
  oil‑alarm tile → "Öldruck D4". (`fa06b6f`)
- **Removed** the TCP debug‑log server on **port 23** (serial/UART logging unchanged). (`fa06b6f`)
- Dashboard colours refined — green (`#00ff7f`) for sensor data, white for the
  NMEA 2000 / System cards. (`df78908`, `e449617`)
- **Free heap** on the System card. (`bf247e3`)
- **Signal K status** on the System card, replacing the old WLAN row: shows the
  live SK WebSocket client state (verbunden / verbindet… / autorisiert… /
  getrennt) via the new `sk` field in `/api/data`. Guarded by `ENABLE_SIGNALK`
  (falls back to the WLAN state in non‑SignalK builds).
- Restart **403 Forbidden fix** + AP SSID aligned with the hostname. (`1ea05d9`)

## Platform
- CAN migrated to **NMEA2000_esp32**; SH‑ESP32 Engine Hat pinout corrected; CAN
  status page added. (`8fddf58`, `7f10550`)
- CAN **RX counted at the driver level** instead of via the message handler. (`20bc074`)
- **`dev_board32`** build environment for separate bench/test hardware, with its
  own navbar‑patch step. (`d5fbd69`, `3d7f55e`)
- Lowercase hostname `perkins`. (`5d93dc6`) · SensESP 3.3.0. (`8fddf58`)

## Calibration (web UI → Configuration, persisted in flash)
- Fuel‑flow curve (Hz → L/h) and fuel‑tank level curve (Ω → level) are tunable.

## Operations
- **OTA:** the PlatformIO espota wrapper can stall at 0 % (it binds the host to
  `0.0.0.0`); flash with `espota.py` directly using an explicit host IP (`-I`).
  See the README.
