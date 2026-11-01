# Streichwurst FX

Eigenständiger FX-Baustein (`streichwurst-fx`, Version `STREICHWURST_FX_VERSION`
0.1.0, Mehrbusvertrag `STREICHWURST_FX_CONTRACT_VERSION` 1.0.0) mit den Funktionen
der Streichfett-Effektsektion: **Animate, Ensemble, Phaser und Reverb**. Eigener Code
und eigene Daten; belegte Beziehungen und eigene Ersatzverfahren stehen getrennt in
[FIDELITY.md](../../instruments/streichwurst/docs/FIDELITY.md).

**Stand 01.11.2026:** implementiert, Hosttests bestanden (auch ASan/UBSan), im Produkt
`streichwurst` eingebunden; eigene Standalone-FX-Firmware 0.1.0 gebaut (unten).
Hardwareabnahme und Hörvergleich mit dem Original NICHT GEMESSEN.

## Vertrag ([include/streichwurst_fx/fx.h](include/streichwurst_fx/fx.h))

* `Prepare(FxConfig{fs, max_block}, FxStorage&)` einmal außerhalb des Callbacks; den
  Speicher (Reverbleitungen, ≈32 KiB) besitzt und platziert der Host.
* **Stringstufe** `ProcessStrings(in, out_l, out_r, gain, n)`: Mono-Stringsumme →
  Animate-Formung → Ensemble-Summe → Phaser → Stereo-Tapkorrektur (−21/128). `gain`
  liefert das Ensemble-Gainwort (aus 1, Modus 0/1/2 = 0,8125/0,625/0,75), das der Host
  **nach** Solomischung und Hall anwendet.
* `AnimateRegistrationOffset()`: typisierter Steuerwert für die Registrierung des
  Instruments (eine Blockverzögerung, 1 ms).
* **Globalstufe** `ProcessReverb(in_l, in_r, wet_l, wet_r, n)`: wet-only; `ReverbWet()`
  ist der geglättete Mischanteil für den Host.
* `Reset()`, `ClearTails()`; keine Allokation, keine Sperren, kein I/O; ungültige Blöcke
  werden vor jedem Pufferzugriff abgelehnt.

Der Baustein besitzt keine Stimmen, keine Boardbedienung und keinen MIDItransport.
[reverb_adapter.h](include/streichwurst_fx/reverb_adapter.h) stellt den Hall als
Stufe des gemeinsamen [FX-Vertrags 1.0](../../common/audio/fx_contract.h) bereit; das
Produkt kann im globalen Slot umgekehrt jede andere Vertragsstufe (z. B. den
Stereo-Delay) verwenden, ohne Instrument oder Stringstufe zu ändern (Produkttest).

## Prüfen

```bash
make -C effects/streichwurst-fx test           # FX allein und die Firmware auf Hostfakes
make -C effects/streichwurst-fx test-sanitize  # unter Windows HOST_CXX=clang++
make PROJECT=streichwurst-fx                   # ARM-Image
python scripts/check-firmware.py effects/streichwurst-fx/build/StreichwurstFx-v0.1.0.bin
```

## Standalone-FX-Firmware 0.1.0 („Modular FX first“)

`make PROJECT=streichwurst-fx` baut `StreichwurstFx-v0.1.0.bin` (Version
`STREICHWURST_FX_FIRMWARE_VERSION` in [src/version.h](src/version.h), getrennt von der
Komponentenversion). Stereo-Line-Eingang (nur links = Mono) → Mono-Speisung der
Stringstufe → Globalstufe → Hostmischung mit Gainwort, Ein- und Ausgangspegel und
Ausgangsschutz. Gleicher FX-Code und gleicher Parametervertrag wie im Produkt (die
Firmware nutzt das Streichwurst-Register nur für die FX-Einträge); sie besitzt nur
Boardzuordnung, Routing und Mischung.

* Potis (wirken erst nach Bewegung): VR1 Animate, VR2 Animate-Rate, VR3 Phaser,
  VR4 Phaser-Rate, VR5 Reverb, VR6 Reverb-Decay, VR7 Eingangspegel, VR8 Ausgangspegel.
* Taster: S1–S3 Ensemble-Modus 0/1/2, S12 Ensemble an/aus, S16 Leitung, Hall und
  Filterzustände löschen (beim Loslassen, nicht im S9+S16-Bootloaderchord).
* MIDI CC 102–109 auf allen Kanälen (USB und DIN), SQ über USB mit einer Komponente
  `streichwurst-fx`; die Control App zeigt nur die Seite FX.
* Mit Ensemble aus und Animate/Phaser/Reverb auf 0 gibt die Stringstufe den Eingang
  exakt weiter (Hostfake-Test).

Abnahme am Kit: [docs/HARDWARE_TEST.md](docs/HARDWARE_TEST.md) (F1–F14, **NICHT
GEMESSEN**). Erst danach gilt der FX als abgenommener Baustein für Streichwurst.
