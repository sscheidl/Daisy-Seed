# Diagnostic App 0.3.3: Control Panel

Die Firmware bleibt bei 0.3.2. Die neue App ordnet die vorhandenen Funktionen
rechts in vier direkt erreichbare Reiter: **SD card**, **CV / Gate**,
**Controls** und **USB**. Der JSON-Bericht ist aus jedem Reiter erreichbar.

CV / Gate zeigt CV IN 1/2 normalisiert, ADC-Rohwerte und Min/Max, beide Gate-
Zustände mit Flankenzähler sowie die vom Gerät gemeldeten CV-OUT-DAC-Codes.
Einstellwerte und gemeldete Ausgangscodes sind getrennt. Voltwerte werden nicht
als gemessen ausgegeben. Tab-Wechsel ändern weder CV-Modus noch Kartenbesitz.

Umbruchfähige Statusfelder und vier statt acht LED-Kästchen je Zeile vermeiden
das vorherige horizontale Abschneiden des rechten Panels. Simulation bleibt
markiert; ihre Steuerung liegt unter Controls. Die Firmware-Updatefunktion und
Transportstatistik liegen unter USB, ungeplante Funktionen bleiben als solche
gekennzeichnet. CV OUT soll vor dem blockierenden SDTEST auf Off stehen.

## Prüfung

- 243 Python-/GUI-Tests bestanden, einschließlich Tab-Wechsel mit SHARED und
  aktivem simulierten CV-Ausgang, Datenausgabe/JSON sowie Seitenbreite und
  Schaltflächen bei 1280 und 1560 Pixeln Fensterbreite.
- Neue Oberfläche mit echter Firmware 0.3.2 verbunden und bei beiden Breiten
  visuell geprüft; nur Zustandsdaten gelesen, keine CV-/SD-Aktion ausgelöst.
- Lokale Bildschirmaufnahmen unter `.work/panel-*`; die erweiterte Hardware-
  Testreihe bleibt beendet. Die separate kurze native CV-Probe mit offenem
  SAW-Kriterium ist in [diesem Protokoll](HARDWARE_NATIVE_CV_2026-10-31.md) erfasst.
- Gepackte EXE 0.3.3 gebaut; `--sim --selftest 5`: Exit 0. Sie ist ein Reviewkandidat; keine neue
  Firmware, kein Flashen und keine Release-Publikation durch diese GUI-Änderung.

Die bisherigen 0.3.2-Downloadlinks auf main bleiben bis zur Freigabe der neuen
App der veröffentlichte Softwarestand. Der unabhängige Meilenstein-Review soll
das gesamte Diagnose-Tool einschließlich dieses Control Panels prüfen.
