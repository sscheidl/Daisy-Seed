# Streichwurst – Änderungen

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
