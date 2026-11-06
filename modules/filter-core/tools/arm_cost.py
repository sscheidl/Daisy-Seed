#!/usr/bin/env python3
"""Static cost estimate of filter code on Cortex-M7, from the ARM compiler's own output.

Compiles a probe file with the firmware's flags, disassembles it and counts the instructions
of every `cost_*` function, which is one sample or the work of one call. Both sides of a
branch are counted, so rarely taken paths add to the total.

    python modules/filter-core/tools/arm_cost.py modules/filter-core/tools/cost_probe.cpp -I...

This is an estimate, not a measurement. Low assumes one instruction per cycle; high adds
the FPU result latency on a dependent chain by counting two cycles per instruction. Both
add 16 cycles per VDIV/VSQRT, as the unit does not pipeline them. The kit's own number is
callback_peak_cycles from `sq_probe.py stats`.
"""
import argparse
import pathlib
import re
import subprocess
import tempfile

FLAGS = ['-O3', '-std=gnu++17', '-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
         '-fno-exceptions', '-fno-rtti', '-c']
DIVIDE_CYCLES = 16
CYCLES_PER_SAMPLE_BUDGET = 480_000_000 // 48_000
LINE = re.compile(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4}(?: [0-9a-f]{4})?)\s+(\S+)\s*(.*)$')
HEAD = re.compile(r'^[0-9a-f]+ <(cost_\w+)>:$')
TARGET = re.compile(r'^([0-9a-f]+)\b')


def functions(listing):
    current, found = None, {}
    for line in listing.splitlines():
        head = HEAD.match(line)
        if head:
            current = found.setdefault(head.group(1), [])
            continue
        match = LINE.match(line)
        if match and current is not None:
            current.append((int(match.group(1), 16), match.group(2), match.group(3)))
        elif not line.strip():
            current = None
    return found


def sample_loop(code):
    """The per-sample loop: the shortest backward branch around a sample store.

    Set-up code outside the loop also branches backwards, and the limiter's rare path can
    sit before the loop head, so neither the widest nor the shortest span is safe alone."""
    spans = []
    for address, name, operands in code:
        if not name.startswith(('b', 'cbz', 'cbnz')) or name.startswith(('bl', 'bx', 'bic')):
            continue
        target = TARGET.match(operands.split(',')[-1].strip())
        if target and int(target.group(1), 16) < address:
            spans.append((int(target.group(1), 16), address))

    def body(span):
        return [c for c in code if span[0] <= c[0] <= span[1]]

    def stores(span):
        return any(op.startswith('vstm') or (op.startswith('vstr') and ('!' in args or ', r' in args))
                   for _, op, args in body(span))

    storing = [s for s in spans if stores(s)]
    # A probe without a loop is one sample, or the work of one call.
    return body(min(storing, key=lambda s: s[1] - s[0])) if storing else code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe', type=pathlib.Path)
    parser.add_argument('-I', dest='includes', action='append', default=[])
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as folder:
        obj = pathlib.Path(folder) / 'probe.o'
        subprocess.run(['arm-none-eabi-g++', *FLAGS, *(f'-I{i}' for i in args.includes), str(args.probe), '-o', str(obj)],
                       check=True)
        listing = subprocess.run(['arm-none-eabi-objdump', '-d', str(obj)], check=True, capture_output=True,
                                 text=True).stdout
    print(f'{"function":24}{"instr":>7}{"div":>5}{"low":>7}{"high":>7}')
    for name, code in sorted(functions(listing).items()):
        loop = sample_loop(code)
        divides = sum(1 for _, op, _ in loop if op.startswith(('vdiv', 'vsqrt')))
        low = len(loop) + (DIVIDE_CYCLES - 1) * divides
        high = 2 * len(loop) + (DIVIDE_CYCLES - 2) * divides
        print(f'{name:24}{len(loop):7}{divides:5}{low:7}{high:7}')
    print(f'Budget at 480 MHz and 48 kHz: {CYCLES_PER_SAMPLE_BUDGET} cycles per sample.')


if __name__ == '__main__':
    main()
