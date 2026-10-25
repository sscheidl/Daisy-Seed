# Torus Reverb als FX-Komponente

Die versionierte Komponente `torus-reverb` stellt den vorhandenen MIT-lizenzierten
Torus-Reverb über den [gemeinsamen Audiovertrag](../../common/audio/fx_contract.h)
bereit. Die DSP-Quelle bleibt einmalig am
[bisherigen Provenienzpfad](../../instruments/physical-modeling/torus/UPSTREAM.md).
Es wurden keine Herstellerbinaries oder weiteren DSP-Kopien übernommen.

`seed::fx::TorusReverb` besitzt keinen Board-, MIDI- oder Editor-Code.
`Prepare({48000, max_block}, storage)` bindet den vom Host bereitgestellten
Speicher (32.768 uint16-Wörter, 65.536 Byte). `SetParameters` übernimmt die
normalisierten Torus-Regler Decay, Damping, Diffusion und Input Gain.
`Process(StereoBlock)` liefert nur Wet; der Host mischt Dry/Wet und begrenzt den
Ausgang. Exakte kanalweise In-place-Verarbeitung ist möglich.

Die ursprüngliche 48-kHz-Abstimmung und alle Setter-Ausdrücke bleiben erhalten.
Die Hosttests vergleichen das Ergebnis samplegenau mit der ursprünglichen
`amount=1`-Verarbeitung und prüfen ungültige Blöcke, Ausklänge und den Audiopfad
ohne Heap-Allokationen. In der neuen Torus-Komposition muss zusätzlich der
bestehende Golden-Hash passen.

```bash
make test-fx
make PROJECT=physical-modeling-fx-torus test
make PROJECT=physical-modeling-fx-torus -j4
```

Dies ist ein wiederverwendbarer FX-Kern. Eine eigene Board-Firmware nur für
Torus-Reverb mit externen Eingängen ist noch kein Buildziel; die vorhandene
Standalone-Ambient-Firmware bleibt das externe FX-Testgerät.
