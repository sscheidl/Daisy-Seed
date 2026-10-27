# Hardwaremessung Performance-Optimierungen — 27.10.2026

**CPU-LAST GEMESSEN (27.10.2026) — HÖRPRÜFUNG NICHT GEMESSEN.**

A/B-Vergleich der Optimierungen aus PR #63 ([Umsetzung](PERFORMANCE_IMPLEMENTATION.md),
[Zweitprüfung](PERFORMANCE_REVIEW.md)) auf dem Seed3 Desktop Kit. Der Mensch hat
beide Images geflasht. Auf seinen Wunsch hat der Agent das Kit über USB-MIDI (J7)
angesteuert und gleichzeitig die CPU-Telemetrie der Firmware gelesen. Der Agent hat
nicht geflasht.

## Images

Beide Images stammen von `main` `aaadb78` (MOD-Build, SDRAM, ohne Profiling) und
wurden vor dem Test lokal bitgleich nachgebaut und mit `scripts/check-firmware.py`
geprüft (v0.1.1, BOOT_SRAM).

| Lauf | Image | Build | SHA256 | Start (UTC) |
| --- | --- | --- | --- | --- |
| A | Referenz, bisheriger Code | `make MOD=1 PM_OPTIMIZE=0` | `bc0f28df696def167a1de6c0431a410878ae316ad3f3624632174cbeff7d088d` | 20:07:16 |
| B | Optimiert (Standard) | `make MOD=1` | `1b24f9a88e26403c0e7ec6ce5846ea8dd884ab2db68e2aeb4617e6c4e9658246` | 19:56:11 |

B wurde zuerst gemessen, A unmittelbar danach unter denselben Bedingungen.

## Ablauf

Die Testroutine (`pm_ab_test.py`, nur lokal im Hardware-Testordner) liest dieselbe
Telemetrie wie [`tools/cpu_probe.py`](../tools/cpu_probe.py): Kanal 16, CC119. Sie
geht die sechs Modelle über CC28 durch. Die Mod-on-Daisy-Matrix war leer, an den
Reglern wurde während der Läufe nichts verändert.

| Phase | Dauer | Inhalt |
| --- | ---: | --- |
| wechsel | 1 s | Zähler unmittelbar vor dem Modellwechsel zurückgesetzt |
| ruhig | 15 s | Vierklang alle 2 s, Regler unverändert |
| regler2 | 15 s | Vierklänge; Structure und Damping mit je 25 Hz bewegt |
| regler8 | 15 s | Vierklänge; acht Regler (CC20–27) mit je 25 Hz bewegt, dazu Pitch Bend |
| stress | 30 s | wie am 22.10.: vier Noten alle 10 ms, CC22/23/25/26/27/31/32/33 = 127 |

Ab Phase „ruhig“ werden die Zähler jeweils nach 1 s Einschwingen zurückgesetzt.
Modellwechselspitzen fließen deshalb nicht in die übrigen Phasen ein. Angegeben
sind der Durchschnitt (Ø) und die höchste Blocklast (Spitze) in Prozent der
1-ms-Frist, dazu die Fristüberschreitungen (Blöcke über 1 ms) pro Phase.

## Ergebnisse (GEMESSEN 27.10.2026)

| Modell | Phase | Ø A | Ø B | Δ Ø | Spitze A | Spitze B | Δ Spitze | Über A | Über B |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Modal Resonator | wechsel | 33,60 | 29,23 | −4,37 | 40,44 | 40,60 | +0,16 | 0 | 0 |
| Modal Resonator | ruhig | 33,87 | 30,05 | −3,82 | 35,16 | 31,57 | −3,59 | 0 | 0 |
| Modal Resonator | regler2 | 33,84 | 34,19 | +0,35 | 35,00 | 35,31 | +0,31 | 0 | 0 |
| Modal Resonator | regler8 | 33,56 | 33,89 | +0,33 | 35,03 | 35,51 | +0,48 | 0 | 0 |
| Modal Resonator | stress | 34,00 | 30,28 | −3,72 | 35,58 | 32,84 | −2,74 | 0 | 0 |
| Sympathetic Strings | wechsel | 42,09 | 41,66 | −0,43 | 136,76 | 134,89 | −1,87 | 1 | 1 |
| Sympathetic Strings | ruhig | 44,00 | 43,18 | −0,82 | 49,63 | 51,55 | +1,92 | 0 | 0 |
| Sympathetic Strings | regler2 | 44,27 | 43,53 | −0,74 | 52,63 | 51,79 | −0,84 | 0 | 0 |
| Sympathetic Strings | regler8 | 43,53 | 42,47 | −1,06 | 50,59 | 50,48 | −0,11 | 0 | 0 |
| Sympathetic Strings | stress | 43,58 | 42,88 | −0,70 | 56,49 | 55,22 | −1,27 | 0 | 0 |
| Inharmonic Strings | wechsel | 39,86 | 38,59 | −1,27 | 128,59 | 127,49 | −1,10 | 1 | 1 |
| Inharmonic Strings | ruhig | 40,44 | 38,85 | −1,59 | 44,93 | 45,46 | +0,53 | 0 | 0 |
| Inharmonic Strings | regler2 | 40,49 | 39,10 | −1,39 | 45,16 | 43,45 | −1,71 | 0 | 0 |
| Inharmonic Strings | regler8 | 40,25 | 38,93 | −1,32 | 44,86 | 43,11 | −1,75 | 0 | 0 |
| Inharmonic Strings | stress | 40,55 | 39,27 | −1,28 | 50,44 | 49,17 | −1,27 | 0 | 0 |
| FM Voice | wechsel | 37,02 | 32,11 | −4,91 | 43,88 | 40,55 | −3,33 | 0 | 0 |
| FM Voice | ruhig | 36,53 | 31,50 | −5,03 | 39,41 | 34,44 | −4,97 | 0 | 0 |
| FM Voice | regler2 | 37,12 | 33,02 | −4,10 | 39,47 | 35,76 | −3,71 | 0 | 0 |
| FM Voice | regler8 | 36,58 | 32,39 | −4,19 | 40,00 | 35,31 | −4,69 | 0 | 0 |
| FM Voice | stress | 37,40 | 32,54 | −4,86 | 40,13 | 34,95 | −5,18 | 0 | 0 |
| Western Chords | wechsel | 43,90 | 43,13 | −0,77 | 140,36 | 137,79 | −2,57 | 1 | 1 |
| Western Chords | ruhig | 43,31 | 42,87 | −0,44 | 47,95 | 50,16 | +2,21 | 0 | 0 |
| Western Chords | regler2 | 43,50 | 42,69 | −0,81 | 47,67 | 48,32 | +0,65 | 0 | 0 |
| Western Chords | regler8 | 42,64 | 41,79 | −0,85 | 48,27 | 48,47 | +0,20 | 0 | 0 |
| Western Chords | stress | 43,12 | 42,46 | −0,66 | 56,72 | 55,41 | −1,31 | 0 | 0 |
| String & Reverb | wechsel | 40,40 | 38,92 | −1,48 | 131,55 | 131,22 | −0,33 | 1 | 1 |
| String & Reverb | ruhig | 40,60 | 39,20 | −1,40 | 45,58 | 44,12 | −1,46 | 0 | 0 |
| String & Reverb | regler2 | 40,70 | 39,32 | −1,38 | 45,18 | 43,13 | −2,05 | 0 | 0 |
| String & Reverb | regler8 | 40,50 | 39,38 | −1,12 | 45,10 | 42,74 | −2,36 | 0 | 0 |
| String & Reverb | stress | 40,93 | 39,65 | −1,28 | 53,95 | 48,18 | −5,77 | 0 | 0 |

Je Phase rund 15 000 Blöcke, in der Stressphase rund 30 000, beim Modellwechsel
rund 1 000.

## Bewertung

- **Keine Fristüberschreitung beim Spielen:** Weder A noch B überschreitet in den
  Phasen ruhig, regler2, regler8 oder stress die Frist.
- **FM Voice** wird um 4,1–5,0 Punkte im Durchschnitt leichter. Das ist mehr als die
  geschätzten rund 2 % aus der Zweitprüfung.
- **Modal Resonator** wird bei unveränderten Reglern um 3,7–3,8 Punkte leichter.
  Mit bewegten Reglern sinkt die Last nicht (+0,3 bis +0,5 Punkte). Der Filtercache
  greift dort nicht, weil sich Structure und Damping in jedem Block ändern. Das liegt
  auf dem Niveau der Referenz und ist kein Rückschritt.
- **Inharmonic Strings und String & Reverb** (die Modelle mit Dispersion) werden um
  1,1–1,6 Punkte leichter.
- **Sympathetic Strings und Western Chords** haben die höchste Last und werden nur
  um 0,4–1,1 Punkte leichter. Ihre Spitzen bleiben bei rund 55 % im Stresstest.
- **Modellwechsel:** In die vier Saitenmodelle tritt bei A und B jeweils genau eine
  Fristüberschreitung auf, mit einer Spitze von 127–140 %. Das ist gegenüber der
  Referenz unverändert und laut Vorgabe kein Optimierungsziel.
- **Messrauschen:** Jede Phase wurde einmal gemessen. Spitzen sind Einzelblöcke;
  Abweichungen von etwa 1–2 Punkten sind nicht belastbar, etwa die höhere „ruhig“-Spitze
  von B bei Sympathetic Strings (+1,92) und Western Chords (+2,21).

Der Vergleich mit dem 22.10. ist nur eingeschränkt möglich: Damals wurde das
Standard-Image v0.1.0 ohne MOD gemessen. Belastbar ist der A/B-Vergleich innerhalb
dieses Protokolls.

## Nicht gemessen

- **Hörprüfung:** Knackser, Verzerrungen, Aussetzer und Klanggleichheit von A und B
  wurden nicht berichtet. NICHT GEMESSEN.
- **ARM-Bitgleichheit:** Die Images mit `PM_RENDER_HASH=1` wurden nicht ausgeführt.
  NICHT GEMESSEN.
- **Anteil der einzelnen Abschnitte**, etwa des Reverbs: Dafür ist ein Profiling-Image
  (`PM_PROFILE=1`) nötig. NICHT GEMESSEN.
- **MOD-Pfad pro Stimme** (Poly AT bei belegter Matrix), **AXI-Speicher** und
  `PM_LOOP_INVARIANTS=1`. NICHT GEMESSEN.

Nach dem Test war Image A (Referenz) auf dem Kit.
