## Änderung

Toy Piano v0.1 für das Seed3 Desktop Dev Kit: Alle 16 Taster spielen eigene
Sinus-/Oberton-Stimmen mit One-Shot-AD-Hüllkurven. VR1 steuert 2–300 ms Attack,
VR2 50–3000 ms Decay; die 16 LEDs zeigen die entprellten Tastenzustände.
Hardwarezugriff über `desktop_devkit::Hardware`, reproduzierbare Bibliotheks-Commits
und BOOT_SRAM-Build mit externem DFU-Bootloader.

## Validierung

* ARM-GCC-Cross-Compile und BIN-Vektortabelle erfolgreich geprüft.
* Host-Tests für alle Tonhöhen, Hüllkurvenzeiten, Retrigger, Potifilter,
  unabhängige Polyphonie und Pegel bestanden, auch mit ASan/UBSan.
* Build-/Flash-Anleitungen und CI-Artefakt-Workflow enthalten.
* Hardware-Flash, Taster-/LED-Zuordnung am Gerät und Audio-/Laufzeittest stehen aus.

Details: `docs/VALIDATION.md`.
