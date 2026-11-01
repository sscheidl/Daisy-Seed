# Hardware test sheet: program manager

**All items: NICHT GEMESSEN (status 2026-11-01).** Every step needs the human (bootloader
mode, flashing, observation). Record results as `GEMESSEN (date, image SHA-256, observation)`;
never fill in expectations.

## Prerequisites

* Installed Daisy bootloader (diagnostics report `bl=v6_1`); **no** `program-boot`.
* Images and SHA-256 from [SOFTWARE_LOG.md](SOFTWARE_LOG.md) / the PR text. Flash path as
  before: update mode (S9+S16 5 s / editor / DDP `BOOT`), then
  `make PROJECT=program-manager-probe program-dfu` or the Web Programmer "Custom" with the BIN
  to `0x90040000`.
* **Recovery:** update mode again (with a hanging manager: reset, then BOOT within ~2 s,
  `docs/FLASHING.md`) and flash a known instrument BIN or the manager again. Last resort: ROM
  DFU on the module port.
* Insert a card only after `python tools/pm_card.py check <template>` (root without `.bin`).

## P – probe (`DaisyProgramManagerProbe-v0.1.0.bin`)

| ID | Step | Expectation (to be observed, not assumed) | Result |
| --- | --- | --- | --- |
| P1 | Flash the probe to `0x90040000`, reset | after ~2 s: LED 16 dimmed, USR LED blinks slowly | NICHT GEMESSEN |
| P2 | `python tools/pm_cli.py -p COMx info` | `app=DaisyProgramManagerProbe profile=probe cat=UNSUPPORTED` | NICHT GEMESSEN |
| P3 | Run for 5 minutes, repeated `status` | stable, no hangs (XIP at 480 MHz) | NICHT GEMESSEN |
| P4 | Press/release key 16; S1+S16 for 3 s | DIAG (LEDs follow keys), back to SELECT | NICHT GEMESSEN |
| P5 | `pm_cli.py mode DIAG`, `raw "LED m=ON"`, `raw "CVOUT m=FIX c1=2048"`, `mode SELECT` | LEDs on; CV OUT with a multimeter: note the value; after SELECT DAC 0 – measure the voltage | NICHT GEMESSEN |
| P6 | S9+S16 5 s or `pm_cli.py service --yes` | DFU `0483:DF11` appears; then flash an instrument BIN (recovery rehearsed) | NICHT GEMESSEN |

## M – manager (`DaisyProgramManager-v0.1.0.bin`)

| ID | Step | Expectation | Result |
| --- | --- | --- | --- |
| M1 | Flash the manager, start without a card | `info`: `cat=NOCARD sd=0`; only LED 16 dimmed | NICHT GEMESSEN |
| M2 | Insert a card with the template (`pm_card.py check` PASS, FAT32) | after ~0.3 s `cat=OK`, slots dimmed; `list` shows the entries; `rootbin=0` | NICHT GEMESSEN |
| M3 | exFAT card | the bootloader still starts the manager; `cat=OK` with `fs=EXFAT` (SDTEST in DIAG) | NICHT GEMESSEN |
| M4 | First start: key 1 (Toy Piano v0.4 package) | LED bar, then reset, briefly DFU, Toy Piano identifies itself (MIDI IDENTIFY); key test | NICHT GEMESSEN |
| M5 | `pm_cli.py start --slot 1 --id toy-piano --hash <prefix> --gen <gen>` | `ACK`, `pct` rises, `RESTARTING`, port gone | NICHT GEMESSEN |
| M6 | Return from Toy Piano: S1+S16 3 s | manager starts (after the 100 ms window), `info` answers | NICHT GEMESSEN |
| M7 | Pull the card during `LOADING` | `uerr=NOCARD`, manager usable, no start | NICHT GEMESSEN |
| M8 | Package with one changed byte (hash error) | `e=HASH`, no start; afterwards a valid package starts | NICHT GEMESSEN |
| M9 | Five changes in a row (Toy Piano ↔ Reverb) | start and return every time | NICHT GEMESSEN |
| M10 | CV OUT with a multimeter: SELECT → DIAG FIX 2048 → SELECT → program start → return | note the voltages per state; assume DAC code 0 ≠ 0 V | NICHT GEMESSEN |
| M11 | Power loss during `LOADING` | next start: manager (no flash changed) | NICHT GEMESSEN |
| M12 | Abort a DFU update of the manager (unplug), then flash again | recovery succeeds | NICHT GEMESSEN |

## Log

| Date | Image / SHA-256 | Step | Observation |
| --- | --- | --- | --- |
| – | – | – | – |
