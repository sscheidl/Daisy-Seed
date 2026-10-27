# Parametervertrag und CC-Plan (Acid Seq)

**Stand:** 26. Oktober 2026 · **Status:** Vertrag aus Phase 0. Maschinenlesbare
Quelle: [`contracts/parameters.json`](../contracts/parameters.json). Diese Tabellen
sind daraus erzeugt, der Vertragstest prüft, dass beide übereinstimmen.

## 1. Regeln

* **Stabile IDs:** Jede Komponente nummeriert ihre Parameter ab 1 (`u14`). Eine ID
  behält ihre Bedeutung für immer. Neue Parameter bekommen neue IDs, entfallene IDs
  werden nicht wieder vergeben. Eine inkompatible Änderung erhöht die Schema-Version
  der Komponente.
* **Werte** reisen als `i32` in Tausendsteln der Einheit (PROTOCOL.md § 2). Enum- und
  Bool-Werte sind ganze Zahlen (`2000` = Index 2).
* **Deskriptoren:** Jede Firmware meldet für ihre Komponenten genau diese Einträge
  über `GET_PARAM_DESC` (Typ, Einheit, Flags, Min, Max, Standard, CC, Schlüssel). Die
  Komponententests vergleichen ihre C++-Tabellen mit `parameters.json`.
* **CC-Plan:** CCs für Stimme und Effekte gelten auf dem **Stimmen-Kanal**
  (`step-seq.voice_channel`, Standard 2). Der Sequencer selbst hat keine CCs, seine
  Potis und Schalter gehören der Hardware.
  * Abbildung: kontinuierlich linear bzw. logarithmisch (Flag) über Min…Max mit
    CC 0…127. Enum: Index = ⌊CC × Anzahl / 128⌋. Bool: CC ≥ 64 ist an.
  * Belegte Nummern: 5, 7, 70–71, 74–79 (Stimme, angelehnt an die
    Sound-Controller-Konvention), 20–27 (Drive), 102–109 (Delay). Kein CC ist
    doppelt belegt. CC 120, 121 und 123 bleiben ihren Standardbedeutungen vorbehalten
    (All Sound Off, Reset All Controllers, All Notes Off).
* **Potis gegen Panel:** Die letzte Änderung gewinnt. Ein Poti schreibt erst, wenn
  er sich über seine Hysterese hinaus bewegt (4.1 des Auftrags).
* **Nur lesen:** `play_mode` folgt SW18. Das Panel zeigt es an, kann es aber nicht
  setzen (D3).

## 2. Parameter

### `step-seq` (Schema 1)

| ID | Schlüssel | Bezeichnung | Typ | Einheit | Bereich | Standard | CC | Hinweis |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `tempo` | Tempo | float | BPM | 30 … 300 | 120 | — | VR5 |
| 2 | `swing` | Swing | float | % | 50 … 75 | 50 | — | VR6 |
| 3 | `length` | Länge | int | — | 1 … 16 | 16 | — | VR7 |
| 4 | `transpose` | Transponierung | int | Halbtöne | −12 … 12 | 0 | — | VR8 |
| 5 | `play_mode` | Modus | enum | — | 0 … 2 | 0 | — | SW18; nur lesen; 0 Vorwärts, 1 Rückwärts, 2 Zufall |
| 6 | `scale` | Skala | enum | — | 0 … 2 | 0 | — | 0 Chromatisch, 1 Dur, 2 Moll |
| 7 | `clock_source` | Clock-Quelle | enum | — | 0 … 3 | 0 | — | 0 Intern, 1 Gate IN 1, 2 MIDI, 3 Auto |
| 8 | `clock_divider` | Clock-Teiler | enum | — | 0 … 3 | 0 | — | 0 1 pro Step, 1 2 pro Viertel, 2 4 pro Viertel, 3 24 pro Viertel |
| 9 | `gate_in_2_mode` | Gate IN 2 | enum | — | 0 … 1 | 0 | — | 0 Reset, 1 Run |
| 10 | `cv_in_1_mode` | CV IN 1 | enum | — | 0 … 1 | 0 | — | 0 Transponierung, 1 Aus |
| 11 | `cv_in_2_mode` | CV IN 2 | enum | — | 0 … 2 | 0 | — | 0 Aus, 1 Gate-Länge, 2 Swing |
| 12 | `seq_channel` | Seq-Kanal | int | Kanal | 1 … 16 | 1 | — |  |
| 13 | `voice_channel` | Stimmen-Kanal | int | Kanal | 1 … 16 | 2 | — |  |
| 14 | `usb_out` | USB-Ausgang | bool | — | 0 … 1 | 1 | — |  |
| 15 | `din_out` | DIN-Ausgang | bool | — | 0 … 1 | 1 | — |  |
| 16 | `accent_velocity` | Accent-Anschlag | int | Anschlag | 1 … 127 | 127 | — |  |
| 17 | `velocity` | Anschlag | int | Anschlag | 1 … 127 | 100 | — |  |
| 18 | `glide_time` | Glide-Zeit | float | ms | 10 … 500 | 60 | — |  |
| 19 | `clock_out` | Clock senden | bool | — | 0 … 1 | 1 | — |  |

### `acid-303` (Schema 1)

| ID | Schlüssel | Bezeichnung | Typ | Einheit | Bereich | Standard | CC | Hinweis |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `waveform` | Wellenform | enum | — | 0 … 1 | 0 | 70 | 0 Säge, 1 Rechteck |
| 2 | `tune` | Tuning | int | Halbtöne | −12 … 12 | 0 | 78 |  |
| 3 | `fine` | Fein | float | Cent | −100 … 100 | 0 | 79 |  |
| 4 | `cutoff` | Cutoff | float | Hz | 40 … 12000 | 800 | 74 | logarithmisch |
| 5 | `resonance` | Resonanz | float | % | 0 … 100 | 50 | 71 |  |
| 6 | `env_mod` | Env Mod | float | % | 0 … 100 | 50 | 76 |  |
| 7 | `decay` | Decay | float | ms | 30 … 3000 | 300 | 75 | logarithmisch |
| 8 | `accent` | Accent | float | % | 0 … 100 | 50 | 77 |  |
| 9 | `volume` | Lautstärke | float | % | 0 … 100 | 80 | 7 |  |
| 10 | `slide_time` | Slide-Zeit | float | ms | 10 … 500 | 60 | 5 |  |
| 11 | `slide_mode` | Slide-Modus | enum | — | 0 … 2 | 1 | — | 0 Aus, 1 Legato, 2 Immer |

### `drive` (Schema 1)

| ID | Schlüssel | Bezeichnung | Typ | Einheit | Bereich | Standard | CC | Hinweis |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `enabled` | Drive an | bool | — | 0 … 1 | 1 | 20 |  |
| 2 | `curve` | Kurve | enum | — | 0 … 3 | 0 | 21 | 0 Soft, 1 Hard, 2 Tape, 3 Fold |
| 3 | `drive` | Drive | float | % | 0 … 100 | 25 | 22 |  |
| 4 | `tone` | Tone | float | % | 0 … 100 | 70 | 23 |  |
| 5 | `level` | Pegel | float | dB | −24 … 12 | 0 | 24 |  |
| 6 | `auto_level` | Auto-Level | bool | — | 0 … 1 | 1 | 25 |  |
| 7 | `compensation` | Ausgleich | float | % | 0 … 100 | 100 | 26 |  |
| 8 | `target_offset` | Zielversatz | float | dB | −3 … 0 | −1,5 | 27 |  |

### `stereo-delay` (Schema 1)

| ID | Schlüssel | Bezeichnung | Typ | Einheit | Bereich | Standard | CC | Hinweis |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `enabled` | Delay an | bool | — | 0 … 1 | 1 | 102 |  |
| 2 | `mode` | Modus | enum | — | 0 … 1 | 0 | 103 | 0 Ping-Pong, 1 Stereo L/R |
| 3 | `time` | Zeit | float | ms | 10 … 2000 | 375 | 104 | logarithmisch |
| 4 | `sync` | Sync | bool | — | 0 … 1 | 1 | 105 |  |
| 5 | `note_value` | Notenwert | enum | — | 0 … 17 | 8 | 106 | 0 1/32, 1 1/32 T, 2 1/32 p, 3 1/16, 4 1/16 T, 5 1/16 p, 6 1/8, 7 1/8 T, 8 1/8 p, 9 1/4, 10 1/4 T, 11 1/4 p, 12 1/2, 13 1/2 T, 14 1/2 p, 15 1/1, 16 1/1 T, 17 1/1 p |
| 6 | `feedback` | Feedback | float | % | 0 … 95 | 40 | 107 |  |
| 7 | `mix` | Dry/Wet | float | % | 0 … 100 | 25 | 108 |  |
| 8 | `damping` | Dämpfung | float | Hz | 1000 … 20000 | 6000 | 109 | logarithmisch |
