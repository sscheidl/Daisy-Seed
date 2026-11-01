# Abtastrate: 40-kHz-Kern mit Resampler gegen direkte 48 kHz

Stand 01.11.2026. Das Original rechnet intern mit 40 kHz und 16er-Schritten und
resampelt anschließend auf die Hostrate (SF-A05 BESTÄTIGT; Resamplingfilter OFFEN).
Das Seed3-Desktop-Kit läuft mit 48 kHz. Der Auftrag verlangt beide Wege zu trennen
und, soweit lokal sinnvoll, zu vergleichen.

## Versuch

`make -C instruments/streichwurst compare-rates` ([tools/compare_rates.cpp](../tools/compare_rates.cpp)):
derselbe eigene Stringkern einmal mit `Prepare(40000)` und eigenem Kaiser-Sinc-
Resampler (48 Taps, Grenzfrequenz 0,45 · 40 kHz) auf 48 kHz, einmal direkt mit
`Prepare(48000)` (alle Zeitgrößen in Sekunden umgerechnet). Gemessen wird der Anteil
der Energie bis 20 kHz außerhalb der Harmonischen der tiefsten Fußlage (±3 FFT-Bins,
16384 Punkte, Hann). Eigene Testsignale, kein Originalrender.

**GEMESSEN 01.11.2026** (Windows, LLVM-MinGW Clang 23.1.3, `-O2`, Host-PC):

| Note / Registrierung / Oktaven | 40 kHz → 48 kHz | direkt 48 kHz | Hostzeit t40/t48 |
| --- | --- | --- | --- |
| 57 / 0,50 / Grundlage | −37,1 dB | −39,6 dB | 0,90 |
| 69 / 0,50 / Grundlage | −34,4 dB | −37,5 dB | 0,86 |
| 81 / 0,50 / Beide | −31,2 dB | −36,0 dB | 1,01 |
| 93 / 1,00 / Grundlage | −33,7 dB | −39,0 dB | 0,95 |
| 100 / 1,00 / Beide | −36,5 dB | −27,0 dB | 0,92 |
| 108 / 1,00 / Grundlage | −24,1 dB | −36,6 dB | 0,86 |

Die Hostzeit enthält den Resampler nicht; er käme auf dem Board hinzu
(48 Multiplikationen je Ausgangssample und Kanal).

## Entscheidung

Das Produkt rechnet **direkt mit 48 kHz**:

* In fünf von sechs Fällen liegt der unharmonische Anteil direkt bei 48 kHz niedriger,
  weil der höhere Nyquist mehr Abstand zu den Rechteckobertönen lässt.
* Ein 40-kHz-Kern spart auf dem M7 nur rund ein Sechstel der Kernlast, kostet aber den
  Resampler und Latenz. Die CPU-Last beider Wege auf dem Seed3 ist **NICHT GEMESSEN**.
* Ausnahme Note 100 mit beiden Oktaven: Hier fällt ein Oberton direkt bei 48 kHz
  ungünstiger. Das bleibt eine dokumentierte Klangabweichung.

Folgen: Die Aliasingmuster entsprechen nicht denen des Originalkerns mit seinem
unbekannten Resamplingfilter. Hüllkurven, Ensemble-LFOs und Delays haben durch die
Zeitskalierung dieselben Zeitkonstanten wie bei 40 kHz. Der Kern bleibt
abtastratenunabhängig (`Prepare(fs)`); ein späterer 40-kHz-Betrieb mit belegtem
Resampler ist ohne Änderung der Modulgrenzen möglich.
