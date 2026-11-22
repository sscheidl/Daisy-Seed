# Upstream: Otherworld vol. 1

| Item | Value |
| --- | --- |
| Repository | https://github.com/andreasjansson/otherworld |
| Commit | `8505568090db52310a255be3f95a0018145e90e6` (2026-10-13) |
| Copied | `effects/*.spn` (32), `bank.json`, `LICENSE` into `upstream/`, unchanged |
| Not copied | the listening page, loops, header video and `site/fv1.wasm` (spinfv1 engine, source-available licence, not MIT) |
| Licence | MIT, Copyright (c) 2026 Andreas Jansson; the notice is kept in `upstream/LICENSE` and in the generated header |

## Assembly (tools/build_bank.py)

Assembler: asfv1 1.2.7 (Nathan Fraser, MIT), vendored unmodified in `../third_party/asfv1/`. Upstream assembled with
a SpinASM-compatible assembler; three differences are bridged in the generator, never in the sources:

1. **Rounding:** coefficients round half away from zero (SpinASM); asfv1 rounds half to even. Only one literal in
   the bank is a tie (`SOF 1.0, 0.30908203125` = 316.5/1024 in both Turtle Grammar programs).
2. **Clamping:** literals such as `2.0` clamp to 1.99993896484375 (asfv1 `--clamp`).
3. **RMPA:** the address field carries ADDR_PTR (0x18) as in the upstream programs; asfv1 writes 0. The field is
   not used by the instruction.
4. **Ventifact:** its register symbol `oR` is renamed in memory, because asfv1 reads it as the mnemonic OR.

Result: 31 programs are identical word for word to those in the upstream release firmware
(`site/otherworld-vol1-fxaid-xl.wav`, git blob `4dbbe0a`); their SHA-256 values are in `tests/release_sha256.json`.
Ventifact (slot 21) has no decoded release program and is covered by the assembler path only.

## Updating

Copy the new upstream files into `upstream/`, set the commit here and in `tools/build_bank.py`, run
`make bank test`, update `tests/release_sha256.json` from the new release, and bump
`SEED_FV1_BANK_OTHERWORLD_VERSION`.
