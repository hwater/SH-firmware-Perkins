# Tank-Eichtabelle — Dieseltank Wilhelmina

Eichung des Dieseltanks: Peilung (cm) gegen analoge VDO-Tankanzeige, den
elektrischen Geber (Analog C → PGN 127505, `tanks.fuel.main.*`) und
Nachfüllmengen. Ziel: Kurve cm ↔ Liter ↔ Geberwiderstand, um Anzeige und
Signal-K-Füllstand zu kalibrieren.

Konfigurierte Kapazität (Firmware): 200 L (`tanks.fuel.0.capacity` = 0,2 m³) —
durch die Eichung zu bestätigen.

## Messungen

| Datum | Peilung (cm) | Anzeige analog | Geber (Ω) | Sensor-Level | Sensor-Volumen | Betriebsstd. analog | Bemerkung |
|---|---|---|---|---|---|---|---|
| 23.07.2026 | 10,0 | ≈ 1/10 | 191,2 | 26,6 % | ≈ 48 L | 1451,0 h | Erster Datenpunkt; Anzeige (1/10) pessimistischer als Geber (27 %) |

## Nachfüllungen

| Datum | Menge (L) | Peilung vorher (cm) | Peilung nachher (cm) | Anzeige nachher | Bemerkung |
|---|---|---|---|---|---|
| — | — | — | — | — | noch keine erfasst |

## Vorgehen

1. Bei jeder Gelegenheit (v. a. vor/nach dem Tanken): Peilstab-cm, analoge
   Anzeige und Betriebsstunden notieren — die Geberwerte (Ω, %, L) holt
   Claude aus InfluxDB (`tanks.fuel.main.senderResistance` etc.) zum
   Zeitpunkt der Messung.
2. Nachfüllungen mit Zapfsäulen-Litern sind die Eichanker: ΔLiter zwischen
   zwei Peilungen ergibt die cm→Liter-Steigung in diesem Bereich.
3. Bei genügend Punkten: Kurve `senderResistance → Liter` in der Firmware
   (Web-UI: Tank-Kurve, Analog C) und die Kapazität nachziehen.

## Hinweis

Eine frühere Version dieser Tabelle wurde auf dem MacBook Pro M3 erstellt und
liegt dort (Claude-Verlauf/Datei) — bei Gelegenheit die historischen Werte
hier nachtragen.
