# ModBench — Messfirmware für Mod on Daisy

Diese Firmware ist **kein Instrument**. Sie misst die Matrix auf dem Seed3 Desktop Kit
(TEST_PLAN_PHASE2 § 3) und ist zugleich der erste Hardwaretest des MIDI-Protokolls.
Eine Sinus-Stimmenbank mit 8 Zielen läuft durch die Matrix:

| Spalte | Ziel | Weg |
| --- | --- | --- |
| 0 | Pitch (`+-1 st`) | Stimme |
| 1 | Level | Stimme |
| 2 | Harmonic | Stimme |
| 3 | Tone | Stimme |
| 4 | Pan (`+-50%`) | Stimme |
| 5 | Master | global |
| 6 | Width | global |
| 7 | Drive | global |

## Bauen und flashen

```bash
make PROJECT=mod-bench -j4                          # build/ModBench-v0.1.0.bin (Block 48)
make -C modules/mod-on-daisy/bench BLOCK=256 -j4     # build/b256-max8/
make -C modules/mod-on-daisy/bench MAX_ACTIVE=64 -j4 # build/b48-max64/ (Szenario S6)
make PROJECT=mod-bench program-dfu                   # wie bei den Instrumenten (docs/FLASHING.md)
```

## Bedienung am Gerät

* **Tasten S1–S9** wählen die Szenarien S0–S8. Die LED der jeweiligen Taste leuchtet.
  Nach dem Einschalten ist S1 aktiv.
* Audio auf J3/J4 (J5 Kopfhörer). Ohne Noten spielen 4 bzw. 32 Stimmen einen festen
  Akkord.
* **USB-MIDI** (J7, „Daisy Seed External“):
  * Note On weist Stimmen nacheinander eine Note zu; Poly AT auf diese Note moduliert
    die Stimme.
  * CC1 und Channel AT sind Quellen.
  * CC3 ist der nummerierte Teststrom zur Verlusterkennung.
* **CV 1/2** und **Gate 1/2** sind die Quellen bzw. Sync-Eingänge der Matrix.

## Messablauf unter Windows

```bash
pip install mido python-rtmidi
python modules/mod-on-daisy/tools/mod_probe.py identify
python modules/mod-on-daisy/tools/mod_probe.py lengths          # H2/H3
python modules/mod-on-daisy/tools/mod_probe.py bench --seconds 60 --csv s48.csv   # S0..S8
python modules/mod-on-daisy/tools/mod_probe.py monitor --seconds 20             # H5/H6, CV-Rohwerte
python modules/mod-on-daisy/tools/mod_probe.py cc-stats         # falls SysEx-Antworten nicht ankommen
```

* Studio One sollte den Port währenddessen nicht geöffnet haben (H4).
* Die Ergebnisse gehören in [docs/VALIDATION.md](../docs/VALIDATION.md).
* Für S6 die Variante `MAX_ACTIVE=64` flashen, für die Messung bei Block 256 die
  Variante `BLOCK=256`.

## Messgrößen

| Größe | Bedeutung |
| --- | --- |
| Matrix-Zyklen | DWT-Zyklen für `ReadHardware` + `Tick` + alle `Apply`/`ApplyVoice` eines Blocks (480 Zyklen = 1 µs bei 480 MHz) |
| Callback-Last | `daisy::CpuLoadMeter`, Mittel und Spitze, in 0,01 % |
| Überläufe | Callbacks, die länger als das Blockbudget dauern |
| MIDI verloren | Lücken im CC3-Teststrom |
| Main max | Höchstdauer eines `HandleMidi`-Durchlaufs bzw. eines `Service()` in Zyklen; Unterbrechungen durch den Audio-IRQ sind enthalten |

## CC-Rückfallebene

Falls das SysEx-Senden der gepinnten libDaisy auf dem Rechner nicht ankommt (H3):

* **Anfrage**: CC119 = 1 auf Kanal 16.
* **Antwort**: 20 CCs CC100–119 auf Kanal 16. Mehrbyte-Werte stehen je 7 Bit, das
  niederwertigste zuerst.

| CC | Inhalt |
| --- | --- |
| 100 | Format 1 |
| 101–103 | Matrix-Zyklen max |
| 104–106 | Matrix-Zyklen Mittel |
| 107–109 | Callback-Last max (0,01 %) |
| 110–112 | Überläufe |
| 113 | Szenario |
| 114–116 | Callback-Last Mittel (0,01 %) |
| 117 | MIDI verloren (max. 127) |
| 119 | 64 = Ende |
