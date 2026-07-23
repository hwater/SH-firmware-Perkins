# Fuel flow sensor — flowTrecs FS-40-10-AL, Version S

The fuel-flow input (**D1**, GPIO 15) is driven by a **flowTrecs FS-40-10-AL**
turbine flow sensor, **Version S (20–60 PS)**.

## Sensor specifications (manufacturer)

| Property | Value |
|---|---|
| Type | Optical turbine flow sensor |
| Version | **S** — for 20–60 HP engines |
| Measuring range (Version S) | **0.5 – 20 L/h** |
| Nozzle (Version S) | 1.2 mm |
| Accuracy | approx. 5 % |
| Supply | 10–16 VDC, approx. 50 mA |
| Output | Pulse signal (counted on D1, rising edge) |
| Housing | Aluminum (saltwater resistant) |
| Rotor / axle / bearings | PA / stainless steel / sapphire |
| Fuel connection | 9.5 mm (3/8″) fuel line |
| Fuel types | Diesel and gasoline |

Other sizes of the same sensor family (M: 1–40 L/h, L: 1.5–80 L/h, …) exist;
this installation uses **Version S**, matching the Perkins 4.108's consumption
range.

> **Manufacturer note on diesel return lines:** for diesel engines with a
> return line the manufacturer recommends differential measurement with two
> sensors. This installation measures with a single sensor; the configurable
> flow curve (below) absorbs the installation-specific calibration.

## Firmware integration

- **Input:** D1 (GPIO 15), rising-edge pulse counter, 500 ms window → frequency (Hz).
- **Conversion:** `CurveInterpolator` frequency → L/h, defaults from the
  manufacturer's Version-S data points:
  `0 Hz = 0 · 21 Hz = 4 · 66 Hz = 10 · 200 Hz = 20 L/h`
  (editable in the web UI: *Kraftstoff-Durchfluss Kurve*).
- **Plausibility filter:** readings above a configurable limit
  (*Kraftstoff-Plausibilitaet*, default 35 L/h) are discarded as electrical
  spikes — the turbine sensor emits isolated spurious pulses.
- **Outputs:** NMEA 2000 **PGN 127489** fuel rate → Signal K
  `propulsion.main.fuel.rate`; the fuel rate also drives the **engine hour
  meter** (engine counts as running above the configurable *Laufschwelle*).

## Sources

- [flowTrecs FS-40 sensor page](https://flowtrecs.pl/strona/?page_id=340&lang=en)
- [flowTrecs shop — FS-40-10-AL](https://www.shop.flowtrecs.com/en/home/4-sensor-przeplywu-fs-20.html)
