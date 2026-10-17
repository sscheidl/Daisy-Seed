# USB-MIDI

Das Kit meldet sich am Computer über **J7 (Desktop-Träger)** als USB-MIDI-Gerät
**„Daisy Seed External“** (Class-compliant, kein Treiber nötig). Es ist derselbe
Anschluss, über den das Kit Strom bekommt und geflasht wird. Der Seed3-eigene
USB-Port bleibt ungenutzt.

## Verhalten

| Richtung | Verhalten |
| --- | --- |
| **Empfang** | Note On auf **jedem Kanal**, Noten 0–127. Jede Note klingt in ihrer echten Tonhöhe, die Velocity (1–127) skaliert die Lautstärke **linear**. |
| | Es gibt **16 MIDI-Stimmen**, getrennt von den 16 Tastenstimmen. Ist jede belegt, übernimmt die älteste Stimme die neue Note (ohne Knacken, Pegel und Phase laufen stetig weiter). Wird eine noch klingende Note erneut gespielt, startet sie auf derselben Stimme neu. |
| | Attack und Decay kommen wie bei den Tasten von VR1/VR2 zum Zeitpunkt des Anschlags. |
| | **Note Off, Sustainpedal, CC, Pitch Bend, Program Change** werden ignoriert. Jede Note ist ein One-Shot-AD ohne Sustain, wie bei den Tasten. |
| **Senden** | S1–S16 senden ihre Note (Tabelle in der [README](../README.md)) auf **Kanal 1**: Note On mit Velocity 100 beim entprellten Druck, Note Off beim Loslassen. |
| | S8 und S9 senden dieselbe Note 72. Halten beide, sieht die DAW zwei Note On und beim Loslassen zwei Note Off. |
| | Gesendet wird nur, wenn der Computer das Gerät fertig eingerichtet hat. Ohne USB-Verbindung werden Tasten nur lokal gespielt. |

Die Tasten klingen **immer lokal**, unabhängig vom USB-Senden. Eingehende MIDI-Noten
werden **nicht** wieder als MIDI ausgegeben.

## Studio One (nicht am Gerät geprüft)

1. Kit über J7 verbinden und starten. Studio One neu starten, falls das Gerät erst
   danach angeschlossen wurde.
2. **Optionen → Externe Geräte → Hinzufügen…**
   * **Neuer Controller/Keyboard:** Empfangen von *Daisy Seed External*. Damit lassen sich
     die 16 Tasten aufnehmen.
   * **Neues Instrument:** Senden an *Daisy Seed External*. Damit spielt eine
     Instrumentenspur das Board. Die Audioausgabe bleibt an den Klinkenbuchsen des Kits.
3. Audio kommt nicht über USB. J3/J4 (oder J5) wie bisher mit Interface oder Kopfhörer verbinden.

**Doppelte Töne vermeiden:** Sendet eine Spur an das Board und ist gleichzeitig das
Keyboard als Eingang aktiv und die Spur auf Monitor, laufen die Tasten durch die DAW
zurück zum Board. Dann klingt jeder Anschlag zweimal (lokal und verzögert).
Für reines Aufnehmen die Spur nicht an das Board senden lassen, oder den Monitor ausschalten.

## Grenzen

* Neue Noten werden im Millisekundenraster übernommen (Audioblock, 1 ms) und im
  Hauptprogramm abgeholt. Das ist für Spielen unkritisch, aber keine Sample-genaue Zeitauflösung.
* Pro Audioblock werden höchstens 16 MIDI-Noten gestartet, die Warteschlange fasst 64.
  Mehr gleichzeitige Noten in einer Millisekunde gehen verloren.
* Die USB-Seriell-Ausgabe des Templates (`Logger`) wird nicht verwendet. Der Port ist
  ein reines MIDI-Gerät.
* Firmware-Updates ändern sich nicht: RESET, danach BOOT in der Bootloader-Wartezeit. Der
  Bootloader läuft vor der Anwendung und meldet sich selbst als DFU-Gerät.
