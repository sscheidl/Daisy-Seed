# Diagnose 0.4.1: Sendepuffer bei Audio-Schnappschuessen

## Befund und Ursache

**GEMESSEN am 01.11.2026, Firmware 0.4.0**, Seed3 Desktop an J7/COM15,
Windows, AUDIO OUT LEFT/RIGHT jeweils mit dem gleichnamigen Eingang verbunden.
Bei einem Sinus mit 1000 Hz und -20 dBFS kamen beide Kanaele richtig zugeordnet
ohne gesaettigte Samples zurueck. Im ersten Durchgang waren alle fuenf
Schnappschuesse mit je 2048 Stereo-Frames vollstaendig; vier ST-Zustandsrahmen
fehlten und die Firmware meldete `txdrop=4`.

Nach einem vom Bediener bestaetigten Neustart wiederholten sich zwei ST-Luecken.
Zusaetzlich fehlte im vierten Schnappschuss der Rahmen `SC i=50`; dieser
Schnappschuss wurde nicht vollstaendig zusammengesetzt. Im Rohdatenstrom fehlen
128 Bytes am Anfang dieses Rahmens, sein hinterer Teil ist vorhanden.
Die Firmware meldete dabei zwei verworfene Rahmen, passend zu den ST-Luecken.
Die Ursache dieses zusaetzlichen Byteverlusts ist **nicht bestimmt**.

Die ST-Luecken sind dagegen im Hosttest mit der echten portablen `diag::App`
reproduziert: Bei einem 10 ms belegten USB-Endpunkt wird ohne Schnappschuss kein
ST-Rahmen verworfen, mit Schnappschuss genau einer. `PumpScope` fuellte den
1536-Byte-Sendepuffer mit mehreren Datenrahmen. Das naechste periodische
Zustandsupdate passte nicht mehr hinein. `TxQueue::Append` verwarf es korrekt
gezaehlt; die bereits erhoehte ST-Sequenz machte die Luecke am Host sichtbar.

Zwei zusaetzliche Reihen mit je zwoelf Schnappschuessen ohne Testton waren
vollstaendig, sowohl mit dem Windows-Standardpuffer als auch nach Anforderung
von 65536 Empfangsbytes. Es wurden keine nativen Windows-Empfangsfehler gemeldet.
Die ST-Luecken traten in beiden Reihen auf. Ein groesserer Hostpuffer ist damit
keine nachgewiesene Loesung fuer den Firmware-Sendepuffer.

## Aenderung

Firmware **0.4.1** reiht einen neuen Oszilloskop-Datenrahmen erst ein, wenn der
wartende Sendepuffer leer ist. Neben dem gerade uebertragenen USB-Puffer wartet
dadurch hoechstens ein weiterer SC-Rahmen. Statusupdates und Befehlsantworten
haben waehrend kurzer USB-Busy-Phasen Platz. Protokoll, Audio-Callback, SD-Treiber
und Host-App bleiben unveraendert; die App 0.4.0 ist weiterhin kompatibel.

Dies beseitigt den reproduzierten ST-Verlust. Ob die kleinere Bulk-Warteschlange
auch den einzelnen unvollstaendigen Schnappschuss verhindert, muss am Geraet
geprueft werden; eine Ursache oder erfolgreiche Behebung wird dafuer nicht behauptet.

## Gezielte Softwarepruefung

- Neuer Regressionstest fuer 256 und 2048 Frames mit 10 ms USB-Busy ueber einer
  Statusfrist sowie einer PING-Antwort: vor der Aenderung fehlgeschlagen;
  nachher lueckenlose ST-Sequenz, vollstaendige SC-Folge, Antwort vorhanden,
  kein `txdrop` und keine Wiederverwendung eines aktiven USB-Puffers.
- Main-Loop-Suite mit clang, `-Wall -Wextra -Werror`: 948 Pruefungen bestanden.
- Core-/Protokoll-Suite samt Versionsantwort: 465 Pruefungen bestanden.
- Beide ARM-Varianten werden lokal mit der festgelegten Toolchain gebaut und
  auf BOOT_SRAM-Vektoren und ihre jeweilige SD-Symbolrichtlinie geprueft.

Hardware **0.4.1: NICHT GEMESSEN**. Keine Agenten-Flashbefehle ausgefuehrt.
Die Hardwarebeobachtungen oben gelten ausschliesslich fuer 0.4.0. Die Bildschirmanzeige
ist durch diesen Firmwarefix unveraendert; ein neuer GUI-Screenshot ist nicht erforderlich.

## Noch am Geraet zu pruefen

Mit Firmware 0.4.1 denselben Audio-Loopback mit fuenf Schnappschuessen wiederholen:
vollstaendige 2048-Frame-Schnappschuesse, lueckenlose ST-Sequenz, kein neuer
`txdrop`, richtige Kanalzuordnung, Testton und CV OUT am Ende Off. Ein Reboot
ersetzt diesen Vergleich nicht; benoetigt wird das neue Firmwareimage.
