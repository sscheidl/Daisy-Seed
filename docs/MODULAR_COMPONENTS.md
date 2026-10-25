# Modularer Ausbau: erste konkrete Stufe

Instrumente, Effekte und Control Panels werden getrennt entwickelt und beim
Bauen zu benannten Produkten kombiniert. Diese Stufe ergänzt den vorhandenen
Rings/Torus-/Mod-on-Daisy-Aufbau. Es gibt keinen Laufzeit-Lader, keine zweite
Modulationsmatrix und kein zusätzliches Editor-Protokoll.

## Implementierte Grenzen

* **Komponentenregister:** Instrumente, FX, Mod-Kern und Panels haben stabile IDs
  und eigene Versionsquellen. Das [Register](../components/README.md) beschreibt
  auch jede gebaute Firmwarezusammenstellung.
* **Audiovertrag 1.0:** `Prepare(Config, Storage&)`, komponentenspezifische
  `SetParameters`, `Process(StereoBlock)`. Keine virtuelle Dispatch-Schicht.
  Die beiden Adapter sind wet-only; Speicher gehört dem Host, Prepare läuft vor
  dem Audiostart. Ungültige Blöcke werden vor Bufferzugriffen abgelehnt, zulässige
  Blöcke sind begrenzt und dürfen exakt kanalweise In-place arbeiten.
* **Torus-FX:** identische ursprüngliche DSP und normalisierte Parameter; der
  Adapter enthält keine Hardwarezuordnung.
* **Ambient-FX:** gleicher DSP wie die reine FX-Firmware, mit vollständigem
  nativen Parametersatz. Dry/Mix, Ausgangsverstärkung und Bypass sind im Adapter
  auf reine Wet-Verarbeitung gestellt; Routing und endgültige Mischung liegen
  beim Host. Parameteranwendung und Filterservice sind auf 50 Hz begrenzt.
* **Anwendungsadapter:** `physical-modeling/src/composed_fx.h` verbindet die
  bestehenden Torus-Parameter mit dem gewählten Effekt. Damit bleibt die
  Reglerzuordnung außerhalb des FX-Kerns.

Die Dry-/Wet-Pfade bleiben beim Torus-Modellwechsel erhalten. Modell 6 behält
seine trockene rechte Polaritätsumkehr; der Send zum FX wird wie zuvor davor
abgegriffen. Der Host hält den bestehenden Dry/Wet-Mix und Ausgangsschutz.

## Getrennte Produkte

| Buildziel | Inhalt | Produktversion |
| --- | --- | --- |
| `physical-modeling` | akzeptierte reguläre Fassung | unverändert `0.1.0` |
| `physical-modeling-mod` | akzeptierte Mod4-Fassung | historischer Dateiname `0.1.0`, IDENTIFY `0.1.0-mod4` |
| `physical-modeling-fx-torus` | Torus + Mod-Kern + Torus-FX-Adapter | `0.2.0` |
| `physical-modeling-fx-ambient` | Torus + Mod-Kern + Ambient-FX-Adapter | `0.2.0` |
| `reverb-fx` | vorhandenes Ambient-FX mit externen Audioeingängen | unverändert `0.2` |

Die neuen Produkte haben eigene Buildverzeichnisse und Marker; Dateiname und
IDENTIFY lesen dieselbe Produktversion. Die Versionsnummer der eingebundenen
Instrument-DSP ist davon getrennt. Die neuen Verbindungen sind Pilotvarianten
ohne Geräteabnahme, keine neuen veröffentlichten Releases.

Die Audioverarbeitung der regulären Fassung und von Mod4 wird durch Compile-
Flags von der neuen Komposition getrennt. Deren alte BINs müssen bytegleich
bleiben. Die neue Torus-FX-Fassung muss klanglich den gleichen Golden-Hash wie
die alte Fassung liefern. Für Ambient ist die Klangänderung ausdrücklich das
Ziel; dafür gelten End-to-End- und Adaptertests statt eines identischen Hashes.

### Bedienung des Piloten

Die 16 Klaviertasten, sechs Torus-Modelle, bestehenden Potis/CCs und die
Modulationsmatrix bleiben erhalten. Externe Audioeingänge werden in diesen
Instrumentvarianten nicht verwendet. Die beiden Schalter wählen Torus-Modelle,
keine Ambient-Programme.

Beim Ambient-Piloten steuern die bisherigen globalen Reverb-Parameter den
Cloud-Modus: Decay wird logarithmisch auf 1–45 s, Damping auf 14.000–400 Hz,
Diffusion auf 0–1 und Input Gain auf den Send-Pegel abgebildet. Die letzte
Mischung bleibt der vorhandene Dry/Wet-Regler. Weitere native Ambient-Regler,
Stretch und die zusätzlichen Ambient-Modi bleiben über den Adapter technisch
verfügbar, sind aber in dieser Torus-Bedienzuordnung noch nicht exponiert.
Sie werden nicht als bereits bedienbar im bestehenden Editor ausgegeben.

## Builds und Prüfungen

```bash
make components test-components test-fx
make PROJECT=physical-modeling test
make PROJECT=physical-modeling-fx-torus test
make PROJECT=physical-modeling-fx-ambient test
make PROJECT=physical-modeling-fx-torus -j4
make PROJECT=physical-modeling-fx-ambient -j4
make PROJECT=physical-modeling-fx-torus manifest
make PROJECT=physical-modeling-fx-ambient manifest
python3 scripts/check-firmware.py instruments/physical-modeling/build/fx-torus/PhysicalModelingFxTorus-v0.2.0.bin
python3 scripts/check-firmware.py instruments/physical-modeling/build/fx-ambient/PhysicalModelingFxAmbient-v0.2.0.bin
```

Die offiziellen Makefiles verwenden flache Objektnamen und sortierte VPATHs.
Da auch Ambient ein `main.cpp` enthält, bindet die neue Komposition ihr
`main.o` ausdrücklich an den Instrument-Einstiegspunkt. Marker- und
Manifestprüfungen müssen eine falsch zugeordnete Firmware zurückweisen.

Gezielte Quick-Checks berücksichtigen die Verbraucher des gemeinsamen
Mod-Kerns und der FX-Header. Native Builds, ARM-Artefakte und Releases bleiben
manuell gemäß AGENTS.md; es wird keine automatische ARM-Matrix eingeführt.

## Bewusst noch offen

Der Baukasten beschreibt momentan konkrete Buildziele; eine freie Auswahl aller
Instrument-/FX-Paare und ein allgemeiner Instrument-Host sind weitere Schritte.
Die Toy-Piano/Reverb-Kombination und der sechste Shimmer-Typ sind noch nicht
implementiert. Bestehende HTML-/JUCE-/VST3-Panels bleiben Matrix-Editoren und
speichern weiterhin Mod-Patches. Vollständige Klangparameter, Live-Komponenten-
Entdeckung, DSP-Anwendungsrevisionen und ein vollständiges Song-Patchformat
brauchen eine gemeinsam getestete Erweiterung des bestehenden Vertrags.

Die Kombination mit Ambient ist **NICHT GEMESSEN am Gerät**. CPU-Last und
Stack-Hochwasser dürfen nicht aus Einzel-Images addiert oder zugesichert werden.
Die bekannten Fristüberschreitungen bei Torus-Modellwechseln bleiben relevant.
Vor Nutzung am Gerät sind niedriger Abhörpegel, Pegel-/Tail-Prüfung und Messung
mit der vorhandenen CPU-Telemetrie nötig. Kein Flash oder Bootloaderwechsel ist
Teil dieser Umsetzung.

## Validierung vom 25.10.2026

**GEMESSEN am Host / ARM-Build**, Basis `a074663` mit diesen Änderungen:

| Prüfung | Ergebnis |
| --- | --- |
| Komponenten-/Identitäts-/Abhängigkeitstests | 5 Python-Tests bestanden; 12 Komponenten, 9 Firmwareprodukte |
| Gemeinsame FX-Tests, auch ASan/UBSan | bestanden: Buffergrenzen, wet-only, Ausklänge, keine Audioallokation, samplegenauer Torus-Referenzvergleich |
| Bestehende Torus-Hostsuite | bestanden, inklusive beider bisherigen Golden-Varianten und Mod-Einbindung |
| Beide neuen Produktionscallback-Varianten, auch Sanitizer | bestanden; Torus-Komposition weiterhin Golden `37DE0BF9D93B81CE` |
| Standalone-Ambient-Hosttests | bestanden |
| Editor-Kern und Browser-Protokolltests | bestanden; Protokoll unverändert |
| xPack 13.2.1-1.1 ARM-Builds, Firmware- und ELF-Prüfung | beide alten und beide neuen Torus-Produkte bestanden |

Die bisherigen BINs bleiben bytegleich: regulär
`eb8f412a973a9b58dde92bf63c7c0314d137cb6e929e6f4ad8e3c7daa8ce0fc2`,
Mod4 `9517db27fa25cb3c967923304d15c081f025e942d34182f6153d2acb5b6f92e1`.

| Neue Komposition | BIN | DTCM statisch | SDRAM statisch | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| Torus-FX | 172.692 B | 110.376 B | 197.964 B | `051e46c2f50349d50fc906a4caf80fd9c758157983baa52a0869a80451439583` |
| Ambient-FX | 189.908 B | 110.376 B | 1.261.564 B | `01bce977d15a554b43e62cfe26122c98385c078deaac6e0cc218ca7acd4cab7c` |

Die DTCM-Linkerreserve beträgt in beiden neuen Produkten 20.696 B; dies ist keine
gemessene Stackreserve. Im isolierten Worktree wurden die vorhandenen,
unveränderten offiziellen Library-Checkouts mit den gepinnten Commits über
`LIBDAISY_DIR`/`DAISYSP_DIR` verwendet; Host-Tests priorisieren die Projekt-Fakes.
Keine neuen nativen JUCE-/VST3-Builds und keine Hardwareprüfung durchgeführt.
