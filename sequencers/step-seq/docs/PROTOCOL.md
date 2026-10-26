# SQ-Protokoll v1.0 (Acid Seq)

**Stand:** 27. Oktober 2026 · **Status:** Vertrag aus Phase 0, umgesetzt in Phase 1
(Geräteseite `include/step_seq/device.h`, Codec `sq_protocol.h`, Editor `SqSession.h` und
`web/sq_protocol.js`, `tools/sq_probe.py`).
Grundlage: [Arbeitsauftrag](../../../docs/acid-seq/ARBEITSAUFTRAG.md) 4.9 (R09, R10), D1.

Das SQ-Protokoll konfiguriert und beobachtet Acid Seq und seine Einzelfirmwares
(`step-seq`, `acid-303`, `drive-fx`, `delay-fx`) über SysEx. Es ist bewusst getrennt
vom Mod-on-Daisy-Protokoll `7D 4D 44` („MD“), das unverändert bleibt. Ein gemeinsamer
Parametervertrag gilt für alle Komponenten. Für Drive oder Delay gibt es kein
eigenes Protokoll.

* **Referenz:** [`tools/sq_codec.py`](../tools/sq_codec.py) ist der Referenz-Codec.
  Er erzeugt die gemeinsamen Testvektoren
  [`contracts/sq_vectors.json`](../contracts/sq_vectors.json), die Firmware (C++),
  Editor (C++ und JavaScript) und `sq_probe.py` prüfen.
* **Parameter:** IDs, Bereiche und CC-Nummern stehen in
  [`contracts/parameters.json`](../contracts/parameters.json), erklärt in
  [PARAMETERS.md](PARAMETERS.md).
* **Transport:** USB-MIDI. SysEx über DIN gibt es in dieser Fassung nicht (4.4).

## 1. Abgrenzung zu anderen Nachrichten

| Kopf | Bedeutung | Behandlung in einer SQ-Firmware |
| --- | --- | --- |
| `F0 7D 53 51 …` („SQ“) | dieses Protokoll | § 3 |
| `F0 7D 42 4C …` („BL“) | Bootloader-Anfrage ([common/bootloader](../../../common/bootloader/README.md)) | wird vor dem SQ-Parser geprüft. Vor dem Eintritt gibt die Firmware Gates und Noten frei (4.1) |
| `F0 7D 4D 44 …` („MD“) | Mod on Daisy | gehört nicht zu SQ. Eine Firmware ohne Mod-Kern verwirft sie stumm |
| alle anderen | fremd | verworfen, nicht beantwortet |

## 2. Rahmenformat

```text
F0 7D 53 51 <ver> <type> <rid> <len> <payload: len Bytes> <crc_hi> <crc_lo> F7
   │  └─┬─┘   │      │      │     │                          └─┬──────────┘
   │   "SQ"   │      │      │     │                            CRC-8 in zwei 7-Bit-Bytes
   │          │      │      │     └ Länge der Nutzdaten 0…109
   │          │      │      └ Request-ID 0…127, vom Panel vergeben, rollierend
   │          │      └ Nachrichtentyp: Anfrage 0x01–0x3F, Antwort = Anfrage | 0x40
   │          └ Protokoll-Hauptversion, hier 1
   └ SysEx-Hersteller-ID „nicht-kommerziell / Entwicklung“
```

| Regel | Wert | Grund |
| --- | --- | --- |
| Rahmenlänge | **≤ 120 Byte** einschließlich `F0` und `F7`, Nutzdaten ≤ 109 Byte | Der gepinnte libDaisy-Parser speichert nur 128 Byte zwischen `F0` und `F7` und meldet längere Nachrichten abgeschnitten, aber als vollständig |
| Antwortbit | Bit 6 von `<type>` (`0x40`) | Antworten, `NAK` (`0x7F`) und Monitorrahmen (`0x7E`) tragen es. Sie werden **nie beantwortet** (Echo-Schutz, wie MD bei Studio-One-Input-Monitoring) |
| Prüfsumme | CRC-8, Polynom `0x07`, Startwert `0x00`, ohne Spiegelung und End-XOR (CRC-8/SMBUS) über `<ver>` bis zum letzten Nutzdatenbyte. Prüfwert für ASCII `123456789`: `0xF4`. Übertragen als `crc_hi = crc >> 7`, `crc_lo = crc & 0x7F` | CRC-8 erkennt Bündelfehler besser als die MD-Summe. Zwei Bytes, weil SysEx-Daten 7 Bit haben |
| Request-ID | 0…127 | Retries wiederholen die ID (§ 4) |

**Eingestreute Echtzeit-Bytes** (`F8`–`FF`, z. B. die MIDI-Clock einer laufenden DAW)
dürfen nach der MIDI-Spezifikation mitten in einer SysEx stehen. Der gepinnte
libDaisy-Parser (`MidiParser::Parse`) gibt sie als eigene Ereignisse aus und sammelt
die SysEx weiter, ebenso `bootloader::SysExMatcher`. Sie erreichen den SQ-Rahmen also
nie. Der Rahmen bleibt gültig, die Clock wirkt normal. `sq_codec.assemble` bildet das
nach, ein Testvektor prüft es.

> **Abweichung vom Arbeitsauftrag 4.9 (begründet):** Dort sollen Rahmen mit
> eingestreuten Echtzeit-Bytes verworfen werden. Das würde bei laufender DAW-Clock
> fast jeden Panel-Rahmen kosten (eine Clock alle 20,8 ms bei 120 BPM) und das Panel
> während der Wiedergabe unbrauchbar machen. Verworfen und gezählt werden deshalb
> Rahmen mit anderen Statusbytes. Getestet werden beide Fälle.

**Annahme eines Rahmens** in dieser Reihenfolge (so prüft es `sq_codec.classify`
für den vom Parser zusammengesetzten Rahmen):

1. Kopf ist nicht `F0 7D 53 51` → kein SQ-Rahmen (§ 1).
2. Letztes Byte ist nicht `F7` → `NAK LENGTH`.
3. Ein Statusbyte (`0x80`–`0xF7`) zwischen `F0` und `F7` → verworfen, Zähler
   `status_in_frame`, `NAK CRC`.
4. Rahmen länger als 120 Byte → verworfen, Zähler `too_long`, `NAK LENGTH`.
5. `<type>` hat das Antwortbit → verworfen, Zähler `echo`, **keine Antwort**.
6. `<len>` passt nicht zur Rahmenlänge → `NAK LENGTH`.
7. `<ver>` ≠ 1 → `NAK VERSION`.
8. CRC falsch → `NAK CRC`.
9. Gleiche Request-ID und gleiche CRC wie die zuletzt ausgeführte Anfrage →
   Wiederholung: Die zwischengespeicherte Antwort wird erneut gesendet, nichts wird
   ein zweites Mal ausgeführt. Zähler `duplicate`.
10. Unbekannter Typ → `NAK UNKNOWN_TYPE`. Sonst wird die Anfrage ausgeführt.

Ein `NAK` hat den Typ `0x7F`, die Request-ID der Anfrage und die Nutzdaten
`u7 Anfragetyp · u7 Fehlercode`. Bei Fehlern in Schritt 2–4 und 6 ist die Request-ID
das Byte an ihrer Position oder 0, falls der Rahmen dafür zu kurz ist.

| Fehlercode | Name | Bedeutung |
| --- | --- | --- |
| 1 | `LENGTH` | Länge, fehlendes `F7` oder Rahmen über 120 Byte |
| 2 | `CRC` | Prüfsumme falsch oder Statusbyte im Rahmen |
| 3 | `UNKNOWN_TYPE` | Typ nicht definiert |
| 4 | `BAD_ARGUMENT` | Wert außerhalb des Bereichs, unbekannte Komponente oder Parameter-ID |
| 5 | `BUSY` | Übergabe-Queue voll, später wiederholen |
| 6 | `VERSION` | Protokoll-Hauptversion nicht unterstützt |
| 7 | `UID_MISMATCH` | Kalibrierung oder Freigabe für ein anderes Kit |
| 8 | `NOT_CALIBRATED` | CV-Freigabe ohne gültige Kalibrierung beider Ausgänge |
| 9 | `NOT_ALLOWED` | im aktuellen Zustand nicht erlaubt (z. B. Start ohne Freigabe) |

**Kodierung der Nutzdaten** (alle Bytes 7 Bit, mehrbytige Werte LSB zuerst):

| Typ | Bytes | Kodierung |
| --- | --- | --- |
| `u7` | 1 | direkt |
| `u14` | 2 | je 7 Bit |
| `u32` | 5 | 32 Bit in 5 × 7 Bit, das letzte Byte ≤ `0x0F` |
| `i32` | 5 | wie `u32`, Zweierkomplement |
| `str` | 1 + n | Länge `u7`, danach druckbares 7-Bit-ASCII. Umlaute zeigt das Panel aus seinen eigenen Texten |
| `step` | 3 | `u7` Note 0…59 (Oktave × 12 + Halbton) · `u7` Gate-Länge 5…100 % · `u7` Bits (0 Gate an, 1 Accent, 2 Slide) |

**Werte von Parametern** sind `i32` in Tausendsteln der Einheit (`Wert × 1000`).
Beispiel: 120 BPM = `120000`, −1,5 dB = `−1500`, Enum-Index 2 = `2000`.

## 3. Nachrichten

### 3.1 Identität und Komponenten

| Typ | Name | Anfrage | Antwort (`type \| 0x40`) |
| --- | --- | --- | --- |
| `0x01` | `IDENTIFY` | — | `u7` Haupt-, `u7` Nebenversion des Protokolls · `str` Produkt (≤ 16, z. B. `step-seq`, `acid-seq`) · `str` Produktversion (≤ 12) · `u32` Boot-ID · 3 × `u32` STM32-UID (Wort 0, 1, 2) · `u14` Fähigkeiten · `u7` Anzahl der Komponenten |
| `0x02` | `GET_COMPONENT` | `u7` Index | `u7` Index · `u7` Anzahl · `str` Komponenten-ID (≤ 16, wie im Register) · `u7` Art (0 Sequencer, 1 Instrument, 2 Effekt, 3 DSP) · `str` Version (≤ 12) · `u7` Schema-Version der Parameter · `u7` Anzahl der Parameter |
| `0x03` | `GET_PARAM_DESC` | `u7` Komponente · `u7` Parameter-Index | `u7` Komponente · `u7` Index · `u7` Anzahl · `u14` Parameter-ID · `u7` Typ · `u7` Einheit · `u7` Flags · `i32` Min · `i32` Max · `i32` Standard · `u7` CC (127 = keiner) · `str` Schlüssel (≤ 16) |
| `0x04` | `GET_ENUM_LABEL` | `u7` Komponente · `u14` Parameter-ID · `u7` Wert | dieselben Felder · `str` Schlüssel des Werts (≤ 16) |

* **Boot-ID:** zufällig pro Start. Ändert sie sich, ist das Gerät neu gestartet, und
  das Panel liest den Zustand neu.
* **Fähigkeiten:** Bit 0 CV-Ausgänge, Bit 1 DIN-MIDI, Bit 2 interne Stimme, Bit 3
  Pattern, Bit 4 Monitor, Bit 5 Kalibrierung.
* **Fragmente:** Listen werden über Index und Anzahl abgefragt (Komponenten,
  Deskriptoren, `GET_PARAMS`). Jeder Rahmen trägt Index und Gesamtzahl, damit das
  Panel Lücken erkennt.
* Typ, Einheit und Flags folgen den Listen `types`, `units` und `flags` in
  `parameters.json` (Index = Code; Flags: Bit 0 nur lesen, Bit 1 logarithmisch).

### 3.2 Parameter

| Typ | Name | Anfrage | Antwort |
| --- | --- | --- | --- |
| `0x05` | `GET_PARAMS` | `u7` Komponente · `u7` erster Index | `u7` Komponente · `u7` erster Index · `u7` Gesamtzahl · `u7` n · n × (`u14` Parameter-ID · `i32` Wert), höchstens 14 je Rahmen |
| `0x06` | `SET_PARAM` | `u7` Komponente · `u14` Parameter-ID · `i32` Wert | `u7` Status (0) · `u14` Parameter-ID · `i32` übernommener Wert (geklemmt und gerastert) · `u14` angenommene Revision |

Parameter werden am Blockrand übernommen. Eine Änderung am Kit (Poti, Schalter)
erscheint im Monitor. Für Potis gilt: Die letzte Änderung gewinnt. Ein Poti schreibt
erst, wenn er sich über seine Hysterese hinaus bewegt.

### 3.3 Pattern

| Typ | Name | Anfrage | Antwort |
| --- | --- | --- | --- |
| `0x10` | `GET_PATTERN` | — | `u14` angenommene Revision · `u14` angewandte Revision · 16 × `step` (52 Byte) |
| `0x11` | `SET_PATTERN` | 16 × `step` (48 Byte) | `u7` Status · `u14` angenommene Revision |
| `0x12` | `SET_STEP` | `u7` Step 0…15 · `step` | `u7` Status · `u14` angenommene Revision |

* Ein Pattern passt in einen Rahmen und wird **atomar** übernommen. `main` legt es in
  eine SPSC-Queue bzw. einen Triple-Buffer, der Callback übernimmt es **am nächsten
  Step-Rand** (3.2 des Auftrags).
* **Revisionen:** Jede angenommene Änderung (Pattern, Step, Parameter) erhöht die
  angenommene Revision (14 Bit, rollierend). Die angewandte Revision meldet, was der
  Callback tatsächlich spielt. Das Panel weiß damit, wann seine Änderung hörbar ist.
* Ungültige Steps (Note > 59, Gate-Länge außerhalb 5…100, unbekannte Bits) lehnen den
  ganzen Rahmen ab (`NAK BAD_ARGUMENT`).

### 3.4 Transport und Zustand

| Typ | Name | Anfrage | Antwort |
| --- | --- | --- | --- |
| `0x20` | `TRANSPORT` | `u7` Befehl (0 Stop, 1 Start) | `u7` Status · `u7` Zustand |
| `0x21` | `GET_STATE` | — | `u7` Zustand (0 STOP, 1 WARTET, 2 LÄUFT, 3 PAUSE) · `u7` Modus · `u7` SW17 (0 Stop, 1 Run) · `u7` SW18 (0 oben, 1 Mitte, 2 unten) · `u7` aktive Clock-Quelle (0 intern, 1 Gate IN 1, 2 MIDI USB, 3 MIDI DIN) · `u7` aktueller Step (0…15, 127 keiner) · `i32` Tempo in milli-BPM · `u14` angenommene · `u14` angewandte Revision · `u7` CV frei (0/1) · `u7` USB verbunden (0/1) |

Panel-Start und -Stop wirken nur bei Freigabe (SW17 auf Run, siehe 4.3 des Auftrags),
sonst `NAK NOT_ALLOWED`.

### 3.5 Kalibrierung und CV-Freigabe

| Typ | Name | Anfrage | Antwort |
| --- | --- | --- | --- |
| `0x28` | `GET_CAL` | `u7` Ausgang (0 CV OUT 1, 1 CV OUT 2) | `u7` Ausgang · `u7` gültig · `i32` Offset in milli-Code bei 0 V · `i32` Skalierung in milli-Code pro Volt · `u14` Gate-Low-Code · `u14` Gate-High-Code · `u14` Klemme min · `u14` Klemme max |
| `0x29` | `SET_CAL` | 3 × `u32` UID · `u7` Ausgang · dieselben sechs Felder | `u7` Status |
| `0x2A` | `CV_ENABLE` | 3 × `u32` UID · `u7` frei (0/1) | `u7` Status · `u7` frei |

* **Code** = Offset + Volt × Skalierung, gerundet und auf die Klemme begrenzt. Eine
  negative Skalierung beschreibt eine invertierende Ausgangsstufe.
* **Grenzen:** Codes 0…4095. |Skalierung| 100 000…2 000 000 milli-Code/V. Klemme
  min < max. Gate-Low und Gate-High innerhalb der Klemme und verschieden. Andere Werte
  werden abgelehnt (`NAK BAD_ARGUMENT`).
* **UID:** Passt sie nicht zum Kit, `NAK UID_MISMATCH`. Panel und `sq_probe.py`
  speichern Kalibrierdaten je UID und senden sie nur an dieses Kit.
* **Getrennte Freigabe:** `SET_CAL` nimmt eine bestehende Freigabe zurück. `CV_ENABLE`
  verlangt gültige Daten für beide Ausgänge (`NAK NOT_CALIBRATED`). Die Freigabe wird
  nie gespeichert und nie automatisch wiederhergestellt. Wird sie bei offenem Gate
  entzogen, geht CV OUT 2 sofort auf den Gate-Low-Code.
* Kalibrierdaten liegen nur im RAM (kein Flash ohne Freigabe des Benutzers).

### 3.6 Monitor und Diagnose

| Typ | Name | Anfrage | Antwort |
| --- | --- | --- | --- |
| `0x30` | `MONITOR` | `u7` an (0/1) | `u7` an |
| `0x31` | `GET_STATS` | — | `u7` n · n × `u32` Zähler (§ 5) |
| `0x32` | `RESET_STATS` | — | `u7` Status |

* Der Monitor sendet ungefragte Rahmen `0x7E` mit `u7` Ereigniscode und Daten. Die
  Request-ID zählt die Monitorrahmen (rollierend), damit das Panel Verluste erkennt.

| Code | Ereignis | Daten |
| --- | --- | --- |
| 1 | Step-Wechsel | `u7` Step · `u7` Gate an |
| 2 | Zustand | wie die ersten fünf Felder von `GET_STATE` |
| 3 | Tempo | `i32` milli-BPM |
| 4 | Revision | `u14` angenommen · `u14` angewandt |
| 5 | Parameter | `u7` Komponente · `u14` Parameter-ID · `i32` Wert (Poti- oder CC-Änderung am Gerät) |
| 6 | Zähler geändert | — (das Panel liest `GET_STATS`) |
| 7 | Step am Kit geändert | `u7` Step · `step` (Poti oder Taste; je geändertem Step ein Rahmen) |

* **Höchstens 50 Rahmen pro Sekunde.** Ereignisse gleicher Art werden zusammengefasst,
  das jüngste gewinnt. Step-Wechsel haben Vorrang.
* Der Monitor endet **3 s nach der letzten `MONITOR`-Anfrage**. Das Panel erneuert ihn
  jede Sekunde.
* Außer Monitorrahmen sendet das Gerät nie ungefragt.

## 4. Zuverlässigkeit (Panel und `sq_probe.py`)

* Timeout **200 ms**, höchstens **3 Wiederholungen** mit derselben Request-ID und
  denselben Bytes. Danach gilt die Anfrage als verloren (Zähler im Panel).
* Das Gerät behandelt Wiederholungen idempotent (§ 2, Schritt 9).
* Antworten mit unerwarteter Request-ID oder unerwartetem Typ werden verworfen und
  gezählt.
* Nach jedem Verbinden liest das Panel zuerst den Gerätezustand. **Das Gerät hat
  Vorrang** (D13). Vom Panel aufs Gerät wird nur auf ausdrückliche Aktion übertragen.

## 5. Zähler (`GET_STATS`)

| Nr. | Zähler |
| --- | --- |
| 0 | `bad_length` |
| 1 | `bad_crc` |
| 2 | `bad_version` |
| 3 | `unknown_type` |
| 4 | `too_long` |
| 5 | `status_in_frame` (Rahmen mit Statusbyte zwischen `F0` und `F7`) |
| 6 | `duplicate` |
| 7 | `echo` (Rahmen mit Antwortbit) |
| 8 | `busy` |
| 9 | `monitor_dropped` |
| 10 | `note_on_dropped` (Queue voll, 4.4) |
| 11 | `midi_queue_overflow` |
| 12 | `clock_ignored` (Clock vom nicht gewählten Port) |
| 13 | `callback_overruns` |
| 14 | `callback_peak_cycles` |
| 15 | `queue_high_water` |
| 16 | `stack_free_bytes` (nie benutzte Stack-Reserve, Stack-Probe) |

Weitere Zähler werden hinten angehängt. Ein Client liest `n` und ignoriert
unbekannte.

## 6. Testvektoren

`contracts/sq_vectors.json` enthält gültige Rahmen (mit Typ, Request-ID und
Nutzdaten) und ungültige Rahmen mit dem erwarteten Ergebnis der Annahme:
gekipptes Bit, falsche Länge, Statusbyte im Rahmen, Version 2, Rahmen über 120 Byte,
unbekannter Typ, MD- und Bootloader-Rahmen. Ein Vektor mit Feld `stream` enthält
eingestreute Echtzeit-Bytes (`F8 FA F8`). Nach `assemble` muss er den gültigen Rahmen
ergeben. Jede Implementierung muss alle
Vektoren genauso einordnen. Der Python-Vertragstest
(`tests/test_acid_seq_contracts.py`) prüft die Vektoren gegen den Referenz-Codec.

## 7. Änderungen

Eine Änderung an diesem Protokoll geschieht nur zusammen mit Firmware, `sq_codec.py`
bzw. `sq_probe.py`, beiden Editoren und ihren Tests. Neue Nachrichten und
angehängte Felder erhöhen die Nebenversion, inkompatible Änderungen die
Hauptversion.
