# Looper: what a save guarantees on the card

Applies from v0.6.0 (FatFs R0.16, writable profile of `common/sd`, FAT32 and exFAT). The
save sequence itself is unchanged from v0.3. Host results below are from
`tests/test_store.cpp` and `tests/test_card.cpp`; behaviour on a real card and a real
power cut is **NOT MEASURED**.

**No step of a save is atomic.** FAT and exFAT have no journal, and `f_rename` is two
directory updates. The design only arranges the steps so that a complete loop stays
findable at almost every moment. This page lists the moments, and the one known gap.

## The sequence

`LoopStore` replaces `looper/loop.wav` in this order (`src/loop_store.cpp`):

1. `OpenWrite(loop.tmp)`: `FA_CREATE_ALWAYS`, cuts any existing `loop.tmp` to zero.
2. Write the WAV header and the frames in 32 KB pieces. Any change to the loop stops the
   save and removes `loop.tmp`.
3. `Close()`: `f_close` writes the remaining data, FAT or allocation bitmap, and the
   directory entry with the file size.
4. `Remove(loop.wav)`: `f_unlink` removes the directory entry, then frees the chain.
5. `Rename(loop.tmp, loop.wav)`: `f_rename` registers the new name, then removes the old
   one, then syncs.

`Recall` at power-up tries `loop.wav`, then `loop.tmp`, and accepts a file only if the WAV
parser finds it complete (a shortened file is rejected; there is no checksum).

## Failure boundaries

| Boundary | Failure while running (card error, card full) | Power loss | On the card afterwards |
| --- | --- | --- | --- |
| 1, 2: write | `AbortSave`: close, remove `loop.tmp`; fault, retry after 10 s | `loop.tmp` incomplete, ignored; overwritten by the next save | old `loop.wav` |
| 3: close | `loop.tmp` removed; fault, retry | as above: the size is only in the directory entry written last | old `loop.wav` |
| 4: unlink | Fault, retry. `loop.tmp` stays complete, and the retry cuts it, which is harmless while `loop.wav` still exists | After the entry is gone: no `loop.wav`, `loop.tmp` complete, recalled. Clusters may stay allocated if the chain was not freed yet: lost space until Windows repairs the card, not lost audio | new loop in `loop.tmp`, or old `loop.wav` |
| 5: rename | **The gap below** | Directory holds `loop.tmp`, or `loop.wav`, or both names (see below) | new loop under one or both names |

## The gap: a failed rename

If `Rename` fails with the card still running, `loop.wav` is already deleted, so
**`loop.tmp` is the only copy on the card**. The store reports a write fault, keeps the
version unsaved and retries after 10 s. A power-up at this point recalls `loop.tmp`.

But the retry starts at step 1 and **cuts that same `loop.tmp` to zero**. From then until
its own close and rename, the card holds no complete loop:

* power loss in that window loses the card copy; `Recall` finds nothing usable;
* a write failure in the retry removes `loop.tmp` (`AbortSave`), leaving neither file;
* a confirmed share in that window does the same (`Halt`, then `AbortSave`).

The loop stays in RAM throughout, so as long as the kit keeps power the next successful
retry restores it. `TestRenameFailureGap` in `tests/test_store.cpp` pins this behaviour.
`f_rename` failing while `f_unlink` just succeeded needs a card error between the two, so
this is rare, but it is not excluded. Finishing the pending rename before a new save would
close the gap; that is a change of the store's behaviour and has not been made.

## Two names for one file

`f_rename` writes the new entry before it removes the old one. Both normally sit in the same
directory sector and reach the card in one block write. If they fall into different sectors
(possible once `looper/` holds other files) a power loss between the two writes leaves
`loop.wav` and `loop.tmp` pointing at the same clusters. Recall works, but the next save's
step 1 frees those clusters while `loop.wav` still names them. The host campaign does not
produce this layout: **not covered by any test**. Keep other files out of `looper/` (imports
go to `looper/loops/`).

## What the host campaign shows, and what it does not

`tests/test_card.cpp` cuts the card after every single block write of a replacing save, on
FAT32 and exFAT images, through the production stack (`LoopStore`, `FatVolume`, `ro_sd.c`,
FatFs R0.16). After each cut, recall gives exactly the old or the new loop, and the next
save and recall work.

It models a block as either fully written or not at all, in order. Not modelled: a torn
block, a card that reorders or caches writes internally (CMD13 reporting "ready" says the
card accepted the data, not that flash holds it), lost clusters (not counted), a second
power loss during recovery, and the directory layout above. Only a test on real cards can
speak to those, and a card that loses acknowledged data defeats any scheme at this level.

The save-before-share path and the "unusable card, confirm" path of 0.5.1 are unchanged:
they sit above `Volume` and use the same sequence.
