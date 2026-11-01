# Streichwurst

Eigenständiges String-/Solo-Instrument mit Ensemble und FX für das
**Electro-Smith Seed3 Desktop Development Kit**. Ziel ist eine möglichst
originalnahe Annäherung an den Waldorf Streichfett auf Grundlage lokaler,
statischer Pluginforschung und selbst entwickelter DSP-Daten und Algorithmen.

**Stand 2026-11-01:** Umgebung und Claude-Auftrag vorbereitet; Projektstatus
`planned`. Es gibt noch keine Streichwurst-C++-Engine, Firmware oder
Hardwareabnahme. Die beigefügten Pythonmodelle sind eigene Forschungsmodelle.

## Einstieg für Claude

1. Repositoryregeln [AGENTS.md](../../AGENTS.md),
   [CLAUDE.md](../../CLAUDE.md) und [HANDOVER.md](../../HANDOVER.md) lesen.
2. [Projektregeln](AGENTS.md) und [Arbeitsauftrag](docs/CLAUDE_AUFTRAG.md) lesen.
3. [Forschungsstand und Herkunft](docs/PROVENANCE.md),
   [Bedienentwurf](docs/CONTROLS.md) und [Prüfstatus](docs/VALIDATION.md) lesen.
4. Stringsektion plus Ensemble zuerst umsetzen, anschließend Solo und übrige FX.

Die [Spezifikation 0.2](docs/research/daisy-dsp-draft-0.2.md) enthält absichtlich
eigene Ersatzverfahren. Sie ist eine Ausgangsbasis; ihre Vereinfachungen sind
keine Nachweise des Originalklangs. Die spätere Firmware soll belegte Beziehungen
bevorzugen und verbleibende Annahmen im Projektbericht offenlegen.

## Arbeitsbereich und Werkzeuge

Projektverzeichnis: `instruments/streichwurst/`, Branch `claude/streichwurst`.
Lokal wurde dafür ein eigener Worktree unter `.work/streichwurst/` vorbereitet,
damit andere laufende Instrumentarbeiten erhalten bleiben.

PowerShell im Repositorywurzelverzeichnis dieses Worktrees:

```powershell
. ./instruments/streichwurst/tools/enter-local.ps1
make list
python -B instruments/streichwurst/tests/reference/own_dsp_model.py --test
```

Das lokale Setup verwendet die bereits installierte Toolchain im gemeinsamen
Checkout. Andere Rechner richten die gepinnten Werkzeuge gemäß
[BUILD.md](../../docs/BUILD.md) ein. Keine globale PATH-Änderung und kein Download
durch das Setupskript.

`make PROJECT=streichwurst` wird derzeit ausdrücklich als noch nicht
implementiertes Projekt abgewiesen. Claude ergänzt den echten Build und meldet
das Projekt erst nach erfolgreichem ARM-Build als `buildable`.

## Geplante Struktur

* `include/streichwurst/`: portabler C++17-DSP-Kern, Parameter und Ereignisse.
* `src/`: Desktop-Kit-Adapter und Firmwareeinstieg.
* `tests/`: eigene DSP-/Ereignis-/Adaptertests und Referenzmodell.
* `docs/`: Auftrag, Bedienung, Forschung, Abweichungen und Prüfberichte.
* `build/`: ignorierte lokale Buildausgaben.

Eigener Code unter der MIT-Lizenz des Repositorys. Herstellerprogramme,
Originaldaten, kopierte Tabellen und rohe Analyseausgaben gehören nicht hierher.
