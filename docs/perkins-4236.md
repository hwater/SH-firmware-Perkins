# Motor — Perkins 4.236 (Wilhelmina)

Zentrale Referenz zum verbauten Motor. Sammelt die gesicherten Erkenntnisse
aus Messung und Konfiguration zur Weiterverwendung. Sensor- und Tank-Details
in eigenen Dateien: [Kraftstoffsensor](fuel-sensor-fs-40-10-al.md),
[Tank-Eichtabelle](tank-eichtabelle.md).

## Technische Basis

| | |
|---|---|
| Typ | Perkins **4.236** (marinisiert, ~72–85 PS je Version) |
| Bauart | 4-Zylinder-Reihen-Diesel, **3,86 L** Hubraum, indirekte Einspritzung |
| Einspritzpumpe | CAV/DPA-Verteiler-(Rotations-)Pumpe |
| Kraftstoffrücklauf | **Ja** — Injektor-Lecköl + Pumpenrücklauf |
| Betriebsstunden (analog VDO) | 1452,3 h (23.07.2026); Zähler-Basis in Firmware konfigurierbar |

## Betriebsstundenzählung — geprüft ✓

Die Firmware zählt Motorstunden als `base_hours` + Laufzeit, solange
`Kraftstoffverbrauch > Laufschwelle`. **Abgleich 23.07.2026 bestätigt die
Zählung als exakt:**

| Quelle | Motorstunden |
|---|---|
| Analog (VDO) | 1452,3 h |
| Firmware / N2K (`propulsion.main.runTime`) | 1452,3 h |
| Differenz | **0,0 h** |

→ Die Stundenzählung ist verlässlich (im Gegensatz zum Verbrauchs-/Tankteil,
der noch kalibriert werden muss). Basiswert `base_hours` = 1445,7 h (gesetzt
16.07.2026), Config *„Zählerstand (h)"* setzt den Zähler jederzeit exakt auf
den analogen Wert.

## Kraftstoffsystem & Rücklauf — wichtig

Der 4.236 hat systembedingt einen **Rücklauf** (die DPA-Pumpe fördert deutlich
mehr, als eingespritzt wird; der Überschuss + Injektor-Lecköl geht zurück).

**Auf diesem Boot gibt es aber nur EINE Leitung zum Tank** — der Rücklauf wird
lokal im Motorraum (vor dem Durchflusssensor) in den Vorlauf zurückgeführt.
Folge: Der einzelne flowTrecs-FS-40-Sensor sieht nur den **Netto-Verbrauch**,
nicht den Rücklauf. Aus Daten bestätigt (18.07.2026, ~3 h Fahrt):

| Quelle | Verbrauch |
|---|---|
| Durchflusssensor (integriert) | 5,5 L |
| Tank-Geber (Abnahme) | 11,9 L |

Sensor **kleiner** als Tankabnahme → kein Rücklauf-Fehler (ein rücklauf-
messender Sensor zeigte das 2–4-fache). Merke für die Zukunft: **Beim 4.236
keine Zwei-Sensor-Differenzmessung nötig, solange die Rückführung vor dem
Sensor bleibt.**

## Fahrten & Verbrauch (Stand 07/2026, Sensor unkalibriert)

Aus `propulsion.main.fuel.rate` segmentiert (Fahrt = zusammenhängende Phase
> 0,5 L/h, Lücke > 15 min trennt), Verbrauch integriert, Zeiten in CEST:

| Datum | Start–Ende | Dauer | Verbrauch (L) | Ø L/h | Art |
|---|---|---|---|---|---|
| 12.07. | 18:13–19:34 | 1h20 | 12,5 | 9,3 | Marsch |
| 18.07. | 11:53–14:20 | 2h26 | 9,4 | 3,8 | Teillast/Manöver |
| 18.07. | 17:24–18:50 | 1h26 | 13,0 | 9,0 | Marsch |
| 21.07. | 14:32–16:41 | 2h08 | 6,6 | 3,1 | Teillast |
| 23.07. | 19:46–20:18 | 0h31 | 1,1 | 2,2 | Manöver |
| 23.07. | 23:31–23:48 | 0h17 | 0,5 | 1,7 | Manöver |
| kleine Manöver (13.07., 23.07. je <0,3 L) | | ~0h20 | 0,5 | — | Rangieren |

**Summe echte Fahrten: ~8,4 h, ~43,6 L (Sensor), Ø ~5 L/h** (inkl. Leerlauf).
Reine **Marschfahrten ~9 L/h** — für einen 4.236 realistisch.

> **Störausreißer ausgeschlossen:** 13.07. 19:41–19:57 loggte 59 L in 16 min
> (220 L/h) — physikalisch unmöglich, Störimpulse am Durchfluss-Eingang D1
> („falsche Peaks nach Motorabschaltung"). Der Plausibilitätsfilter (max. 35
> L/h) fängt das am N2K-Output ab, im Roh-Influx-Log stehen sie noch. Aus
> allen Summen entfernt.

**Kalibrier-Stand:** Marschfahrten (~9 L/h) sind plausibel. Am
18.07.-Vormittag (Teillast) zeigt der Sensor 9,4 L vs. Tank-Geber-Abnahme
11,9 L — **nur ~20 % Abweichung**, nicht 2× wie zunächst gedacht (der frühere
5,5-L-Wert hatte ein zu enges Zeitfenster). Der Sensor ist damit besser als
vermutet; die **Nachtank-Bilanz** über eine volle Tankfüllung bleibt der
finale Abgleich.

## Sensor-Dimensionierung

FS-40-10-AL **Version S** (0,5–20 L/h) ist für 20–60 PS spezifiziert, der
4.236 liegt darüber. Weil der Sensor nur Netto-Verbrauch bei meist Teillast
sieht, reicht der Bereich im Normalbetrieb. **Bei Volllast** kann der echte
Durchfluss ~20 L/h erreichen und der Sensor sättigen → Werte am oberen
Kurvenende als Untergrenze behandeln.

## Offene Punkte / To-do

- [ ] Nachtank-Bilanz über eine volle Tankfüllung → Durchflusskurve
  (D1, Web-UI) kalibrieren.
- [ ] Tank Ω→Liter-Kurve (Analog C) nachziehen — aktuell ~12 % zu flach.
- [ ] Peilung-cm ↔ Liter aus Nachfüllungen (siehe Tank-Eichtabelle).
- [ ] Prüfen, ob Volllast-Durchfluss > 20 L/h → ggf. Sensor Version M.
