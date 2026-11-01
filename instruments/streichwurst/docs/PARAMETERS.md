# Streichwurst: Parametervertrag und CC-Plan

Stand 01.11.2026, Schema 1 aller drei Komponenten. Maschinenlesbare Quelle:
[contracts/parameters.json](../contracts/parameters.json). Daraus erzeugt
`sequencers/step-seq/tools/gen_params.py` die Firmwaretabelle
`include/streichwurst/param_table.h` sowie die Beschriftungen beider Editoren
(`--check` prüft, dass alles übereinstimmt).

## Regeln

* **Ein Register für alles:** Potis und Taster am Kit, MIDI CC, SQ-Protokoll (Control App) und
  die Mod-on-Daisy-Matrix greifen auf dieselben Werte zu (`src/register.h`). Die Matrix
  addiert nur Offsets auf acht Ziele; der Registerwert selbst wird nie moduliert.
* **Stabile IDs** je Komponente ab 1, Werte als `i32` in Tausendsteln der Einheit (SQ § 2).
  Entfallene IDs werden nicht neu vergeben; inkompatible Änderungen erhöhen das Schema.
* **CCs** gelten auf dem eingestellten MIDI-Kanal (`midi_channel`, 0 = Omni). Abbildung wie
  bei Acid Seq: stetig linear bzw. logarithmisch über Min…Max, Enum/Int = ⌊CC·Anzahl/128⌋,
  Bool = CC ≥ 64. Nicht belegt: CC1 (Mod on Daisy), CC64 (Sustain), CC120/123 (All Sound/
  Notes Off). Verwendet werden die laut MIDI-Spezifikation undefinierten Nummern 20–31,
  85–87 und 102–109 sowie CC7 (Master) und CC8 (Balance). **Eigener Plan**, keine
  Übernahme einer Herstellerbelegung.
* **Letzte Änderung gewinnt** (Poti, CC oder App). Ein Poti übernimmt erst, wenn es den
  gespeicherten Wert überquert oder ihm auf 2 % nahekommt (Soft takeover, [CONTROLS.md](CONTROLS.md)).
* **Nur lesen:** `pot_page` und `preset` sind Zustand am Kit, nicht Teil eines Patches.
* Zusätzliche Regler (Animate-/Phaser-Rate, Reverb-Decay, Anschlag, Stimmung) sind
  **eigene Parameter**, keine behaupteten Originalknöpfe.

## Instrument `streichwurst-instrument` (Schema 1)

| ID | Schlüssel | Bezeichnung | Typ | Einheit | Bereich | Standard | CC | Seite / Bedienung |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `registration` | Registrierung | float | % | 0 … 100 | 50 | 20 | strings · VR1 Haupt |
| 2 | `octave` | Oktaven | enum | — | 0 … 2 (0 Grundlage, 1 Beide, 2 Höhere) | 0 | 21 | strings · S10 |
| 3 | `crescendo` | Crescendo | float | % | 0 … 100 | 20 | 22 | strings · VR2 Haupt |
| 4 | `release` | Release Strings | float | % | 0 … 100 | 45 | 23 | strings · VR3 Haupt |
| 5 | `solo_tone` | Solo Tone | float | % | 0 … 100 | 50 | 24 | solo · VR1 Solo |
| 6 | `solo_attack` | Solo Attack | float | % | 0 … 100 | 10 | 25 | solo · VR2 Solo |
| 7 | `solo_release` | Solo Release | float | % | 0 … 100 | 30 | 26 | solo · VR3 Solo |
| 8 | `solo_sus_mode` | Solo Sustain-Modus | enum | — | 0 … 2 (0 Abklingen auf Pegel, 1 Halten, 2 Perkussiv) | 0 | 27 | solo · S13 |
| 9 | `solo_sustain` | Solo Sustain | float | % | 0 … 100 | 70 | 28 | solo · VR6 Solo |
| 10 | `solo_trem_depth` | Tremolo-Tiefe | float | % | 0 … 100 | 0 | 29 | solo · VR4 Solo |
| 11 | `solo_trem_rate` | Tremolo-Rate | float | Hz | 0.5 … 12 | 5 | 30 | solo · VR5 Solo · logarithmisch |
| 12 | `split_mode` | Layer/Split | enum | — | 0 … 1 (0 Layer, 1 Split) | 0 | 31 | keyboard · S14 |
| 13 | `split_key` | Splitpunkt (MIDI-Note) | int | — | 0 … 127 | 60 | 85 | keyboard · VR7 Solo |
| 14 | `string_velocity` | Anschlag Strings | float | % | 0 … 100 | 0 | 86 | strings |
| 15 | `solo_velocity` | Anschlag Solo | float | % | 0 … 100 | 100 | 87 | solo |
| 16 | `bend_range` | Bend-Bereich | int | Halbtöne | 0 … 12 | 2 | — | keyboard |
| 17 | `transpose` | Transponierung | int | Halbtöne | -24 … 24 | 0 | — | keyboard |
| 18 | `tune` | Stimmung | float | Cent | -50 … 50 | 0 | — | keyboard |

## FX `streichwurst-fx` (Schema 1)

| ID | Schlüssel | Bezeichnung | Typ | Einheit | Bereich | Standard | CC | Seite / Bedienung |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `animate` | Animate | float | % | 0 … 100 | 0 | 102 | fx · VR5 Haupt, VR1 FX |
| 2 | `animate_rate` | Animate-Rate (eigen) | float | Hz | 0.05 … 5 | 0.5 | 103 | fx · VR2 FX · logarithmisch |
| 3 | `ensemble_on` | Ensemble | bool | — | 0 … 1 | 1 | 104 | fx · S12 |
| 4 | `ensemble_mode` | Ensemble-Modus | enum | — | 0 … 2 (0 Modus 0 (3 Taps), 1 Modus 1 (Dry + 5), 2 Modus 2 (Dry + 3)) | 1 | 105 | fx · S11 |
| 5 | `phaser` | Phaser | float | % | 0 … 100 | 0 | 106 | fx · VR6 Haupt, VR3 FX |
| 6 | `phaser_rate` | Phaser-Rate (eigen) | float | Hz | 0.05 … 5 | 0.4 | 107 | fx · VR4 FX · logarithmisch |
| 7 | `reverb` | Reverb | float | % | 0 … 100 | 20 | 108 | fx · VR7 Haupt, VR5 FX |
| 8 | `reverb_decay` | Reverb-Decay (eigen) | float | % | 0 … 100 | 50 | 109 | fx · VR6 FX |

## Produkt `streichwurst` (SQ-Komponentenart 4) (Schema 1)

| ID | Schlüssel | Bezeichnung | Typ | Einheit | Bereich | Standard | CC | Seite / Bedienung |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `balance` | Balance Strings/Solo | float | % | 0 … 100 | 50 | 8 | global · VR4 Haupt, VR8 FX |
| 2 | `master` | Master | float | % | 0 … 100 | 80 | 7 | global · VR8 Haupt/Solo, VR7 FX |
| 3 | `midi_channel` | MIDI-Kanal (0 = Omni) | int | Kanal | 0 … 16 | 0 | — | global |
| 4 | `pot_page` | Reglerseite am Kit | enum | — | 0 … 2 (0 Haupt, 1 Solo, 2 FX) | 0 | — | global · S9 · nur lesen |
| 5 | `preset` | RAM-Preset (zuletzt geladen) | int | — | 0 … 3 | 0 | — | global · S15 · nur lesen |

## Modulationsmatrix (Mod on Daisy, eigene Auswahl)

Acht Spalten, 1 kHz Regeltakt (`src/mod_targets.h`). Jede Spalte addiert einen Offset zur
normierten Grundstellung des Registers (0…1). Eine leere Matrix lässt den Klang bitgenau
unverändert (Produkt- und Firmwaretest).

| Spalte | Ziel | Tiefe 100 |
| --- | --- | --- |
| 1 | Registrierung | ±100 % |
| 2 | Animate | ±100 % |
| 3 | Phaser | ±100 % |
| 4 | Reverb | ±100 % |
| 5 | Balance | ±100 % |
| 6 | Solo Tone | ±100 % |
| 7 | Tremolo-Tiefe | ±100 % |
| 8 | Tonhöhe | ±1 Halbton |

Quellen: LFO 1/2, Random, CV 1/2, Modwheel (CC1), Kanal- und Poly-Aftertouch, wie bei allen
Mod-on-Daisy-Firmwares. Ein allgemeiner Laufzeitlader oder neue Quellen gehören nicht dazu.
