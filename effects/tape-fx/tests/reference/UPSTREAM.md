# Referenz-DSP (drumlogue)

Unveränderte Kopien zum Vergleich in den Hosttests, nicht Teil einer Firmware:

| Datei | Quelle | SHA-256 |
| --- | --- | --- |
| `tapevibe.h` | `sscheidl/drumlogue_fx_units` `units/masterfx/tapevibe/tapevibe.h` | `e1de84d9698547858d763e9f07e95876c871b697faa0ec4fda6141c4fb85f1d1` |
| `mixforge.h` | `sscheidl/drumlogue_fx_units` `units/masterfx/mixforge/masterfx.h` | `22bb7907bce6f5402f1ca7cdc19d380d040d8986cb6251012277e27dfcbcfa4a` |

Stand: Commit `0db9c37d5d34943e5aad8eb6fb62663809f22216` (2. Oktober 2026).
`unit.h` ist ein minimaler Host-Ersatz für die Korg-SDK-Typen, die beide Header
verwenden; es wurde kein Korg-SDK-Code übernommen.
