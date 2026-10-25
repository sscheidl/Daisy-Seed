# Idee: Modularer Firmwarekern („Daisy-Baukasten“)

**Umsetzung der ersten Stufe:** [Modulare Komponenten](MODULAR_COMPONENTS.md)
beschreibt die zusätzlichen Torus-/Ambient-FX-Builds, den gemeinsamen
Audiovertrag und die Versionsmanifeste. Die folgenden Überlegungen bleiben der
historische Entwurf für den weitergehenden Baukasten einschließlich Laufzeitwechsel.

Bestandsaufnahme vom 25.10.2026, vor dieser ersten Stufe:
[Rings/Torus, Mod-Modul, Editoren und Versionierung](MODULARITY_AUDIT_2026-10-25.md).
Sie trennt die bereits implementierten Bausteine von den noch offenen
Instrument-/FX-Schnittstellen und enthält erneut ausgeführte Prüfungen.

**Stand:** Idee und erste Überlegungen vom 24.10.2026 (Hardwaretest-Abend mit
Physical Modeling mod3). **Noch nicht geplant, kein Code.** Nächster Schritt: daraus einen
ausgearbeiteten Plan machen (Schnittstellen, Speicher- und CPU-Modell, Bedienkonzept,
Protokoll, Migrationsreihenfolge).

## 1. Die Idee

Statt jedes Instrument als eigene Firmware mit eigener Matrix-Einbindung zu bauen, ein
**Baukasten aus drei Bausteinarten**, die sich gegenseitig ergänzen und austauschen lassen:

1. **Instrument** (z. B. Physical Modeling, Toy Piano, Nebulae, Looper)
2. **FX** (z. B. Torus-Reverb, Ambient Reverb, Stereo Delay, Granular)
3. **Mod-Matrix** (Mod on Daisy)

Solange Speicher und CPU reichen, sollen alle drei in einer **Grundfirmware** beliebig
kombinierbar sein, auch einzeln:

| Kombination | Beispiel |
| --- | --- |
| nur Instrument | Physical Modeling wie heute |
| nur FX | Ambient Reverb am Audio-Eingang |
| Instrument + Mod | Physical Modeling `MOD=1` wie heute |
| FX + Mod | Ambient Reverb, dessen Parameter die Matrix moduliert |
| Instrument + FX + Mod | „Rings into Clouds“ mit Matrix |

Ziel: Neue Instrumente und Effekte werden nur noch als Baustein geschrieben; Kern, Bedienung,
MIDI, SysEx, Editor-Anbindung und Messung gibt es einmal. Das vereinfacht die weitere
Entwicklung und erhöht die Flexibilität.

## 2. Woher die Idee kam

Am 24.10.2026 wurden Wünsche für Physical Modeling gesammelt:

* zwei alternative FX zum mitgelieferten Torus-Reverb, im Sinne von **„Rings into Clouds“**:
  **Stereo Delay**, unser **Ambient Reverb** (`effects/reverb`, an Slöer angelehnt, Modi
  Cloud/Halo/Scatter/Abyss/Bloom) oder ein **einfacher Granular-Effekt wie Clouds**;
* das aktive Modell (SW17/SW18 bzw. CC28) oben im Verbindungsbereich des Editors anzeigen.

Daraus entstand die allgemeinere Frage, ob Instrument, FX und Matrix nicht grundsätzlich
austauschbare Bausteine sein sollten.

## 3. Was es schon gibt

* **Mod on Daisy** ist bereits ein austauschbarer Baustein: Toy Piano und Physical Modeling
  binden dieselbe Matrix über eine feste Schnittstelle ein (`InstrumentInfo`, Zieltabellen,
  `Engine::Apply`/`ApplyVoice`, [ARCHITECTURE.md](../modules/mod-on-daisy/docs/ARCHITECTURE.md) § 6).
* Gemeinsamer Build `common/desktop.mk`, gemeinsamer SD-Treiber `common/sd`.
* SysEx-Protokoll 0x7D mit zwei Editoren (JUCE, Browser) und `mod_probe.py`.
* FX-Kandidaten:
  * Torus-Reverb (in Physical Modeling, gemessen als Teil des Instruments);
  * Ambient Reverb v0.2 (`effects/reverb`, eigener Code, MIT; **am Gerät nie gelaufen**);
  * Mutable-Quellen im lokalen Archiv `Firmware Hacking/Eurorack/Rings (NanoRings)/DIY/eurorack-master.zip`:
    `rings/dsp/fx/chorus.h`, `rings/dsp/fx/ensemble.h` (Rings' „Disastrous Peace“ schaltet
    selbst zwischen Chorus, Ensemble und Reverb um), `clouds/dsp/fx/pitch_shifter.h`,
    `clouds/dsp/fx/diffuser.h` — alle auf derselben `FxEngine` wie der Torus-Reverb;
  * Granular: Kern von Nebulae on Daisy (`instruments/granular`); das Live-Projekt
    `effects/granular` ist nur geplant.

## 4. Skizze

```
                ┌──────────── Firmwarekern ────────────┐
 Audio-In ─────►│                                      │
 MIDI/USB ─────►│  [Instrument] ──► [FX] ──► Ausgang   │
 Potis/Tasten ─►│        ▲            ▲                │
 CV ───────────►│        └── [Mod-Matrix] ─────────────│
                │  Bedien-Mapping · Parameter-Register │
                │  Speicher-Arena · CPU-Budget · SysEx │
                └──────────────────────────────────────┘
```

| Baustein | Schnittstelle (Entwurf) |
| --- | --- |
| Instrument | Noten/MIDI rein, Stereo raus; meldet Parameter (Name, Bereich, „pro Stimme“, „beim Anschlag“) |
| FX | Stereo rein, Stereo raus; ohne Instrument am Audio-Eingang („nur FX“) |
| Mod-Matrix | vorhanden; Spalten künftig aus den Parametern **aller** aktiven Bausteine, also Instrument und FX zugleich |

Ein leerer Platz ist ein Durchgang; damit ergeben sich alle Kombinationen.

## 5. Knackpunkte

1. **Speicher (härtester Punkt).** Die Images laufen als BOOT_SRAM aus dem SRAM (480 KB).
   Physical Modeling mit Matrix belegt 170 KB, das DTCM 84 % (`GEMESSEN` beim Build am
   24.10.2026). Einzel-Images: Toy Piano v0.2 126 KB, Physical Modeling v0.1.0 138 KB,
   Nebulae v0.1.0 163 KB, Looper v0.3.0 147 KB, Ambient Reverb v0.1 121 KB (jeweils mit
   eigener libDaisy). Alles zugleich passt so nicht. Möglichkeiten: Code aus dem QSPI
   ausführen und nur heißen DSP-Code im SRAM/ITCM halten; Arbeitsspeicher aus einer
   gemeinsamen Arena im SDRAM (64 MB, kaum belegt), da immer nur ein Instrument und ein FX aktiv ist.
2. **CPU.** Jeder Baustein bringt gemessene Kosten mit; der Kern lehnt Kombinationen ab, die
   nicht ins 1-ms-Budget passen. Belastbar gemessen ist nur Physical Modeling (Spitze 56,71 %,
   4 Stimmen, Western Chords, 22.10.2026). Ambient Reverb, Granular und Looper: `NICHT GEMESSEN`.
3. **Bedienung.** 8 Potis, 16 Tasten und 2 Schalter müssen sich die Bausteine teilen. Nötig ist
   eine Bedienschicht (z. B. Potis per Schalter/Tastenkombination wahlweise Instrument oder FX),
   eine feste CC-Belegung pro Baustein und der Editor als vollständige Oberfläche.
4. **Protokoll und Editor.** IDENTIFY müsste die aktive Zusammenstellung melden; der Editor zeigt
   oben „Instrument · FX · Matrix“ (dazu das aktive Modell) und kann umschalten. Protokoll-
   änderungen immer zusammen mit Firmware, `mod_probe.py`, beiden Editoren und Tests (AGENTS.md).
5. **Klang darf sich nicht versehentlich ändern.** Golden-Test: ein Instrument im Kern klingt
   bitgleich zu seiner Einzelfassung. Bestehende Images (Toy Piano v0.3, reguläres Physical
   Modeling, ModBench) bleiben unberührt.

## 6. Vorgeschlagenes Vorgehen

* **Stufe 1 — Baukasten beim Bauen:** gleiche Schnittstellen, Zusammenstellung per
  `make INSTRUMENT=physical-modeling FX=ambient-reverb MOD=1`. Wenig Risiko, keine Speicherfrage.
* **Stufe 2 — Umschalten zur Laufzeit** in einer Grundfirmware (QSPI-Code, Arena), sobald
  Messungen zeigen, was zusammen passt.
* Migration nacheinander; **Physical Modeling als Pilot** (am besten gemessen).
* Vorab sinnvoll: Ambient Reverb (`reverb-fx`) allein am Kit messen.

## 7. Offene Entscheidungen

* Laufzeit-Umschaltung als Ziel oder Zusammenstellung beim Bauen ausreichend?
* Welche FX zuerst (Stereo Delay, Ambient Reverb, Granular à la Clouds; Ensemble/Chorus als Reserve)?
* Bedienkonzept am Kit: wie Potis zwischen Instrument und FX wechseln?
* FX-Kette (z. B. Delay → Reverb) oder genau ein FX-Platz?
* Neuer Produktname/Version für die Grundfirmware; Verhältnis zu den Einzel-Images.
