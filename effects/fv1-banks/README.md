# FV-1 program banks for fv1-vm

Program banks are separate components that the [FV-1 interpreter](../fv1-vm/README.md) runs. Only **openly
licensed** programs go here, each bank with its pinned upstream, licence, generator and tests. Programs without such
a licence stay in the git-ignored local table of `effects/fv1-vm` (`PROGRAMS=local`) and never enter a commit,
release or CI artifact.

| Bank | Component | Programs | Licence | Product |
| --- | --- | --- | --- | --- |
| [otherworld](otherworld/README.md) | `fv1-bank-otherworld` 0.1.0 | 32 | MIT (Andreas Jansson) | `fv1-fx-suite` |

`third_party/asfv1/` holds the shared host assembler (MIT).
