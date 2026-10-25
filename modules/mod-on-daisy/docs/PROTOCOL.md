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
2a. `<cmd>` hat das Antwortbit (`0x40`–`0x7F`: Antwort, `NAK`, Monitorrahmen) → verbraucht,
   **nie beantwortet**. Grund (Hardwaretest 24.10.2026): Studio One schickt mit
   Input-Monitoring alles, was vom Daisy kommt, an den Daisy zurück. Ein NAK auf jeden
   zurückgeschickten Rahmen ergab endlose NAK-Schleifen (gemessen rund 500 Rahmen/s), die
   die Antworten an den Editor verdrängten. Teil von `MOD_ON_DAISY_EXTENSIONS`
   (Physical Modeling `0.1.0-mod4`); einzeln mit `MOD_ON_DAISY_IGNORE_REPLIES=1`
   (Toy Piano ab v0.4). Toy Piano v0.3 und die Messfirmware haben die Regel nicht, damit
   ihre Images unverändert bleiben.
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
| `0x01` | `IDENTIFY` | — | `u7` Haupt-, `u7` Nebenversion des Protokolls · `u7 u7 u7` Modulversion · `str` Instrument-Key (≤ 24) · `str` Instrumentversion (≤ 12) · `u7` kMaxActive · `u14` effektive Tick-Rate in Hz · `u7` Preset-Slots (v0.1: 0) · `u32` Schema-Hash · `u7` Fähigkeiten (Bit 0 Speicher, Bit 1 Monitor, Bit 2 Diagnose, Bit 3 Poly AT global = Last-Note, Bit 4 Grundwerte im Monitor, Bit 5 erweiterter Monitor, Bit 6 LFO-Quermodulation; § 3.6) · optional angehängt `u7` Fähigkeiten 2 (Bit 0 Instrumentmodus; § 3.6) |
| `0x02` | `GET_TARGET` | `u7` Spalte 0…7 | `u7` Spalte · `u7` Flags (Bit 0 vorhanden, Bit 1 `per_voice`, Bit 2 `latched`) · `u14` span · 16383 · `str` Name (≤ 12) · `str` span-Text (≤ 8) |
| `0x03` | `GET_ALLOWED` | — | `mask64` erlaubte Zellen (ARCHITECTURE § 3.2) |
| `0x04` | `GET_MODE` | `u7` Modus 0…Anzahl−1 | `u7` Modus · `u7` Anzahl · `str` Name (≤ 24) · `str` Bedienhinweis (≤ 24). Nur mit Fähigkeiten 2 Bit 0, sonst `NAK 3` (§ 3.6) |

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
| `0x18` | `GET_SOURCE` | `u7` Quelle (0 LFO 1, 1 LFO 2, 2 Random) | `u7` Quelle · `u14` Rate · `u7` Form bzw. Modus · `u7` Sync · `u7` Gate · `u7` Teilung · `u7` Retrigger (Random: 0); mit Fähigkeit Bit 6 bei LFO 1/2 zusätzlich `u14` fm + 100 (§ 3.6) |
| `0x19` | `SET_SOURCE` | `u7` Quelle · `u7` Parameter (0 Rate, 1 Form/Modus, 2 Sync, 3 Gate, 4 Teilung, 5 Retrigger, 6 fm + 100 nur mit Fähigkeit Bit 6) · `u14` Wert | Status · aktive · Patch-CRC |
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
| `0x30` | `MONITOR` | `u7` Rate Hz (0 = aus, ≤ 25) | `u7` Rate (Antwortcode `0x70`); danach ungefragte Rahmen **`0x7E`** mit 8 × `u14` Quellwerten, 8 × `u14` globalen Offsets und 2 × `u14` CV-Rohwerten vor Kalibrierung und Totzone (Offset-Binär, 8192 = 0, ±8191 = ±1), 36 Byte. Mit Fähigkeit Bit 4 folgen 8 × `u14` Grundwerte der Spalten (Poti-/CC-Stellung oder fester Wert vor der Modulation, 0…1; −1 = nicht gemeldet), dann 52 Byte. Mit Bit 5 folgen außerdem 8 × `u14` Minimum und 8 × `u14` Maximum je Quelle im Monitorfenster, `u7` Taktzustand, `u7` Aftertouch-Art, `u7` Note, `u7` Wert, `u7` Alter: 89 Byte. Mit Fähigkeiten 2 Bit 0 folgen `u7` aktiver Modus und `u7` Herkunft: 91 Byte (§ 3.6) |
| `0x31` | `GET_STATS` | — | 10 × `u32` (50 Byte): Ticks · Matrix-Zyklen min/mittel/max · Callback-Last mittel/max (in 0,01 %) · Überläufe · verlorene MIDI-Ereignisse · Main-Loop-Höchstwerte in Zyklen für `HandleMidi`-Durchlauf und `Service` (Messfirmware, TEST_PLAN § 3) |
| `0x32` | `RESET_STATS` | — | — |
| `0x33` | `SET_SCENARIO` | `u7` Szenario-Nr. | Status (nur Messfirmware `bench/`; im Instrument `NAK 9`) |

* Das Monitoring endet **3 s nach der letzten `MONITOR`-Anfrage**. Der Editor erneuert
  es jede Sekunde. Ein geschlossener Editor hinterlässt deshalb keinen Datenstrom.
* Die Werte liest der Main-Loop aus einem Schnappschuss, den der Audio-IRQ über eine
  SPSC-Queue veröffentlicht.
* Außer Monitor-Rahmen sendet das Gerät **nie ungefragt**.
* Grundwerte und erweiterter Monitor sind **abwärtskompatible Erweiterungen**: Sie
  hängen hinten an, ein Client, der 36 Byte liest, merkt nichts. Ein Client erkennt sie
  an der Rahmenlänge und an den Fähigkeitsbits. Einzelheiten in § 3.6.
* Die Monitor-Rahmen haben den Code `0x7E`, weil `0x70` schon die Antwort auf `MONITOR`
  ist (`0x30 | 0x40`). Der Browser-Editor-Test hat diese Kollision gefunden. Code `0x7E`
  gehört zur Anfrage `0x3E`, die es nicht gibt und die immer mit `NAK` beantwortet wird.

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

### 3.6 Erweiterungen (Fähigkeiten Bit 4–6, Stand 24.10.2026)

Die Erweiterungen entstehen nur beim Bauen mit `MOD_ON_DAISY_EXTENSIONS=1` und werden
pro Instrument zur Laufzeit eingeschaltet (`Module::EnableExtensions({monitor, lfo_fm})`).
Ohne den Schalter ist der Code bytegleich zum bisherigen Stand. Eingeschaltet sind sie
derzeit nur im Physical-Modeling-Build `MOD=1` (`0.1.0-mod3`, Fähigkeiten `0x7A`). Toy
Piano v0.3 und die Messfirmware melden weiter `0x0A` und senden 36-Byte-Rahmen.

**Erweiterter Monitor (Bit 4 + Bit 5).** Der Monitorrahmen `0x7E` wächst auf 89 Byte:

| Byte | Inhalt |
| --- | --- |
| 0–35 | 8 Quellwerte, 8 globale Offsets, 2 CV-Rohwerte (wie bisher) |
| 36–51 | 8 Grundwerte der Spalten, 0…1, −1 = nicht gemeldet (`Engine::ReportBase`) |
| 52–67 | 8 × `u14` Minimum je Quelle über das Monitorfenster |
| 68–83 | 8 × `u14` Maximum je Quelle über das Monitorfenster |
| 84 | Taktzustand: Bit 0 MIDI-Clock läuft, Bit 1 Gate 1, Bit 2 Gate 2 |
| 85 | Aftertouch-Art der zuletzt empfangenen Nachricht: 0 keine, 1 Kanal, 2 Poly |
| 86 | Note der letzten Poly-AT-Nachricht |
| 87 | Druckwert: Kanal-AT aktuell; Poly AT der Note aus Byte 86, 0 sobald sie losgelassen ist (auch nach All Notes Off) |
| 88 | Alter der Aftertouch-Nachricht in 20 ms, 127 = 2,54 s oder älter |

* **Fenster:** Minimum und Maximum gelten seit dem vorigen Rahmen (bei 20 Hz 50 ms) und
  werden im Audio-IRQ pro Tick nachgeführt. Der Main-Loop schaltet nach jedem Rahmen eine
  atomare Fensternummer weiter; der Audio-IRQ setzt beim nächsten Tick seine eigenen
  Akkumulatoren zurück. Es gibt also keinen ungeschützten Schreibzugriff des Main-Loops
  auf gemeinsame Werte. Eine Quelle, die keine aktive Verbindung hat, wird nicht
  berechnet; ihr Fenster zeigt den Ruhewert.
* **Taktzustand:** Ein Takt gilt als laufend, solange der letzte Impuls weniger als
  3 Perioden (MIDI-Clock, mindestens 0,25 s) bzw. 2,5 Perioden (Gate, mindestens 50 ms)
  zurückliegt, höchstens 4 s. So unterscheidet der Editor „kein Takt“ von „keine
  Monitordaten“.
* **Aftertouch-Art:** Bestimmt durch die zuletzt empfangene Nachricht, nicht durch den
  größeren Wert. Eine Nachricht mit Wert 0 setzt die Art ebenfalls. Die Erfassung läuft
  unabhängig davon, ob eine Matrixverbindung aktiv ist. Die Quellen Kanal-AT (Zeile 6)
  und Poly AT (Zeile 7) bleiben technisch getrennt; die Zeile Poly AT folgt weiter der
  Regel N1 (zuletzt gespielte Note).

**LFO-Quermodulation (Bit 6).** Jeder LFO bekommt einen Parameter `fm` von −100 bis
+100: Der jeweils andere LFO verändert seine Rate um bis zu ±2 Oktaven
(Wert des anderen LFOs aus dem vorigen Tick × fm/50 Oktaven). Wirksam nur im freien Lauf;
bei MIDI-Clock oder Gate bestimmt der Takt die Rate, der Wert bleibt gespeichert.

* `SET_SOURCE` mit Parameter `6` (`fm`), Wert `fm + 100` (0…200). Ohne Bit 6 → `NAK 4`.
* `GET_SOURCE` hängt für LFO 1 und 2 `u14` `fm + 100` an (Nutzdaten 10 statt 8 Byte).
* Der Patch hat dann **Format 2** mit 111 statt 109 Byte (STORAGE § 2). Die Patch-CRC
  deckt das Formatbyte ab: Standardpatch Format 2 `C422B385`, Testvektor `9D6E020E`
  (Format 1: `61C264C4`, `E2340CC6`).

**Instrumentmodus (Fähigkeiten 2, Bit 0; Stand 24.10.2026 abends).** Ein Instrument mit
umschaltbaren Modi (Physical Modeling: sechs Modelle über SW17/SW18 oder CC28) meldet,
welcher Modus gerade klingt. Eingeschaltet mit `Extensions::modes`, nur zusammen mit dem
erweiterten Monitor; derzeit nur im Physical-Modeling-Build `MOD=1` (`0.1.0-mod4`).

* Die sieben Fähigkeitsbits von IDENTIFY sind belegt. Deshalb hängt IDENTIFY ein zweites
  Byte **Fähigkeiten 2** an. Ein Client, der nach den Fähigkeiten aufhört zu lesen, merkt
  nichts; fehlt das Byte, gilt 0.
* `GET_MODE` (`0x04`) liefert Name und Bedienhinweis eines Modus, z. B. „Western Chords“
  und „SW17 gedrückt · SW18 Mitte“. Der Editor fragt alle Modi einmal beim Verbinden ab.
* Der Monitorrahmen wächst um 2 Byte auf **91 Byte**:

| Byte | Inhalt |
| --- | --- |
| 89 | aktiver Modus 0…Anzahl−1; 127 = noch unbekannt |
| 90 | Herkunft der letzten Umschaltung: 0 Bedienelemente am Gerät, 1 MIDI, 127 unbekannt |

* Gemeldet wird der Modus, der **klingt** (bei Physical Modeling nach dem Überblenden),
  nicht der angeforderte.

Wer ein Protokollfeld ändert, ändert Firmware, `mod_probe.py`, beide Editoren, die
Simulatoren und ihre Tests gemeinsam.

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
