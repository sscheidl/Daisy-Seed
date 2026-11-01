# Streichwurst FX

Geplanter eigenständiger FX-Baustein für die Funktionen der Streichfett-
Effektsektion: **Animate, Ensemble/Chorus, Phaser und Reverb**. Eigener Code
und eigene Daten auf Basis der dokumentierten Forschung; Originalbeziehungen
und Annäherungen werden getrennt ausgewiesen.

**Stand 2026-11-01:** Auftrag und Modulgrenzen vorbereitet, keine FX-
Implementierung oder eigenständige FX-Firmware vorhanden.

* [Claude-Auftrag](../../instruments/streichwurst/docs/CLAUDE_AUFTRAG.md).
* [Verbindliche Modul-/Routingverträge](../../instruments/streichwurst/docs/MODULAR_ARCHITECTURE.md).
* [Forschungsstand](../../instruments/streichwurst/docs/PROVENANCE.md).
* [Gemeinsamer Audiovertrag](../../common/audio/fx_contract.h).

String-FX und globaler Reverb sind separat aufrufbar: Die Firmware mischt
zwischen ihnen Stringsection und Soli. Dieser Baustein besitzt keine Stimmen,
keine Boardbedienung und keinen MIDItransport. Eigene Header, Zustände,
Versionsquelle, Speichervertrag und Hosttests hier entwickeln. Hardware- und
Appadapter bleiben beim Produkt bzw. bei der gemeinsamen Control-Anbindung.

Die bestehende gemeinsame wet-only FX-Schnittstelle bleibt unverändert.
Zusätzliche Mehrbus-/Animate-Verträge werden explizit beschrieben und getestet.
Ein separater Standalone-FX-Build ist zunächst optional; Pflicht ist die
unabhängige Host-Testbarkeit und Einbindung ins Produkt `streichwurst`.
