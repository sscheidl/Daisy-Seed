# Mod on Daisy — MIDI-Protokoll (Entwurf v1)

**Stand:** 22. Oktober 2026 · **Status:** Entwurf, nicht implementiert. Wird in Phase 2 als
`protocol.h` umgesetzt und in Firmware, Host-Tests und Editor gemeinsam genutzt.
Hintergrund und Begründungen: [ARCHITECTURE.md § 4](ARCHITECTURE.md#4-midi-protokoll-frage-3).

## 1. Ebenen

| Ebene | Nachrichten | Zweck | Braucht Editor? |
| --- | --- | --- | --- |
| **Live** | CC, Channel-Aftertouch, Note On/Off (Velocity, Keytrack), Pitch Bend, optional MIDI-Clock | Werte der MIDI-*Quellen* | nein |
| **Konfiguration** | SysEx, Hersteller-ID `0x7D` | Beschreibungen, Slots, Quellparameter, Presets, Monitoring | ja |

Die eigenen CCs eines Instruments (z. B. CC20–33 in Physical Modeling) bleiben
unverändert. Mod on Daisy **verbraucht** nur die SysEx-Rahmen mit eigenem Kopf sowie die
CCs, die einer MIDI-Quelle zugewiesen sind. Alle anderen Nachrichten gibt
`Controller::HandleMidi()` mit `false` an das Instrument weiter. Eine Ausnahme sind
Noten: Sie werden für Velocity, Keytrack und Retrigger **mitgelesen** und trotzdem
weitergegeben.

### 1.1 Standardbelegung der MIDI-Quellen

| Quelle | Standard | Begründung |
| --- | --- | --- |
| Mod-Wheel | CC1 | Konvention |
| GP1–GP4 | CC16–19 | im MIDI-Standard „General Purpose Controller 1–4“; kollidiert nicht mit CC20–33 der vorhandenen Instrumente |
| Aftertouch | Channel Pressure | — |
| Velocity, Keytrack | letzte Note On | — |
| Pitch Bend | Pitch Bend | als Quelle; die eigene Bend-Verarbeitung des Instruments bleibt |

Jede Zuweisung lässt sich ändern: CC-Nummer, Kanal oder Omni, 7- oder 14-Bit (MSB n,
LSB n+32). Das Instrument meldet seine reservierten CC-Bereiche (`IDENTIFY`), damit der
Editor Konflikte anzeigt. Kanal 16 CC100–119 bleibt für die vorhandene
Telemetrie frei.

## 2. Rahmenformat

```text
F0 7D 4D 44 <dev> <ver> <cmd> <seq> <payload …> <chk> F7
    │  └─┬─┘   │     │     │     │                  └─ Prüfsumme
    │   "MD"   │     │     │     └─ 0…127, rollierend, vom Editor vergeben
    │          │     │     └─ Kommando (Anfragen 0x00–0x3F, Antworten = Anfrage | 0x40)
    │          │     └─ Protokollversion (1)
    │          └─ Geräte-ID 0…15, 0x7F = alle
    └─ Hersteller-ID „nicht-kommerziell“
```

* **Maximale Rahmenlänge 120 Byte** einschließlich `F0` und `F7`. Grund: Der gepinnte
  libDaisy-Parser speichert höchstens 128 Byte zwischen `F0` und `F7` und meldet längere
  Nachrichten abgeschnitten, aber als vollständig.
* **Prüfsumme** im Roland-Stil: `chk = (128 − (Σ Bytes von <dev> bis Ende Payload) mod 128) mod 128`.
  Falsche Prüfsumme oder falsche Länge → `NAK`. Abgeschnittene Rahmen werden so erkannt.
* **Kodierung der Nutzdaten**, alle Bytes 7-Bit:

| Typ | Bytes | Kodierung |
| --- | --- | --- |
| `u7` | 1 | direkt |
| `u14` | 2 | LSB zuerst, je 7 Bit |
| `s14` | 2 | Offset-Binär: `v + 8192`, Bereich −8192…+8191 |
| `depth` | 2 | `s14`; ±8191 ≙ ±1,0 (Auflösung ≈ 1,2·10⁻⁴) |
| `u32` / `f32` | 5 | 32 Bit in 5 × 7 Bit, LSB zuerst (`f32` = IEEE-754-Bitmuster) |
| `str` | 1 + n | Länge `u7` (≤ 16), danach 7-Bit-ASCII |

## 3. Kommandos

Jede Anfrage bekommt genau eine Antwort: `cmd | 0x40` bei Erfolg, `0x7F NAK` bei einem
Fehler. Der Editor hat **höchstens eine Anfrage gleichzeitig offen**, wartet 200 ms und
wiederholt höchstens dreimal. Das Gerät sendet **höchstens eine Antwort pro Millisekunde**,
ausschließlich aus dem Main-Loop.

### 3.1 Identifikation und Beschreibung

| Cmd | Name | Anfrage | Antwort (`| 0x40`) |
| --- | --- | --- | --- |
| `0x01` | `IDENTIFY` | — | Protokollversion, `str` Instrument-Key (z. B. `physical-modeling`), `str` Firmwareversion, `u7` Quellen, `u7` Ziele, `u7` Slots, `u7` Preset-Slots, `u14` Tick-Rate in Hz, `u32` Schema-Hash, reservierte CC-Bereiche (Anzahl + Paare) |
| `0x02` | `GET_SOURCE_INFO` | `u7` Index | Index, `u7` Typ, `u7` Flags (bipolar, audio-fähig, belegt durch Instrument), `str` Name, `u7` Parameterzahl |
| `0x03` | `GET_SOURCE_PARAM_INFO` | `u7` Quelle, `u7` Parameter | `str` Name, `u14` Min, Max, Standard, `u7` Einheit/Kurve, bei Auswahllisten Anzahl der Einträge |
| `0x04` | `GET_TARGET_INFO` | `u7` Index | Index, `u14` stabile ID, `u7` Gruppe, `u7` Reihenfolge, `u7` Art, `u7` Stufen, `u7` max. Rate, `u7` Flags (bipolar), `f32` Anzeige-Min, `f32` Anzeige-Max, `u7` Einheit, `str` Key |
| `0x05` | `GET_TARGET_NAME` | `u7` Index | `str` Anzeigename (getrennt, damit Rahmen klein bleiben) |
| `0x06` | `GET_GROUP_INFO` | `u7` Gruppe | `str` Gruppenname |

Der **Schema-Hash** ist eine CRC-32 über alle Quell-, Parameter- und Zielbeschreibungen.
Kennt der Editor den Hash bereits, überspringt er `0x02`–`0x06`.

### 3.2 Routing und Quellparameter

| Cmd | Name | Anfrage | Antwort |
| --- | --- | --- | --- |
| `0x10` | `GET_SLOT` | `u7` Slot | Slot, `u7` Quelle (0x7F = leer), `u14` Ziel-ID, `u7` Via (0x7F = keine), `depth`, `u7` Flags |
| `0x11` | `SET_SLOT` | wie Antwort von `GET_SLOT` | Slot, `u7` Status (0 = übernommen, 1 = Ziel unbekannt → deaktiviert) |
| `0x12` | `SET_DEPTH` | `u7` Slot, `depth` | Slot |
| `0x13` | `SET_SLOT_FLAGS` | `u7` Slot, `u7` Flags | Slot |
| `0x14` | `CLEAR_SLOTS` | — | — |
| `0x18` | `GET_SOURCE_PARAM` | `u7` Quelle, `u7` Parameter | `u14` Wert |
| `0x19` | `SET_SOURCE_PARAM` | `u7` Quelle, `u7` Parameter, `u14` Wert | — |

Für die Slot-Flags gilt: Bit 0 aktiv, Bit 1–2 Polarität (`0` wie Quelle, `1` uni→bi,
`2` bi→uni), Bit 3 invertieren, Bit 4 Audio-Rate gewünscht. Ein Audio-Rate-Wunsch wird nur
erfüllt, wenn Ziel und Quelle es unterstützen, sonst läuft die Route mit Control-Rate.

Außerhalb einer Transaktion wird jede Änderung **sofort** kompiliert und veröffentlicht.
Beim Ziehen eines Reglers im Editor begrenzt der Editor `SET_DEPTH` auf etwa 50 pro
Sekunde. Der Triple-Buffer sorgt dafür, dass im Gerät immer der letzte Stand gewinnt.

### 3.3 Transaktionen

| Cmd | Name | Wirkung |
| --- | --- | --- |
| `0x20` | `BEGIN` | Ab jetzt wird nur die Schattenkopie geändert. |
| `0x21` | `COMMIT` | Schattenkopie prüfen, kompilieren und auf einmal veröffentlichen. |
| `0x22` | `ABORT` | Schattenkopie verwerfen. |

Wenn 2 s lang keine Nachricht eintrifft, bricht das Gerät die Transaktion ab. Nutzen:
Beim Laden eines Presets aus dem Editor hört man keine Zwischenzustände.

### 3.4 Presets im Gerät

| Cmd | Name | Anfrage | Antwort |
| --- | --- | --- | --- |
| `0x30` | `STORE` | `u7` Slot, `str` Name | nach dem Schreiben. Bis dahin ist der Main-Loop blockiert, der Editor zeigt „Speichere…“ und wartet bis zu 2 s |
| `0x31` | `RECALL` | `u7` Slot | `u7` Status (0 ok, 1 leer, 2 ungültig, 3 fremdes Instrument) |
| `0x32` | `GET_PRESET_INFO` | `u7` Slot | `u7` Status, `str` Name, `u32` CRC |
| `0x33` | `DUMP` | `u7` Slot (0x7F = Arbeitspatch), `u7` Chunk | `u7` Chunk, `u7` Chunks gesamt, bis zu 96 Byte 7-Bit-gepackter Patch-Daten |
| `0x34` | `LOAD_CHUNK` | `u7` Chunk, `u7` gesamt, Daten | Chunk; nach dem letzten Chunk CRC prüfen → Arbeitspatch |
| `0x35` | `SET_POWER_ON` | `u7` Slot (0x7F = aus) | — |

Für Binärdaten gilt die übliche 8-in-7-Packung: 7 Datenbytes und davor ein Byte mit den
sieben MSBs. Ein Patch von etwa 600 Byte braucht damit 8 Chunks.

### 3.5 Monitoring (optional, Phase 3b)

| Cmd | Name | Anfrage | Antwort / Strom |
| --- | --- | --- | --- |
| `0x3C` | `MONITOR` | `u7` Rate in Hz (0 = aus, max. 30), `u7` Auswahl (Quellen, Ziel-Offsets) | Bestätigung; danach Rahmen `0x7C` mit bis zu 32 × `s14` Werten |

Das Abonnement erlischt **3 s nach der letzten `MONITOR`-Anfrage**. Der Editor erneuert es
jede Sekunde. Ein geschlossener Editor lässt deshalb keinen Datenstrom zurück, der eine
DAW belastet. Monitor-Daten liest der Main-Loop aus einem Schnappschuss, den der
Audio-IRQ über eine SPSC-Queue veröffentlicht, nach dem Muster von `CpuSnapshot` in
Physical Modeling.

### 3.6 Fehler

`0x7F NAK`: `u7` abgelehntes Kommando, `u7` Code.

| Code | Bedeutung |
| --- | --- |
| 1 | Prüfsumme falsch |
| 2 | Länge falsch oder Rahmen abgeschnitten |
| 3 | Kommando unbekannt |
| 4 | Wert außerhalb des Bereichs |
| 5 | beschäftigt (Speichern läuft) |
| 6 | Schema oder Version passt nicht |
| 7 | Transaktion fehlt oder ist abgelaufen |
| 8 | Speicherfehler (QSPI) |

## 4. Ablauf einer Editor-Sitzung

```text
Editor                                Gerät
IDENTIFY ───────────────────────────► 
         ◄─────────────────────────── IDENTIFY|40 (Key, Version, Hash …)
[Hash unbekannt] GET_*_INFO × n ────► … ◄── Beschreibungen (einmalig, danach im Cache)
GET_SLOT × 32, GET_SOURCE_PARAM × m ► … ◄── aktueller Zustand
… Benutzer zieht Regler …
SET_DEPTH (≤ 50/s) ─────────────────► sofort hörbar
… Preset aus der Bibliothek laden …
BEGIN, SET_SLOT × 32, SET_SOURCE_PARAM × m, COMMIT ──► auf einmal hörbar
STORE 3 "Pad Drift" ────────────────► QSPI (blockiert kurz)
```

Bei 32 Slots und etwa 40 Quellparametern umfasst eine vollständige Abfrage rund 75
Anfragen. Bei einer Antwort pro Millisekunde und USB-Latenz ist sie in **deutlich unter
einer Sekunde** erledigt (`SCHÄTZUNG`). Über DIN-MIDI mit 3125 Byte/s dauert sie etwa 2 s.

## 5. Offene Punkte

* `OFFEN`: Wie Hosts den von libDaisy erzeugten SysEx-Sendestrom verarbeiten, an dem
  `F7` als getrenntes Paket hängt ([ARCHITECTURE.md § 1](ARCHITECTURE.md#1-ausgangslage-im-repository)).
  Getestet wird mit Windows (Studio One, MIDI-Monitor) und macOS. Falls nötig, kommt ein
  eigenes Framing.
* `OFFEN`: Ob NRPN als Rückfallebene für Tiefen nötig ist, für DAWs, die SysEx filtern
  (NRPN-MSB = Slot, LSB = Feld). Entscheidung erst nach dem VST3-Test.
* `OFFEN`: Preset-Wechsel per Program Change. Standardmäßig aus, weil Instrumente PC
  selbst nutzen können.
