# Tank-Eichtabelle — Dieseltank Wilhelmina

Eichung des Dieseltanks: Peilung (cm) gegen analoge VDO-Tankanzeige, den
elektrischen Geber (Analog C → PGN 127505, `tanks.fuel.main.*`) und
Nachfüllmengen. Ziel: Kurve cm ↔ Liter ↔ Geberwiderstand, um Anzeige und
Signal-K-Füllstand zu kalibrieren.

Konfigurierte Kapazität (Firmware): 200 L (`tanks.fuel.0.capacity` = 0,2 m³) —
**durch zwei unabhängige Betankungen bestätigt** (23.07. und 09.08., siehe
unten): 204 L bzw. 198 L.

## Frühere Messreihe (Anfang Juli 2026, Motorstand 1445,7 h)

Erste Eichserie (ursprünglich auf dem M3 erfasst), Peilung + Geber-% gegen
kumulierte Nachfüllmenge. „Anzeige %" = elektrischer Geber (`currentLevel`).

| Ereignis | Peilung (cm) | Geber (%) | eingefüllt kum. (L) | Betriebsstd. |
|---|---|---|---|---|
| Start | 14 | 36 | 0 | — |
| +21 L | 18 | 44 | 21 | — |
| +21 L | 23 | 58 | 42 | — |
| nach Fahrt | 12 | — | 42 | 1445,7 h |
| +20 L | 16 | 42 | 62 | 1445,7 h |

## Messungen (aktuell)

| Datum | Peilung (cm) | Anzeige analog | Geber (Ω) | Sensor-Level | Sensor-Volumen | Betriebsstd. analog | Bemerkung |
|---|---|---|---|---|---|---|---|
| 23.07.2026 ~13:55 | 10,0 | ≈ 1/10 | 191,2 | 26,6 % | ≈ 48 L | 1451,0 h | Erster Datenpunkt, vor Nachfüllung |
| 23.07.2026 ~14:12 | — | ≈ 1/2 (Zeiger waagrecht, genauer nicht ablesbar) | 257,1 | 36,4 % | ≈ 65,5 L | 1451,0 h | Nach Nachfüllung +20,0 L |

## Nachfüllungen

| Datum | Menge (L) | Peilung vorher (cm) | Peilung nachher (cm) | Anzeige nachher | Bemerkung |
|---|---|---|---|---|---|
| 23.07.2026 | 20,0 | 10,0 | — (nachtragen) | ≈ 1/2 | Geber: 191,2→257,1 Ω (+65,8 Ω), Level 26,6→36,4 % |
| 08.08.2026 | ~2 | 10,5 | — | — | Menge unsicher (2–4 l), als Stützpunkt untauglich |
| 09.08.2026 | 55,0 | 10,5 | 26,0 | — | **GTL**, erster GTL-Schnitt; Level 24,32→52,05 %, Volumen 43,5→93,7 L |

## Erkenntnisse

**Peilung ist die verlässlichste Eichgröße** — aus drei Nachfüllungen:

| Nachfüllung | Δ Liter | Δ cm | L/cm |
|---|---|---|---|
| 14→18 cm (Juli-Serie) | 21 | 4 | 5,25 |
| 18→23 cm (Juli-Serie) | 21 | 5 | 4,20 |
| 12→16 cm (Juli-Serie) | 20 | 4 | 5,00 |

→ **≈ 4,8 L/cm** im mittleren Tankbereich, gut reproduzierbar. Der Peilstab
schlägt sowohl die analoge Nadel als auch den elektrischen Geber.

- **cm und Geber sind untereinander konsistent:** ≈ 2,3 %/cm
  (10 cm→26,6 %, 14 cm→36 %). Beide messen also *dieselbe* Höhe plausibel.
- **Aber: Geber-% eignet sich nicht zur Mengenbestimmung** — die drei
  Nachfüllungen ergeben 1,5–2,6 L pro %-Punkt (stark streuend), der untere
  Kurvenbereich ist nichtlinear und die Ω→Liter-Kurve ~12 % zu flach
  (20 L real = +9,7 % ≈ 205 L Kapazität, aber die Juli-%-Werte widersprechen
  sich). Konfigurierte 200 L sind plausibel, aber noch nicht scharf.
- **Analoge VDO-Anzeige stark nichtlinear:** +10 % Inhalt bewegten die Nadel
  von ≈ 1/10 auf ≈ 1/2 — unterer Skalenbereich extrem gestaucht, nur
  Grobwarnung.
- **Fehlt für die Absolut-Eichung:** ein **Volltank-Referenzpunkt** (Peilung
  bei randvollem Tank) — dann liefert 4,8 L/cm direkt Liter aus cm.

Pegel-Trend: 12 cm (nach Fahrt, 1445,7 h) → 10 cm (1451,0 h, vor Tanken) —
zwischenzeitliche Fahrten haben ~2 cm ≈ 10 L gezehrt.

## Eichanker 09.08.2026: +55 L (GTL) — und die Auflösung des cm-Widerspruchs

Bisher größte Einzelbetankung, damit der schärfste Anker.

| | vorher | nachher |
|---|---|---|
| Peilung | 10,5 cm | 26,0 cm |
| Geber-Level | 24,32 % | 52,05 % |
| Geber-Volumen | 43,5 L | 93,7 L |

Geberwerte aus InfluxDB: vorher Mittel 06:00–09:00 UTC, nachher Tagesmittel
des 10.08. (Beruhigung abgewartet). Am Füllnachmittag schwankten die
Einzelwerte zwischen 91 und 103 L — **Streuung rund ±5 L**, Einzelablesungen
taugen nicht.

### Liter pro Prozentpunkt — hier stimmen beide Serien überein

Das ist die belastbarste Größe, weil sie nur getankte Liter und den Geber
braucht, keine Peilung:

| Betankung | Δ Liter | Δ Geber | L pro Prozentpunkt | → Kapazität |
|---|---|---|---|---|
| 23.07., +20,0 L | 20,0 | 26,6 → 36,4 % | **2,04** | 204 L |
| 09.08., +55 L | 55,0 | 24,32 → 52,05 % | **1,98** | 198 L |

**Zwei unabhängige Betankungen, unterschiedliche Mengen, unterschiedliche
Füllbereiche — Abweichung 3 %.** Die konfigurierten **200 L sind damit
bestätigt**, und `currentLevel` ist in diesem Bereich linear im Volumen.

Damit gilt für Auswertungen: **Liter ≈ `currentLevel` × 200.**

### Die cm-Seite widerspricht sich weiterhin

| Quelle | L/cm | %/cm |
|---|---|---|
| Juli-Serie (drei Nachfüllungen, 12–23 cm) | ≈ 4,8 | 2,35 |
| 09.08. (10,5 → 26,0 cm) | 3,55 | 1,79 |

Die Bereiche überlappen, Tankgeometrie erklärt das also **nicht**: Läge
zwischen 10,5 und 26 cm durchgehend die Juli-Steigung, hätten die 55 L rund
**11,5 cm** ergeben, nicht 15,5 cm. Umgekehrt müsste die Juli-Serie mit
3,55 L/cm für 21 L knapp 6 cm gebraucht haben statt 4.

Da beide Serien beim **Volumen** übereinstimmen und nur bei den **cm**
auseinanderlaufen, liegt der Fehler auf der Peilstab-Seite — Ablesewinkel,
Stabneigung, oder eine Messung zu früh nach dem Einfüllen. Welche der beiden
Reihen danebenliegt, lässt sich aus dem vorhandenen Material **nicht**
entscheiden.

> **Konsequenz: 4,8 L/cm ist vorerst nicht belastbar, 3,55 L/cm ebenso wenig.**
> Für Mengenrechnungen den Geber-Prozentwert nehmen, nicht den Peilstab. Der
> Peilstab bleibt als Plausibilitätsprüfung nützlich.

Zur Klärung braucht es eine Betankung, bei der **beide** Peilungen unter
gleichen Bedingungen genommen werden: gleicher Ablesepunkt, Stab senkrecht,
mindestens 30 min nach dem Einfüllen, Schiff ruhig.

### Geberkennlinie ist deutlich nichtlinear

| Abschnitt | Ω pro Prozentpunkt |
|---|---|
| 191,2 Ω (26,6 %) → 257,1 Ω (36,4 %) | 6,72 |
| 257,1 Ω (36,4 %) → 299,1 Ω (50,1 %) | 3,07 |

Der Faktor zwei zwischen beiden Abschnitten bestätigt die Notiz oben: Die
Ω→Level-Kurve im unteren Bereich ist stark gekrümmt. Dass `currentLevel`
trotzdem linear im Volumen liegt, spricht dafür, dass die in der Firmware
hinterlegte Kurve diese Krümmung bereits richtig ausgleicht.

## Rücklauf-Test (Motor Perkins 4.236)

> Zusammengefasst und weitergeführt in der Motor-Referenz
> [perkins-4236.md](perkins-4236.md).

Der 4.236 hat systembedingt einen Kraftstoffrücklauf (Injektor-Lecköl +
CAV/DPA-Pumpenrücklauf). Da es nur **eine** Tankleitung gibt, wird der Rücklauf
lokal im Motorraum (vor dem Sensor) zurückgeführt → der Durchflusssensor sieht
nur den Netto-Verbrauch. Aus Daten bestätigt:

| Fahrt | Sensor (integriert) | Tank-Geber (Abnahme) |
|---|---|---|
| 18.07.2026, 11:53–14:20 (volle Fahrt) | 9,4 L | 11,9 L |

Sensor < Tankabnahme → **kein Rücklauf-Fehler** (ein rücklaufmessender Sensor
zeigte das 2–4-fache). Abweichung nur **~20 %** (nicht 2× — ein früherer
5,5-L-Wert hatte ein zu enges Zeitfenster). Beide noch unkalibriert;
Schiedsrichter = Nachtank-Bilanz (kumulierter Sensor-Verbrauch zwischen zwei
Betankungen vs. echte Liter). Details der Fahrten: [perkins-4236.md](perkins-4236.md).

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
