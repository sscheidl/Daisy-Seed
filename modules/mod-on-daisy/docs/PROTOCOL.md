# Mod on Daisy — MIDI-Protokoll v1 (Spezifikation)

**Stand:** 23. Oktober 2026 · **Status:** umgesetzt in `include/mod_on_daisy/protocol.h`
(Codec) und `controller.h` (Kommandos), auf dem Host geprüft. Firmware, Host-Tests und
der JUCE-Editor nutzen denselben Codec; `tools/mod_probe.py` spiegelt ihn in Python. Transport: **USB-MIDI** über J7 („Daisy Seed
External“, D6). Grundlagen: [DECISIONS.md](DECISIONS.md) D9 und E9,
[ARCHITECTURE.md](ARCHITECTURE.md) § 3–4.

## 1. Ebenen

| Ebene | Nachrichten | Wirkung | Editor nötig? |
| --- | --- | --- | --- |
| **Live** | CC1, Channel Pressure, Poly Key Pressure, Note On/Off, CC120/123, MIDI-Clock `F8`, Start `FA` | Werte der Quellen 6–8, Drucktabelle, Retrigger, LFO-/Random-Sync | nein |
| **Konfiguration** | SysEx `F0 7D 4D 44 …` | Raster, Quellparameter, Beschreibungen, Diagnose, später Presets | ja |

* Die Live-Ebene ist **omni**, also auf allen Kanälen gültig (v0.1).
* CC1 und Aftertouch werden von Mod on Daisy **verbraucht**.
* Noten werden **mitgelesen und weitergegeben**.
* Fremde SysEx-Rahmen und SysEx mit anderer Projektkennung gibt `HandleMidi()` mit
  `false` an das Instrument weiter.
* Die vorhandenen Instrument-CCs (Physical Modeling CC20–33, Telemetrie Kanal 16
  CC100–119; Nebulae CC20–27) bleiben unberührt.

## 2. Rahmenformat

```text
F0 7D 4D 44 <ver> <dev> <cmd> <seq> <len> <payload: len Bytes> <chk> F7
   │  └─┬─┘   │     │     │     │     │                            └ Prüfsumme
   │   "MD"   │     │     │     │     └ Länge der Nutzdaten, 0…109
   │          │     │     │     └ 0…127, vom Editor vergeben, rollierend
   │          │     │     └ Anfrage 0x01–0x3F; Antwort = Anfrage | 0x40; 0x7F = NAK
   │          │     └ Geräte-ID 0…15; 0x7F = alle (nur für IDENTIFY)
   │          └ Protokoll-Hauptversion, hier 1
   └ SysEx-Hersteller-ID „nicht-kommerziell / Entwicklung“ (D9)
```

| Regel | Wert | Grund |
| --- | --- | --- |
| Rahmenlänge | **≤ 120 Byte** einschließlich `F0` und `F7` | libDaisy speichert höchstens 128 Byte zwischen `F0` und `F7` und meldet längere Nachrichten abgeschnitten, aber als vollständig (`SYSEX_BUFFER_LEN`, E9) |
| Projektkennung | `4D 44` („MD“) | `0x7D` teilen sich alle nicht-kommerziellen Projekte |
| Prüfsumme | `chk = (128 − (Σ Bytes von <ver> bis Ende Payload) mod 128) mod 128` (Roland-Stil) | Erkennt Bytefehler; das Längenfeld erkennt abgeschnittene oder verschobene Rahmen |
| Geräte-ID | Standard 0. In v0.1 fest in der Firmware, später gespeichert (STORAGE § 3) | mehrere Kits an einem Rechner |

**Annahme eines Rahmens** in dieser Reihenfolge:

1. Kopf `7D 4D 44` passt nicht → `false`, ans Instrument.
2. `<dev>` ist nicht die eigene ID und nicht `0x7F` → verbraucht, keine Antwort.
3. `<ver> ≠ 1` → `NAK 6`.
4. Länge oder Prüfsumme falsch → `NAK 1` bzw. `NAK 2`.
5. Sonst ausführen.

**Kodierung der Nutzdaten** (alle Bytes 7-Bit):

| Typ | Bytes | Kodierung |
| --- | --- | --- |
| `u7` | 1 | direkt |
| `u14` | 2 | LSB zuerst, je 7 Bit |
| `u32` | 5 | 32 Bit in 5 × 7 Bit, LSB zuerst |
| `str` | 1 + n | Länge `u7`, danach 7-Bit-ASCII, ohne Null („±“ wird als `+-` übertragen) |
| `packed(n)` | n + ⌈n/7⌉ | 8-in-7: je 7 Datenbytes gehen ein MSB-Byte und 7 × 7 Bit voraus |
| `mask64` | 10 | 64 Bit in 10 × 7 Bit, LSB zuerst; Bit `8·s + t` = Zelle (Quelle s, Ziel t) |
| `depth8` | 1 Byte vor dem Packen | `int8_t` −100…+100 im Zweierkomplement |

## 3. Kommandos

### 3.1 Identifikation und Zielbeschreibung

| Cmd | Name | Anfrage | Antwort (`cmd \| 0x40`) |
| --- | --- | --- | --- |
| `0x01` | `IDENTIFY` | — | `u7` Haupt-, `u7` Nebenversion des Protokolls · `u7 u7 u7` Modulversion · `str` Instrument-Key (≤ 24) · `str` Instrumentversion (≤ 12) · `u7` kMaxActive · `u14` effektive Tick-Rate in Hz · `u7` Preset-Slots (v0.1: 0) · `u32` Schema-Hash · `u7` Fähigkeiten (Bit 0 Speicher, Bit 1 Monitor, Bit 2 Diagnose, Bit 3 Poly AT global = Last-Note) |
| `0x02` | `GET_TARGET` | `u7` Spalte 0…7 | `u7` Spalte · `u7` Flags (Bit 0 vorhanden, Bit 1 `per_voice`, Bit 2 `latched`) · `u14` span · 16383 · `str` Name (≤ 12) · `str` span-Text (≤ 8) |
| `0x03` | `GET_ALLOWED` | — | `mask64` erlaubte Zellen (ARCHITECTURE § 3.2) |

Der **Schema-Hash** ist eine CRC-32 über Instrument-Key, alle acht Zielbeschreibungen und
die Poly-AT-Regel. Kennt der Editor den Hash bereits, überspringt er `GET_TARGET`.

### 3.2 Raster und Quellparameter

Jede Antwort auf ein zustandsänderndes Kommando enthält `u7` Status (0 = übernommen),
`u7` aktive Zellen und `u32` **CRC-32 des gesamten Patches nach der Änderung**.

| Cmd | Name | Anfrage | Antwort |
| --- | --- | --- | --- |
| `0x10` | `GET_MATRIX` | — | `packed(64)` Tiefen · `mask64` eingeschaltet · `u32` Patch-CRC |
| `0x11` | `SET_MATRIX` | `packed(64)` Tiefen · `mask64` eingeschaltet | Status · aktive · Patch-CRC |
| `0x12` | `SET_CELL` | `u7` Quelle · `u7` Ziel · `u7` Betrag 0…100 · `u7` Bits (0 negativ, 1 eingeschaltet) | Status · aktive · Patch-CRC |
| `0x13` | `CLEAR_MATRIX` | — | Status · aktive · Patch-CRC |
| `0x18` | `GET_SOURCE` | `u7` Quelle (0 LFO 1, 1 LFO 2, 2 Random) | `u7` Quelle · `u14` Rate · `u7` Form bzw. Modus · `u7` Sync · `u7` Gate · `u7` Teilung · `u7` Retrigger (Random: 0) |
| `0x19` | `SET_SOURCE` | `u7` Quelle · `u7` Parameter (0 Rate, 1 Form/Modus, 2 Sync, 3 Gate, 4 Teilung, 5 Retrigger) · `u14` Wert | Status · aktive · Patch-CRC |
| `0x1A` | `GET_NAME` | — | `str` Patch-Name (≤ 16) |
| `0x1B` | `SET_NAME` | `str` Name | Status · aktive · Patch-CRC |

**Rahmengrößen**: `GET_MATRIX`-Antwort = 9 Kopf + 74 + 10 + 5 + 2 = **100 Byte**;
`SET_MATRIX` = **95 Byte**. Das ganze Raster wird also in einem Rahmen übertragen und
**atomar** übernommen. Ein ungültiges Raster (mehr als 8 aktive Zellen, nicht erlaubte
Zelle aktiv, Tiefe außerhalb ±100) wird als Ganzes abgelehnt.

**Ablehnungen bei `SET_CELL`**:

* Eine 9. aktive Zelle → `NAK 7`; der Patch bleibt unverändert.
* Eine nicht erlaubte Zelle → `NAK 8`.
* Eine ausgeschaltete Zelle darf jederzeit eine Tiefe bekommen. Sie zählt erst mit,
  wenn sie eingeschaltet wird.

### 3.3 Diagnose und Monitoring

| Cmd | Name | Anfrage | Antwort |
| --- | --- | --- | --- |
| `0x30` | `MONITOR` | `u7` Rate Hz (0 = aus, ≤ 25) | `u7` Rate; danach ungefragte Rahmen `0x70` mit 8 × `u14` Quellwerten, 8 × `u14` globalen Offsets und 2 × `u14` CV-Rohwerten vor Kalibrierung und Totzone (Offset-Binär, 8192 = 0, ±8191 = ±1), 36 Byte |
| `0x31` | `GET_STATS` | — | 10 × `u32` (50 Byte): Ticks · Matrix-Zyklen min/mittel/max · Callback-Last mittel/max (in 0,01 %) · Überläufe · verlorene MIDI-Ereignisse · Main-Loop-Höchstwerte in Zyklen für `HandleMidi`-Durchlauf und `Service` (Messfirmware, TEST_PLAN § 3) |
| `0x32` | `RESET_STATS` | — | — |
| `0x33` | `SET_SCENARIO` | `u7` Szenario-Nr. | Status (nur Messfirmware `bench/`; im Instrument `NAK 9`) |

* Das Monitoring endet **3 s nach der letzten `MONITOR`-Anfrage**. Der Editor erneuert
  es jede Sekunde. Ein geschlossener Editor hinterlässt deshalb keinen Datenstrom.
* Die Werte liest der Main-Loop aus einem Schnappschuss, den der Audio-IRQ über eine
  SPSC-Queue veröffentlicht.
* Außer Monitor-Rahmen sendet das Gerät **nie ungefragt**.

### 3.4 Gerät und Presets (spezifiziert, in v0.1 nicht implementiert)

| Cmd | Name | Anfrage | Antwort |
| --- | --- | --- | --- |
| `0x20` | `STORE` | `u7` Slot 0…7 | Status · `u32` CRC (erst nach Abschluss; siehe STORAGE § 5) |
| `0x21` | `RECALL` | `u7` Slot | Status (0 ok, 1 leer, 2 CRC ungültig, 3 fremdes Instrument/Schema) · aktive · Patch-CRC |
| `0x22` | `GET_SLOT_INFO` | `u7` Slot | Status · `str` Name · `u32` CRC · `u32` Sequenz |
| `0x23` | `SET_POWER_ON` | `u7` Slot (0x7F = Standardpatch) | Status |
| `0x24` | `GET_CALIBRATION` | — | CV-Offsets 2 × `u14` · Geräte-ID |
| `0x25` | `SET_CALIBRATION` | wie Antwort | Status (v0.1: nur RAM, ohne Speichern) |

In v0.1 beantwortet die Firmware `0x20`–`0x23` mit `NAK 9`, der Editor graut die
Speicherfunktionen aus (IDENTIFY, Bit 0). `0x24`/`0x25` dienen in Phase 2 der Messung der
CV-Nullpunkte und wirken nur im RAM.

### 3.5 Fehler

`0x7F NAK`: `u7` abgelehntes Kommando, `u7` Code.

| Code | Bedeutung |
| --- | --- |
| 1 | Prüfsumme falsch |
| 2 | Länge falsch (Längenfeld ≠ tatsächliche Länge, Rahmen > 120 Byte) |
| 3 | Kommando unbekannt |
| 4 | Wert außerhalb des Bereichs |
| 5 | beschäftigt |
| 6 | Protokollversion nicht unterstützt |
| 7 | Grenze aktiver Verbindungen erreicht |
| 8 | Zelle nicht erlaubt (ausgegraut) |
| 9 | nicht implementiert |
| 10 | Speicherfehler |
| 11 | Schema passt nicht |

## 4. Zuverlässige Übertragung

1. **Eine offene Anfrage**: Der Editor sendet die nächste Anfrage erst nach Antwort
   oder Zeitüberschreitung (250 ms). Danach wiederholt er höchstens dreimal mit
   **derselben** `seq`. Bleibt die Antwort aus, gilt die Verbindung als getrennt.
2. **Wiederholungen ohne Doppelwirkung**:
   * Bei zustandsändernden Kommandos (`SET_*`, `CLEAR_MATRIX`, `SET_SCENARIO`) merkt sich
     das Gerät die letzte `seq` und die letzte Antwort.
   * Kommt ein bytegleicher Rahmen mit derselben `seq` erneut, sendet es die
     gespeicherte Antwort, ohne erneut auszuführen.
   * Jede andere Änderung des Patches löscht diesen Speicher, zum Beispiel eine
     Tastenkombination.
   * Lesekommandos werden immer frisch beantwortet.
   * Clients beginnen mit einer zufälligen `seq`, damit ein neu gestarteter Client
     nicht versehentlich eine alte Antwort erhält.
3. **Bestätigung mit CRC**: Jede Änderung wird mit der Patch-CRC beantwortet. Der Editor
   berechnet dieselbe CRC aus seinem Modell, mit demselben `patch.h`. Weichen beide ab,
   folgt eine **Neusynchronisation** mit `GET_MATRIX`, `GET_SOURCE` × 3 und `GET_NAME`.
4. **Atomare Übernahme** ganzer Raster über `SET_MATRIX`, etwa beim Laden einer Datei
   im Editor.
5. **Gedrosseltes Ziehen**: Beim Ziehen einer Tiefe sendet der Editor `SET_CELL`
   höchstens alle 20 ms. Kommt eine Antwort aus, sendet er den jeweils letzten Wert.
6. **Begrenzte Last im Gerät**: Antwortwarteschlange mit 2 Rahmen (weitere werden
   verworfen und vom Editor wiederholt), höchstens ein Rahmen pro `Service()`, höchstens eine
   Antwort pro Millisekunde, Senden nur bei konfiguriertem USB-Host (wie Toy Piano).

## 5. Ablauf einer Sitzung

```text
Editor                                         Gerät
IDENTIFY (dev 0x7F) ─────────────────────────► jedes Gerät antwortet mit seiner ID
[Hash unbekannt] GET_TARGET × 8, GET_ALLOWED ► Spaltenköpfe, ausgegraute Zellen
GET_MATRIX, GET_SOURCE × 3, GET_NAME ────────► Zustand; CRC vergleichen
… Zelle wählen, Tiefe ziehen …
SET_CELL (≤ 50/s) ───────────────────────────► sofort hörbar; Antwort mit CRC
… Datei laden …
SET_MATRIX, SET_SOURCE × 6, SET_NAME ────────► Raster auf einmal, Parameter danach
MONITOR 20 (jede Sekunde erneuert) ──────────► Live-Werte für die Zeilen-/Zellenanzeige
```

Eine vollständige Synchronisation braucht 15 Anfragen. Über USB dauert sie
`SCHÄTZUNG` **unter 0,1 s**.

## 6. Offene Punkte

* `OFFEN`: Wie Windows den SysEx-Sendestrom der gepinnten libDaisy verarbeitet, die
  `F7` als getrenntes Paket sendet (E9). Das ist der **erste Hardwaretest in Phase 2**
  (TEST_PLAN § 4). Falls nötig, kommt ein eigenes Framing im Adapter.
* `OFFEN`: Gleichzeitige Portnutzung durch Editor und Studio One (E10). Phase 3.
* Später: 14-Bit-Modwheel (CC1/CC33), MPE, NRPN als Rückfallebene für DAWs, die SysEx
  filtern.
