# Hardware test session 2026-11-04: Nebulae 0.2.1, Looper 0.5.1, control panel 0.6.0

> **Status 2026-11-06: historical.** This sheet tests Looper 0.5.1. `main` has moved on to Looper
> 0.6.0; for the current steps see
> [the Looper hardware test](../instruments/looper/docs/HARDWARE_TEST.md).

A run sheet for one day at the kit. It puts the SD card sharing tests of both instruments
into one order and points to the detailed per-product checklists. **The human does every
hardware step** (flash, keys, pots, cables, cards, listening). The agent flashes nothing
and fills in no results. Write each result as **MEASURED 2026-11-04** with what you saw or
heard, or **NOT MEASURED**. Never write the expected value as the result.

Candidates are the images on `main` @ `bf08bb3` (PR #78). The open PR #94 (Looper 0.5.0
with writable exFAT) is **not** part of this session (see
[the handover](HANDOVER_2026-11-04_SD.md)).

## 0. What is tested

| Item | File | Bytes | SHA-256 |
| --- | --- | --- | --- |
| Nebulae on Daisy 0.2.1 | `NebulaeOnDaisy-v0.2.1.bin` | 183,740 | `cbc4c37686be32eca39825459f95c676af7a7010c3c719faba1cf0def4e381ff` |
| Looper 0.5.1 | `Looper-v0.5.1.bin` | 186,324 | `d4e3b1b320b485c9e7b38cc135e053bb3182d04a9a259fbeaec0dd52d1afd3d1` |
| Looper MOD 0.5.1 | `LooperMod-v0.5.1.bin` | 220,020 | `22d8c8839e5a527507cabbf931f8c3084c068c02c31288c0ab4b0ba1c5e2b4bd` |
| Control panel 0.6.0 (from `main`) | `DaisyDiagnostic.exe` | - | `39510a840d3ac219644418d0a998724b5f249b52eccc2bc7e4e3d62c83f4ac21` (local Windows build of the PR #78 integration) |

**Use the panel built from `main`.** The 0.6.0 EXE of PR #94 (SHA-256 `38539a6b…d8a6`,
CI run 41714425126) speaks PR #94's Looper protocol (app `Looper`, 12-byte STATUS). It
does **not** find Looper 0.5.1 (app `LooperOnDaisy`, 13-byte STATUS). Without the
`39510a84…` EXE, run the panel from source on Windows: `cd diagnostics`,
`python -m pip install -r windows/requirements.txt`, `windows\run.cmd`. A CI build would
need a manual "Hardware diagnostics" run on `main`, which is not dispatched without your
request (Actions budget).

Software checks before the session, **MEASURED 2026-11-03** (Linux, pinned xPack GCC
13.2.1-1.1, libDaisy `f715e48`, DaisySP `2c72eaf`, clean worktree of `bf08bb3`):

* All three images rebuilt **bit-identically** to the hashes above (the Codex Windows
  build). `scripts/check-firmware.py` PASS for all three (BOOT_SRAM vectors, versions
  0.2.1 / 0.5.1 / 0.5.1).
* `make PROJECT=looper test` and `make PROJECT=granular test` exit 0. The Looper golden
  stays `2A2DBCEC41A54F57`, regular = MOD.
* `pytest diagnostics/tests/python`: 334 passed.
* `arm-none-eabi-nm` on the Looper ELF: no `f_mkfs`, `f_fdisk` or `f_setlabel`. The
  Looper cannot format a card, so the exFAT negative test in B6 cannot erase it.

These are software results, not hardware results.

## 1. Preparation (evening before or first 30 min)

Hardware and PC:

- [ ] Seed3 Desktop Kit with the **external Daisy bootloader v6.x already installed**.
      Do not reinstall it (`program-boot` is not part of this session).
- [ ] USB-C data cable to **J7**. Headphones or monitors at low level. A line source
      (phone, synth) for the Looper's left input. DAW or MIDI monitor.
- [ ] Windows PC, Chrome or Edge for the [Daisy Web Programmer](https://flash.daisy.audio/),
      control panel 0.6.0 from `main` (section 0).
- [ ] Check the three BINs against the SHA-256 table (PowerShell:
      `Get-FileHash <file> -Algorithm SHA256`).

Cards. Test cards only: back up anything on them first. No format or repair in the kit;
do repairs on the PC.

- [ ] **Card F (FAT32, at most 32 GB):** used for both instruments.
- [ ] **Card X (exFAT, more than 32 GB):** used for Nebulae, and as the negative test
      for Looper 0.5.1.
- [ ] On card F and card X (on the PC), for Nebulae:

  ```text
  NEBULAE/SAMPLES/A Tone 48k stereo.wav     (L and R different, PCM16)
  NEBULAE/SAMPLES/b Tone 44k1 mono.wav
  NEBULAE/SAMPLES/c 24bit.wav               (must be rejected: 2 pulses)
  NEBULAE/SAMPLES/Drones/2 Low.wav
  NEBULAE/SAMPLES/Drones/10 Air.wav         (order: 2 before 10)
  NEBULAE/SAMPLES/Empty/note.txt            (no WAV: no bank)
  ```

- [ ] On card F only, for the Looper: `looper/loops/` with a few WAVs (48 kHz 16/24-bit
      or float, mono and stereo; one longer than 87 s; one at 96 kHz; the same short loop
      at 48 kHz and at 44.1 kHz; one 44.1 kHz file of about 80 s).
- [ ] A few spare WAVs on the PC for copying during the share tests, plus one large file
      (more than 200 MB, any type) for the interruption tests.

Flashing, every image the same way ([FLASHING.md](FLASHING.md)):

1. Running firmware into update mode: **S9 + S16 alone for 5 s** (all LEDs on). In
   Nebulae this works only in NEBULAE mode (F7). In the Looper, a running card write
   finishes first. If that does not work: RESET, then BOOT within about 2 s.
2. In the Web Programmer choose Custom, select the BIN and program it. The kit restarts
   into the new image.
3. Record: image, SHA-256, time.

Stop rules: if the kit hangs, power-cycle (power-on always starts the instrument). If an
image does not start at all, use the RESET/BOOT recovery from FLASHING.md and flash the
previous image. If a card looks damaged, stop that card's tests, check it on the PC and
note it.

## 2. Block A: Nebulae on Daisy 0.2.1 (about 2 h)

Detailed steps: [instruments/granular/docs/HARDWARE_TEST_PLAN.md](../instruments/granular/docs/HARDWARE_TEST_PLAN.md)
(written for 0.2.0, in German; 0.2.1 behaves the same, the panel shows `0.2.1`).
Background: [V0_2_ARCHITECTURE.md](../instruments/granular/docs/V0_2_ARCHITECTURE.md),
[panel profile](../diagnostics/docs/NEBULAE_PROFILE.md).

- [ ] **A1 Flash and boot.** Windows shows a **USB-MIDI** device and **no** COM port.
- [ ] **A2 Basics, without a card, then card F.**
  - SW18 up/middle/down = NEBULAE/SAMPLE/KEYBOARD. Note the lever direction.
  - Insert card F: bank 1 scanned, "A Tone" loaded, L/R separate, the 44.1 kHz file has
    the correct pitch, "c 24bit" gives 2 pulses and the old sound stays.
  - Eight pots, SOURCE short/long, FILE, RESET, FREEZE, RECORD/overdub: as v0.1.1, no
    dropouts.
- [ ] **A3 Banks and SW17** (SAMPLE mode).
  - **Note which SW17 lever position shows banks.**
  - Bank view: active bank bright, other present banks dim, missing banks off. S2 selects
    "Drones": the sound keeps running, because choosing a bank loads nothing.
  - SW17 back: S1 = "2 Low", S2 = "10 Air".
  - NEBULAE: FILE steps only through the selected bank.
  - Pull card: live audio continues. Re-insert: rescan.
- [ ] **A4 MIDI and F7.**
  - Notes, pitch bend and CC20–27 work in all modes.
  - KEYBOARD: S9 + S16 for 6 s causes **no** restart.
  - NEBULAE: S9 + S16 for 5 s enters the bootloader. Then RESET.
- [ ] **A5 CPU and D16** (F3).
  - DENSITY max, OVERLAP max, BLEND half to full, live recording running, for 2 min.
  - D16 lights only after an overrun and stays lit until restart.
  - In the panel's **SD share** tab, note the callback peak % and the overrun count.
    These are firmware counters, not bench measurements.
- [ ] **A6 Share card F with Windows.**
  1. Panel: "Performance", firmware `0.2.1`, bank/slot shown.
  2. Record a short live loop. **Share** shows the loss warning. **Cancel**: Nebulae
     keeps playing.
  3. Share and confirm: the fade has no click (note), held DAW notes end, all LEDs go off
     briefly, then the restart. Then the panel shows "SD management", D16 blinks slowly,
     D2 is lit and Windows shows a drive (note the drive letter, the device name
     "Nebulae on Daisy SD" and any driver install). Note the **time from click to drive**.
  4. In Explorer: create `NEBULAE/SAMPLES/New`, copy two WAVs, rename one file, delete
     one. Note the copy speed Windows shows for the large file.
  5. **Return** is disabled while Windows has the card.
  6. Eject in Windows: the panel says "ejected" and D4 lights. **Return to Nebulae**:
     restart into performance, bank "New" exists, the previous bank/slot is loaded and
     the files sound right.
  7. Without the panel: share, eject in Windows, close the panel, hold **S4 alone for
     2 s**: back to performance.
- [ ] **A7 Failure cases** (card F, test data only).
  1. Shared, without ejecting: unplug USB and plug it back. The kit comes back in
     **performance** (power cycle), and the card reads normally.
  2. Shared, panel "Disconnect", then reconnect: still SHARED (no automatic return).
  3. Shared, pull the **card** during a copy: INTERRUPTED, D5 blinks, and the panel
     demands **Acknowledge**. Then Return. It is never shown as "ejected".
  4. Share without a card gives "no microSD card". Share while RECORD runs gives
     "recording".
  5. If MIDI comes back instead of a drive after a share, the panel says "did not enter
     SD management". **This is risk R1** (the bootloader clears backup SRAM): note it
     with the bootloader version from the USB tab, then skip the remaining share tests.
- [ ] **A8 exFAT, card X.** Repeat A2 (card part), A3 (order) and A6 steps 1, 3, 4 and 6
      with card X. Optional: long names and umlauts created on Windows and macOS.

## 3. Block B: Looper 0.5.1 (about 2.5 h)

Detailed steps: [instruments/looper/docs/HARDWARE_TEST.md](../instruments/looper/docs/HARDWARE_TEST.md)
(file names there still say `0.5.0`; use the `0.5.1` files). Behaviour and protocol:
[SD_MANAGEMENT.md](../instruments/looper/docs/SD_MANAGEMENT.md); panel:
[LOOPER_PROFILE.md](../diagnostics/docs/LOOPER_PROFILE.md).
**The Looper needs card F (FAT32) for its own files.**

Starting pots: 0 at about 40 %, 1 left, 2 centre, 3 left, 4 fully right, 5 and 6 left,
7 at about 30 %.

- [ ] **B1 Flash and boot** with card F. Note the time to audible input. D16 stays off.
- [ ] **B2 Regression short pass:** HARDWARE_TEST sections 2, 3 and 4. Keys and LEDs,
      record/play/undo, seam modes, speed/reverse. If time is short, skip sections 5–6.
- [ ] **B3 Card (section 7).**
  - The S8 LED is dim with a card.
  - About 2 s after a change the S8 LED blinks while `loop.wav` is written. **Note the
    save time of a short loop and of a full 87 s loop.** No dropout, D16 off.
  - Power-cycle: the loop comes back.
  - S8 imports `looper/loops/` in name order. The 96 kHz and the over-long file are
    rejected. The 44.1 kHz twin sounds and runs like the 48 kHz one.
  - Pull the card while saving: no crash. Re-insert: the next change is saved.
- [ ] **B4 MIDI (section 8)**, only CC102–105 and CC20, plus one editor bootloader
      request.
- [ ] **B5 Share card F with Windows (section 10).** Copy the card's `looper` folder to
      the PC first.
  1. Panel **SD share**: Looper found, firmware `0.5.1`, loop length, "saved in
     looper/loop.wav".
  2. Overdub a short layer, then **Share** at once. The panel shows "Saving the loop
     before sharing …" and the S8 LED blinks, then the restart. **Note the time from
     click to restart** (and the loop length).
  3. In SD management: D1 then D2 lit, D16 and the Seed LED blink, no audio. Windows
     shows a drive "Looper SD" (note the letter and any driver install).
  4. The drive has `looper/loop.wav` (new time) and `looper/loops/`. Copy two WAVs
     (48 kHz and 44.1 kHz) into `looper/loops/`.
  5. While SHARED, **Return** is disabled and S4 does nothing.
  6. Eject in Windows: D4 lights. **Return to the Looper**: the overdubbed loop plays,
     and S8 imports the copied files at the right pitch.
  7. Without the panel: share, eject, then **S4 alone for 2 s**: back to performance.
  8. Interruption: share, then pull USB while Windows copies the large file. Plug it
     back: INTERRUPTED/ERROR, D5 blinks, S4 does nothing. Acknowledge, return. Note what
     the Looper does with `loop.wav`.
  9. While recording, Share is refused (BUSY). Without a card: "no card".
  10. Power-cycle while in SD management: it starts in performance.
  11. R1 as in A7.5.
- [ ] **B6 New in 0.5.1: card the Looper cannot use.** Insert **card X (exFAT)**. The
      Looper's FatFs R0.12c is FAT32 only.
  - Boot: note the S8 LED and what the panel shows (card present, not saved, fault).
  - Record a loop and press Share. Expected per the 0.5.1 fix: no endless "Saving …".
    The panel asks for confirmation to share **without** the unsaved changes. **Cancel**
    keeps the loop playing.
  - Share again and confirm: SD management. Windows sees the exFAT card normally, since
    MSC passes raw blocks. Eject, return: the Looper starts empty (the RAM loop is gone,
    as confirmed).
  - Put card F back in: saving works again.
  - Check card X on the PC afterwards: the Nebulae folders are unchanged.

## 4. Block C: Looper MOD 0.5.1 (about 30 min)

- [ ] Flash `LooperMod-v0.5.1.bin`. Short pass of HARDWARE_TEST sections 2–3: same
      behaviour as the regular image, D16 off.
- [ ] `python modules/mod-on-daisy/tools/mod_probe.py identify` shows instrument
      `looper`, version `0.5.1`. Optional: one modulation from section 9.
- [ ] Repeat B5 steps 1–3 and 6 (share, eject, return) once. Afterwards the Mod on Daisy
      editor connects again.

## 5. Block D: wrap-up (about 15 min)

- [ ] Panel: **Save report** (JSON) once in performance and once in SD management, for
      each instrument.
- [ ] Note the Windows version, card brands and sizes, cable, panel EXE hash and which
      image is left on the kit.
- [ ] Hand the filled-in table below to the next chat. It records the results in
      `instruments/granular/docs/VALIDATION.md`, `instruments/looper/docs/VALIDATION.md`
      and `docs/SD_CARD_INTEGRATION_2026-11-03.md`. The agent never fills in results
      that were not reported.

## Results

| Step | Result (MEASURED 2026-11-04 + observation / NOT MEASURED) | Times, LEDs, notes |
| --- | --- | --- |
| A1 flash/boot Nebulae | NOT MEASURED | |
| A2 basics | NOT MEASURED | SW18 lever direction: |
| A3 banks/SW17 | NOT MEASURED | SW17 bank position: |
| A4 MIDI/F7 | NOT MEASURED | |
| A5 CPU/D16 | NOT MEASURED | peak %, overruns: |
| A6 share FAT32 | NOT MEASURED | click to drive: s, copy MB/s: , click at fade: |
| A7 failure cases | NOT MEASURED | R1 seen? |
| A8 exFAT | NOT MEASURED | |
| B1 flash/boot Looper | NOT MEASURED | boot to audio: s |
| B2 regression | NOT MEASURED | |
| B3 card | NOT MEASURED | save short: s, save 87 s: s |
| B4 MIDI | NOT MEASURED | |
| B5 share | NOT MEASURED | click to restart: s (loop length: s) |
| B6 exFAT card (unusable) | NOT MEASURED | |
| C MOD | NOT MEASURED | |
| D reports | NOT MEASURED | |
