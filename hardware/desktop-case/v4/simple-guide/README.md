# Simple print start: one part

**User decision of 2026-10-26:** The original caps stay fitted on the
buttons. A shared plate guides them laterally; additional
retention against pulling out is not required. This drops the
separate retaining sleeves as a requirement for the case.

## What to print?

**Choose only one of the two files:**

| Your caps | One single printed part |
| --- | --- |
| Square E-Switch 1SBLK | `C2-direct-S.stl` |
| Round E-Switch 1RLGR | `C2-direct-R.stl` |

No sleeve, no second print plate, no PCB dummy. To compare
both cap shapes, it is two parts in total. The previous catalog with
38 STL files is a collection of older functional tests, not a mandatory print.

The test piece has three openings side by side: from left to right
0.1 / 0.2 / 0.3 mm clearance per side, radial for the round part. The small dots
next to the label are for checking the file orientation, not for choosing the fit.
The three openings belong to the same plate; no additional inserts.

## Print and try out

1. Flat underside on the bed. PETG-CF black, 0.12 mm layers,
   40 % infill, four perimeters, scale 1:1, no hole compensation.
   No supports. Do not mirror. The label tab is part of the part.
2. First guide a real cap without force through all three openings.
   Choose the smallest opening through which it slides freely.
3. Then check lateral guidance and free key travel on the unpowered board.
   The test piece nominally represents a plate from PCB+9 to PCB+10;
   hold it at this height or support it at the correct spacing. Do not press on
   neighboring components or use the plate as a key stop.
4. Leave the cap on its button: press, release, repeat.
   It must return fully on its own. The shared plate
   holds the cap laterally; it may still be pulled off upwards.

All dimensions **NOMINAL**, hardware **NOT MEASURED**. Mounted cap height 13
and switch travel 0.25 are still assumptions. The FDM plate has LED clearance holes;
real transparent lower acrylic may stay closed there. This is
a fit test, not an optical or mechanical manufacturing release.

| Measurement | MEASURED / observation | Date |
| --- | --- | --- |
| Cap shape, actual width / diameter | | |
| Chosen opening left / middle / right | | |
| Actual size of this opening, rework | | |
| Mounted cap height and actual plate height | | |
| Free travel, full return, no preload | | |

## Simplified goal for the later case

A bottom tray and a shared print carrier protect the board.
A separate rear panel is only added if the jack mounting
requires it. This includes the already requested two acrylic sheets with
paper as well as the existing caps. **No 16 loose retaining sleeves.**

C5 dummy stacks and light pipe comparisons are not a prerequisite for this
first test. Standoffs, acrylic and paper can be measured later with the real
parts. Only reconsider light pipes if the LEDs are poorly visible without
them. Jack support and access to the Seed remain
requirements; the complete case follows after the real fit dimensions.

## Files and reproduction

The repository contains both models and test reports in `coupons/`.
Each ZIP contains only the chosen model, directly next to this guide.
From the repository root:

```powershell
.tools/case/Scripts/python.exe hardware/desktop-case/v4/build_simple_guide.py
```

This additionally generates two ZIPs under `build/case-v4-print/`, each with
**exactly one STL**, this guide and a test report. Both models use
the central dimension set `v4-parameters.json` unchanged. Closedness,
a single component, RH orientation including the mirrored counter-check and the
cap clearance are checked on the actual STL files.
