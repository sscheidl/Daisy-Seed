# CDC-Protokoll der Programmverwaltung (DDP-Erweiterung `PM*`)

**Stand 01.11.2026.** Transport, Framing, CRC und die Grundbefehle sind das
[Daisy Diagnostic Protocol v1](../../../diagnostics/protocol/PROTOCOL.md): USB CDC auf J7
(`0483:5740`), Rahmen `$<body>*<CRC16>\n`, CRC-16/CCITT-FALSE, Body ≤ 400 Zeichen,
`key=value`-Felder, erster Token = Typ. Implementierung: `src/pm_protocol.*` als
`diag::CommandHook` vor der Diagnose-Session; Tests `tests/test_protocol.cpp`,
`tests/test_diag_hooks.cpp`.

## 1. Identität

`HELLO proto=1` antwortet wie die Diagnose, mit eigener Identität:

```text
HELLO proto=1 fw=0.1.0 app=DaisyProgramManager dev=seed3-desktop boot=BOOT_QSPI bl=v6_1 libdaisy=f715e48 caps=btn,…,audio,pm,pmsd
```

`app` ist `DaisyProgramManager` (Profil manager) oder `DaisyProgramManagerProbe`
(Profil probe, ohne `pmsd`). Hosts prüfen `app` und `proto` und sprechen sonst kein
PM-Kommando. Eine COM-Nummer ist keine Geräteidentität; bei mehreren Daisy-Ports
wählt der Mensch explizit (CLI `-p`, Panel-Portauswahl).

## 2. Befehle

| Befehl | Sitzung nötig | Antwort | Fehler (`ERR c=<Befehl> e=…`) |
| --- | --- | --- | --- |
| `PMINFO` | nein | `PMINFO app= fw= profile= build= mode= gen= cat= cgen= n= sd= rootbin= slots=15` | – |
| `PMSTATUS` | nein | `PMST mode= state= e= ie= ve= slot= rid= pct= gen= cat= ce= cd= sd= rootbin= uerr=` | – |
| `PMLIST` | nein | `PMLIST gen= cgen= n= mask=<Hex4> cat=` (Bit i = Platz i+1 belegt) | – |
| `PMLIST s=<1..15>` | nein | `PMENT gen= slot= id= name=<prozentkodiert> cat= ver= hash=<64 Hex>` | `ARG`, `NOENTRY` |
| `PMSTART slot= id= hash=<16..64 Hex> gen= rid=` | ja | `ACK c=PMSTART rid= slot=`; wiederholte `rid`: `ACK c=PMSTART rid= dup=1 state= e=` | `SESSION`, `ARG`, `MODE`, `BUSY`, `NOCATALOG`, `GENERATION`, `NOENTRY`, `HASH`, `UNSUPPORTED` |
| `PMMODE m=SELECT\|DIAG` | ja | `ACK c=PMMODE m=` | `SESSION`, `ARG`, `BUSY` (während LOADING/SERVICE) |
| `PMRESCAN` | ja | `ACK c=PMRESCAN` (Katalog wird im nächsten Schritt neu gelesen, `gen` steigt) | `SESSION` |
| `BOOT` | ja (DDP) | `ACK c=BOOT`, dann Reset in den DFU-Bootloader | `BUSY` während LOADING |
| `GET`, `RATE`, `PING`, `BYE`, `HELLO` | wie DDP | unverändert | |
| `LED`, `CVOUT`, `TONE`, `SCOPE`, `SDTEST`, `READER` | wie DDP, **nur Modus DIAG** | unverändert | `MODE` in jedem anderen Modus |

Werte: `mode` ∈ `SELECT, DIAG, LOADING, SERVICE`; `state` ∈ `IDLE, OPENING, HEADER,
LOADING, VERIFY, FINALIZE, RESTARTING, ERROR`; `e` Ladefehler (`OK, BUSY, NOENTRY,
NOCARD, OPEN, FILESIZE, HDRREAD, HEADER, PRODUCT, CATHASH, READ, SHORT, LONG, HASH,
VECTOR, BUDGET, CANCELLED`), `ie` Headerfehler, `ve` Vektorfehler
([PACKAGE.md](PACKAGE.md)); `cat` ∈ `NOCARD, SCANNING, OK, MISSING, INVALID, MOUNT,
READ, UNSUPPORTED`, `ce` Katalogfehler, `cd` FatFs-Code; `uerr` ∈ `OK, EMPTY, NOCARD,
CATALOG, LOAD`. Hex-Ausgaben sind Großbuchstaben, Eingaben werden
groß/klein akzeptiert. `name` ist prozentkodiert: `[A-Za-z0-9._-]` direkt, alle
anderen Bytes als `%XX` (UTF-8).

## 3. Regeln für Hosts

* **Generation:** `gen` aus `PMLIST`/`PMINFO` gehört zu jedem `PMSTART`. Kartenwechsel,
  Entnahme und `PMRESCAN` erhöhen sie; veraltete Aufträge erhalten `GENERATION`.
* **Request-ID:** jeder Startauftrag trägt eine neue `rid`. Dieselbe `rid` wird nie ein
  zweites Mal ausgeführt (auch nicht nach Reconnect oder Fehler) – die Antwort trägt
  `dup=1` mit dem gespeicherten Zustand. Ein Retry nach Fehler braucht eine neue `rid`.
* **Hash:** `hash` ist ein Präfix (≥ 16 Hexziffern, gerade Anzahl) des Katalog-
  Payloadhashs aus `PMENT`. Abweichung → `HASH`. Der Host zeigt diesen Katalogwert
  als „erwartet", nicht als Messung des laufenden Programms.
* **ACK ≠ Start:** `ACK c=PMSTART` bedeutet angenommen. Fortschritt per `PMSTATUS`
  (`pct`), `RESTARTING` unmittelbar vor dem Reset. Danach verschwindet der CDC-Port,
  kurz erscheint DFU `0483:DF11`, dann meldet sich das Programm (USB-MIDI `IDENTIFY`
  oder, bei Diagnose-Images, CDC `HELLO`). Erst ein solcher Dialog zählt als
  `Identified`; eine Hashbestätigung des laufenden Images gibt es in Version 1 nicht.
* **Keine automatischen Starts:** Reconnect, Songladen, Transport, Program-Change oder
  Wiedererkennen eines Geräts lösen nie `PMSTART` oder `BOOT` aus – nur eine
  ausdrückliche Nutzeraktion.
* **Sitzung:** HELLO alle ≤ 3 s durch PING/Befehle am Leben halten (DDP); nach Ablauf
  lehnt das Gerät `PMSTART`/`PMMODE`/`PMRESCAN` mit `SESSION` ab. Beim Verlassen von
  `DIAG` setzt das Gerät CV OUT 0/0 und Ton aus.
* **Zustandsmodell (Host):** `Disconnected → Connected → (Accepted → Loading →
  Restarting) → Disconnected → Identified`. `ERROR` fällt nach `Connected` zurück.

## 4. Beispiele

```text
> HELLO proto=1
< HELLO proto=1 fw=0.1.0 app=DaisyProgramManager dev=seed3-desktop boot=BOOT_QSPI bl=v6_1 libdaisy=f715e48 caps=…,pm,pmsd
> PMLIST
< PMLIST gen=1 cgen=9 n=2 mask=0005 cat=OK
> PMLIST s=1
< PMENT gen=1 slot=1 id=toy-piano name=Toy%20Piano cat=instrument ver=0.4.0 hash=3F…(64)
> PMSTART slot=1 id=toy-piano hash=3F9A1C2E0B7D4466 gen=1 rid=4711
< ACK c=PMSTART rid=4711 slot=1
> PMSTATUS
< PMST mode=LOADING state=LOADING e=OK ie=OK ve=OK slot=1 rid=4711 pct=37 gen=1 cat=OK ce=OK cd=0 sd=1 rootbin=0 uerr=OK
> PMSTATUS
< PMST mode=LOADING state=RESTARTING … pct=100 …      (danach kein Port mehr)
```

## 5. Abgrenzung

MD/SQ/BL (USB-MIDI) bleiben unverändert. Der Rückkehrdienst der Instrumente nutzt den
neuen SysEx-Namensraum `PM` (`common/bootloader/return_entry.h`), kein DDP. JUCE
Standalone/VST3 erhalten dieselbe Semantik über einen späteren seriellen Adapter;
in dieser Version existieren CLI (`tools/pm_cli.py`) und HTML-Panel (`web/`).
