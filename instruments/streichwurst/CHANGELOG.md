# Streichwurst – Änderungen

## Product 0.1.1 (instrument 0.1.0, FX 0.1.0) – 2026-11-01

Firmware fix after the first hardware session; DSP and sound unchanged (fingerprint and
empty-matrix checks identical).

* MIDI back-pressure: the main loop moves USB and DIN MIDI into the command queue only
  while 8 entries stay free; the rest waits in libDaisy's event FIFO (256) until the audio
  callback has drained the queue. Before, a burst after a main-loop stall overflowed the
  64-entry queue and dropped Note-Ons, pitch bends and CCs. On the kit, test build 1
  reported "MIDI queue overflow" 64 after normal playing (no audible effect reported).
* The overflow counter now only counts true losses; the All Notes Off fallback for a
  release a full queue cannot take stays.
* Host test: a 200-event burst without audio callbacks in between keeps every note; the
  fallback is tested separately.

## Produkt 0.1.0 (Instrument 0.1.0, FX 0.1.0) – 01.11.2026

Erster Firmwareprototyp, lokal gebaut und auf dem Host geprüft; Hardware und
Klangvergleich NICHT GEMESSEN.

* Instrument: Stringsection mit zwölf frei laufenden Tonklassentakten, binärer
  Teilerkette, eigenem bandbegrenztem 8-Tap-Ereigniskern und Leckintegration;
  Hüllkurven, Registrierung, Oktavtransformation und Notenziele nach SF-F01..F05;
  acht eigene Solostimmen; samplegenaue Ereignisse, 16-Kanal-Tastenverwaltung.
* FX: Ensemble mit einer Leitung, Modi 3/5/3 Taps, parabolischen LFOs, belegten
  Phasen und Stereo-Tapkorrektur, 20-ms-Übergänge; eigener Phaser, eigenes Animate,
  eigener FDN-Hall; Adapter für den FX-Vertrag 1.0.
* Produkt: ein Parameterregister (Kit, CC, SQ, Matrix), Balance nach SF-F07,
  Gainwort nach Solo und Hall (SF-F06), Panik-Rampe, Ausgangsschutz; Mod on Daisy
  mit acht Zielen; SQ 1.1 (Mod on Daisy am selben Port, Produktkomponente).
* Eigener Klang-Fingerprint in `tests/test_product.cpp` (erstmals festgelegt).
