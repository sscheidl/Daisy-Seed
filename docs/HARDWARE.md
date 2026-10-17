# Seed3 Desktop Development Kit

Ziel ist ausdrücklich das **Seed3 Desktop Dev Kit**, kein Pod/Field/Patch und kein
unbeschaltetes Seed-Board. Die Anwendung verwendet ausschließlich
`daisy::desktop_devkit::Hardware` aus dem festgelegten libDaisy-Commit.
GPIOs werden in der Anwendung nicht selbst zugeordnet.

## Geprüfte API und Zuordnungen

| Funktion | Offizielle Hardwareklasse | Offizielle Verdrahtung (nur Referenz) |
| --- | --- | --- |
| 16 Taster | `button[0..15]`, `UpdateAllControls()` | 2 verkettete Shiftregister, Clock D8, Latch D9, Daten D10 |
| 16 LEDs | `SetLed(0..15, brightness)`, `UpdateLeds()` | PCA9685, I²C1, SCL D11, SDA D12 |
| 8 Potis | `pot[0..7].Value()` | 8:1-Mux: ADC D15, Auswahl D24/D25/D26 |
| Stereoausgänge | `seed.StartAudio()` | J3 links, J4 rechts, J5 Kopfhörer |
| Audioformat | 48 kHz, 48 Samples pro Block | Seed3/TAC5242, durch libDaisy initialisiert |

`Hardware::UpdateAllControls()` kehrt intern die Shiftregister-Indizes um:
`button[i].Debounce(button_sr.State(15 - i))`. Anwendungscode darf diese Reihenfolge
**nicht nochmals umkehren**. Index 0 entspricht S1/D1, Index 15 entspricht S16/D16.
Die LED-Hardwareklasse berücksichtigt die invertierte Helligkeit bereits selbst.
VR1/VR2 entsprechen Mux-Kanälen und Pot-Indizes 0/1.

Die DigitalControl-Entprellung arbeitet mit einer 8-Bit-Historie und liefert ihre
Flanken nach sieben stabilen Abtastwerten. Bei 1 kHz ergibt das ungefähr 7–8 ms.
Die Firmware hält den entprellten LED-Zustand zwischen Druck- und Loslassflanke.
Dadurch flackert eine LED nicht bei einem einzelnen abweichenden Rohwert.
Eine weitere Druckflanke wird bei bereits gesetztem Haltezustand ignoriert:
DigitalControl kann nach einem kurzen Kontaktaussetzer eine neue Druckflanke
melden, ohne zuvor eine Loslassflanke geliefert zu haben.

Die AnalogControls in `Hardware::Init()` sind für 1 kHz konfiguriert.
Deshalb verwendet die Firmware 48 Samples bei 48 kHz und ruft einmal pro Block
`UpdateAllControls()` auf. Bei der Standard-Slew von 2 ms ergibt AnalogControl
einen Koeffizienten von 1,0 und glättet selbst nicht. Der ADC verwendet 32-faches
Oversampling; der eigene PotFilter übernimmt 20 ms Glättung und 0,1 % Hysterese.
Die äußersten 0,3 % werden auf die Zeit-Endwerte abgebildet.

LED-Daten gehen über eine lockfreie 32-Bit-Mailbox ins Hauptprogramm.
`UpdateLeds()` verwendet den offiziellen DMA-Treiber, dessen Buffer-Swap auf eine
vorherige Übertragung warten kann. Er wird daher ausschließlich außerhalb des
Audiocallbacks und höchstens alle 10 ms bei einer Zustandsänderung aufgerufen.
Ein sehr kurzer entprellter Tastendruck kann vollständig zwischen zwei LED-Abfragen
liegen: Dann erklingt der Ton, ohne dass die LED sichtbar aufleuchtet.

Bekannte Grenze des unveränderten libDaisy-Treibers: Ein sofort fehlgeschlagener
`TransmitDma()`-Start kann den nächsten Buffer-Swap dauerhaft warten lassen.
Dann können die LEDs einfrieren; die höher priorisierte Audiowiedergabe läuft weiter.
Dieses Verhalten ist am eigenen Board noch nicht beobachtet oder gemessen worden.

`Hardware::Init()` initialisiert auch unbenutzte Board-Peripherie wie DIN-MIDI/CV.
Die Anwendung startet keine DIN-MIDI-Verarbeitung und erzeugt keine CV/Gate-Signale.

## USB-MIDI

`MidiUsbHandler` mit `MidiUsbTransport::Config::EXTERNAL` verwendet den Daisy-USB-Port,
der am Desktop-Träger J7 herausgeführt ist (derselbe wie im offiziellen Template für den
Serial-Logger, Produktname „Daisy Seed External“). Eingehende Pakete werden im USB-Interrupt
zu `MidiEvent`s geparst. `ServiceUsbMidi()` holt sie im Hauptprogramm ab und reicht NoteOn
über eine lockfreie Warteschlange an den Audiocallback weiter. Tasten-Flanken gehen den
umgekehrten Weg, weil `SendMessage()` blockieren kann (bis zu drei Wiederholungen mit
je 100 µs) und deshalb nie im Audiocallback laufen darf.

libDaisys Sendefunktion prüft nicht, ob ein Host das Gerät eingerichtet hat. Die Firmware
sendet deshalb nur bei `hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED` und verwirft sonst
die Tastenereignisse. Dieser Zustand stammt aus dem festgelegten libDaisy-Commit.

`MidiHandler` enthält eine FIFO von 256 `MidiEvent`s (ca. 37 KB). Das DTCM ist damit zu
88,7 % belegt (116.208 von 131.072 Bytes), für den Stack bleiben etwa 14,8 KB.

## Offizielle Quellen

Geprüft am 16.10.2026:

* [Hardware und Schaltplan](https://github.com/daisyaudio/Seed3-DevKit-Desktop),
  Stand `987297fb7c9a8c58721ef40abf6d6eedd1fcda2a`.
* [Produktdokumentation](https://docs.daisy.audio/product/Seed3-Desktop-Dev-Kit/).
* [libDaisy](https://github.com/daisyaudio/libDaisy),
  `f715e4875b2d28445d0c11a0a17f064a248f69d0`.
* [Desktop-Hardwareklasse](https://github.com/daisyaudio/libDaisy/blob/f715e4875b2d28445d0c11a0a17f064a248f69d0/src/desktop_devkit.cpp).
* [Offizielles Desktop-Template](https://github.com/daisyaudio/libDaisy/tree/f715e4875b2d28445d0c11a0a17f064a248f69d0/examples/devkits/Desktop-DevKit-UI-Template).
* [DaisySP](https://github.com/daisyaudio/DaisySP),
  `2c72eaf9eac5fc0dca1919d65d606da907832618`.

Die Zuordnung ist gegen API und offizielle Hardwaretabellen geprüft.
Die tatsächliche Bestückung und Funktion des eigenen Boards muss beim ersten
Hardwaretest bestätigt werden.
