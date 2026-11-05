# fv1-vm hardware test plan (research / feasibility study)

Status: **NOT MEASURED**. Hardware is the owner's: the agent builds and runs host tools; flashing happens only
after the owner has put the kit into update mode (or uses S9 + S16 for 5 s on a running image) and asked for it.
Log every run with date, image, SHA-256 and the CSV.

## H1 Standalone, demo programs (`Fv1VmFx-v0.1.0.bin`, float, SDRAM)

1. Flash, line input on J1/J2, headphones or line output.
2. Program 1 (LEDs: S1 on): thru. Input must pass unchanged.
3. S2 → program 2 (LEDs: S2 on): echo 0.5 s (16 383 samples at 32 kHz), VR2 feedback, VR3 wet.
4. S5 bypass, S6 noise input: audible and LED states correct.
5. `fv1vm_probe.py` without `--sweep`: telemetry arrives; note average and peak per program.

## H2 CPU sweep over the catalogue (local images only)

For each image: `fv1vm_probe.py --noise --pots 64 64 64 --sweep 240 --seconds 3 --csv <image>.csv`,
then again with `--pots 0 0 0` and `--pots 127 127 127`.

| Image | Question |
| --- | --- |
| `Fv1VmFxLocal` float, SDRAM | baseline cost; worst program; any overrun |
| `Fv1VmFxLocal` double, SDRAM | cost of double precision |
| `Fv1VmFxLocal` float, AXI | is the SDRAM delay RAM the bottleneck (risk R1 of the feasibility report)? |
| `Fv1VmFx` double, AXI (demo programs) | double plus fast memory, only where it fits |

## H3 Composed product (`ToyPianoFv1Local`)

1. Select programs with CC22/CC23 on channel 16 (`fv1vm_probe.py --sweep`), play keys or send MIDI notes.
2. Record whole-callback load per program (CC119) and the Mod on Daisy CPU statistics.
3. Listening (owner): a few reverbs, delays, pitch shifters and modulation programs; note audible faults.

## Acceptance questions

* Peak load per program below 100 % of the block budget, zero overruns over the sweep.
* Difference float vs double audible on the 40 bit-sensitive programs (owner listening, not sample equality).
* Headroom left next to the Toy Piano for the composed product.
