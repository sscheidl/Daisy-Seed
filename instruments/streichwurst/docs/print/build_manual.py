#!/usr/bin/env python3
"""Writes manual.html (English user manual of Streichwurst 0.1.0) for render.js.

The parameter, range and CC tables come from contracts/parameters.json, so the manual
cannot drift from the firmware's parameter register. Run from this directory:
    python build_manual.py && node render.js
"""
import html
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
CONTRACT = json.loads((HERE.parent.parent / 'contracts' / 'parameters.json').read_text(encoding='utf-8'))

# English names of the contract's parameters (the contract labels are German).
NAMES = {
    'registration': 'Registration', 'octave': 'Octaves', 'crescendo': 'Crescendo', 'release': 'Release (strings)',
    'solo_tone': 'Solo Tone', 'solo_attack': 'Solo Attack', 'solo_release': 'Solo Release',
    'solo_sus_mode': 'Solo sustain mode', 'solo_sustain': 'Solo Sustain', 'solo_trem_depth': 'Tremolo depth',
    'solo_trem_rate': 'Tremolo rate', 'split_mode': 'Layer / Split', 'split_key': 'Split key (MIDI note)',
    'string_velocity': 'Velocity (strings)', 'solo_velocity': 'Velocity (solo)', 'bend_range': 'Bend range',
    'transpose': 'Transpose', 'tune': 'Tune', 'animate': 'Animate', 'animate_rate': 'Animate rate',
    'ensemble_on': 'Ensemble on/off', 'ensemble_mode': 'Ensemble mode', 'phaser': 'Phaser',
    'phaser_rate': 'Phaser rate', 'reverb': 'Reverb', 'reverb_decay': 'Reverb decay',
    'balance': 'Balance strings/solo', 'master': 'Master', 'midi_channel': 'MIDI channel (0 = omni)',
    'pot_page': 'Pot page on the kit', 'preset': 'RAM preset (last recalled)',
}
ENUMS = {
    'octave': ['Base', 'Both', 'Upper'], 'solo_sus_mode': ['Decay to level', 'Hold', 'Percussive'],
    'split_mode': ['Layer', 'Split'], 'ensemble_mode': ['Mode 0 (3 taps)', 'Mode 1 (dry + 5)', 'Mode 2 (dry + 3)'],
    'pot_page': ['Main', 'Solo', 'FX'],
}
UNITS = {'none': '', 'percent': '%', 'hz': 'Hz', 'ms': 'ms', 'db': 'dB', 'semitones': 'st', 'cents': 'cent',
         'channel': '', 'velocity': ''}
OWN = {'animate_rate', 'phaser_rate', 'reverb_decay', 'string_velocity', 'solo_velocity', 'tune'}


def num(v):
    return f'{v:g}'.replace('.', '.')


def param_rows(component):
    rows = []
    for p in CONTRACT['components'][component]['parameters']:
        unit = UNITS[p['unit']]
        if p['type'] == 'enum':
            rng = ', '.join(f'{i} {n}' for i, n in enumerate(ENUMS[p['key']]))
            default = ENUMS[p['key']][int(p['default'])]
        elif p['type'] == 'bool':
            rng, default = 'off / on', 'on' if p['default'] else 'off'
        else:
            rng = f"{num(p['min'])} – {num(p['max'])} {unit}".strip()
            default = f"{num(p['default'])} {unit}".strip()
        notes = []
        if 'readonly' in p.get('flags', []):
            notes.append('read only')
        if 'log' in p.get('flags', []):
            notes.append('log scale')
        if p['key'] in OWN:
            notes.append('own addition')
        cc = str(p['cc']) if p['cc'] is not None else '–'
        rows.append(f"<tr><td class=\"k\">{html.escape(NAMES[p['key']])}</td><td>{html.escape(rng)}</td>"
                    f"<td>{html.escape(default)}</td><td>{cc}</td><td>{', '.join(notes)}</td></tr>")
    return '\n'.join(rows)


def cc_rows():
    rows = []
    for comp in CONTRACT['components'].values():
        for p in comp['parameters']:
            if p['cc'] is not None:
                rows.append((p['cc'], NAMES[p['key']]))
    rows.sort()
    cells = [f'<td class="k">CC {cc}</td><td>{html.escape(name)}</td>' for cc, name in rows]
    half = (len(cells) + 1) // 2
    out = []
    for i in range(half):
        right = cells[half + i] if half + i < len(cells) else '<td></td><td></td>'
        out.append(f'<tr>{cells[i]}{right}</tr>')
    return '\n'.join(out)


TABLE_HEAD = '<tr><th>Parameter</th><th>Range</th><th>Default</th><th>CC</th><th>Note</th></tr>'

page = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>Streichwurst 0.1.0 – User Manual</title>
<link rel="stylesheet" href="style.css"></head><body>

<h1>Streichwurst 0.1.0 – User Manual</h1>
<div class="sub">String machine with solo voices and ensemble effects for the Electro-Smith Seed3 Desktop Development Kit ·
Test build 1 · 1 November 2026 · built from <code>main</code> @ <code>0a29966</code></div>

<p>Streichwurst is a polyphonic string machine in the spirit of the Waldorf Streichfett: a fully polyphonic
string section, eight solo voices, and an effects section with Animate, Ensemble, Phaser and Reverb. It is
its own implementation built from documented research; it is not a copy of the original and does not claim
to sound identical. The string section and the ensemble follow the documented structure of the original
most closely; the soli, Animate, Phaser and Reverb are own designs.</p>

<table>
<tr><th>Image</th><th>Purpose</th><th>Size</th><th>SHA-256</th></tr>
<tr><td class="k">Streichwurst-v0.1.0.bin</td><td>The instrument: strings, soli, FX, Mod on Daisy matrix</td><td style="white-space:nowrap">239 020 B</td><td><code>f244ae6bf53d452b…21681ca75e9</code></td></tr>
<tr><td class="k">StreichwurstFx-v0.1.0.bin</td><td>The FX section alone, for external audio (line input)</td><td style="white-space:nowrap">164 508 B</td><td><code>1a0f4c96310af322…aa55185479f9b6</code></td></tr>
</table>

<div class="warn"><b>Status:</b> host tests, sanitizers, the control app self-test and both ARM builds passed.
Sound, controls, MIDI, CPU load and the comparison with the original on real hardware are <b>NOT MEASURED</b>.
Start with a low listening level.</div>

<h2>1. Installing the firmware</h2>
<ul>
<li>Compare the checksum with <code>SHA256SUMS.txt</code> before flashing.</li>
<li>First time or recovery: hold <b>BOOT</b>, press <b>RESET</b>, release BOOT, then flash over DFU (Daisy Web Programmer
or <code>make PROJECT=streichwurst program-dfu</code>).</li>
<li>Without the Seed buttons: hold <b>S9 + S16</b> (and no other key) for <b>5 s</b>, or use <b>Firmware-Update …</b> in the
control app. All 16 LEDs light up and the bootloader waits for DFU without a time limit.</li>
<li>Recommended order: first test the FX image alone, then the instrument (“Modular FX first”).</li>
</ul>

<h2>2. Connections</h2>
<table>
<tr><th>Connector</th><th>Use</th></tr>
<tr><td class="k">Audio OUT L/R, headphones</td><td>Stereo output, 48 kHz</td></tr>
<tr><td class="k">USB J7</td><td>USB-MIDI: notes, CCs, the control app (SQ protocol and Mod on Daisy on the same port), firmware update</td></tr>
<tr><td class="k">MIDI DIN in</td><td>Notes and CCs (no SysEx)</td></tr>
<tr><td class="k">CV IN 1/2, Gate IN 1/2</td><td>Sources of the modulation matrix (CV 1/2; gates clock the LFOs)</td></tr>
<tr><td class="k">Audio IN L/R</td><td>Only used by the FX image (left alone = mono to both sides)</td></tr>
</table>

<h2>3. How it sounds and is built</h2>
<pre>Strings  -> Animate -> Ensemble -> Phaser -> stereo  \\
Soli (bypass the string effects) ------------------->  Balance -> Reverb -> Master -> limiter -> OUT</pre>
<ul>
<li><b>Strings:</b> all 128 notes are available at the same time. Twelve free-running clocks (one per pitch class)
are divided down into all octaves, like on classic string machines. <b>Registration</b> morphs from a dark,
full sound to a thin, bright one; <b>Octaves</b> selects the base footage, both, or the upper octave.
<b>Crescendo</b> and <b>Release</b> are the swell and fade times (about 0.6 ms to 4.4 s).</li>
<li><b>Soli:</b> eight voices with a Tone morph (hollow square → saw → narrow pulse, darker → brighter), attack,
release, sustain level, three sustain modes and tremolo. In <b>Split</b> mode the keys below the split key play
the strings and the keys from the split key up play the soli; in <b>Layer</b> mode every key plays both.</li>
<li><b>Ensemble:</b> three modes with 3, 5 and 3 modulated delay taps and three LFOs (0.32, 0.81 and 7.77 Hz).
Switching the mode or the ensemble on/off cross-fades in 20 ms. As in the original, the ensemble makes the
whole output louder or quieter depending on the mode (mode 1 is the densest).</li>
<li><b>Animate</b> slowly moves the registration and adds a wandering band emphasis; <b>Phaser</b> sweeps the
ensemble sum; <b>Reverb</b> works on the whole mix including the soli.</li>
<li><b>Balance</b> in the middle plays both sections at full level; turning it to one side fades the other out.</li>
</ul>

<h2>4. Controls on the kit</h2>
<p>The eight pots have three pages. <b>S9</b> switches Main → Solo → FX; S9–S11 show the active page (a short blink
after a change). After a page change, a preset, or a change from MIDI or the app, a pot does nothing until you turn
it across the current value (<b>soft takeover</b>), so values never jump.</p>
<table>
<tr><th>Pot</th><th>Main page</th><th>Solo page</th><th>FX page</th></tr>
<tr><td class="k">VR1</td><td>Registration</td><td>Solo Tone</td><td>Animate</td></tr>
<tr><td class="k">VR2</td><td>Crescendo</td><td>Solo Attack</td><td>Animate rate</td></tr>
<tr><td class="k">VR3</td><td>Release (strings)</td><td>Solo Release</td><td>Phaser</td></tr>
<tr><td class="k">VR4</td><td>Balance</td><td>Tremolo depth</td><td>Phaser rate</td></tr>
<tr><td class="k">VR5</td><td>Animate</td><td>Tremolo rate</td><td>Reverb</td></tr>
<tr><td class="k">VR6</td><td>Phaser</td><td>Solo Sustain</td><td>Reverb decay</td></tr>
<tr><td class="k">VR7</td><td>Reverb</td><td>Split key</td><td>Master</td></tr>
<tr><td class="k">VR8</td><td>Master</td><td>Master</td><td>Balance</td></tr>
</table>
<table>
<tr><th>Key</th><th>Function</th><th>LED</th></tr>
<tr><td class="k">S1 – S8</td><td>Play C4 D4 E4 F4 G4 A4 B4 C5 on the MIDI channel</td><td>lit while held</td></tr>
<tr><td class="k">S9</td><td>Next pot page (on release)</td><td rowspan="3">S9/S10/S11 = Main/Solo/FX page</td></tr>
<tr><td class="k">S10</td><td>Octaves: Base → Both → Upper</td></tr>
<tr><td class="k">S11</td><td>Ensemble mode 0 → 1 → 2</td></tr>
<tr><td class="k">S12</td><td>Ensemble on/off</td><td>lit = on</td></tr>
<tr><td class="k">S13</td><td>Solo sustain mode: Decay to level → Hold → Percussive</td><td>off / dim / bright</td></tr>
<tr><td class="k">S14</td><td>Layer / Split</td><td>lit = Split</td></tr>
<tr><td class="k">S15</td><td>Next RAM preset (0–3)</td><td>blinks preset number + 1 times</td></tr>
<tr><td class="k">S16</td><td>Panic: all sound off with a 2 ms fade (on release)</td><td>lit during the fade</td></tr>
<tr><td class="k">S9 + S16</td><td>Held alone for 5 s: firmware update (bootloader). Neither page change nor panic.</td><td>all LEDs</td></tr>
</table>
<p><b>Sustain modes:</b> <i>Decay to level</i> falls to the Solo Sustain level (80 ms); <i>Hold</i> stays at full level
like an organ; <i>Percussive</i> fades out even while held (0.05–3.2 s, set with Solo Sustain).</p>
<p><b>RAM presets</b> (own presets, nothing is stored in flash): 0 defaults · 1 dark ensemble strings · 2 bright strings
with both octaves, phaser and Animate · 3 split: strings below C4, solo lead above. A preset keeps the MIDI channel,
Master and Balance.</p>

<h2>5. MIDI</h2>
<ul>
<li>Notes, velocity, pitch bend (±2 semitones by default), sustain pedal (CC64), All Notes Off (CC123) and
All Sound Off (CC120). Pitch bend moves strings and soli together.</li>
<li><b>MIDI channel:</b> omni by default; set 1–16 in the app (Global page). Changing it releases held notes.</li>
<li>The strings ignore velocity by default (Velocity (strings) = 0 %); the soli follow velocity fully.</li>
<li>CC1 (mod wheel) and aftertouch belong to the modulation matrix.</li>
</ul>
<table>
<tr><th>CC</th><th>Parameter</th><th>CC</th><th>Parameter</th></tr>
{cc_rows()}
</table>
<p>Continuous parameters use the full CC range; switches use equal zones (on from 64).</p>

<h2>6. Control app and modulation matrix</h2>
<ul>
<li>Use the <b>Mod on Daisy Editor 0.3.0</b> (Windows app, VST3 plug-in, or the browser page
<code>editors/mod-on-daisy/web/index.html</code> in Chrome/Edge). Close other programs that hold the MIDI port.</li>
<li>On connect the app <b>reads</b> the kit first; nothing is overwritten. The status line shows
<code>streichwurst 0.1.0 · SQ + Matrix</code>.</li>
<li>Pages <b>Strings, Solo, Tastatur (keyboard), FX, Global</b> hold every parameter. Pot moves on the kit appear in
the app.</li>
<li><b>Matrix</b> (top) switches to the 8 × 8 modulation matrix; <b>Klangparameter</b> switches back. Sources: LFO 1/2,
Random, CV 1/2, mod wheel, channel and poly aftertouch. Targets: Registration, Animate, Phaser, Reverb, Balance,
Solo Tone, Tremolo (±100 %) and Pitch (±1 semitone). At most eight active connections; an empty matrix leaves the
sound unchanged.</li>
<li><b>Patch speichern / laden</b> saves a <code>.swpatch</code> file with all parameters and the matrix. Loading
shows what fits; only <b>Auf Gerät übertragen</b> sends it. In the VST3 plug-in the patch is stored in the song.</li>
<li><b>Zähler lesen</b> shows diagnostics, e.g. callback peak (CPU) and free stack.</li>
</ul>
<div class="note">The app's interface is in German. The FX image shows only the FX page and no matrix.</div>

<h2>7. The FX image (StreichwurstFx-v0.1.0.bin)</h2>
<p>Line in → Animate, Ensemble, Phaser → Reverb → out. Plug only the left input for mono to both sides. The pots act
only once they are moved.</p>
<table>
<tr><th>Control</th><th>Function</th></tr>
<tr><td class="k">VR1 – VR6</td><td>Animate, Animate rate, Phaser, Phaser rate, Reverb, Reverb decay</td></tr>
<tr><td class="k">VR7</td><td>Input level (unity at about 70 % of the travel)</td></tr>
<tr><td class="k">VR8</td><td>Output level</td></tr>
<tr><td class="k">S1 / S2 / S3</td><td>Ensemble mode 0 / 1 / 2 (LED shows the mode)</td></tr>
<tr><td class="k">S12</td><td>Ensemble on/off (LED)</td></tr>
<tr><td class="k">S16</td><td>Clear delay line and reverb tail (on release); LED S16 = CPU overrun since start</td></tr>
<tr><td class="k">CC 102 – 109</td><td>The FX parameters on any MIDI channel (USB and DIN)</td></tr>
</table>
<p>With the ensemble off and Animate, Phaser and Reverb at 0 the signal passes unchanged.</p>

<h2>8. Parameter reference</h2>
<h3>Instrument</h3>
<table>{TABLE_HEAD}
{param_rows('streichwurst-instrument')}
</table>
<h3>Effects</h3>
<table>{TABLE_HEAD}
{param_rows('streichwurst-fx')}
</table>
<h3>Global</h3>
<table>{TABLE_HEAD}
{param_rows('streichwurst')}
</table>
<p>“Own addition” marks controls the original does not have as separate knobs.</p>

<h2>9. Troubleshooting</h2>
<table>
<tr><th>Observation</th><th>Check</th></tr>
<tr><td>No sound from MIDI</td><td>MIDI channel on the Global page (0 = omni); USB port held by another program?</td></tr>
<tr><td>A pot does nothing</td><td>Soft takeover: turn it across the current value. Check the page (S9 LEDs).</td></tr>
<tr><td>Strings only, no solo (or the reverse)</td><td>Balance (VR4 on Main); Layer/Split and split key (S14, VR7 on Solo).</td></tr>
<tr><td>Volume jumps when switching the ensemble</td><td>Expected: the ensemble mode sets the overall level, as in the original.</td></tr>
<tr><td>Hanging notes</td><td>S16 (panic) or CC123; report the situation.</td></tr>
<tr><td>Crackles, drop-outs</td><td>Note the setting and read the counters in the app (callback peak, overruns); report them.</td></tr>
<tr><td>The app shows no Streichwurst pages</td><td>Editor 0.3.0 or newer is needed.</td></tr>
</table>

<h2>10. Test sheet</h2>
<p>Record results with the date as <b>MEASURED</b>; anything not checked stays <b>NOT MEASURED</b>.</p>
<table>
<tr><th>#</th><th>Step</th><th>Expected</th><th class="result">Result</th></tr>
<tr><td>1</td><td>FX image: line source, defaults</td><td>widened chorus, some reverb</td><td></td></tr>
<tr><td>2</td><td>FX image: S12 off, VR1/VR3/VR5 at 0</td><td>signal unchanged</td><td></td></tr>
<tr><td>3</td><td>Instrument: power up, play S1–S8</td><td>strings in ensemble mode 1, stereo</td><td></td></tr>
<tr><td>4</td><td>MIDI keyboard over USB and DIN, bend, pedal</td><td>notes, ±2 st bend, pedal holds</td><td></td></tr>
<tr><td>5</td><td>Main page VR1–VR8</td><td>each control audible, no jumps</td><td></td></tr>
<tr><td>6</td><td>S9 pages, S10–S15</td><td>pages and switches as in section 4</td><td></td></tr>
<tr><td>7</td><td>Split (S14) with solo on the upper keys</td><td>strings below, solo above</td><td></td></tr>
<tr><td>8</td><td>Dense chord, octaves Both, mode 1, reverb</td><td>no drop-outs; app counter overruns = 0</td><td></td></tr>
<tr><td>9</td><td>App: connect, change values, matrix LFO 1 → Registration</td><td>changes audible, pots visible in the app</td><td></td></tr>
<tr><td>10</td><td>S16 panic, S9 + S16 for 5 s</td><td>silence; bootloader with all LEDs</td><td></td></tr>
</table>
<p>Full test lists: <code>effects/streichwurst-fx/docs/HARDWARE_TEST.md</code> (F1–F14) and
<code>instruments/streichwurst/docs/HARDWARE_TEST.md</code> (H1–H20).</p>

<div class="foot">Sources: instruments/streichwurst/docs (CONTROLS, PARAMETERS, FIDELITY, VALIDATION),
effects/streichwurst-fx/README.md · Parameter tables generated from contracts/parameters.json ·
Repository sscheidl/Daisy-Seed, PR #77</div>
</body></html>
"""

(HERE / 'manual.html').write_text(page, encoding='utf-8', newline='\n')
print('wrote', HERE / 'manual.html')
