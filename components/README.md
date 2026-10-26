# Komponenten und Produktversionen

`registry.json` ordnet allen implementierten Instrumenten, den beiden Reverb-
Komponenten, der Tape-FX-Komponente (`tape-fx`), Mod on Daisy, dem gemeinsamen Bootloader-Einstieg (`bootloader-entry`,
in jedem Firmwareprodukt) und den drei Control Panels stabile IDs zu. Die
Versionen werden aus den eigenen Headern bzw. CMake gelesen. Im Register steht
keine zweite Kopie dieser Versionsnummern. Der Browser zeigt seine eigene
Version aus `editors/mod-on-daisy/web/version.js` an.

```bash
make components                         # Register und Versionsquellen prüfen
make test-components                    # Zuordnung, Identität und Abhängigkeiten testen
python3 scripts/component_manifest.py   # alle Komponentenversionen anzeigen
python3 scripts/component_manifest.py --project physical-modeling-fx-ambient
make PROJECT=physical-modeling-fx-ambient manifest # gebautes BIN prüfen, COMPONENTS.json ablegen
```

Ein Manifest nennt Produkt-ID/-Version, Komponentenversionen mit ihren
Versionsquellen und Abhängigkeiten, passende Control Panels, Git-Revision,
Status des Arbeitsbaums sowie BIN-Größe und SHA-256. Panels stehen getrennt von
den eingebetteten Komponenten: Der VST3-Editor ist kein Audio-DSP im Daisy-BIN.
Der manuelle Firmware-Workflow legt das Manifest neben BIN/ELF und Prüfsumme ab.

Neue Komponenten und Produktvarianten verwenden dreiteilige SemVer. Bestehende
Werte wie Toy Piano `0.3` und Ambient Reverb `0.2` bleiben für akzeptierte Images
erhalten. Eine Änderung der Klangsemantik verlangt eine begründete Versionierung
und einen neuen Golden-Nachweis; ein Registereintrag allein ändert keinen Klang.

Die neuen zusammengesetzten Produkte verwenden **eine** Produktversion aus
`composed_version.h` für Dateiname, Firmwaremarker und IDENTIFY. Die historischen
Mod4-Dateinamen bleiben erhalten; `legacy_device_version` weist dort auf die
bekannte abweichende IDENTIFY-Kennung hin. Das Manifest ist ein Build-Artefakt,
keine neue SysEx-Nachricht. Die Zusammensetzung wird noch nicht live im Panel
angezeigt.

**Acid Seq** (Arbeitsauftrag 3.1, R12): Die Komponenten `step-seq` (Art
`sequencer`), `acid-303`, `saturation` (Art `dsp`, Version im Makro
`SEED_SATURATION_VERSION` von `common/dsp/saturation.h`), `drive` und
`stereo-delay` stehen im Register, sobald ihre Versionsquelle existiert. Ihre
Firmwareprodukte (`step-seq`, `acid-303`, `drive-fx`, `delay-fx`, `acid-seq`) kommen
erst mit dem ersten validierten Build dazu, weil die Produktliste genau
`BUILDABLE_PROJECTS` entsprechen muss. Gültige Arten: `instrument`, `effect`,
`control`, `panel`, `contract`, `sequencer`, `dsp`.

Audiovertrag und genaue Grenzen: [MODULAR_COMPONENTS.md](../docs/MODULAR_COMPONENTS.md).
