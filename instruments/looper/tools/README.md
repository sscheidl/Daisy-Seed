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
