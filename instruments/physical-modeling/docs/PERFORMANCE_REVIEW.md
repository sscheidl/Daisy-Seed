# Zweitprüfung: Performance-Optimierung der Rings/Torus-Portierung

Stand: 27.10.2026. Zweitprüfung durch Claude zum Codex-Vorschlag vom 22.10.2026
(„Vorschlag zur Performance-Optimierung der Rings/Torus-Portierung“).
**Keine Freigabe zur Umsetzung. Kein Code geändert, nichts gebaut, nichts geflasht.**

Grundlage sind die Quellen sowie Map, ELF und Disassembly des vorhandenen Builds
`build/PhysicalModeling-v0.1.0.*` (Build vom 25.10.2026, xPack ARM-GCC 13.2.1,
Standard-Image ohne `MOD`). Alle Zyklen- und Prozentangaben in diesem Dokument sind
**Schätzungen, NICHT GEMESSEN**, gerechnet mit 480 MHz (`seed.Init(true)`):
1 % CPU entspricht etwa 4,8 Mio. Zyklen pro Sekunde.

Gemessene Ausgangswerte ([HARDWARE_RESULTS_2026-10-22.md](HARDWARE_RESULTS_2026-10-22.md),
Durchschnitt bei eingeschwungenem Modell, GEMESSEN 22.10.2026):

| Modell | Durchschnitt (%) | Spitze (%) |
| --- | ---: | ---: |
| Modal Resonator | 33,07 | 34,65 |
| Sympathetic Strings | 42,69 | 55,78 |
| Inharmonic Strings | 39,23 | 48,12 |
| FM Voice | 36,24 | 38,66 |
| Western Chords | 42,38 | 56,71 |
| String & Reverb | 39,65 | 49,16 |

## Die fünf wichtigsten Befunde

1. **Der größte Code-Hotspot fehlt im Plan.** In `FMVoice::SineFm`
   ([fm_voice.h](../torus/dsp/fm_voice.h)) steht `static_cast<int64_t>(...)`. Der
   Cortex-M7 hat keinen Befehl für float→int64; der Code ruft deshalb die
   libgcc-Routine `__aeabi_f2lz` auf (`bl`, Double-Arithmetik, gesicherte Register).
   Das geschieht **zweimal pro Sample und Stimme**, also 384 000 Aufrufe pro Sekunde,
   rund zehnmal öfter als die 32 000 `powf`-Aufrufe. Geschätzt etwa 2 % CPU im
   FM-Modell, gegenüber etwa 0,5–0,7 % für `powf`.
   - Das Original-Rings castet direkt nach `uint32_t` (ein `vcvt`-Befehl). Das int64
     kam beim Portieren dazu, vermutlich wegen des `float-cast-overflow`-Sanitizers.
   - Bitidentische Lösung ohne undefiniertes Verhalten:
     `x >= 0.0f && x < 4294967296.0f ? uint32_t(x) : uint32_t(int64_t(x))`.
     Sie liefert für jeden Wert dasselbe Ergebnis wie heute. Da `fm` zwischen −4 und 4
     liegt, läuft der langsame Zweig nur bei genau 2³².
2. **Der D2-Plan überschreibt den Heap.** Laut Map liegen `.heap` und `end` bei
   `0x30008000`, also genau an der geplanten Basisadresse.
   - Das `_sbrk` aus libnosys wächst ab `end` ohne Obergrenze; `calloc`/`free` werden
     von `usbd_cdc.o` gebraucht.
   - Eine DSP-Sektion an dieser Adresse würde mit der ersten USB-Allokation überlappen.
   - `scripts/check-physical-elf.py` prüft Programmheader. Der Heap hat dort die
     Größe 0; das Skript würde den Konflikt nicht bemerken.
3. **AXI-SRAM (D1) ist das bessere Ziel.** Das BOOT_SRAM-Image belegt 137 784 Bytes
   (Text plus `.data`-LMA bis `0x24021a38`) von 480 KiB; frei sind etwa 345 KiB.
   - Die 197 740 Bytes DSP-Daten passen; danach bleiben etwa 152 KiB für Codewachstum.
   - Gegenüber D2: kein Heap im Weg, kein Umweg über die D1–D2-Bridge, 64-Bit-AXI-Bus.
4. **Caches senken den Mittelwert, nicht den schlechtesten Fall.** Bei Reglerfahrten
   und im MOD-Build mit LFO ändern sich die Cache-Schlüssel in jedem Block; dann gibt es
   keine Treffer. Für die Fristreserve und spätere 6/8 Stimmen zählen nur Änderungen,
   die auch dann wirken.
5. **Der Host-Golden-Test sichert den ARM-Code nicht ab.** ARM-GCC fasst Multiplikation
   und Addition standardmäßig zu FMA zusammen (`-ffp-contract=fast` im GNU-Modus; im
   Disassembly stehen überall `vfma`), der x86-Host-Build nicht. Varianten mit anderen
   Flags, LTO oder anderer Speicherplatzierung deckt `test_golden` nicht ab.

## Antworten auf die Fragen

### 1. Welche Stellen sind auf dem M7 relevant, was erledigt GCC schon?

| Stelle | Befund im Assembly | Wirkung (geschätzt) | betrifft Modell |
| --- | --- | --- | --- |
| `SineFm` int64-Cast | 2× `bl __aeabi_f2lz` pro Sample | ~2 % | FM |
| FM `powf` | 2× `bl powf` + 4 `vdiv` pro Abschnitt | ~0,5–0,7 % | FM |
| `Resonator::ComputeFilters` | 2 `vdiv` pro Mode (12 Moden × 4 Stimmen × 4000/s) | ~1,5 % | Modal |
| `Resonator::Process`, Innenschleife | bereits eng, 2 Moden pro Durchlauf | keine Reserve | Modal |
| `1.0f / (0.2f + noise_filter)` | `vdiv` in der Schleife (Offset `0x402`), nicht herausgezogen | ~0,5 % | **nur 3 und 6** (Dispersion) |
| Member-Variablen in `String::ProcessInternal` | `src_phase_` u. a. werden pro Sample geladen und gespeichert (`vstr s15,[r0,#44]`), weil `out`/`aux` darauf zeigen könnten (Aliasing) | klein | alle Saitenmodelle |
| SDRAM-Zugriffe | 8 × 12 KiB Delay-Lines + 64 KiB Reverb, D-Cache 16 KiB; SDRAM-MPU-Region ist Write-Back **ohne** Write-Allocate (`libDaisy/src/sys/system.cpp:539–546`) | unbekannt, messen | vor allem 2 und 5 |

Folgerung: Die beiden schwersten Modelle (Sympathetic Strings und Western Chords, etwa
43 %) haben keine Dispersion. Bei ihnen greifen praktisch nur die Speicherplatzierung
und lokale Kopien in `String`.

Warum GCC die Division nicht herauszieht: `-ftrapping-math` ist Standard, und die
Division steht nur im Zweig `src_phase_ > 1.0f`. Weder die GIMPLE- noch die
RTL-Optimierung darf eine Operation, die eine FP-Ausnahme auslösen kann, spekulativ
vorziehen. **Auch `-fmove-loop-invariants` behebt das nicht.** Das manuelle
Herausziehen ist bitidentisch (dieselbe Operation, einmal berechnet).

Den Mischpfad in `Synth::Render` und den Reverb habe ich im Assembly **nicht** geprüft.

### 2. Gibt es einen belegbaren Grund für `-fno-move-loop-invariants`?

Belegbar ist nur die Herkunft, kein technischer Grund:

- Der Flag-Block in libDaisy (`-fasm -finline -finline-functions-called-once
  -fshort-enums -fno-move-loop-invariants -fno-unwind-tables`) entspricht genau dem in
  `makefile.inc` von Mutable Instruments' stmlib (dort mit `-O2`):
  https://github.com/pichenettes/stmlib/blob/master/makefile.inc
- Dorthin kam er aus dem AVR-Makefile von MI (`avril/makefile.mk`, mit
  `-Os -mcall-prologues`): https://github.com/pichenettes/avril/blob/master/makefile.mk
- In keiner der drei Dateien und im libDaisy-Commit
  `f37ac3b169e3f0cc284b20d5d4c274f4b3391746` steht eine Begründung.

Dass es auf dem AVR um Registerdruck oder Codegröße ging, ist eine Vermutung. Sicher ist:
Die originale Rings-Firmware wurde selbst mit diesem Flag gebaut.

Erwartet wird wenig Gewinn, weil die GIMPLE-Variante (LIM) ohnehin aktiv ist. Die
Ergebnisse sollten bitidentisch bleiben, weil der RTL-Pass erst nach der
FMA-Zusammenfassung läuft; das muss per ARM-Hash bestätigt werden.

Umsetzung: Das `filter-out` gehört hinter `include ../../common/desktop.mk`; dann wirkt
es auch auf die eigene `.cc`-Regel im Instrument-Makefile.

### 3. Sind D2-Platzierung, NOLOAD-Initialisierung und Heap-/DMA-Abgrenzung tragfähig?

- **NOLOAD plus explizites Nullen:** tragfähig. `InitializeSynth()` füllt den Speicher
  schon heute mit `fill_n`; `InitResources()` beschreibt `lut_sine`.
- **D2-Takte und MPU:** unproblematisch. `SystemInit` schaltet die Takte für SRAM1–3
  ein (`libDaisy/src/sys/system_stm32h7xx.c:205`). Nur die ersten 32 KiB von D2 sind über
  die MPU als nicht cachebar markiert; der Rest läuft über die Default-Map als
  Write-Back mit Write-Allocate.
- **Heap:** so **nicht tragfähig** (Befund 2). Bei D2 muss die Sektion im Linkerskript
  vor `.heap` stehen, damit `end` dahinter rutscht.
- **Empfehlung:** eine Sektion `.axi_bss (NOLOAD)` in `SRAM`, hinter der `.data`-LMA.
  Das ELF-Prüfskript sollte dafür Sektionen und Symbole lesen und prüfen:
  - Die Sektion beginnt hinter dem Image-Ende und endet spätestens bei `0x24078000`.
  - `end` und der Heap liegen nicht darin.
  - `synth_storage` und `lut_sine` liegen in der erwarteten Region.
  - Das gilt **für jedes Image** (Standard, MOD, COMPOSED torus/ambient). Die
    `Synth`-Größen unterscheiden sich; SDRAM als Ausweichoption per Schalter behalten.
- **Nebeneffekt:** Das `memset` über 96 KiB Delay-Lines in `String::Init` beim
  Modellwechsel wird schneller. Kein Ziel, aber gerade dort entstehen die gemessenen
  Fristüberschreitungen.

### 4. Welche Cache-Abhängigkeiten fehlen im Plan?

- **Schlüssel über das Bitmuster vergleichen, nicht mit `==`.** Sonst gelten −0 und +0
  als gleich, und NaN führt nie zu einem Treffer.
- **Resonator:**
  - Schlüssel: `frequency_`, `structure_`, `brightness_` (der bereits quadrierte Wert
    aus `Part`), `damping_`, `resolution_`. Zusätzlich `num_modes` zwischenspeichern.
  - Die Rechnung ist eine reine Funktion dieser Werte und konstanter LUTs, das Ergebnis
    also bitidentisch. Invalidierung in `Init()`.
  - In die Frequenz fließen `bend_` (ohne Snap in `ParameterManager::Advance()`, ändert
    sich also noch einige Blöcke lang), `voice_fm_` (MOD-Build, Poly AT) und Potis
    oberhalb der `PotFilter`-Hysterese ein.
  - Unberührte Potis bleiben exakt stabil; Treffer sind plausibel, auf der Hardware
    NICHT GEMESSEN.
- **FM:** Statt zweier Varianten reicht **ein Speicher in `Part`, der nach dem
  Damping-Wert fragt.** Bei gleichem Damping wird einmal gerechnet, bei
  unterschiedlichem Damping pro Stimme (MOD) automatisch pro Stimme. So wird nichts
  fest gleichgesetzt.
- **String, versteckter Zustand:**
  - `DampingFilter::Configure` rechnet die Schrittweite aus dem aktuellen
    Filterzustand. Nach 12 Schritten liegt dieser nicht exakt auf dem Ziel; diese Werte
    dürfen **nie** zwischengespeichert werden.
  - Die `ParameterInterpolator` schreiben ihre `previous_`-Werte fort.
  - `set_frequency(f, glide)` lässt die Saitenfrequenz pro Block gleiten.
  - `lfo_[i].Next()` muss pro Block aufgerufen werden.
- **`Random` ist globaler Zustand in stmlib**, geteilt von allen Saiten und dem
  Plucker. Wird ein `GetFloat()`-Aufruf übersprungen oder verschoben, ändert sich die
  gesamte folgende Ausgabe.
- **Modellwechsel:** `ConfigureResonators()` ruft `Init()` auf; alle Caches müssen dort
  invalidiert werden.

### 5. Reihenfolge, Umfang und Abnahmekriterien

Vorgeschlagene Reihenfolge:

0. **Zuerst ein Messinstrument:** Profiling-Build `PM_PROFILE=1` mit DWT-Zyklenzähler
   pro Abschnitt (Stimmen pro Modell, `ComputeFilters`, Reverb, Mischen/Ausgang),
   ausgelesen über den vorhandenen Telemetriekanal. Dazu ein deterministischer
   Render-Hash, berechnet direkt auf dem Gerät (ARM-Golden). Gewinne von etwa einem
   Prozentpunkt gehen sonst im geglätteten Mittelwert unter.
1. **Bitidentische Codeänderungen:** f2lz-Fast-Path, Division herausziehen, lokale
   Kopien in `String`, FM-Speicher, Resonator-Cache.
   - Abnahme: Host-Golden für Standard- und MOD-Build unverändert, ARM-Hash
     unverändert, Sanitizer grün.
   - Klanggleichheit ist damit belegt; die Hörprüfung dient dann der Stabilität.
   - Weil der Profiler die Abschnitte trennt, können diese Änderungen in **einem**
     Flash landen. Insgesamt etwa 4 Flashes statt 7 oder mehr.
2. **Speicher:** AXI-SRAM. Ausgabe sollte bitidentisch sein; Abnahme über den ARM-Hash.
3. **Compilerflags:** Schleifenflag zuletzt. **LTO streichen:** Die heißen Schleifen
   liegen bereits jeweils in einer Übersetzungseinheit, Aufrufe über Dateigrenzen gibt
   es nur einmal pro Block. Zusätzliches Inlining über Dateigrenzen kann aber die
   FMA-Zusammenfassung und damit die Bits verändern.

Zusätzlich zur Abnahme:

- Pro Modell auch **Callback-Spitzenwert und Fristüberschreitungen** auswerten, nicht
  nur den geglätteten Mittelwert.
- Caches ausdrücklich im Szenario mit bewegten Reglern messen. Dort darf die Variante
  nicht langsamer sein als die Referenz, denn die Schlüsselprüfung kostet auch etwas.
- Standard- und MOD-Image messen.

Erwartung: Alle Kandidaten zusammen bringen geschätzt wenige Prozentpunkte. Für mehr
Stimmen reicht das voraussichtlich nicht.

## Nicht geprüft

- Mischpfad `Synth::Render` und Reverb im Assembly.
- MOD- und COMPOSED-Images (Map und Größen nur für das Standard-Image ausgewertet).
- Alle CPU-Wirkungen: NICHT GEMESSEN.
