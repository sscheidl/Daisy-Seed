# Quellen für die nächsten Instrumente und Effekte

Stand: 17.10.2026. Diese Übersicht ist ein erster Abgleich öffentlicher Original-
und Daisy-Quellen sowie vorhandener eigener Forschungsberichte. Sie ist keine
Klangvergleichsmessung und kein Nachweis, dass die Ports auf unserem Desktop-Kit laufen.
Zusätzliche Originalengines und DaisyExamples-Ports wurden weder importiert noch
gebaut oder ausgeführt. DaisySP bleibt die bereits vorhandene gepinnte Bibliothek.

## Festgehaltene Quellenstände

| Quelle | Geprüfter Commit |
| --- | --- |
| [Mutable Instruments / pichenettes/eurorack](https://github.com/pichenettes/eurorack/tree/f4cfa7697095a28d292bff758beb51deb49b5a86) | `f4cfa7697095a28d292bff758beb51deb49b5a86` |
| [DaisyExamples](https://github.com/daisyaudio/DaisyExamples/tree/259ed82c7d4c0d7699f695945e6e111b7dc7cb27) | `259ed82c7d4c0d7699f695945e6e111b7dc7cb27` |
| [DaisySP, bereits als Submodul eingebunden](https://github.com/daisyaudio/DaisySP/tree/2c72eaf9eac5fc0dca1919d65d606da907832618) | `2c72eaf9eac5fc0dca1919d65d606da907832618` |
| [Eigene Forschungsberichte, Custom-Firmware](https://github.com/sscheidl/Custom-Firmware/tree/3e041b5b9ce98eb687316ffb3c4ba19492ec74e9) | `3e041b5b9ce98eb687316ffb3c4ba19492ec74e9` |
| [Eigene FV-1-Forschung und C++-Referenzen](https://github.com/sscheidl/fv-1-DSP-xtractor/tree/d1ef0525d072091d9d4da46f1821d45e930a2745) | `d1ef0525d072091d9d4da46f1821d45e930a2745` |

Bei DaisySP wurden Header, Herkunftskommentare und Lizenz geprüft. Bei den
Originalen wurden die unten verlinkten DSP-Schnittstellen und die Projekt-README
gelesen, bei Torus/Nimbus die READMEs sowie ergänzend Torus-Makefile und Resonator-Header.
Die Forschungsberichte wurden ausschließlich lesend als Textreferenzen konsultiert.

## Physical Modeling / Resonator

| Quelle | Belegter Ansatz | Bedeutung für unser Projekt |
| --- | --- | --- |
| [Rings Part](https://github.com/pichenettes/eurorack/blob/f4cfa7697095a28d292bff758beb51deb49b5a86/rings/dsp/part.h) | Modellwahl umfasst Modal-, Saiten- und weitere Resonatormodelle; blockweise Verarbeitung von Eingang, Ausgang und Aux | Vollständigere Originalreferenz für Modell-/Stimmenorganisation |
| [Elements Part](https://github.com/pichenettes/eurorack/blob/f4cfa7697095a28d292bff758beb51deb49b5a86/elements/dsp/part.h) | Eigene Performance-/Patchdaten; getrennte Blow-/Strike-Eingänge und Main/Aux-Ausgänge | Zweite Referenz für Anregung und resonierenden Körper |
| [Torus](https://github.com/daisyaudio/DaisyExamples/tree/259ed82c7d4c0d7699f695945e6e111b7dc7cb27/patch/Torus) | README bezeichnet es als Rings-Port für **Daisy Patch**; eigenes DSP und stmlib im Build | Mit Rings vergleichbare Portstruktur, aber keine direkte Desktop-Firmware |
| [DaisySP ModalVoice](https://github.com/daisyaudio/DaisySP/blob/2c72eaf9eac5fc0dca1919d65d606da907832618/Source/PhysicalModeling/modalvoice.h) / [Resonator](https://github.com/daisyaudio/DaisySP/blob/2c72eaf9eac5fc0dca1919d65d606da907832618/Source/PhysicalModeling/resonator.h) | Aus **Plaits** portierte Bausteine, laut Herkunftskommentaren; ModalVoice beschreibt Click → Tiefpass → Resonator | Kleiner bereits verfügbarer Versuchspfad, kein vollständiges Rings/Elements |
| [Offizielles Seed-Resonator-Beispiel](https://github.com/daisyaudio/DaisyExamples/tree/259ed82c7d4c0d7699f695945e6e111b7dc7cb27/seed/DSP/resonator) | Demonstriert den Resonator mit zufälligen Parametern | API-/Integrationsbeispiel, kein fertiges Instrument für unser Kit |
| [Eigene Iridium-Resonator-Studie](https://github.com/sscheidl/Custom-Firmware/blob/3e041b5b9ce98eb687316ffb3c4ba19492ec74e9/analysis/synthesizers/waldorf-iridium/04_resonator/README.md) | Forschungsbericht über Resonatororganisation und Parameter; kein offener Herstellerquellcode | Dritte Konzeptreferenz; keine Herstellerimplementierung als Bibliothek |

**Entscheidungsvorschlag:** zuerst eigene einfache Anregung und begrenzte Modalbank,
dann anhand identischer Testsignale mit kleinen DaisySP-Versuchen vergleichen.
Rings/Torus und Elements bleiben ausführlichere Referenzen. Klanggleichheit ist kein
Abnahmekriterium; Laufzeit, Speicher und musikalisch brauchbare Parameter sind es.

## Granular / Freeze

| Quelle | Belegter Ansatz | Bedeutung für unser Projekt |
| --- | --- | --- |
| [GR-1-Forschungsstand](https://github.com/sscheidl/Custom-Firmware/blob/3e041b5b9ce98eb687316ffb3c4ba19492ec74e9/analysis/synthesizers/gr-1/README.md) | Eigene Berichte zu GR-1-DSP und Bedienung; kein freigegebener Hersteller-DSP-Baustein | Musikalisches Vorbild; eigener Scheduler und eigener Samplepfad |
| [GR-1-/Iridium-Vergleich](https://github.com/sscheidl/Custom-Firmware/blob/3e041b5b9ce98eb687316ffb3c4ba19492ec74e9/analysis/comparative/gr1-iridium-granular/README.md) | Vergleich von Grainorganisation, Ressourcen und UI-Rollen | Begrenzter Pool; Basis-, Scan- und Grainposition getrennt halten |
| [Iridium Particle](https://github.com/sscheidl/Custom-Firmware/blob/3e041b5b9ce98eb687316ffb3c4ba19492ec74e9/analysis/synthesizers/waldorf-iridium/05_particle/README.md) | Eigene Forschungsreferenz, keine hardwaregeprüfte eigene Engine | Ergänzende Konzepte für Organisation und Parameter |
| [DaisySP GranularPlayer](https://github.com/daisyaudio/DaisySP/blob/2c72eaf9eac5fc0dca1919d65d606da907832618/Source/Sampling/granularplayer.h) | Samplepuffer und Größe bei Init; Tempo, Transposition in Cent und Grainzeit in ms bei Process | Direkt verfügbarer Vergleichsbaustein für kleine Versuche |
| [Clouds GranularProcessor](https://github.com/pichenettes/eurorack/blob/f4cfa7697095a28d292bff758beb51deb49b5a86/clouds/dsp/granular_processor.h) | Vorgegebene Speicherpuffer, blockweiser Audioeingang/-ausgang, mehrere Betriebsarten | Öffentliches Original für den Audioeffekt-/Freeze-Vergleich |
| [Nimbus Patch](https://github.com/daisyaudio/DaisyExamples/tree/259ed82c7d4c0d7699f695945e6e111b7dc7cb27/patch/Nimbus) / [Field](https://github.com/daisyaudio/DaisyExamples/tree/259ed82c7d4c0d7699f695945e6e111b7dc7cb27/field/Nimbus) | READMEs benennen Clouds-Ports; Patch-/Field-spezifische Bedienung | Vorhandene Daisy-Referenzen; Desktop-Anpassung bleibt Arbeit |
| [Beads GranularProcessor](https://github.com/pichenettes/eurorack/blob/f4cfa7697095a28d292bff758beb51deb49b5a86/beads/dsp/granular_processor.h) | Öffentliches Original mit Delay-/Granular-Verarbeitung | Zusatzreferenz; keine hier nachgewiesene Daisy-Portierung |

Mit „Beats“ ist **vermutlich Beads** gemeint; diese Namenszuordnung ist eine Annahme.
Der Original-[README-Hinweis](https://github.com/pichenettes/eurorack/blob/f4cfa7697095a28d292bff758beb51deb49b5a86/README.md)
zu Beads lautet sinngemäß: Desktop-Build möglich, Build und Betrieb auf dem Modul
nicht verifiziert. Öffentlich verfügbar bedeutet deshalb nicht hardwarevalidiert.

Die historischen Forschungs-PRs sind [GR-1 #17](https://github.com/sscheidl/Custom-Firmware/pull/17),
[Vergleich #18](https://github.com/sscheidl/Custom-Firmware/pull/18) und
[Iridium-GUI #19](https://github.com/sscheidl/Custom-Firmware/pull/19).
Für diese Planung gilt der oben fixierte aktuelle main-Stand; frühe Aussagen über
damals noch offene PRs sind keine aktuellen Integrationsaussagen.

## Reverb

**Vom Nutzer konkretisiert:** Der neue [Reverb-Bereich](../effects/reverb/) widmet
sich der Reverb-Sektion von **Clouds/Nimbus** und dem **Walrus Audio Slöer
Stereo-Reverb** als zweitem gewünschten Entwicklungs-/Portierungsziel.

| Grundlage | Geprüfter Stand | Verwendung / Grenze |
| --- | --- | --- |
| [Clouds Reverb](https://github.com/pichenettes/eurorack/blob/f4cfa7697095a28d292bff758beb51deb49b5a86/clouds/dsp/fx/reverb.h) | Öffentlicher Originalpfad im oben fixierten Mutable-Stand | Reverb getrennt von der Granular-Anwendung untersuchen |
| [Nimbus Reverb, Patch](https://github.com/daisyaudio/DaisyExamples/blob/259ed82c7d4c0d7699f695945e6e111b7dc7cb27/patch/Nimbus/dsp/fx/reverb.h) / [Field](https://github.com/daisyaudio/DaisyExamples/blob/259ed82c7d4c0d7699f695945e6e111b7dc7cb27/field/Nimbus/dsp/fx/reverb.h) | Reverb-Header und MIT-Hinweise gelesen; vorhandene Daisy-Ports | Ausgangspunkt für Desktop-Kit-Experimente; noch kein eigener Build dieser Sektion |
| Walrus Audio Slöer | Nutzer hat am 17.10.2026 im geöffneten eigenen Gerät einen Spin Semiconductor FV-1 mit Zusatzmarkierung „2211“ identifiziert; keine unabhängige Sichtprüfung hier | FV-1 am Nutzergerät beobachtet. Bedeutung der Zusatzmarkierung, Programm, Taktung, vollständiger Stereo-Signalweg und portierbare Algorithmusgrundlage offen |
| [FV-1-Portierungsstrategie](https://github.com/sscheidl/fv-1-DSP-xtractor/blob/d1ef0525d072091d9d4da46f1821d45e930a2745/docs/FV1_DAISY_PORTING.md) und [Quellenkatalog](https://github.com/sscheidl/fv-1-DSP-xtractor/blob/d1ef0525d072091d9d4da46f1821d45e930a2745/docs/FV1_SOURCE_CATALOG.md) | Vorhandene C++17-Plate-/Hall-Referenzen; Berichte gelesen, hier keine Portierung ausgeführt | Eigene FV-1-Grundlage; ausdrücklich keine Slöer-Implementierung. Im historischen Quellenstand fehlte der Chipnachweis; die neue Nutzerbeobachtung ergänzt ihn, ohne den Algorithmus nachzuweisen |

Die Reverb-Arbeit ist damit vom Granular-Effekt abgegrenzt. CloudSeed ist nicht
die vom Nutzer gewählte Cloud-Reverb-Grundlage. Eine echte Slöer-Portierung und
eine eigene klangliche Annäherung müssen getrennt bezeichnet werden.

## Übernahme und offene Prüfungen

Die gelesenen Mutable-STM32-DSP-Header tragen MIT-Lizenztexte; DaisySP weist seine
MIT-Lizenzen und Herkunft aus. Eine spätere Übernahme muss die konkreten Dateien,
Abhängigkeiten, Copyright-Hinweise und Namensregeln berücksichtigen. Noch wurden
keine dieser zusätzlichen Engines oder Assets kopiert. Herstellerbinaries,
Disassemblate und rekonstruierte Herstellerfunktionen werden nicht übernommen.

**Offen:** detaillierter Codevergleich, Rate-/Blockanpassung der Ports an Seed3,
Speicher-/CPU-Messungen, Klangvergleich mit identischen Testsignalen und die
endgültige Wahl eigener Algorithmen. Ein Cortex-M7 besitzt kein NEON; fremde
Stimmen-/Grainzahlen sind keine Daisy-Leistungszusagen.
