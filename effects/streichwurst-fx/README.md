# Streichwurst FX

Eigenständiger FX-Baustein (`streichwurst-fx`, Version `STREICHWURST_FX_VERSION`
0.1.0, Mehrbusvertrag `STREICHWURST_FX_CONTRACT_VERSION` 1.0.0) mit den Funktionen
der Streichfett-Effektsektion: **Animate, Ensemble, Phaser und Reverb**. Eigener Code
und eigene Daten; belegte Beziehungen und eigene Ersatzverfahren stehen getrennt in
[FIDELITY.md](../../instruments/streichwurst/docs/FIDELITY.md).

**Stand 01.11.2026:** implementiert, Hosttests bestanden (auch ASan/UBSan), im Produkt
`streichwurst` eingebunden. Hörvergleich mit dem Original NICHT GEMESSEN.

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
make -C effects/streichwurst-fx test           # HOST_CXX=clang++ unter Windows
make -C effects/streichwurst-fx test-sanitize
```

Eine eigenständige FX-Firmware gibt es in dieser Version nicht; Pflicht war die
unabhängige Host-Testbarkeit und die Einbindung ins Produkt.
