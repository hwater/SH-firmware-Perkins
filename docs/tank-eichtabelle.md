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
| 23.07.2026 ~13:55 | 10,0 | ≈ 1/10 | 191,2 | 26,6 % | ≈ 48 L | 1451,0 h | Erster Datenpunkt, vor Nachfüllung |
| 23.07.2026 ~14:12 | — | ≈ 1/2 (Zeiger waagrecht, genauer nicht ablesbar) | 257,1 | 36,4 % | ≈ 65,5 L | 1451,0 h | Nach Nachfüllung +20,0 L |

## Nachfüllungen

| Datum | Menge (L) | Peilung vorher (cm) | Peilung nachher (cm) | Anzeige nachher | Bemerkung |
|---|---|---|---|---|---|
| 23.07.2026 | 20,0 | 10,0 | — (nachtragen) | ≈ 1/2 | Geber: 191,2→257,1 Ω (+65,8 Ω), Level 26,6→36,4 % |

## Erste Erkenntnisse (23.07.2026, nach 20-L-Anker)

- **Kapazität bestätigt:** 20,0 L echte Liter = +9,7 % Geber-Level → Tank
  ≈ **205 L**; die konfigurierten 200 L passen.
- **Ω→Liter-Kurve leicht zu flach:** Sensor sah nur +17,5 L statt +20 L
  (−12 % in diesem Bereich). Auflösung ≈ **3,3 Ω/L** um 200–260 Ω.
- **Analoge VDO-Anzeige stark nichtlinear:** +10 % Tankinhalt bewegten die
  Nadel von ≈ 1/10 auf ≈ 1/2 — der untere Skalenbereich ist extrem
  gestaucht. Die Anzeige taugt nur als Grobwarnung, nicht zur Mengenschätzung.

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
