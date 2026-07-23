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
| Betriebsstunden (analog VDO) | 1451,0 h (Foto 23.07.2026); Zähler-Basis in Firmware konfigurierbar |

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

## Gemessener Verbrauch (Stand 07/2026, unkalibriert)

Stundenmittel echter Fahrten (nur Punkte > 0,5 L/h):

| Datum | Ø L/h (Motorstunden) |
|---|---|
| 12.07. | 2,5 / 1,5 |
| 18.07. | 3,2 / 3,6 / 1,3 (vorm.) · 1,9 / 2,6 (nachm.) |
| 21.07. | 1,5 / 3,1 / 2,1 |

**Vorbehalt:** Diese Werte wirken für einen 4.236 niedrig (Marsch typ.
6–12 L/h). Der Durchflusssensor **unterschätzt vermutlich** — der Tank-Geber
zeigt über dieselbe Fahrt ~2× mehr. Welcher stimmt, klärt erst die
**Nachtank-Bilanz** (kumulierter Sensor-Verbrauch zwischen zwei Betankungen
vs. echte getankte Liter). Bis dahin: Verbrauchszahlen als Untergrenze lesen.

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
