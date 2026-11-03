# Desktop case / Paper on Daisy

**Continuation after the test print:** [Handover and goals of the next case version](v4/POST_PRINT_HANDOVER.md)
with [starter text for a new chat](v4/START_NEXT_CHAT.md).

**Current status, 2026-10-27:** [V4 as a complete first case print](v4/full/V4.md),
at the user's explicit request without a preceding coupon order.
**Four printed parts**, two 1 mm acrylic sheets and paper; original caps fitted directly
and guided laterally. [Print package](v4/full/V4-KOMPLETTDRUCK.zip)
with four STLs, three pre-positioned 3MF plates, SVGs and assembly/purchasing.
Checked digitally; hardware **NOT MEASURED**.
Rear panel fastenings now in the free middle area, including
head/tool/nut access checked. The thin webs between the
paired jack pockets have been removed. The ZIP from state b904a7e is superseded.

**For the originally planned pure fit test:** [Economy profile and separate print order](v4/fit-print/README.md)
with a [separate ZIP](v4/fit-print/V4-PASSPROBE-SPARPROFIL.zip): the same four checked
STLs, request for PLA / 0.20 mm / 20 % infill, without cosmetic post-processing.
Price advantage not yet calculated; put the final acrylic on hold for now.

## Manufacturing suppliers

These two suppliers were already used for the PreenFM2 case;
assigned on 2026-10-26 from the existing order documents:

| Supplier | Service | Earlier PreenFM2 order |
| --- | --- | --- |
| [Lazycut](https://lazycut.de/) | Laser cutting of the acrylic sheets | Order AU-202609-10258 of 2026-09-08 lists "Plexiglas 1 mm klar" (Plexiglas 1 mm clear). |
| [3ddruck-express.de](https://3ddruck-express.de/) | FDM printing of the case parts | Order 4431 of 2026-08-15: PETG-CF black, 0.12 mm layers; tray 40 %, rear panel 60 % infill. |

For the new V4 order the [current print and acrylic specifications](v4/full/V4.md#fertigungsauftrag) apply.
The earlier settings serve as order history. The private invoices
and order e-mails stay outside the repository.

## Earlier designs and reviews

The following sections document older designs. The earlier recommendation
to order coupons before the complete print has been replaced for V4 by the new
user decision. The [three-hole test](v4/simple-guide/README.md)
and the [catalog C1–C7](v4/COUPONS.md) remain optional historical aids.

**Current error review 2026-10-25:** The [comparison of V3 and Claude's
counter-design](DESIGN_REVIEW_2026-10-25.md) finds open connector support,
missing cap retention and further concrete dimension/access problems.
The RH/ACCESS checks do not cover these additional requirements.
The existing STLs are still a design study.

**2026-10-25: [Orientation fix RH](ORIENTATION_FIX.md). Do not use earlier
print STLs and ZIP packages.** All V1/V2/V3 STLs have been re-exported
and independently checked. Renders and print files now use
the same physical coordinate space.

**Current V3 additionally with [lateral wheel access and enlarged
headphone opening](RIGHT_ACCESS.md)** (`ACCESS-2026-10-25`). The earlier
10 mm height assumption for the volume wheel has been corrected.

**Current design: [continuous print carrier V3](UNIFIED_V3.md).**
One printed part connects the control surface, Seed/jack hoods and the molded-on
button grid. On top of it there are still two 1 mm acrylic sheets with paper,
carried by metal standoffs and removable via four M3 Knurlies. Jack and
switch nuts fasten only the lower acrylic sheet. Geometry checked
digitally; cap height, standoff tolerances, strength and fit on the board
NOT MEASURED. No manufacturing release yet.

A [detailed assembly view with KiCad component placement, Seed3 model,
materials and GLB/STL export](REALISTIC_RENDER.md) complements the CAD design.

The [V2 with separate hoods and grid](CLOSED_V2.md) is kept as an earlier
design study.

## First design V1 (reference)

An **open, screwed prototype** for the Seed3 Desktop Dev Kit: tray,
separate top frame and replaceable paper labeling. An optional
transparent 1 mm sheet protects the paper on the frame. The jacks,
Seed service area, BOOT/RESET and microSD remain cut out.

**Status 2026-10-24: CAD checked digitally; print, fit on the board and
mechanical connector strain relief NOT MEASURED.** The files are intended for a
first fit prototype. Before a paid complete print,
use the paper template and the small test piece first.

![CAD prototype, electronics and mounted caps only schematic](prototype/case-preview.png)

The electronics in the preview show arrangement and clearances; they are not a
complete manufacturer 3D model. In particular, the shown mounted
cap height must not be used as proof of dimensions. The transparent protective sheet is
omitted in the preview; the light surface represents the paper.

## Files

The directly usable files are in [prototype/](prototype/):

| File | Purpose |
| --- | --- |
| `tray.stl` | Tray with four PCB supports, M3 nut pockets, lid posts and cable tie slots |
| `overlay-frame.stl` | Flat-printable top frame with eight pot openings and two switch windows |
| `fit-coupon.stl` | Small test piece, 62 × 24 mm, for screws, nut and openings |
| `paper-master-a4.pdf` | A4 landscape, 1:1; neutral fit template with 100 mm control distance |
| `clear-cover-cut.svg` | Pure cut contours in mm for the optional clear sheet; no engraving |
| `case-preview.png` | Preview of the actual STL geometry with schematic electronics |
| `validation.json` | Digital test results and SHA-256 of the sources and print files |

All STL files use **millimeters**, lie with the print surface on Z=0
and are imported individually, without scaling. The large part footprint is
215 × 129 mm; with a brim, also allow for its width on both sides.
No firmware build and no GitHub Actions run are needed.

## Dimensional basis and design dimensions

The mechanical data come from the official
[KiCad files](https://github.com/daisyaudio/Seed3-DevKit-Desktop/tree/987297fb7c9a8c58721ef40abf6d6eedd1fcda2a/pcb),
not from a scaled product photo. The four published revisions
were read out with KiCad 10.0.6 and compared for outline dimensions, PCB thickness,
mounting holes and the center points of all controls.
These values are the same for Rev1 to Rev4. This is no statement about the
complete electrical or three-dimensional identity of the revisions.

| Quantity | Value | Source |
| --- | --- | --- |
| PCB outline | 197 × 111 mm, corner radius 2.5 mm | KiCad edge center lines |
| PCB thickness | 1.6 mm | KiCad board stackup |
| Holes | 4 × Ø 3.5 mm | KiCad mounting pads |
| Hole spacing | 187 × 101 mm | Difference of the pad center points |
| Pot pitch | 15.279 mm | KiCad VR1–VR8 |
| Button pitch | 12.285714 mm horizontal, 16 mm between the rows | KiCad S1–S16 |
| Case footprint | 215 × 129 mm | Design |
| Floor / side wall | 2.8 / 2.4 mm | Design |
| PCB underside above print surface | 8.8 mm; 6 mm above the inner floor | Design |
| Frame underside above PCB top | 9 mm | Adjustable design, fit test open |
| Frame / total height | 2.4 mm / 21.8 mm without feet and clear sheet | Design |
| Clear sheet and paper | optional 1 mm + approx. 0.15 mm | Design |
| Pot openings | Ø 14 mm | Adjustable design, check knob diameter |

Origin in `board.json`: rear left of the board; X to the right,
Y towards the player. For example H1=(5,5), H2=(5,106), H3=(192,5),
H4=(192,106). The 0.05 mm drawing line width of Edge.Cuts does not count as PCB.
File names, commit and SHA-256 of all four source files are in `board.json`.

Not all components have a complete 3D model in the manufacturer project.
The generous service windows and connector cutouts open at the top therefore avoid
tight-fitting, unconfirmed jack outlines. The frame height and
accessibility with the knobs and cables actually fitted remain to be confirmed in the
fit test. For knobs above about 13 mm diameter the
pot opening must be adapted or the knob pulled off for assembly. The
shared pot opening must not be enlarged arbitrarily because of the narrow
webs in between.

## Paper on Daisy and ordered caps

Taken over from the [existing concept](https://chatgpt.com/share/6adce96c-8884-83ed-b405-74142ff307ea):
**one shared window for all 16 buttons and their LEDs**,
eight pot openings, two generous switch openings, a replaceable paper overlay
and optionally a clear cover. The neutral template initially carries only
VR1–VR8, S1–S16 and SW17/SW18; instrument-specific function labels can
be built on the same geometry.

The order contains 32 each of E-Switch **1SBLK** (black, square) and
**1RLGR** (light gray, round). Manufacturer data:
[1S](https://www.e-switch.com/product/1s/) 5 × 5 × 9 mm,
[1R](https://www.e-switch.com/product/1r/) Ø 5 × 9 mm.
The existing TL1105SPF250Q stay on the board. The caps are pushed onto the
actuators; a self-built key mechanism is not part of V1.
The cap height of 9 mm is **not** the additional height above the fully
mounted button. Fit, travel and mounted height on our own board:
**NOT MEASURED**.

The 96 × 31.5 mm window leaves both cap shapes including
0.3 mm clearance per side as well as all LEDs free in top view. The sheet
does not guide or hold the individual caps and does not rest on the buttons.
Paper and clear sheet are held on the top frame with its four separate
screws. To change the paper, the frame does not have to be released from the
PCB screws. A sheet can be cut slightly smaller at the outer edge
so that it does not protrude.

## Connector strain relief

Four PCB screws connect the board to the tray. In addition, next to the four audio jacks,
the three DIN jacks, both USB accesses and the
headphone jack, there are pairs of **2.6 mm wide slots for narrow
cable ties** in the floor outside the PCB outline.

Fasten the cables with some slack at the respective slot pairs. The
loop goes around the cable sheath behind the plug, without stressing a plug
sideways or pressing on the board. The loops may
run under the floor; feet keep them off the table. Release the cables before
unplugging. The slots for the service USB remain optional.

**The cutouts alone do not relieve the jacks.** The design has
no force-fit jack clamping. Only the fastened cables can transfer tension into
the tray. Absorption of tension, plug-in forces and lateral leverage:
**NOT MEASURED**; V1 is not a certified cable clamp. The CV/gate jacks
at the top have no individual cable tie points.

## Print, parts and assembly

As a first approach: normal PETG, 0.4 mm nozzle, 0.2 mm layers, four perimeters
and about 20–30 % infill. These are **starting values**, not proven print parameters.
Print both main parts flat. The small nut pockets need a short
bridge; check its print quality on the test piece. A print service can choose the
parameters to suit its process.

| Parts | Quantity / purpose |
| --- | --- |
| Printed tray and frame | 1 each |
| M3×12 and normal M3 nuts | 4 each for the PCB mounting |
| M3×10, pan head | 4 for the frame; screw carefully into Ø 2.6 mm plastic holes |
| Small insulating M3 washers | 4 on the PCB, as needed; check seating on the mounting pad |
| Rubber feet, at least 3 mm high | 4; keep the underside and screw ends clear |
| Narrow cable ties, at most 2.5 mm wide | according to cable assignment, preferably releasable |
| Clear sheet 1 mm and paper | optional; use existing material after the fit test |

The lower nut pockets have 5.7 mm width across flats and 2.4 mm depth. Without a washer, M3×12
protrude by a calculated approx. 1.6 mm below the print surface; the
feet are therefore part of the assembly. Do not use existing M3×14 without checking.
The lid screws engage only in the four outer plastic posts, not in
the board. No threaded inserts and no gluing required.

1. Print the PDF on A4 landscape at **100 % / actual size**. Check the control distance
   with a ruler for exactly 100 mm; only then cut out and lay it on.
2. Print the test piece. The two small round holes are Ø 3.4 and Ø 2.6 mm,
   the cap openings Ø 5.6 and 5.6 × 5.6 mm, the large hole Ø 14 mm. At the bottom
   left is the M3 nut test. Use it to check screws, nut and possible pot knobs;
   the caps do not need these openings in the finished case.
3. Print the tray, insert the nuts from below. Place the unpowered board on the four
   supports and screw it down without stress. Check the underside clearance.
4. Fit the button caps. Initially lay the frame on loosely. Every pot and
   switch must reach its full travel freely; every button must spring back
   completely, all LEDs must remain visible.
5. Lay the paper and, if applicable, the clear sheet on the frame; only lightly tighten the four lid screws.
   In case of stress or blocked controls, correct the dimensions;
   do not force any parts into the openings.
6. Check USB, SD, BOOT/RESET, all used jacks and the cable ties with the real
   plugs for accessibility. Fit cable loops and feet.

## Reproducing locally

Requires normal Python 3 with `requirements.txt`; KiCad Python only for
re-reading the manufacturer files. No change to the pinned
firmware dependencies.

```powershell
python -m venv .tools/case
.tools/case/Scripts/python.exe -m pip install -r hardware/desktop-case/requirements.txt
.tools/case/Scripts/python.exe hardware/desktop-case/build_case.py
```

This generates files under `hardware/desktop-case/output/` and checks them locally.
The checked-in version was created with Python 3.12. Parameter example:

```powershell
.tools/case/Scripts/python.exe hardware/desktop-case/build_case.py --knob-opening 14 --panel-clearance 9
```

To read them out again, download the four unmodified KiCad boards from the
manufacturer commit named above. Pass all paths individually or let PowerShell
generate the list (Python does not expand wildcards on Windows):

```powershell
$caseBoards = Get-ChildItem build/case-reference/*.kicad_pcb | ForEach-Object FullName
& 'C:/Program Files/KiCad/10.0/bin/python.exe' hardware/desktop-case/extract_board.py @caseBoards --output hardware/desktop-case/board.json
```

Optionally create the preview with Blender:

```powershell
& 'C:/Program Files/Blender Foundation/Blender 5.2/blender.exe' --background --python hardware/desktop-case/render_preview.py
```

## Validation of 2026-10-24

**Checked digitally:** three closed, consistently oriented STL bodies, each with
exactly one connected component; PCB collision with the tray
0 mm³; collision of tray and mounted frame 0 mm³; 16 cap envelopes
and 16 LED envelopes completely within the open area. The exported STLs
were read in again. PDF rendered as PNG and checked visually; preview
generated from the STLs with Blender 5.2.1 and checked. No complete
component or plug collision check, strength calculation or slicer check.

**Hardware NOT MEASURED:** first print, screw/nut fit, mounted
cap height and seating, pot/switch operation, LED viewing angle, cable/SD access,
temperature and mechanical strain relief. Add results as MEASURED with the date only after the actual
fit test.

The earlier reference
[Daisy Pod Case by Planet Zero Designs](https://cults3d.com/de/modell-3d/gadget/daisy-pod-case-by-pzd)
concerns the smaller Pod board. The existing chat names it as a design
model; the Cults page returns HTTP 403 on the current fetch. No
third-party STLs were taken over or scaled for the Desktop Kit.
