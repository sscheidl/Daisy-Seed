# Review-Auftrag an Codex: Arbeitsauftrag Acid Seq

* **Stand:** 2026-10-26
* **Gegenstand:** [ARBEITSAUFTRAG.md](ARBEITSAUFTRAG.md)
* **Basis:** Branch `claude/acid-seq-work-order`, abgezweigt von `main` @ `1eb091c`
* **Verfasser des Arbeitsauftrags:** Claude Opus 5.5

## Auftrag

Prüfe den Arbeitsauftrag unabhängig und kritisch, bevor Phase 0 beginnt.
**Ändere dabei keinen Code und nicht den Arbeitsauftrag selbst.** Schreib das
Ergebnis nach `docs/acid-seq/CODEX_REVIEW.md`. Die Regeln in
[AGENTS.md](../../AGENTS.md) gelten: Nicht flashen, keine Workflows starten,
und nur berichten, was du selbst geprüft hast.

Behauptungen über das Repo bitte am Code nachprüfen, nicht am Text des
Auftrags. Wo du etwas nicht prüfen konntest, schreib das dazu.

## Prüfpunkte

1. **Repo-Fakten.** Stimmen die Angaben zu diesen Stellen?
   * `common/desktop.mk`: Projekte liegen zwei Ebenen unter der Wurzel.
   * `common/audio/fx_contract.h`
   * `components/registry.json`, insbesondere ob neue `kind`s wie `sequencer`
     und `dsp` von `scripts/component_manifest.py` und dessen Tests akzeptiert
     werden.
   * `scripts/select_test_suites.py`: Löst `common/` außer `audio/` und `sd/`
     alle Suites aus?
   * `effects/tape-fx/src/tape_fx.h`: Sättigungskurven `tape::Saturate*`, Tests
     gegen die drumlogue-Referenz.
   * `libDaisy/src/desktop_devkit.*`: CV OUT nur über den DAC, keine
     Gate-Ausgänge, `tog_sw18` wird nicht entprellt, DIN-MIDI über `hw.midi`.
   * Nutzt bisher keine Firmware DIN-MIDI?
2. **Machbarkeit auf der Hardware.**
   * Ist `hw.SetDacOut` (DAC im Polling-Modus) im Audio-Callback zulässig?
   * Reichen 1 ms Zeitauflösung für Clock-In, Swing und Gates?
   * DIN-MIDI: Puffer und DTCM-Bedarf. Toy Piano belegt laut `docs/HARDWARE.md`
     bereits 88,6 % DTCM.
   * Delay-Speicher von etwa 768 KB im SDRAM.
   * Ist die CPU-Reserve für Ladder-Filter, Drive, Delay und Sequencer plausibel?
3. **Regelkonformität.**
   * Bleiben die Golden-Hashes und Referenztests bestehender Produkte
     unverändert, besonders bei der Auslagerung der Sättigung in Phase 3a?
   * Bleibt das Mod-on-Daisy-Protokoll unverändert?
   * Hält die CI-Erweiterung das Budget ein?
   * Kein Flash-Schreiben, Bootloader-Einstieg in jeder Firmware.
4. **Bedienkonzept.**
   * Gibt es Kollisionen zwischen Step-Wahl, langem Druck und der
     Bootloader-Geste S9 + S16?
   * Ist das Abspielverhalten der drei Modi (Vorwärts, Rückwärts, Zufall über
     SW18) bei Moduswechsel und Längenänderung eindeutig festgelegt?
   * Start/Stop über SW17 (4.2): Ist der Vorrang vor MIDI Start/Stop, Panel und
     Gate IN 2 widerspruchsfrei? Was passiert beim Einschalten mit SW17 auf Run?
     Was passiert, wenn die externe Clock ausfällt?
5. **Protokoll und Panel.**
   * Ist ein eigener SysEx-Kopf `7D 53 51` (D1) sinnvoll gegenüber dem
     Parametervertrag, den `docs/MODULARITY_AUDIT_2026-10-25.md` § 3 empfiehlt?
   * Wie greift er in die vorhandene Session und die Transporte in
     `editors/mod-on-daisy` (Web, JUCE, VST3)?
6. **Phasenschnitt.**
   * Sind die Phasen wirklich unabhängig (2, 3, 4 parallel)?
   * Fehlen Lieferteile, Tests oder PASS-Kriterien?
   * Ist die Reihenfolge riskant, zum Beispiel weil das Protokoll in Phase 1 und
     das Panel erst in Phase 5 kommt?
7. **Offene Entscheidungen D1–D9.** Stimmst du den Vorschlägen zu? Wo nicht, mit
   Begründung und Alternative.
8. **Lücken.** Was fehlt im Auftrag ganz, etwa Fehlerfälle, USB-Trennung,
   Verhalten beim Einschalten, Pegel und Polarität von Gate-Ausgängen, oder
   Verhalten, wenn keine externe Clock anliegt?

## Format des Ergebnisses

```
# Codex Review: Arbeitsauftrag Acid Seq
VERDICT: GO | GO MIT KORREKTUREN | NO-GO
Geprüft: <Branch @ Commit>, Datum

## Befunde
| ID | Schwere (P1 blockierend / P2 vor der Phase beheben / P3 Hinweis) | Abschnitt | Befund | Beleg (Datei:Zeile) | Vorschlag |

## Entscheidungen D1–D9
| Nr. | Zustimmung ja/nein | Begründung |

## Nicht geprüft
```

P1 bedeutet, dass der Auftrag in diesem Punkt falsch ist oder gegen
AGENTS.md verstößt. Danach arbeitet Claude die Befunde ein, und der Benutzer
entscheidet die offenen Punkte.
