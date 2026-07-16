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
- **Engine hour meter** — the engine counts as running while the fuel rate
  exceeds a configurable threshold (default 0.1 L/h). The reading is a base
  offset (the analogue gauge reading) plus the runtime accumulated since, and
  goes out as **PGN 127489** *Engine Total Hours of Operation* and Signal K
  `propulsion.main.runTime`. The sender already had a `total_engine_hours_`
  input but nothing fed it, so the field used to go out as N/A. Shown on
  `/dash` (Verbrauch card, with a läuft/aus pill), the Status page and
  `/api/data` (`engine_h`, `engine_run`). (`b02b051`)

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
- Lowercase hostname `perkins`. (`5d93dc6`) · SensESP **3.4.0**. (`8fddf58`, `a359cde`)
- Runtime log level **`ESP_LOG_INFO`** instead of `ESP_LOG_DEBUG`: suppresses the
  per‑reading `debugD` output and the SensESP debug chatter, keeps INFO/WARN/
  ERROR. Raise it back to `ESP_LOG_DEBUG` in `setup()` when debugging. Note the
  port‑23 log server is gone, so the serial log is USB‑only. (`a359cde`)
- **Engine instance 0 is now this board's alone** → Signal K `propulsion.port`.
  The AchternSensorik board used to share it and sent the same engine PGNs, so
  the two overwrote each other: its own uptime landed in the engine‑hours field
  of PGN 127489 and won, making `propulsion.port.runTime` read 96.2 h (that
  board's uptime) instead of the engine's 1445.7 h, and its **shaft** RPM
  competed with this board's **engine** RPM on PGN 127488 — different numbers,
  separated by the gearbox ratio. That board now reports NA for engine hours
  (`SH-firmware-Achtern` `dd8a3d3`) and has moved to engine instance 1 /
  `propulsion.starboard`, leaving the actual engine on `propulsion.port` where
  displays expect it.

## Calibration (web UI → Configuration, persisted in flash)
- Fuel‑flow curve (Hz → L/h) and fuel‑tank level curve (Ω → level) are tunable.
- **Motorstunden** (`/engine/hours`) — "Zaehlerstand (h)" sets the meter to
  exactly the value entered (read it off the analogue gauge; the runtime
  accumulated since the last set is reset), "Laufschwelle (L/h)" is the fuel
  rate above which the engine counts as running. Seeded to **1445.7 h**
  (gauge reading on 2026‑07‑16). The accumulator is written to flash when the
  engine stops and every 5 min of runtime, so a power cut while running loses
  at most 0.08 h — below the gauge's own 0.1 h step. (`b02b051`)

## Operations
- **OTA:** the PlatformIO espota wrapper can stall at 0 % (it binds the host to
  `0.0.0.0`); flash with `espota.py` directly using an explicit host IP (`-I`).
  See the README.
