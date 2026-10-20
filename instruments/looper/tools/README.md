# Stardust Analysis Scripts

Analysis-only scripts for the [firmware analysis](../docs/STARDUST_ANALYSIS.md).
None of them writes to the input binary, and none of their output may be flashed.
Supply `Stardust_v1_1_1.bin` locally; it is not part of this repository.
`.gitignore` excludes `*.bin`, `*.elf`, and `*.pkl` in this project and the
`work/` directory.

| Script | Source | Purpose | Dependencies |
| --- | --- | --- | --- |
| `stardust_static_analysis.py` | Feasibility study, unchanged | SHA-256, vector table, startup literal pool, strings, entropy → JSON | Python 3 only |
| `stardust_wrap_elf.py` | Feasibility study, unchanged | Wraps the raw image in an ELF for disassemblers. **Do not flash** | Python 3 only |
| `cfg.py` | This analysis | Recovers the control-flow graph with angr and saves it as a pickle | angr |
| `q.py` | This analysis | Queries that graph: references to an address, function info, decompilation | angr |
| `ghidra/StardustSetup.java` | Second pass | Pre-script: memory map (`.data` copy, `.bss`, SRAM, SDRAM, peripherals), Thumb mode, vector-table functions | Ghidra 12.1 |
| `ghidra/StardustPointers.java` | Second pass | Creates functions at Thumb code pointers in the image and `.data` (callbacks, vtables) | Ghidra 12.1 |
| `ghidra/StardustTypes.java` | Second pass | Opaque `Looper`, `Tape`, `Fx` types and signatures for the documented entry points | Ghidra 12.1 |
| `ghidra/StardustDump.java` | Second pass | Function index (`funcs.tsv`) and decompiled output (`all.c` or one file per address) | Ghidra 12.1 |
| `ghidra/StardustListing.java` | Second pass | Disassembly of an address range, to check decompiler output | Ghidra 12.1 |

## Reproduce

Tested with Python 3.13, angr 10.0.4.post1, and Capstone 5.0.7 on Linux x86_64.

```bash
cd instruments/looper
mkdir -p work && cp /path/to/Stardust_v1_1_1.bin work/
sha256sum work/Stardust_v1_1_1.bin   # e7556ffd3bbfa578b7061e4b06de6d010d4842115439121eea458e2e6cf1a48a
python3 -m venv work/venv && work/venv/bin/pip install angr==10.0.4.post1

python3 tools/stardust_static_analysis.py work/Stardust_v1_1_1.bin -o work/evidence.json
work/venv/bin/python tools/cfg.py work/Stardust_v1_1_1.bin work/cfg.pkl   # about 1 minute

Q="work/venv/bin/python tools/q.py work/cfg.pkl"
$Q refs 0x24034e90            # who references "1:/loop.wav"
$Q dec  0x2400f809            # looper initializer: buffer layout
$Q dec  0x24013bed            # loop-menu WAV import
```

Pass Thumb addresses with bit 0 set (`0x…9`) or the plain entry address; `q.py`
finds the containing function either way. Decompiled output is for reading only:
keep it in `work/` and do not commit or copy it into the engine.

`q.py` loads a pickle. Only load a `cfg.pkl` you generated yourself.

## Reproduce with Ghidra (second pass)

Tested on Windows 11 with Ghidra 12.1.4 and Temurin JDK 21.0.12, both unpacked
outside the repository (here `C:\Tools`). Use the language `ARM:LE:32:v8-m`: the
`Cortex` variant does not decode the FPv5 instructions `VSEL`, `VMAXNM`, and
`VMINNM` that this firmware uses for its clamps.

```powershell
$env:JAVA_HOME = "C:\Tools\jdk-21.0.12.1+1"
$G = "C:\Tools\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat"
$W = "$PWD\instruments\looper\work"; $S = "$PWD\instruments\looper\tools\ghidra"
New-Item -ItemType Directory -Force "$W\ghidra" | Out-Null

# Import, analyze, add pointer-referenced functions and types, dump (about 1 minute)
& $G "$W\ghidra" Stardust -import "$W\Stardust_v1_1_1.bin" -overwrite `
  -loader BinaryLoader -loader-baseAddr 0x24000000 -processor ARM:LE:32:v8-m -cspec default `
  -scriptPath $S -preScript StardustSetup.java -postScript StardustPointers.java `
  -postScript StardustTypes.java -postScript StardustDump.java "$W\out"

# Disassembly of the tape and head functions
& $G "$W\ghidra" Stardust -process Stardust_v1_1_1.bin -noanalysis -readOnly `
  -scriptPath $S -postScript StardustListing.java "$W\out\lst_tape.txt" 0x2400b454 0x2400d610
```

`work/out/all.c` then contains `looper_process`, `tape_process`, `tape_head`,
`looper_init`, and `fx_process` with typed object fields. Like the angr output,
everything in `work/` is for reading only and is not committed.
