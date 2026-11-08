# CDC protocol of the program manager (DDP extension `PM*`)

**Status 2026-11-01.** Transport, framing, CRC and the base commands are the
[Daisy Diagnostic Protocol v1](../../../diagnostics/protocol/PROTOCOL.md): USB CDC on J7
(`0483:5740`), frames `$<body>*<CRC16>\n`, CRC-16/CCITT-FALSE, body ≤ 400 characters,
`key=value` fields, first token = type. Implementation: `src/pm_protocol.*` as a
`diag::CommandHook` in front of the diagnostics session; tests `tests/test_protocol.cpp`,
`tests/test_diag_hooks.cpp`.

## 1. Identity

`HELLO proto=1` is answered like the diagnostics, with the manager's own identity:

```text
HELLO proto=1 fw=0.1.0 app=DaisyProgramManager dev=seed3-desktop boot=BOOT_QSPI bl=v6_1 libdaisy=f715e48 caps=btn,…,audio,pm,pmsd
```

`app` is `DaisyProgramManager` (manager profile) or `DaisyProgramManagerProbe` (probe
profile, without `pmsd`). Hosts check `app` and `proto` and send no PM command otherwise.
A COM number is not a device identity; with several Daisy ports the human chooses
explicitly (CLI `-p`, panel port selection).

## 2. Commands

| Command | Session required | Reply | Errors (`ERR c=<command> e=…`) |
| --- | --- | --- | --- |
| `PMINFO` | no | `PMINFO app= fw= profile= build= mode= gen= cat= cgen= n= sd= rootbin= slots=15` | – |
| `PMSTATUS` | no | `PMST mode= state= e= ie= ve= slot= rid= pct= gen= cat= ce= cd= sd= rootbin= uerr=` | – |
| `PMLIST` | no | `PMLIST gen= cgen= n= mask=<hex4> cat=` (bit i = slot i+1 occupied) | – |
| `PMLIST s=<1..15>` | no | `PMENT gen= slot= id= name=<percent-encoded> cat= ver= hash=<64 hex>` | `ARG`, `NOENTRY` |
| `PMSTART slot= id= hash=<16..64 hex> gen= rid=` | yes | `ACK c=PMSTART rid= slot=`; repeated `rid`: `ACK c=PMSTART rid= dup=1 state= e=` | `SESSION`, `ARG`, `MODE`, `BUSY`, `NOCATALOG`, `GENERATION`, `NOENTRY`, `HASH`, `UNSUPPORTED` |
| `PMMODE m=SELECT\|DIAG` | yes | `ACK c=PMMODE m=` | `SESSION`, `ARG`, `BUSY` (during LOADING/SERVICE) |
| `PMRESCAN` | yes | `ACK c=PMRESCAN` (catalog re-read in the next step, `gen` increases) | `SESSION` |
| `BOOT` | yes (DDP) | `ACK c=BOOT`, then reset into the DFU bootloader | `BUSY` during LOADING |
| `GET`, `RATE`, `PING`, `BYE`, `HELLO` | as DDP | unchanged | |
| `LED`, `CVOUT`, `TONE`, `SCOPE`, `SDTEST`, `READER` | as DDP, **mode DIAG only** | unchanged | `MODE` in every other mode |

Values: `mode` ∈ `SELECT, DIAG, LOADING, SERVICE`; `state` ∈ `IDLE, OPENING, HEADER,
LOADING, VERIFY, FINALIZE, RESTARTING, ERROR`; `e` load error (`OK, BUSY, NOENTRY, NOCARD,
OPEN, FILESIZE, HDRREAD, HEADER, PRODUCT, CATHASH, READ, SHORT, LONG, HASH, VECTOR, BUDGET,
CANCELLED`), `ie` header error, `ve` vector error ([PACKAGE.md](PACKAGE.md)); `cat` ∈
`NOCARD, SCANNING, OK, MISSING, INVALID, MOUNT, READ, UNSUPPORTED`, `ce` catalog error,
`cd` FatFs code; `uerr` ∈ `OK, EMPTY, NOCARD, CATALOG, LOAD`. Hex output is upper case,
input is accepted in either case. `name` is percent-encoded: `[A-Za-z0-9._-]` verbatim,
every other byte as `%XX` (UTF-8).

## 3. Rules for hosts

* **Generation:** `gen` from `PMLIST`/`PMINFO` belongs to every `PMSTART`. Card changes,
  removal and `PMRESCAN` increase it; stale orders receive `GENERATION`.
* **Request id:** every start order carries a new `rid`. The same `rid` is never executed a
  second time (not even after a reconnect or an error) – the reply carries `dup=1` with the
  stored state. A retry after an error needs a new `rid`.
* **Hash:** `hash` is a prefix (≥ 16 hex digits, even count) of the catalog payload hash from
  `PMENT`. Mismatch → `HASH`. The host shows this catalog value as "expected", not as a
  measurement of the running program.
* **ACK ≠ start:** `ACK c=PMSTART` means accepted. Progress via `PMSTATUS` (`pct`),
  `RESTARTING` immediately before the reset. Then the CDC port disappears, a DFU device
  `0483:DF11` appears briefly, then the program identifies itself (USB-MIDI `IDENTIFY` or,
  for diagnostics images, CDC `HELLO`). Only such a dialogue counts as `Identified`; version
  1 has no hash confirmation of the running image.
* **No automatic starts:** reconnect, song load, transport, program change or recognizing a
  device never trigger `PMSTART` or `BOOT` – only an explicit user action.
* **Session:** keep HELLO alive with PING/commands every ≤ 3 s (DDP); after it expires the
  device refuses `PMSTART`/`PMMODE`/`PMRESCAN` with `SESSION`. On leaving `DIAG` the device
  sets CV OUT 0/0 and the tone off.
* **State model (host):** `Disconnected → Connected → (Accepted → Loading → Restarting) →
  Disconnected → Identified`. `ERROR` falls back to `Connected`.

## 4. Examples

```text
> HELLO proto=1
< HELLO proto=1 fw=0.1.0 app=DaisyProgramManager dev=seed3-desktop boot=BOOT_QSPI bl=v6_1 libdaisy=f715e48 caps=…,pm,pmsd
> PMLIST
< PMLIST gen=1 cgen=9 n=2 mask=0005 cat=OK
> PMLIST s=1
< PMENT gen=1 slot=1 id=toy-piano name=Toy%20Piano cat=instrument ver=0.4 hash=3F…(64)
> PMSTART slot=1 id=toy-piano hash=3F9A1C2E0B7D4466 gen=1 rid=4711
< ACK c=PMSTART rid=4711 slot=1
> PMSTATUS
< PMST mode=LOADING state=LOADING e=OK ie=OK ve=OK slot=1 rid=4711 pct=37 gen=1 cat=OK ce=OK cd=0 sd=1 rootbin=0 uerr=OK
> PMSTATUS
< PMST mode=LOADING state=RESTARTING … pct=100 …      (then no port any more)
```

## 5. Scope

MD/SQ/BL (USB-MIDI) remain unchanged. The instruments' return service uses the new SysEx
namespace `PM` (`common/bootloader/return_entry.h`), not DDP. JUCE standalone/VST3 get the
same semantics through a later serial adapter; this version ships the CLI
(`tools/pm_cli.py`) and the HTML panel (`web/`).
