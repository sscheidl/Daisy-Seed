# Changelog Step Seq

## 0.1.0 (2026-10-27), Phase 1

Erster Kern und erste Firmware `step-seq`. Hardware NICHT GEMESSEN.

* **Kern:** Pattern mit 16 Steps und die Regeln aus 4.2 des Auftrags (Länge am Step-Rand,
  Moduswechsel mit gehaltener Position, xorshift32 mit festem Seed, Skalen,
  Transponierung). Dazu Zeitbasis mit Sample-Akkumulator, Swing, Gate-Grenzen und
  Retrigger-Lücke, die Transport-Zustandsmaschine aus 4.3 und die Clock-Quellen Intern,
  Gate IN 1 (Teiler), MIDI (erster Port gewinnt) und Auto (mit Echo-Schutz).
  Außerdem Vorhören mit Kennungen und Slides als Legato.
* **Bedienung:** Tasten mit langem Druck und Akkord-Markierung (R07), Poti-Glättung aus
  Toy Piano (D6), Stufen-Hysterese 25 %, Pickup 2 %, SW18-Entprellung 20 ms.
* **MIDI:** Notenverfolgung je Ausgang, Note-Offs aus der Tabelle, Sendebudget 8
  Nachrichten pro Port und Durchlauf, DIN 24 Byte. Panic bei Stop, Pause, Timeout,
  Kanalwechsel, Abschalten, Überlauf, USB-Verlust und Bootloader.
* **CV:** Kalibrierung mit Grenzen, getrennte Freigabe, UID-Bindung, Glide,
  DAC-Registeradapter ohne HAL im Callback (R01).
* **SQ-Protokoll 1.0** auf der Geräteseite, `tools/sq_probe.py` (Kalibrierung,
  Freigabe, Diagnose).
* **Golden-Test:** FNV-1a über eine feste Ereignisfolge (64 Steps, drei Modi),
  `0x7528DC6F`.

Abweichungen und Ergänzungen gegenüber dem Auftrag:

* Echtzeit-Bytes in SysEx trennt der libDaisy-Parser ab. Der Rahmen bleibt gültig
  (PROTOCOL.md § 2).
* **Auto-Modus:** Ein MIDI-Port, an den gerade Clock gesendet wird, gilt nicht als Quelle
  (Echo-Schutz). Mit „Clock senden“ an folgt Auto deshalb nur Gate IN 1 und Ports mit
  abgeschaltetem Ausgang. Nach dem Timeout setzt die interne Clock im Raster der
  letzten Periode fort.
* **Callback-Diagnose:** eigener Zykluszähler und Stack-Probe in
  `include/step_seq/diagnostics.h` statt des Mod-on-Daisy-Adapters
  (`callback-diagnostics` hängt am MD-Kern, den Step Seq nicht enthält).
* **Monitor:** zusätzlich Ereignis 7 (Step am Kit geändert) und Zähler 16
  (`stack_free_bytes`), beides hinten angehängt.

Korrekturen nach der Codex-Fehlersuche (2026-10-27, `docs/acid-seq/CODEX_FEHLERSUCHE.md`):

* **F03 MIDI-Legato:** `ServiceMidi` sendet erst die Ereignisse aus der Queue und gleicht
  danach die Note-Offs aus der Tabelle ab, nur wenn die Queue leer ist. Ein Slide geht so
  als neues Note-On vor dem alten Note-Off hinaus und wird nicht zum Retrigger. Ein
  wegen voller Queue verlorenes Note-Off wird weiterhin nachgeholt.
* **F04 Clock-Quelle:** Ein Wechsel der Clock-Quelle bei eingeschaltetem SW17 startet den
  Transport neu wie SW17 aus und an: Noten enden, Intern startet sofort am Startpunkt,
  eine externe Quelle wird scharfgeschaltet.
* **F06 Monitor:** Parameteränderungen werden je Komponente und Parameter
  zusammengefasst. Mehrere geänderte Parameter kommen alle mit ihrem neuesten Wert an.
* **F08 Kalibrierung:** Die Prüfung von `scale` rechnet in 64 Bit. `INT32_MIN` wird ohne
  Überlauf abgelehnt.
* **F09 `sq_probe.py`:** Antworten werden vor der Auswertung auf Kopf, Länge, Version
  und CRC geprüft (`sq_codec.decode_reply`). Beschädigte Antworten gelten als nicht
  angekommen und werden wiederholt.

Der Golden-Hash `0x7528DC6F` bleibt unverändert.
* **F11 (aus Phase 6):** `Engine::ParameterApplied` ersetzt nie eine neuere durch eine
  ältere Revision (14 Bit, über den Überlauf).
