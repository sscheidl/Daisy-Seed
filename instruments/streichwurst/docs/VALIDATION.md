# Streichwurst: Prüfstatus

2026-11-01. Vorbereitung, noch keine Instrumentfirmware.

| Gegenstand | Stand |
| --- | --- |
| Eigener C++-DSP-Kern / ARM-Firmware | noch nicht implementiert |
| Originalforschung | lokaler statischer Checkpoint, keine Herstellerprogramme ausgeführt |
| Historische Tests | 36 begrenzte Imageanker; 25 synthetische Werkzeug-/Modelltests über zwei Forschungscheckpoints bestanden |
| Neue Umgebung | ARM-/Host-Compilerprobe, neun Modelltests und zehn Registry-/Testauswahlprüfungen bestanden; Details unten |
| Board-Audio / Regler / USB MIDI / CPU | NICHT GEMESSEN für Streichwurst |
| Klangvergleich zum Originalplugin/-gerät | NICHT GEMESSEN |

## Mitgeliefertes eigenes Referenzmodell

[own_dsp_model.py](../tests/reference/own_dsp_model.py) verwendet selbst
erzeugte Signale, eigene Spektren und Float-Ensemble. Es enthält neun
Tests und ist kein Originalrenderer. NumPy ist eine zusätzliche lokale
Werkzeugabhängigkeit, kein Bestandteil der späteren Firmware.

```powershell
python -B instruments/streichwurst/tests/reference/own_dsp_model.py --test
```

Abweichungen: eigene Quelle, per-Sample-LFO/Interpolation, exakte Drittelphasen,
eigene DC-Normierung, Reset und Bypass. Die späteren Gain-/Balancebefunde im
[Vertiefungsbericht](research/focused-findings.md) werden vom historischen
Modell nicht vollständig nachgebildet. Die übrigen historischen Prüfskripte
und Image-Metadaten bleiben im privaten Forschungsarbeitsbereich.

## Von Claude zu ergänzen

Tatsächlich ausgeführte Host-/Sanitizer-/Queue-/ARM-Prüfungen samt Versionen,
Ergebnissen und Ressourcen. Misserfolge und nicht verfügbare Werkzeuge ebenso
eintragen. Boardmessungen und Originalhörvergleich erst nach realer Ausführung.

## Vorbereitung am 01.11.2026

Basis `origin/main` bei `654e42e127350691759346e96f0ca5cedefb1a64`;
eigener Worktree `.work/streichwurst/`, Branch `claude/streichwurst`.
Der Hauptcheckout blieb auf seinem bisherigen Branch; seine vorhandenen
ungetrackten Reviewdateien wurden nicht übernommen oder verändert.

* libDaisy und DaisySP an den dokumentierten Pins sowie libDaisy-Abhängigkeiten
  aus vorhandenen lokalen Checkouts initialisiert; kein Versionsupdate.
* `tools/enter-local.ps1` erfolgreich dot-gesourct; `make list` enthält
  `streichwurst planned instruments/streichwurst`.
* xPack ARM GCC 13.2.1: kleine C++17-Compilerprobe für Cortex-M7,
  FPv5-D16/Hardfloat erfolgreich zu einem Objekt übersetzt. Das ist kein
  Streichwurst-Firmwarebuild und keine Linker-/Speicherprüfung der späteren App.
* LLVM-MinGW Clang 23.1.3: dieselbe Compilerprobe als Hostprogramm übersetzt
  und erfolgreich ausgeführt. Probedateien liegen nur im ignorierten `build/`.
* `python -B instruments/streichwurst/tests/reference/own_dsp_model.py --test`:
  neun Tests bestanden. Der erste Lauf meldete einen fehlenden Import des
  privaten Imageprüfers; dieser wurde durch die eigenständige Ausgabepfadprüfung
  ersetzt. DSP und Tests wurden dabei nicht verändert.
* `python -B -m unittest discover -s tests -p test_components.py -v`:
  zehn Registry-/Testauswahlprüfungen bestanden.
* `make PROJECT=streichwurst`: erwarteter Abbruch mit
  `planned and has no firmware yet`; keine andere Firmware als Ersatz gebaut.

Es wurde kein manueller Actions-Workflow gestartet und kein Gerät angesprochen.
Host-/Toolchainprüfungen sind keine Hardwaremessung und kein Klangvergleich.
