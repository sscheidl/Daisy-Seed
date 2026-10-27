# Codex Fehlersuche: Acid Seq Phasen 0–6

**VERDICT: NICHT BEREIT**

Geprüft: `claude/acid-seq-phase-6` @ `d3297fdc6f7c2faf0b781f7cb1b5b25364c24170`, 27.10.2026. Enthält `bfd02c4`. Vergleich mit `main` / Merge-Basis `b5fe004d84bf8ce575dcaf35dd8abc81ed2e5754`. Eigene Umgebung: Windows, clang++ 23.1.3, x86_64-w64-windows-gnu, Python 3.12, Node; ASan/UBSan aus dem vorhandenen llvm-mingw. Zusätzlich bestehende Linux-CI-Protokolle gelesen.

**6 P1, 11 P2, 1 P3 (F01–F18).** Die Host-/Sanitizer-/Thread-Suites bestehen. Der nun ausgeführte unveränderte Browser-e2e-Test scheitert dagegen am neuen P1 F16; zusätzliche Prüfhelfer legen danach die Testaufbaufehler F17/F18 offen. Frühere Befund-IDs bleiben unverändert. Die P1-Befunde sollten vor dem Hardwaretest behoben werden. Die P2-Befunde gehören vor dem Merge bearbeitet. Dieses Verdikt ist keine Hardware-Abnahme.

Keine Produktions- oder vorhandenen Testquellen und keinen Phasen-Branch geändert, kein Release gestartet, nichts geflasht. Die Prüfung erfolgte in einem separaten detached Worktree. Auf den Folgeauftrag des Benutzers hin wurde ein einzelner begrenzter Linux-Prüflauf eingerichtet: separater Branch `codex/acid-seq-review-validation-20261027`, Commit `a26443db118039d328b291993646019ba82b3667`, ausschließlich zusätzlicher Prüfworkflow. Dieser checkt ausdrücklich den unveränderten Prüfstand `d3297fdc` aus. Kein Merge dieses Prüfworkflows vorgesehen. Buildausgaben liegen in ignorierten Buildverzeichnissen; zusätzliche Prüfhelfer liegen außerhalb der Produktionsquellen. Für die Übergabe sind Bericht, Quellen der Prüfhelfer, Logs und Screenshots im Belegpaket `CODEX_REVIEW_EVIDENCE.zip` zusammengefasst.

## Phasen und geprüfte Branch-Stände

Alle PRs sind offen und basieren auf `main`; ihre Inhalte sind kumulativ gestapelt. Zur Zuordnung der Fehler wurden die Phasen-Commits und bei Bedarf `git blame` verwendet.

| Phase / PR | Branch | Head | Inhalt der Phase |
| --- | --- | --- | --- |
| 0 / [#54](https://github.com/sscheidl/Daisy-Seed/pull/54) | `claude/acid-seq-phase-0-f1c35b` | `ddccebc3` | Gerüst, Register, SQ-/Parameter-/Transportverträge, Fixtures, CI-Auswahl |
| 1 / [#55](https://github.com/sscheidl/Daisy-Seed/pull/55) | `claude/acid-seq-phase-1` | `17cb1602` | Step-Seq-Kern, Transport, Bedienung, CV/DAC, MIDI-Ausgabe, SQ-Gerät und erste Clients |
| 2 / [#56](https://github.com/sscheidl/Daisy-Seed/pull/56) | `claude/acid-seq-phase-2` | `195c33a8` | Acid-303-Stimme, Notenstapel, Standalone-Firmware und CCs |
| 3 / [#57](https://github.com/sscheidl/Daisy-Seed/pull/57) | `claude/acid-seq-phase-3` | `64155925` | Sättigung auslagern (`cf156fa`), Drive mit Auto-Level und `drive-fx` |
| 4 / [#58](https://github.com/sscheidl/Daisy-Seed/pull/58) | `claude/acid-seq-phase-4` | `86569a29` | Stereo-Delay, Host für Einspeisung/Nachhall/Mix, `delay-fx` |
| 5 / [#59](https://github.com/sscheidl/Daisy-Seed/pull/59) | `claude/acid-seq-phase-5` | `a1e9b664` | JUCE-/Browser-SQ-Seiten, `.acidpatch`, VST3-Songzustand, GCC-Warnung korrigiert |
| 6 / [#60](https://github.com/sscheidl/Daisy-Seed/pull/60) | `claude/acid-seq-phase-6` | `d3297fdc` | Produktkette und Firmware `acid-seq`, GCC-Pufferinitialisierung, Prüfauftrag |

## Ausgeführt

Erstprüfung: **GEMESSEN 27.10.2026** auf dem oben genannten Windows-Host. Die nachfolgende Tabelle beschreibt diese Erstprüfung. Die anschließend nachgeholten eigenen Linux- und UI-Läufe sind im gesonderten Abschnitt „Nachprüfung für Claude“ dokumentiert; ältere Quick-CI-Logs bleiben davon getrennt.

| Kommando / Prüfung | Ergebnis |
| --- | --- |
| `make -C <Komponente> test HOST_CXX=clang++` für Step Seq, Acid 303, Drive, Stereo Delay, Acid Seq | PASS, einschließlich Produktions-Callbacks auf Fakes und Komponenten-Goldens |
| Dieselben fünf `test`-Ziele mit `HOST_FLAGS='-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'` | PASS. Direkter Aufruf mit Sanitizer-Flags, weil rekursive `test-sanitize`-Aufrufe der Komponenten unter dem lokalen Windows-Make an Shell-/sed-Quoting scheiterten. Kein Linux-Sanitizer-Ergebnis |
| `make -C editors/mod-on-daisy test-sanitize HOST_CXX=clang++` | PASS: MD-Kern, SQ-Kern, Simulator, JS-Codec und JS-Client gegen C++-Gerät, generierte Parameterdateien. Das frühere Leerzeichenproblem wurde durch einen getrennten Prüfpfad umgangen |
| `python -m unittest discover -s tests -p test_acid_seq_contracts.py` | PASS, 12 Tests |
| `python -m unittest discover -s tests -p test_components.py` | PASS, 10 Tests |
| `python -m unittest discover -s tests -p test_firmware_checker.py` | PASS, 2 Tests |
| `python sequencers/step-seq/tools/sq_codec.py --check` | PASS |
| `python tests/fixtures/acid-seq/generate.py --check` | PASS, Abweichung 0 |
| `python scripts/component_manifest.py --validate` | PASS, 21 Komponenten / 18 Produkte |
| Separate ASan-/UBSan-Prüfprogramme `core_cases.cpp`, `firmware_cases.cpp`, `acid303_cases.cpp` | Reproduzieren F01–F08, F10–F12 wie unten erläutert; F07/F08 lösen UBSan aus |
| Separates `python_case.py` | Beschädigte Antwort wird vom Probe akzeptiert: F09 |
| `select_suites(['sequencers/step-seq/include/step_seq/spsc.h'])` | Fehlende Verbraucher bestätigt: F14 |
| Eigener Tape-FX-Vorher/Nachher-Test, `b5fe004` gegen geprüften Head, gleicher clang++ und `-O2`, 4 s `saw_lp3`, alle vier Kurven | Bitgleich. Einzelheiten unten |
| `check-firmware.py` und `check-elf-placement.py` an den bereits vorhandenen fünf BIN/ELF-Paaren im lokalen Phasen-Worktree | Alle PASS. Diese Images wurden von mir geprüft, **nicht neu gebaut**. DTCM-Reserve Step 20.472 B, Acid 23.256 B, Drive 23.888 B, Delay 24.000 B, Produkt 17.912 B. Delay-Speicher jeweils 4.608.512 B bei `0xc0000000`. Vorhandenes Produkt-BIN: 193.540 B |

Die üblichen Make-Flags wurden lediglich um Compiler, Shell und Pfade zu den bereits vorhandenen Abhängigkeiten ergänzt. Kein Abhängigkeiten-Download. Die eigenen Reproduktionen sind zusätzliche Prüfungen; ihr Erfolg bedeutet hier, dass sie das falsche Verhalten nachweisen, nicht dass das betroffene Produkt fehlerfrei wäre.

### Linux-CI und Golden-Ergebnisse

Die neuesten zu den sieben abgefragten PR-Heads gehörenden Quick-checks-Läufe sind **alle erfolgreich**. Insbesondere ist der zunächst noch laufende [#60-Lauf 40454749862](https://github.com/sscheidl/Daisy-Seed/actions/runs/40454749862) für `d3297fdc` inzwischen `completed / success`, beendet am 27.10.2026 um 05:22:31 UTC. Runner: Ubuntu 24.04.5. Die protokollierten Host-Builds verwenden `g++ -O2 -Wall -Wextra -Werror` für die neuen Komponenten und den Editor. Die Pufferinitialisierung aus `bfd02c4` verändert den Produkt-Golden in diesem Lauf nicht.

| Golden / Referenzprüfung | Im Linux-Protokoll von #60 beobachtet |
| --- | --- |
| Step Seq | `0x7528DC6F` |
| Acid 303 Säge / Rechteck | `0xB05F5A1F` / `0x355159C8` |
| Drive | `0x50B9CF6E` |
| Stereo Delay | `0xD11BFF18` |
| Acid Seq | `0xBC3D9C4E` |
| Toy Piano | `52580874FA24361F`, Vergleich mit v0.2 PASS |
| Physical Modeling, regulär / MOD / FX-Torus | jeweils `37DE0BF9D93B81CE`, Referenzvergleich PASS |
| Looper, regulär / MOD | jeweils `2A2DBCEC41A54F57`, Vergleich PASS |
| Tape FX | TapeVibe-Referenzpfad (7 Einstellungen × 20 s × 2 Kanäle) und MixForge-Kurven/-Stufen bitgleich: PASS |
| Toy Piano → Tape FX | Vergleich mit der Komposition der Einzelkomponenten, 60 s: bitgleich, PASS |

**Tape-FX-Zusatzprüfung:** Die CI druckt keine vier FNV-Ausgabe-Hashes für Tape FX. Deshalb wurde zusätzlich derselbe eigene kleine Renderer vor/nach der Auslagerung gebaut (`audit-bin/tape_hash.cpp`). Bei Default-Parametern, wiederholter `saw_lp3`-Fixture, 48er-Blöcken und FNV-1a über beide Kanäle ergeben sich vor/nach jeweils `8BB16168`, `77CC1499`, `FE26B4D1`, `D42F5539`. Diese Werte gehören zu diesem ausdrücklich mitgelieferten Prüfprogramm. Sie sind **kein Nachmessen** der vier anders parametrisierten PR-Werte `F268144A`, `B62697C3`, `9B34720E`, `94BAA1BA`; deren ursprüngliches Render-Rezept lag nicht als eigener Golden-Test vor. Der arithmetische Diff der ausgelagerten vier Kurven ist ebenfalls unverändert.

Die Quick-CI führt `test`, aber keine `test-thread`-/TSan- und keine `test-sanitize`-Ziele aus. Der normale Step-Seq-Threadtest im `test`-Ziel besteht dort; das ersetzt TSan nicht. Die zunächst auf Benutzerwunsch übersprungenen zusätzlichen Linux-Prüfungen wurden auf dessen Folgeauftrag nachgeholt, siehe unten.

## Nachprüfung für Claude — 27.10.2026

**Linux: alle elf angeforderten Ziele PASS.** [Prüflauf 40460614328](https://github.com/sscheidl/Daisy-Seed/actions/runs/40460614328), Job `122044056681`, abgeschlossen `success` um 06:23:15 UTC. Geprüfte Produktionsquellen: exakt `d3297fdc6f7c2faf0b781f7cb1b5b25364c24170`. Ubuntu 24.04, Linux 6.17, **g++ 13.3.0**, Node 22.23.3, Python 3.12.3. Gepinnte Quellen: libDaisy `f715e487`, DaisySP `2c72eaf9`. Ein Runner, 15-Minuten-Limit, eigentliche Prüfphase etwa 94 Sekunden. Keine JUCE-/Playwright-Installation und kein Download dieser optionalen Abhängigkeiten.

| Bereich | `test-sanitize`, g++/ASan/UBSan | `test-thread`, g++/TSan |
| --- | --- | --- |
| Step Seq | PASS | PASS, einschließlich nebenläufigem SPSC-/Snapshot-/NoteFeed-Test |
| Acid 303 | PASS | PASS |
| Drive | PASS | PASS |
| Stereo Delay | PASS | PASS |
| Acid Seq | PASS | PASS |
| Editor-Kern einschließlich SQ-/JS-Clienttests | PASS | kein eigenes angefordertes TSan-Ziel |

Kommandos: `make -C <Verzeichnis> test-sanitize HOST_CXX=g++-13` und `setarch x86_64 -R make -C <Verzeichnis> test-thread HOST_CXX=g++-13`; zusätzlich `make -C editors/mod-on-daisy test-sanitize HOST_CXX=g++-13`. ASLR wurde nur für die TSan-Prozessbäume deaktiviert, nicht durch Änderung eines Systemparameters. Ein rein lesender `sysctl`-Inventuraufruf war nicht erlaubt; kein Testziel war davon abhängig. Keine Sanitizer-Unterdrückung. Alle Exitcodes sind 0. Die Goldens der sechs neuen Render-/Ereignisfälle stimmen auch unter beiden Sanitizer-Builds mit der obigen Tabelle überein. Achtung beim Lesen des TSV: Wegen einer lokalen Shell-Variablen im Log-Helfer heißen die TSan-Zeilen etwa `step-sanitize-thread`; ausgeführt wurde nachweislich `test-thread -fsanitize=thread`.

Die vier weiteren Komponenten haben im `test-thread`-Ziel keine eigenen vergleichbar breiten nebenläufigen Firmware-Scheduler-Szenarien; die Hauptprüfung der gemeinsamen Übergabe steckt im Step-Seq-Threadtest. PASS bedeutet keine erschöpfende Prüfung aller Interruptabläufe und widerlegt insbesondere die logischen Überlauf-/Reihenfolgefehler F01/F03/F11 nicht.

**JUCE: vorhandene lokale App erfolgreich erneut getestet.** Der bestehende Windows-Build mit JUCE 9.0.0 wurde mit `--selftest` und `MOD_ON_DAISY_NO_AUTOCONNECT=1` gegen Simulatoren ausgeführt: Exit 0. Alle zehn PASS-Zeilen einschließlich Acid-Seq-Seiten, Parameteränderungen und Songzustand mit Nicht-Übertragen/Immer bestanden. SHA-256 der bestehenden EXE: `75546fc3b2d8e22bee02ea4c785544aa3547036d51c6bd0ff6d2b4d6fc1cf3b2`. Dies ist ein erneuter Test der vorhandenen Binärdatei, **kein frischer Build des Prüf-Heads gegen 9.0.4**. Der Simulator verarbeitet Antworten in einer Schleife und deckt den asynchronen Ablauf von F05 nicht ab.

**Playwright: Prüfung durchgeführt, Originaltest FAIL.** Das bereits installierte Playwright 1.62.1 und Chromium-Headless-Shell 1194 konnten ohne Download gestartet werden. Damit wurde die vorherige Startblockade umgangen. Der originale Testablauf `web/tests/e2e_sq.cjs` mit den vorhandenen Simulatoren scheitert beim Verbindungsaufbau; ein Prüfhelfer protokolliert `TypeError: Illegal invocation` bei `sq_protocol.js:247` (F16). Das Gerät hatte bereits eine gültige IDENTIFY-Antwort geliefert. Das ist ein Browser-Produktfehler, kein fehlendes Linux und kein fehlendes MIDI-Gerät.

Zur Eingrenzung wurden ausschließlich außerhalb des Repos im Prüfhelfer drei Änderungen injiziert: Browser-Timer korrekt binden, die Kalibrierfelder des **simulierten** Geräts explizit mit gültigen Fixture-Werten füllen (F17), MD-Simulator über EOF statt unbekanntem `Q` beenden (F18). Mit allen drei Hilfen läuft der komplette SQ-/MD-e2e-Ablauf bis zur letzten Assertion durch (Exit 0): Device-first ohne Schreiben, Step, Stimme, Stop/Start, Kalibrierung/Freigabe, Diagnose, Trennen und MD-Fallback. **Dieser diagnostische PASS ist kein PASS des unveränderten Produkts oder des unveränderten Testaufbaus.** Es wurde keine der drei Korrekturen in Produkt- oder bestehende Testquellen eingearbeitet.

Belege im Paket: `audit-logs/linux-ci/` mit elf Einzelprotokollen und `results.tsv`, kompletter `linux-validation-ci.log`, `juce-existing-selftest/selftest.txt` plus Screenshots, `playwright-protocol-diagnostic.log` (F16), `playwright-bound-timers.log` (F17), `playwright-diagnostic-complete.log` (F18), `playwright-diagnostic-final.log` (vollständiger diagnostischer Ablauf), `acid-seq-panel.png` sowie `audit-bin/playwright-existing.cjs`. Der Prüfworkflow ist ebenfalls beigefügt. Für spätere Korrekturstände muss dessen fest gepinnter Checkout-Commit bewusst angepasst werden; ein erneuter Lauf unverändert würde weiterhin nur `d3297fdc` prüfen.

## Befunde

Zeilenangaben beziehen sich auf den oben genannten Head. P1 vor Hardwaretest, P2 vor Merge, P3 Hinweis/Dokumentation. Vorschläge sind keine vorgenommenen Korrekturen.

### P1

| ID | Phase / PR | Datei:Zeile | Befund und Auslöser | Vorschlag |
| --- | --- | --- | --- | --- |
| **F01** | Ursprung Phase 2 / #56; erneut Phase 6 / #60 | `instruments/acid-303/src/main.cpp:184`; `sequencers/acid-seq/src/main.cpp:381` | **Volle Eingabe-Queue verliert Note-Off, All-Off und Mute ohne Ersatzweg.** Eine MIDI-Note klingt; 64 Parameterkommandos füllen die gemeinsame Queue; das folgende Note-Off wird ohne Prüfung des `Push`-Ergebnisses verworfen. Nach Abarbeitung bleibt die Note gehalten. Dasselbe Muster betrifft All-Off bei USB-Verlust und Mute beim Bootloader. Der SQ-Parameterweg hat BUSY, die kritischen MIDI-/Mute-Wege haben keine entsprechende Absicherung. | Sicheren Off-/Panic-/Mute-Zustand getrennt von der verlierbaren Queue übergeben oder beim Überlauf eine garantiert abgearbeitete Bereinigung auslösen; verlorene Eingaben zählen. Nicht einfach nur Note-Ons verwerfen und Offs ebenfalls verlieren. |
| **F02** | Phase 4 / #58; wirkt auch in #60 | `effects/stereo-delay/include/stereo_delay/host.h:53`, `:75`, `:77` | **Die Nachhall-Erkennung hält eine Lücke zwischen Echos für ein Ende und friert gefüllte Leitungen ein.** Bei 1 s Delay, Feedback 0, Impuls und unmittelbar anschließendem Ausschalten ist der Ausgang zunächst 500 ms still. Der Host wechselt schon dann auf Off; das erste Echo bei 1 s fehlt. Beim späteren Einschalten mit ausschließlich stillem Eingang taucht der alte Impuls wieder auf, weil Off die Verarbeitung stoppt und die Leitung weiter gefüllt ist. Das betrifft den vorgesehenen Bereich bis 12 s und ist unabhängig von der offenen Dry/Wet-Frage. | Stille nur als Ende werten, wenn auch zukünftige Ausgaben der Leitung berücksichtigt sind. Off darf keine alten Echos für ein späteres Einschalten aufbewahren. Lösung muss ohne großen SDRAM-Clear im Callback auskommen, z. B. begrenztes Weiterverarbeiten/geeignete Zustands- oder Gültigkeitsverwaltung. |
| **F03** | Phase 1 / #55 | `sequencers/step-seq/include/step_seq/midi_out.h:185`, `:219` | **MIDI-Slide wird zum Retrigger.** Für Legato stellt `NoteFeed` absichtlich neues Note-On vor altes Note-Off. `ServiceMidi` versendet aber bereits vor dem Lesen dieser Queue alle nicht mehr gewünschten alten Noten als Off. Übergang C4→D4 liefert damit `80 3C 00` vor `90 3E 64`. Ein monophoner Empfänger sieht zwischen den Noten kein gehaltenes Gate und startet seine Hüllkurve neu. | Normal gequeue-te Legato-Folgen erhalten; die Notenverfolgung als zuverlässigen Ersatz für verlorene Offs verwenden, ohne vorab den vorgesehenen On-vor-Off-Übergang zu zerstören. Regression am MIDI-Byteverlauf prüfen. |
| **F04** | Phase 1 / #55 | `sequencers/step-seq/include/step_seq/params.h:56`; `sequencers/step-seq/include/step_seq/engine.h:254`, `:429` | **Wechsel der Clock-Quelle ändert nur die Einstellung, nicht den laufenden Transport.** Intern starten, dann `clock_source=Gate IN 1` setzen: `active_` bleibt Internal und der Sequencer erzeugt weiter Noten ohne einen Gate-Puls. Im Gegenzug kann die Rückkehr von einer externen Quelle zu Intern im Wartezustand stecken bleiben. SW17 muss erst aus/an, damit die neue Quelle wirksam wird; eine solche Einschränkung wird beim akzeptierten Parameter nicht gemeldet. | Quellenwechsel im Engine-Adapter als definierten Transportübergang mit Bereinigung und erneutem Scharfschalten ausführen, oder den Wechsel während des Laufs ausdrücklich ablehnen. |
| **F05** | Phase 5 / #59 | `editors/mod-on-daisy/src/core/SqSession.h:454`, `:483`; Folgestelle `editors/mod-on-daisy/src/ui/SqPanel.h:268` | **JUCE meldet Ready vor den Deskriptoren, Werten und Enum-Labels.** `ReadAll` reiht Monitor hinter den Komponenten ein; deren Antworten hängen Deskriptorabfragen erst hinten an. Die Monitorantwort setzt schon Ready. Am ersten Ready waren in der Reproduktion alle 46 Parameter-Schlüssel leer und die Queue noch nicht leer. Bei echten asynchronen MIDI-Antworten kann `SqPanel` dann Regler aus den leeren Deskriptoren bauen; spätere `UpdateParams` bauen diese Reihen nicht neu. Auch der VST3-Patchvergleich/-Upload kann zu früh beginnen und Werte überspringen. Der Browser-Client liest dagegen sequenziell zu Ende. | Ready erst nach Abschluss aller abhängigen Deskriptor-, Wertefragment- und Labelabfragen setzen. Einen Test mit zeitlich getrennten Antworten und UI-Refresh zwischen den Antworten ergänzen; der Simulator, der alle Antworten in einer Schleife abarbeitet, verdeckt den Fehler. |
| **F16** | Phase 5 / #59 | `editors/mod-on-daisy/web/sq_protocol.js:219`, `:247`, `:266` | **Der Browser-SQ-Client scheitert bei jeder ersten Anfrage an einem falsch gebundenen Timer.** Der Konstruktor speichert die nativen Window-Funktionen `setTimeout`/`clearTimeout` unverändert. `this.setTimer(...)` ruft sie anschließend mit dem Client als Empfänger auf. Chromium wirft `TypeError: Illegal invocation` an Zeile 247, obwohl das Senden und die IDENTIFY-Antwort bereits funktioniert haben. `connectSq` behandelt den Fehler als nicht antwortendes SQ-Gerät und versucht erfolglos MD. Auch `this.clearTimer(...)` ist ungebunden. Node akzeptiert diese Aufrufart, deshalb bestehen die bisherigen Node-Tests. | Native Timer explizit an Window/globalThis binden oder über Wrapper mit korrektem Empfänger aufrufen. Einen echten Browser-Handshake ohne Timer-Injektion als Regression laufen lassen. Geprüft: Originalablauf FAIL; mit ausschließlich im Prüfhelfer gebundenen Timern gelingt der Handshake. |

### P2

| ID | Phase / PR | Datei:Zeile | Befund und Auslöser | Vorschlag |
| --- | --- | --- | --- | --- |
| **F06** | Phase 5 / #59; zugrunde liegende Monitor-Regel aus Phase 1 ist dokumentiert | `editors/mod-on-daisy/src/core/SqSession.h:607`; `editors/mod-on-daisy/web/sq_protocol.js:408`; Gegenstelle `sequencers/step-seq/include/step_seq/device.h:141` | **Zusammengefasste Monitoränderungen lassen beide Editoren dauerhaft mit falschen Werten zurück.** Tempo 140 und Swing 60 vor dem nächsten Monitorversand ändern: nur Swing wird als Parameterereignis gesendet. Nach einer weiteren Sekunde kennt der JUCE-Client weiterhin Parameter-Tempo 120 und Swing 60, obwohl das Gerät Tempo 140 hat. `EvTempo` aktualisiert nur das Status-BPM, nicht den Parameterwert. Der Vertrag erlaubt ausdrücklich „jüngstes gewinnt“; die Clients gleichen die dabei verlorenen Werte aber nicht wieder ab. Speichern erzeugt so einen veralteten `.acidpatch`. | Zusammenfassen je Komponenten-/Parameter-ID statt global oder nach erkannten/zugelassenen Verlusten vollständige Werte nachlesen. Beide Clients müssen vor dem Speichern einen konsistenten Gerätestand besitzen. |
| **F07** | Phase 5 / #59 | `editors/mod-on-daisy/src/core/AcidPatch.h:263`, `:278`, `:299`; Browser-Gegenstück `web/sq_protocol.js:450` | **Beschädigte numerische Patchwerte werden vor ihrer Prüfung in Integer umgewandelt.** Die JSON-Zahl `1e999` als `format_version` führt in C++ bei `int(version->n)` zu UBSan „inf is outside the range“. Vergleichbare ungesicherte Konvertierungen existieren für Schema, Step-Note und Gate-Länge; Parameterwerte werden später in `SqSession::SetParam:151` konvertiert. Der Browser prüft ebenfalls keine Endlichkeit/strikten Zahlentypen und verwendet Bitkonvertierungen, die große Werte umklappen lassen. | Format, Struktur, endliche Zahlen, Ganzzahligkeit und Wertebereich **vor** jedem Cast prüfen; ungültige Dateien mit Fehlermeldung vollständig ablehnen. Gemeinsame Negativvektoren für Browser und C++ ergänzen. |
| **F08** | Phase 1 / #55 | `sequencers/step-seq/include/step_seq/cv.h:24`; Aufruf `device.h:405` | **Ungültige Kalibrierung kann schon in der Plausibilitätsprüfung vorzeichenbehafteten Überlauf auslösen.** `scale=INT32_MIN` ist auf dem i32-Wire zulässig kodierbar, muss als Kalibrierung aber abgelehnt werden. `-c.scale` ist bei genau diesem Wert undefiniert. Der separate UBSan-Lauf bestätigt den Fehler. | Gegen positive/negative erlaubte Grenzen vergleichen, ohne `INT32_MIN` zu negieren, oder vorzeichenlos/in breiterem Typ prüfen. Korrekt gerahmtes SET_CAL mit passender UID und diesem Wert als Negativtest verwenden. |
| **F09** | Phase 1 / #55 | `sequencers/step-seq/tools/sq_probe.py:78`, `:89` | **Der Python-Probe validiert empfangene Antworten nicht mit CRC, Länge oder Version.** Eine Antwort mit passender Request-ID und passendem Typ wird unmittelbar als Reader zurückgegeben. Ein separat erzeugter GET_STATE-Reply mit absichtlich verändertem CRC-Lo-Byte wurde akzeptiert und als Zustand 2 gelesen. Das gilt auch für die Antworten beim Auslesen/Speichern von Kalibrierdaten. Die gemeinsamen Codec-Vektoren prüfen diesen tatsächlichen Empfangspfad nicht. | Einen expliziten Antwortvalidator für Rahmen, Länge, 7-Bit-Felder, Version und CRC nutzen; erst danach NAK oder Nutzdaten auswerten. `classify()` allein ist hierfür ungeeignet, weil es Antwortklassen absichtlich früh als Echo ignoriert. |
| **F10** | Ursprung Phase 2 / #56; wiederholt Phase 3 / #57 und Phase 4 / #58 | `instruments/acid-303/src/main.cpp:176`; `effects/drive/src/main.cpp:145`; `effects/stereo-delay/src/main.cpp:162` | **Die drei Standalone-Firmwares verwerfen die angenommene SQ-Revision.** `PushParam` erhält die Revision, speichert sie jedoch nicht im Kommando. Nach einer gültigen Änderung meldet das gemeinsame SQ-Gerät etwa accepted=1 und dauerhaft applied=0, obwohl der Parameter bereits im Callback übernommen wurde. Eigene Reproduktion an `acid-303` bestätigt 1/0. | Revision mit dem Kommando und Snapshot bis zur tatsächlichen Übernahme führen; `device.view.applied_revision` danach aktualisieren. Je Standalone testen, statt nur den Simulator zu prüfen. |
| **F11** | Phase 6 / #60 | `sequencers/acid-seq/src/main.cpp:183`, `:186`, `:148` | **Die zwei Produkt-Queues können die globale angewandte Revision rückwärts setzen.** Voice-Parameter mit Revision 1, danach Sequencer-Parameter mit Revision 2 vor demselben Callback annehmen: der Callback leert erst die Sequencer-Queue (applied=2), dann die Voice-Queue (applied=1). Beide Werte sind übernommen, aber der gemeldete Stand bleibt 1. Die separate Reproduktion bestätigt diesen Rückschritt. | Annahmereihenfolge über die Queues hinweg bewahren oder einen globalen, zusammenhängenden Übernahmestand verwalten; auch Revision-Wrap und auf den Step-Rand wartende Pattern berücksichtigen. Ein gewöhnliches numerisches `max` genügt bei Wrap nicht. |
| **F12** | Phase 2 / #56 | `instruments/acid-303/src/main.cpp:148`; Vertrag `sequencers/step-seq/include/step_seq/param_table.h:74` | **Der als schreibbar angebotene Slide-Modus wird sofort wieder von SW18 überschrieben.** Per SQ `slide_mode=Always` setzen, SW18 unverändert oben lassen: die Anfrage wird angenommen, aber nach dem Callback ist der Modus weiterhin Off. Die Reproduktion liefert `actual_mode=0`, obwohl 2000 angefordert wurde. Für Step-Seq-`play_mode` gibt es im Gegensatz dazu bereits ein Readonly-Flag. | Festlegen und implementieren, ob der Hardware-Schalter nur bei Bewegung gewinnt oder der Wert in dieser Firmware readonly ist. Kein erfolgreich bestätigter schreibbarer Wert, der im selben Callback wieder verschwindet. |
| **F13** | Phase 5 / #59 | `editors/mod-on-daisy/src/ui/SqPanel.h:125`, `:351`, `:352` | **JUCE verliert beim bloßen Anzeigen und erneuten Senden Bruchteile einer Kalibrierung.** `c.scale / 1000` wird als Ganzzahldivision in `int32_t values[]` ausgeführt. Gültige −409600 milli-Code/V erscheinen als −409; unverändertes erneutes Senden ergibt −409000. Offset-Bruchteile gehen ebenfalls verloren. Der Browser zeigt dagegen z. B. −409,6 an. Dies ist am Ausdruck statisch nachgewiesen, keine ausgeführte JUCE-/Hardwaremessung. | Offset/Skalierung mit ausreichender Dezimalpräzision darstellen und einlesen. Ein unverändert angezeigtes und erneut gesendetes Kalibrierpaar muss dieselben Wire-Werte erzeugen. |
| **F14** | Erste fehlende Verbraucher-Ergänzung Phase 2 / #56, weitere Phasen 3 / #57 und 4 / #58; Auswahlgrundlage aus Phase 0 | `scripts/select_test_suites.py:35`, `:37`, `:38`, `:39`; Verbraucher z. B. `instruments/acid-303/src/main.cpp:18`, `effects/drive/src/main.cpp:19`, `effects/stereo-delay/src/main.cpp:18` | **Änderungen am gemeinsamen Step-Seq-Unterbau testen nicht alle Firmware-Verbraucher.** Für `sequencers/step-seq/include/step_seq/spsc.h` liefert die Auswahl stepseq=true und acidseq=true, aber acid303=false, drive=false, delay=false. Alle drei ausgelassenen Standalone-Firmwares verwenden diese Queue direkt. Ebenso sind weitere gemeinsame Header nicht vollständig in den Verbraucherregeln abgebildet. Die heute grünen PRs ändern `quick.yml` und wählen dadurch ohnehin alle Suites; das verdeckt die Lücke für spätere isolierte Headeränderungen. | Die kleinen gemeinsamen Header präzise ihren wirklichen Verbrauchern zuordnen und die Auswahltests ergänzen. Dafür ist keine neue Runner-Matrix und kein breiterer Workflow-Trigger nötig. |
| **F17** | Phase 5 / #59 | `editors/mod-on-daisy/web/tests/e2e_sq.cjs:92`, `:97`; Gegenstellen `web/sq_panel.js:198`, `src/core/SqSimulator.h` | **Der Browser-e2e-Test setzt keine gültigen Kalibrier-Fixtures, erwartet aber erfolgreiche Freigabe.** Nach Umgehung von F16 liest er vom unkalibrierten Simulator u. a. Offset=0, Scale=0, Gate-Low=Gate-High=0. Die Oberfläche zeigt diese vorhandenen Werte an; der Test klickt nur zweimal Senden und dann Freigeben. Beide SET_CAL werden korrekt mit BAD_ARGUMENT abgelehnt; CV_ENABLE bleibt NOT_CALIBRATED, und die Erwartung „CV frei“ läuft an Zeile 97 in den Timeout. | Im Test die sechs Felder beider Ausgänge ausdrücklich mit gültigen **simulierten** Kalibrierwerten füllen und deren Bestätigung abwarten. Keine gültige Kalibrierung aus bloßen UI-Standardwerten voraussetzen. Mit einer separat injizierten Fixture (Offset 2048, Scale −409,6, Gate 2048/410, Klemme 0/4095) besteht dieser Abschnitt. Kein Auftrag, diese Beispielwerte auf echter Hardware zu verwenden. |
| **F18** | Phase 5 / #59 | `editors/mod-on-daisy/web/tests/e2e_sq.cjs:113`, `:30`; Gegenstelle `modules/mod-on-daisy/tools/mod_sim.cpp:92` | **Der neue SQ-e2e-Test beendet den MD-Simulator mit einem nicht unterstützten `Q` und kann bei der Bereinigung abstürzen.** Nach Umgehung von F16 und korrekter Testkalibrierung bestehen auch die SQ-/MD-Auswahlassertions. Dann schreibt der Test `Q` direkt nach stdin. Der MD-Simulator kennt S/T/D, antwortet auf das unbekannte Q trotzdem mit `OK` und wartet weiter. Dafür wurde kein Promise in `waiting` eingereiht; Zeile 30 wirft `waiting.shift(...) is not a function`. | Simulator nach Abschluss kontrolliert über EOF beenden und Prozessende abwarten oder beide Simulatorprotokolle ausdrücklich angleichen. Das `Q` des SQ-Simulators nicht ungeprüft für den MD-Simulator voraussetzen. Mit EOF ausschließlich im externen Prüfhelfer endet der komplette diagnostische Lauf mit Exit 0. |

### P3

| ID | Phase / PR | Datei:Zeile | Befund / Auslöser | Vorschlag |
| --- | --- | --- | --- | --- |
| **F15** | Phase 4 / #58 | `effects/stereo-delay/tests/test_firmware.cpp:131` | Die PASS-Zeile verspricht „tail then exact dry“, der Test prüft laut eigenem Kommentar bei Zeile 96 tatsächlich `dry * (1 - mix)`. Ein Leser des Logs kann daraus fälschlich einen Unity-Bypass ableiten. | Nach Entscheidung Q1 die PASS-Zeile und die Beschreibung an das tatsächlich geprüfte Verhalten anpassen. Dies ist kein zusätzlicher Klangfehler neben Q1. |

## Reproduktionen und vorgeschlagene Regressionen

Die kompletten kleinen Prüfprogramme liegen separat in `audit-bin/`; die folgenden Ausschnitte beschreiben die Assertions, die anschließend in den jeweiligen Phasen-Tests stehen sollten. Sie wurden **nicht** in die vorhandenen Tests eingebaut. Die beobachteten Ausgaben stehen in `audit-logs/core-cases.log`, `firmware-cases.log`, `acid303-cases.log`, `cal-int-min.log`, `bad-patch.log`, `python-case.log` und `selection-case.log`.

### F01 — `sequencers/acid-seq/tests/test_firmware.cpp`, vorgeschlagen `TestVoiceQueueOverflow`

```cpp
fw::PushVoice(fw::VoiceCommand::Kind::NoteOn, 60, 100);
Tick();
fw::VoiceCommand c; c.id = 4; c.value = 1000000;
for(int i = 0; i < 64; ++i) voice_commands.Push(c);
fw::PushVoice(fw::VoiceCommand::Kind::NoteOff, 60);
Tick();
CHECK(chain.GetVoice().Held(acid303::Source::MidiIn) == 0);
```

Erwartet: Note endet oder eine sichere Überlaufbereinigung beendet sie. Tatsächlich: `gate=1 held=1`. Diese Assertion ist am geprüften Stand falsch. Entsprechenden Test am Standalone-Adapter aus Phase 2 ergänzen. Der externe Prüfhelfer ruft genau den Produktions-Callback auf den vorhandenen Fakes auf.

### F02 — `effects/stereo-delay/tests/test_stereo_delay.cpp`, vorgeschlagen `TestLongGapTail`

```cpp
// 48 kHz, Block 48, time=1000 ms, sync=false, feedback=0, mix=100 %.
// Ein Impuls von 0.5 in beiden Eingängen, dann ausschließlich Nullen.
host.SetEnabled(false); // unmittelbar nach dem Impulsblock
// Bis 1500 ms weiterverarbeiten: das erste linke Echo muss vorhanden sein.
CHECK(first_echo_peak > 0.49f);
// Nach abgeschlossenem Nachhall und erneutem Einschalten mit Nullen:
CHECK(stale_echo_peak == 0.f);
```

Tatsächlich: `first_echo_peak=0`, Host bereits Off; nach erneutem Einschalten `stale_echo_peak=0.5`. Separat auch 12 s und 95 % Feedback abdecken. Diese größeren Fälle wurden in der neuen Reproduktion nicht zusätzlich ausgeführt.

### F03/F04 — `sequencers/step-seq/tests/test_step_seq.cpp`

Vorgeschlagen `TestMidiLegatoOrder`: nach bereits gesendetem Note-On 60 ein `Legato` auf 62 füttern und einen Service-Pass ausführen. Erwartete Status-/Notenfolge: `90/62`, danach `80/60`. **Beobachtet: `80/60 90/62`.**

Vorgeschlagen `TestClockSourceChangeWhileRunning`: intern freigeben, dann `WriteParam(settings, 7, 1000)` und 1000 Ticks ohne Gate-Puls ausführen. Erwartet ist ein zur gewählten externen Quelle passender Warte-/Übergangszustand oder eine Ablehnung des Parameters. **Beobachtet: Einstellung Gate, aktive Quelle Internal, Zustand Running, acht weitere Note-Ons ohne Gate-Puls.**

### F05/F06 — `editors/mod-on-daisy/tests/test_sq.cpp`

Vorgeschlagen `TestReadyAfterCompleteRead`: auf die vorhandene `Link`-/`Pump`-Infrastruktur aufsetzen, im **ersten** `changed`-Callback mit `Ready()` alle Descriptor-Schlüssel und Enum-Labels prüfen; Antworten zusätzlich einzeln mit dazwischenliegendem UI-Refresh liefern. **Beobachtet: 46 Parameter, 46 leere Schlüssel, `Idle()==false`.**

Vorgeschlagen `TestTwoKnobChanges`: nach abgeschlossenem Connect vor dem nächsten Pump gleichzeitig `TurnKnob(0,1,140000)` und `TurnKnob(0,2,60000)` ausführen. Nach 1000 ms müssen die Parameterwerte 140/60 sein. **Beobachtet: 120/60, `CntMonitorDropped=1`.** Der Status-BPM kann dabei bereits 140 zeigen; deshalb ausdrücklich den gespeicherten Parameterwert prüfen. Im Browser denselben Fall testen.

### F07/F08/F09 — beschädigte Eingaben

Für F07 in `editors/mod-on-daisy/tests/test_sq.cpp`, vorgeschlagen `TestInvalidPatchNumbers`:

```cpp
std::string error;
auto p = editor::ParseAcidPatch(
    "{\"format\":\"acid-seq-patch\",\"format_version\":1e999}", error);
CHECK(!p); // ohne undefiniertes Verhalten
```

Tatsächlich beendet UBSan den separaten Lauf an `AcidPatch.h:263`. Weitere Negativfälle sollten Schema, Parameter und Step-Felder prüfen; die Prüfung dieses Beispiels ersetzt sie nicht.

Für F08 in `sequencers/step-seq/tests/test_step_seq.cpp`, vorgeschlagen `TestCalIntMin`: `OutputCal c; c.scale=INT32_MIN; CHECK(!ValidCal(c));`. Tatsächlich UBSan an `cv.h:24`: Negation von −2147483648 nicht darstellbar. Anschließend denselben Wert mit gültiger UID über SET_CAL testen.

Für F09 in einem Python-Clienttest `TestBadReplyCrc`: GET_STATE-Antwort mit `sq.encode(GET_STATE | RESPONSE, 1, [2])` erzeugen, `reply[-2] ^= 1`, einen fake Link genau diesen Reply liefern lassen. Erwartet: ignorieren und Retry/Timeout. **Beobachtet: `link.request(GET_STATE).u7()` liefert 2.** Das separate Prüfprogramm öffnet dafür keinen echten MIDI-Port.

### F10–F14 — Adapter und Auswahl

| Befund | Regression / Erwartung | Beobachtung |
| --- | --- | --- |
| F10 | Je `*/tests/test_firmware.cpp`: SET_PARAM, Callback, Snapshot, GET_STATE; accepted muss nach Übernahme applied entsprechen | Acid 303: accepted=1, applied=0; gleichartige verlorene Revisionsargumente in Drive und Delay statisch geprüft |
| F11 | `sequencers/acid-seq/tests/test_firmware.cpp`, `TestCrossQueueRevision`: Voice-Param Revision 1 und Seq-Param Revision 2 vor demselben Callback einstellen | `engine.AppliedRevision()==1`, obwohl auch Revision 2 übernommen wurde |
| F12 | `instruments/acid-303/tests/test_firmware.cpp`, `TestSqSlideMode`: SW18 oben, SET_PARAM 11 auf 2000, Tick | Anfrage angenommen, `params.slide_mode==Off` |
| F13 | JUCE-UI-Test: Cal mit scale=−409600 und offset=2048500 anzeigen und ohne Edit senden | Aus der Integerarithmetik nachweislich scale=−409000, offset=2048000; UI-Ausführung nicht erfolgt |
| F14 | `tests/test_components.py`: nur `spsc.h` als geändert angeben; alle drei Standalone-Verbraucher müssen ausgewählt werden | acid303/drive/delay jeweils false |

## Bewertung der ausdrücklich markierten Punkte / offene Fragen an den Benutzer

| Nr. | Frage / Bewertung | Empfehlung |
| --- | --- | --- |
| **Q1 — Delay aus** | Soll `delay-fx` nach abgeschlossenem Nachhall denselben Unity-Bypass wie das Produkt bekommen? Aktuell ist die Einzel-Firmware bei Mix 25 % exakt Faktor 0,75, also `20 log10(0,75) = −2,499 dB`, leiser. Bei Mix 100 % ist sie ausgeschaltet vollständig stumm. Das Produkt setzt nach Tail-Ende das Mix-Ziel auf 0 und wird nach der Überblendung trocken. Belege: `effects/stereo-delay/src/main.cpp:138`, `include/stereo_delay/host.h:67`, `sequencers/acid-seq/include/acid_seq/chain.h:86`. | **Ja, auf Unity-Bypass vereinheitlichen**, sofern Ein/Aus einen üblichen Bypass bedeuten soll. Das ist eine Verhaltensentscheidung, deshalb hier als offene Frage, nicht als eigenmächtig festgelegter zusätzlicher P1. F02 bleibt davon unabhängig zu beheben. |
| **Q2 — callback-diagnostics** | Der Registereintrag `components/registry.json:156` ist tatsächlich der MD-Adapter und hängt von `mod-on-daisy` ab. Acid Seq verwendet stattdessen die unabhängigen `CallbackMeter`-/`StackProbe`-Primitiven aus `step_seq/diagnostics.h`; kein falscher Registereintrag wird behauptet. Das ist eine nachvollziehbare, bereits dokumentierte Abweichung von der Komponentenliste in Auftrag 4.10. | **Abweichung akzeptieren.** Komponentenliste im Auftrag an den tatsächlichen Unterbau angleichen; nicht nur für den Namen den Mod-Kern einbinden. Die Hardware-Ressourcenabnahme bleibt offen. Für Acid 303/Drive/Delay ist der Stack-Zähler derzeit kein ausgeführter Nachweis: deren Adapter liefern keinen entsprechenden Stack-Scan. |
| **Q3 — Drive-Ausgleich abhängig vom Eingangspegel** | Tabelle/Interpolation und das 300-ms-RMS-Fenster wurden am Code und mit den vorhandenen Tests geprüft. Ein allein vom Drive-Regler abhängiger Ausgleich kann bei unterschiedlich lauten Eingängen den unterschiedlichen Sättigungsgrad nicht erfassen. Die begrenzte ±3-dB-Nachregelung löst dieses Grundproblem nicht. Eigener Test: maximal 0,41 dB stationäre Abweichung in den vorgesehenen Nicht-Ausnahmefällen. Belege: `effects/drive/include/drive/drive.h:68`, `:177`, `:184`. | **Abweichung akzeptieren und Auftrag 4.7 angleichen.** Das ist keine Garantie für beliebige Transienten oder Signale; der Test misst stationär nach Einschwingen. Hörtest bleibt offen. |
| **Q4 — Fold auf Rechtecken** | Die vorhandene Ausnahme ist echt: eigener Test bestätigt bis −16,9 dB. Die Tests nehmen Fold/Rechteck gezielt aus der unteren ±1-dB-Grenze heraus (`effects/drive/tests/test_drive.cpp:79`); das ist kein voller PASS der ursprünglichen Anforderung für alle Fixtures. Starker Pegelverlust nach dem Falten und anschließender DC-Entfernung lässt sich mit einer auf ±3 dB begrenzten Nachregelung nicht beliebig ausgleichen. | **Explizit als begrenzte Ausnahme akzeptieren**, falls dieser Klang erwünscht ist, und das PASS-Kriterium im Auftrag anpassen. Falls ±1 dB zwingend für jede Fixture gelten soll, ist #57 in diesem Punkt nicht abgenommen und braucht eine andere Strategie. Keine automatische starke Verstärkung nahezu ausgelöschter Fold-Ausgänge empfehlen. |

Diese Empfehlungen ändern den Auftrag oder die Branches nicht. Die Entscheidungen Q1–Q4 können anschließend bei der phasenweisen Einarbeitung festgehalten werden.

## Abdeckung der zwölf Prüfbereiche

| Bereich | Geprüft / Ergebnis und Grenze |
| --- | --- |
| 1 Echtzeitregeln | Callback-/main-Aufteilung, feste Puffer, SPSC-/Triple-Buffer-Übergabe, direkter DAC-Pfad, vorhandene Null-Allokations-Tests. Kein neuer Lock-/Allokationsaufruf im geprüften Firmware-Callback gefunden. 48er-Blockprüfung vorhanden. Kritischer Überlauf: F01. TSan-Ziele nachgeholt: PASS; gemessene CPU-/Stack-Last am Gerät offen |
| 2 Sequencer | Pattern-/Transport-/Gate-/Swing-/Bedienungsfälle der vorhandenen Tests, Source-Umschaltung zusätzlich: F04. Kein Hardware-Jitter-/Pulsbreitennachweis |
| 3 CV/Kalibrierung | getrennte Freigabe, UID-Prüfung, RAM-Speicherung, Startup-DAC und Registeradapter geprüft. F08 und F13. Keine Spannung gemessen |
| 4 MIDI | Notenverfolgung/Budgets/Überläufe, Callback-/main-Wege und Panic/USB geprüft; F01/F03. Realtime-Bytes anhand gepinntem Parser und Vertragsvektoren; keine Änderung des libDaisy-Pins. DIN am Gerät offen |
| 5 SQ in vier Umsetzungen | Codec-/Vertrags- und Clienttests sowie tatsächliche Device-/Probe-/Browser-/C++-Wege geprüft. F05–F11. MD-Protokolldatei unverändert; Dispatcher nach SQ/MD/BL getrennt |
| 6 Stimme | vorhandene Golden-, Slide-, Accent-, Stack-, DC-/Peak- und Fake-Firmware-Tests bestanden; Adapterfehler F01/F10/F12. Kein Hörtest |
| 7 Drive | Tabelle neu im Test berechnet, stationäre Pegelprüfung und Sanitizer bestanden; Saturation-Auslagerung arithmetisch unverändert, eigener Tape-Vorher/Nachher-Vergleich bitgleich. Q3/Q4 |
| 8 Delay | maximale Zeit/Interpolation, Sync, Feedback und vorhandene Klick-/Tailtests bestanden; zusätzlicher langer Echoabstand zeigt F02. Q1 offen |
| 9 Produkt | vorhandenes End-to-End-Golden und Produktions-Callback auf Fakes bestanden; Besitz-/CC-/Tempo-Wege sowie Limiter geprüft. F01/F11; übernommenen Komponentenfehlern zugeordnet |
| 10 Editor/.acidpatch | C++-/JS-Kern und Sanitizer ausgeführt; bestehende lokale JUCE-App erneut mit erfolgreichem Selbsttest geprüft. Browser-e2e ausgeführt: F16–F18; kompletter diagnostischer Ablauf mit externen Hilfen PASS. D13 im JUCE-Selbsttest und Browserablauf geprüft. F05–F07/F13 weiterhin offen. Exakter neuer JUCE-9.0.4-Build mangels installiertem Quellstand offen |
| 11 Build/CI/Register | PR-Heads/CI-Status, g++-Logs, Registry-/Checker-Tests, vorhandene ARM-Images/SDRAM-Platzierung, unveränderte Pins geprüft. F14. Auf Folgeauftrag ein isolierter Linux-Prüfworkflow mit elf erfolgreichen Zielen ausgeführt |
| 12 Doku gegen Code | Bedienung, Parameter/CC-Tabellen, Versions-/Imagebezeichner und NICHT-GEMESSEN-Liste gegen die zentralen Implementierungen abgeglichen. F15 und Q1–Q4. Vorhandene PASS-Behauptungen werden durch die neuen Befunde eingeschränkt |

## Nicht geprüft

* **Hardware überall NICHT GEMESSEN:** H14, CV-Ausgangsspannungen/Polarität/Ruhecode, 1V/Oct-Genauigkeit, CV-/Gate-Schwellen und kurze Pulse, DIN-Senden/-Empfangen, MIDI-Jitter, Bedien-/Hörtest, CPU-Reserve im Stressfall, tatsächlicher Stack-Hochwasserstand, Queue-Last, 1-h-Lauf. Kein Flashen, kein Bootloader-Installieren, kein Release.
* **Frischer Build/Selbsttest mit exakt JUCE 9.0.4:** nicht ausgeführt, weil kein passender bereits installierter Quellstand gefunden wurde. Der untersuchte `build/j9/CMakeCache.txt` verweist auf JUCE 9.0.0 im Preen-FM-Editor-Build. Die vorhandene lokale App wurde inzwischen erneut erfolgreich mit `--selftest` ausgeführt; dies ersetzt keinen neuen Build des Prüf-Heads gegen 9.0.4. Gemäß Auftrag kein JUCE-Download. Die genaue Compiler-/Quellherkunft der bereits vorhandenen EXE wurde nicht neu reproduziert.
* **Browser nach echten Korrekturen ohne Prüfhelfer:** noch nicht grün; dies ist wegen F16–F18 ein FAIL des ausgeführten Originalablaufs, keine ausgelassene Prüfung. Der diagnostische Ablauf mit extern gebundenen Timern, expliziter Fake-Kalibrierung und kontrolliertem MD-Prozessende besteht. Nach Einarbeitung muss Claude den originalen Browser-e2e-Test ohne diese Hilfen ausführen.
* **ARM-Neubuilds und BIN-Vorher/Nachher-Gleichheit der Phase 3:** nicht selbst neu gebaut. Bestehende fünf lokale BIN/ELF-Paare wurden geprüft; die zusätzlich bestätigte Tape-Gleichheit betrifft den eigenen Host-Renderer. Die im PR genannten vier anderen Tape-Hashwerte wurden mangels exaktem ursprünglichem Render-Rezept nicht separat nachgerechnet.
* Keine vollständige Exhaustivprüfung aller möglichen MIDI-/SysEx-/Dateieingaben, aller nebenläufigen Ablaufpläne oder aller Klangparameter. Die aufgeführten Negativtests belegen die konkreten Fehler; bestehende bestandene Goldens schließen sie nicht aus.
