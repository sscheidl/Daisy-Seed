# Mod on Daisy — Assignable destinations (design proposal)

Status: **PROPOSAL, nothing implemented.** As of 01.11.2026.
Owner decision (01.11.2026): the matrix gets **freely assignable destinations per column**,
like the mod slots of many synthesizers, for all instruments. This replaces decision D4
("8 columns, instrument-specific", [DECISIONS.md](DECISIONS.md)) once implemented.

**Owner decisions on the open points (01.11.2026):**

1. **Pilot: Streichwurst** (newest code, catalog from the parameter contract), then
   **Toy Piano** in any case, as the test and reference instrument of the matrix.
2. **VR1–VR8 always come first** in every catalog and are the default slots, including
   pots that hardly modulate. Further destinations follow after them.
3. **Curated catalogs:** not every parameter becomes a destination. The extra destinations
   are chosen per firmware, case by case (§ 5).
4. **No migration of old patches:** no stored or shared patches depend on today's column
   meaning, so `legacy_slots` and the mapping of old files are dropped (§ 3.3).

## 1. Why

Users read the eight columns as the eight pots VR1–VR8. Today every firmware fixes its
own eight columns, and the editor cannot show which pot a column belongs to:

| Firmware | Columns 1–8 (pot of the same function) | Matches VR1–VR8 |
| --- | --- | --- |
| Physical Modeling | Pitch (VR1), Structure (VR2), Brightness (VR3), Damping (VR4), Position (VR5), Excitation (VR6), Level (–), Dry/Wet (VR8) | 7 of 8 |
| Looper | Speed (VR1), Start (VR4), Size (VR5), Slice (VR6), Skip (VR7), Inertia (VR2), Fripp Decay (VR8), Mix (–) | 1 of 8 |
| Streichwurst | Registration (VR1), Animate (VR5), Phaser (VR6), Reverb (VR7), Balance (VR4), Solo Tone (Solo VR1), Tremolo (Solo VR4), Pitch (–) | 1 of 8 |
| Toy Piano v0.3 | Pitch, Level, Harmonic (–), Attack (VR1), Decay (VR2), free, free, free | 0 of 8 |
| Toy Piano Ambient | Pitch (VR1), Level, Harmonic, Attack (–), Decay (VR2), HallDecay (VR3), HallMix (VR6), HallX (VR7) | 2 of 8 |
| Toy Piano Tape | Pitch, Level, Harmonic, Attack, Decay (all –), free × 3 | 0 of 8 |

Pots without any column today: Looper VR3; Streichwurst VR2, VR3, VR8; Toy Piano Ambient
VR4, VR5, VR8; Toy Piano Tape VR1–VR8.

## 2. Concept

* The grid stays **8 sources × 8 columns**. A column becomes a **slot**: it holds a
  destination chosen from the instrument's **destination catalog**.
* Each firmware publishes a catalog of everything that can be modulated: the functions of
  its pots on every page plus targets without a pot (Pitch, Level, Mix, …).
* **Default assignment = VR1–VR8** of the main pot page, in pot order. A new or cleared
  patch therefore matches what users expect. A pot whose function cannot be modulated
  sensibly (e.g. a master volume) still gets its slot by default, so the order never shifts;
  the user can reassign it.
* Several slots may use the same destination; their offsets add and are clamped as today.
* The source side (rows, LFOs, Random, CV, MIDI sources) is unchanged.

## 3. Module changes (`modules/mod-on-daisy`, module 0.2.0)

### 3.1 Data model

```cpp
struct DestinationInfo {          // replaces TargetInfo as catalog entry
    const char* key;              // stable, <= 16 ASCII, e.g. "hall_decay" (patch files)
    const char* name;             // column header, <= 12
    const char* span_text;        // as today
    const char* control;          // where it lives on the kit, <= 8, e.g. "VR3", "FX VR2", "" = no pot
    float span; bool per_voice; bool latched;   // as today
};
struct InstrumentInfo {
    ...                                        // key, version, sources, modes as today
    const DestinationInfo* destinations;       // catalog, ids = index, <= kMaxDestinations (48)
    uint8_t destination_count;
    uint8_t default_slots[8];                  // VR1..VR8 of the main page (0xFF = empty)
};
```

* Catalog order: the destinations of VR1–VR8 (main page) first, then the curated extras.
* An instrument without a catalog (`destinations == nullptr`) keeps today's fixed columns
  from `targets[8]`, unchanged. The other firmwares therefore build and sound as before
  until they adopt the catalog in their own PR.

* `Patch` format 3 adds `uint8_t slot[8]` (destination id per column, 0xFF = empty).
* `Program` compiles each connection to its **destination id**; the engine keeps
  `offset_[kMaxDestinations]` and `voice_gain_[kMaxDestinations]` (2 × 48 floats = 384 B;
  DTCM budget to be checked per firmware, Streichwurst is at 93.3 %).
* `Engine::Apply(destination, base)` and `ApplyVoice` take a destination id. The firmware
  calls them for the destinations it implements; a destination no slot uses returns `base`
  bit for bit, so an empty matrix stays sound-identical (golden tests).
* `AllowedCells` is computed per slot from the assigned destination's flags and changes
  when a slot is reassigned. A reassignment keeps the depths of the column; cells that are
  no longer allowed are switched off (and reported).
* Base values for the monitor (`ReportBase`) are reported per destination; the monitor
  sends the base of each slot's destination.

### 3.2 Protocol (SysEx 0x7D, minor version bump, capability bit)

| Code | Command | Request | Reply |
| --- | --- | --- | --- |
| new | `GET_DEST` | `u7` index | index · count · flags · `u14` span · `str` key · `str` name · `str` span text · `str` control |
| new | `GET_SLOTS` | — | 8 × `u7` destination id (0x7F = empty) |
| new | `SET_SLOT` | `u7` column · `u7` destination id | status · active · patch CRC (cells switched off are reported by GET_ALLOWED) |
| changed | `GET_TARGET` | as today | describes the destination currently in the column, so editors without the capability keep working |
| changed | `GET_MATRIX` / `SET_MATRIX` | as today | unchanged payload; slots travel with `GET_SLOTS`/`SET_SLOT` |

* The **schema hash** covers key, catalog and the poly AT rule, not the slot assignment
  (that is patch data and part of the patch CRC).
* Changes go together into firmware, `mod_probe.py`, both editors and their tests
  (repository rule for the protocol).

### 3.3 Compatibility

* No migration (owner decision 4): a patch of format 1/2 sent to an instrument with a
  catalog loads with the default slots; the editor's existing schema check warns as today.
* `.modpatch` files get version 2 with `"slots": ["registration", "animate", …]` by
  destination **key**, so files survive catalog reordering.
* Storage slots (STORAGE.md) need the format-3 layout; flash writes remain unapproved (D8).

## 4. Editor (JUCE app, VST3, browser)

* Each column header becomes a **destination picker**: a list grouped by page/area, each
  entry shown with its control, e.g. `VR3 · Hall Decay`, `— · Pitch`.
* Under every header the pot of the current destination (`VR3`) or `—`.
* "Reset slots to VR1–VR8" restores `default_slots`.
* Old firmware without the capability: fixed columns as today, picker disabled.
* Simulators (`SimInstruments.h`, `StreichwurstSim.h`, `mod_sim`) get the same catalogs.

## 5. Firmware catalogs and default slots

`*` = destination that needs a new modulation hook in the DSP or FX component.

### 5.1 Streichwurst (pilot) — proposal for owner confirmation

| # | Destination | Control | Span (depth 100) | Note |
| --- | --- | --- | --- | --- |
| 1 | Registration | VR1 | ±100 % | column today |
| 2 | Crescendo* | VR2 | ±100 % | swell time; whether running notes follow is to be checked in the DSP |
| 3 | Release* | VR3 | ±100 % | as Crescendo |
| 4 | Balance | VR4 | ±100 % | column today |
| 5 | Animate | VR5 | ±100 % | column today |
| 6 | Phaser | VR6 | ±100 % | column today |
| 7 | Reverb | VR7 | ±100 % | column today |
| 8 | Master* | VR8 | ±100 % | default slot by decision 2 |
| 9 | Pitch | — | ±1 st | column today (vibrato) |
| 10 | Solo Tone | Solo VR1 | ±100 % | column today |
| 11 | Tremolo depth | Solo VR4 | ±100 % | column today |
| 12 | Tremolo rate* | Solo VR5 | ±100 % | log |
| 13 | Animate rate* | FX VR2 | ±100 % | log |
| 14 | Phaser rate* | FX VR4 | ±100 % | log |
| 15 | Reverb decay* | FX VR6 | ±100 % | |

Not in the catalog: enums and switches (Octaves, sustain mode, Layer/Split, ensemble
mode/on), keyboard settings (split key, bend range, transpose, tune, velocities), solo
envelope times and sustain, MIDI channel, pot page, preset.

### 5.2 Toy Piano (reference instrument) — proposal for owner confirmation

All three builds share `src/modulation.h`; each gets its own catalog.

| Build | VR1–VR8 (default slots) | Extras |
| --- | --- | --- |
| Toy Piano v0.3 | Attack, Decay; VR3–VR8 have no function → slots 3–5 Pitch, Level, Harmonic, 6–8 empty | Pitch, Level, Harmonic |
| Toy Piano Ambient | Pitch, Decay, Hall Decay, Filter*, Mod Depth*, Mix, X, Stretch* | Level, Harmonic, Attack |
| Toy Piano Tape | Drive*, Tone*, Age*, Mix*, Wow*, Flutter*, Hiss*, Dropouts* | Pitch, Level, Harmonic, Attack, Decay |

Ambient and Tape hooks are added in their FX components first; Stretch changes the wet
clock, whose limits stay in `effects/reverb/src/`.

### 5.3 Later firmwares (first draft, decided when they are adopted)

| Firmware | Default slots VR1–VR8 | Further catalog entries |
| --- | --- | --- |
| Physical Modeling | Pitch, Structure, Brightness, Damping, Position, Excitation, Reverb Decay*, Dry/Wet | Level |
| Looper | Speed, Inertia, Slope*, Start, Size, Slice, Skip, Fripp Decay | Mix |

Notes:

* FX destinations (Ambient Filter/Mod Depth/Stretch, Tape) are hooks in their own
  `effects/` components first ("Modular FX first"); wet-clock limits stay in
  `effects/reverb/src/`.
* Envelope times and other values read only at the strike are `latched` destinations.
* Physical Modeling replaced Reverb Decay by Level in 2026-10 because it had little
  audible effect; as a catalog entry both can coexist, the default follows the pot.
* Streichwurst's catalog entries refer to its parameter register
  (`contracts/parameters.json`), so name, range and pot come from one source.

## 6. Rules

* Empty matrix: bit-identical sound for every firmware (golden hashes unchanged).
* Firmwares without a catalog are untouched by the module change (fixed columns).
* No allocation or locks in the audio path; slot changes are compiled in the main loop and
  handed over like patches today (mailbox).
* Each firmware adoption is its own PR with version bump, host tests and a hardware test.

## 7. Rollout

1. Module 0.2.0: data model, engine, patch format 3, protocol, `mod_probe.py`, host tests,
   bench measurement of CPU/RAM.
2. Editors: picker in JUCE/VST3 and browser, `.modpatch` v2, simulators, self-test, e2e.
3. Streichwurst (pilot), hardware test.
4. Toy Piano (all three builds), hardware test.
5. Remaining firmwares one by one; FX hooks in their components first.

## 8. Open decisions (owner)

* Confirm or change the catalogs in § 5.1 (Streichwurst) and § 5.2 (Toy Piano).
