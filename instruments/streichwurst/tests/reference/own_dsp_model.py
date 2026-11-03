#!/usr/bin/env python3
"""Independent analytical source and ensemble experiment, NOT a plugin emulator.

No input files, manufacturer tables, presets or samples. NumPy is used locally.
Per-sample float delays, exact third-cycle offsets and normalization are our
design choices; these differ from the observed integer block algorithm.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import unittest

import numpy as np

def outside_git(path: Path) -> Path:
    """Keep optional experiment output outside every Git checkout/worktree.

    The standalone handover needs no PE/disassembly checker dependencies.
    """
    path = path.resolve()
    if any((parent / ".git").exists() for parent in (path, *path.parents)):
        raise ValueError("Input/output must be outside every Git checkout/worktree")
    return path


def parabola(phase: float) -> float:
    v = 2 * ((phase + 0.5) % 1) - 1
    return 4 * v * (1 - abs(v))


def source(notes: list[int], fs: float, count: int, start: int = 0) -> np.ndarray:
    """Own bandlimited harmonic design with phases shared by pitch class.

    Twelve low reference clocks, octave multipliers; no voice-start phase reset.
    This does not reconstruct the plugin's event kernels or registration data.
    """
    if not math.isfinite(fs) or not 8000 <= fs <= 192000:
        raise ValueError("Source sample rate")
    if not 0 <= count <= 1920000 or not 0 <= start < 2**48 or len(notes) > 128:
        raise ValueError("Bounded source frame/note count")
    time = np.arange(start, start + count, dtype=np.float64) / fs
    out = np.zeros(count)
    for note in notes:
        if not isinstance(note, (int, np.integer)) or not 0 <= note <= 127:
            raise ValueError("MIDI note range")
        cls, octave = note % 12, note // 12
        low_frequency = 440 * 2 ** ((cls - 69) / 12)
        frequency = low_frequency * 2**octave
        phase = time * low_frequency * 2**octave
        harmonics = min(16, int(0.45 * fs / frequency))
        for h in range(1, harmonics + 1):
            out += math.exp(-0.16 * h) / h * np.sin(2 * np.pi * h * phase)
    return out * (0.18 / max(1, len(notes)))


class GateBank:
    """Own explicit note/sustain contract; no claim about plugin priorities."""
    def __init__(self):
        self.held = [0] * 128
        self.pending: set[int] = set()
        self.sustain = False
        self.level = [0.0] * 128

    def event(self, kind: str, note: int = 0):
        if not 0 <= note < 128:
            raise ValueError("Note range")
        if kind == "on":
            self.held[note] += 1
            self.pending.discard(note)
        elif kind == "off":
            self.held[note] = max(0, self.held[note] - 1)
            if self.held[note] == 0 and self.sustain:
                self.pending.add(note)
        elif kind == "pedal_on":
            self.sustain = True
        elif kind == "pedal_off":
            self.sustain = False
            self.pending.clear()
        elif kind == "all_off":
            self.held[:] = [0] * 128
            self.pending.clear()
            self.sustain = False
        elif kind == "reset":
            self.__init__()
        else:
            raise ValueError("Event kind")

    def tick(self, fs: float, attack: float, release: float):
        if not all(math.isfinite(v) and v > 0 for v in (fs, attack, release)):
            raise ValueError("Envelope times")
        for n in range(128):
            target = float(self.held[n] > 0 or n in self.pending)
            alpha = -math.expm1(-1 / (fs * (attack if target else release)))
            self.level[n] += alpha * (target - self.level[n])
            if not target and self.level[n] < 1e-7:
                self.level[n] = 0.0


class Ensemble:
    def __init__(self, fs: float = 48000, mode: int = 0):
        if not 8000 <= fs <= 192000 or mode not in (0, 1, 2):
            raise ValueError("Rate/mode range")
        self.fs, self.mode = float(fs), mode
        self.ring = np.zeros(math.ceil(512 * fs / 40000) + 2)
        self.write = 0
        self.phases = [0.0, 0.0, 0.0]
        self.wet = 1.0
        self.min_delay, self.max_delay = math.inf, -math.inf

    def process(self, signal: np.ndarray, targets: np.ndarray | None = None) -> np.ndarray:
        signal = np.asarray(signal, dtype=np.float64)
        if signal.ndim != 1 or not np.isfinite(signal).all():
            raise ValueError("Finite mono vector required")
        if len(signal) > 1920000:
            raise ValueError("Frame limit")
        if targets is not None:
            targets = np.asarray(targets, dtype=np.float64)
            if targets.ndim != 1 or len(targets) != len(signal) or not np.isfinite(targets).all():
                raise ValueError("Wet target shape/length")
            if len(targets) and (np.min(targets) < 0 or np.max(targets) > 1):
                raise ValueError("Wet target range")
        out = np.empty((len(signal), 2))
        frequencies = (0.81, 7.77, 0.32)
        smooth = -math.expm1(-1 / (0.020 * self.fs))
        scale = self.fs / 40000
        for n, sample in enumerate(signal):
            self.ring[self.write] = sample
            for k, frequency in enumerate(frequencies):
                self.phases[k] = (self.phases[k] + frequency / self.fs) % 1
            slow, fast, chorus = self.phases
            # Third-cycle phase pairs are an explicit independent choice.
            delays = [(248 - 64 * parabola(slow + k / 3) -
                       8 * parabola(fast + k / 3)) * scale for k in range(3)]
            if self.mode == 2:
                delays = [(248 + 128 * parabola(chorus + phase)) * scale
                          for phase in (0, 3 / 8, -3 / 8)]
            elif self.mode == 1:
                delays += [(248 + 128 * parabola(chorus + phase)) * scale
                           for phase in (0, 3 / 8)]
            taps = []
            for delay in delays:
                self.min_delay = min(self.min_delay, delay)
                self.max_delay = max(self.max_delay, delay)
                position = (self.write - delay) % len(self.ring)
                index = math.floor(position)
                frac = position - index
                taps.append((1 - frac) * self.ring[index] +
                            frac * self.ring[(index + 1) % len(self.ring)])
            common = sum(taps) + (sample if self.mode else 0)
            # Our DC-normalized matrix retains the observed subtraction scale.
            gain = 0.8 / (len(taps) + bool(self.mode) - 21 / 128)
            left, right = (common - 21 / 128 * taps[-1]) * gain, (common - 21 / 128 * taps[-2]) * gain
            target = 1.0 if targets is None else float(targets[n])
            self.wet += smooth * (target - self.wet)
            out[n] = ((1 - self.wet) * sample + self.wet * left,
                      (1 - self.wet) * sample + self.wet * right)
            self.write = (self.write + 1) % len(self.ring)
        return out


def metrics(y: np.ndarray, fs: float, fundamental: float | None = None) -> dict:
    stereo = np.column_stack((y, y)) if y.ndim == 1 else y
    mono = stereo.mean(axis=1)
    power = np.mean(stereo**2, axis=0)
    corr = float(np.corrcoef(stereo.T)[0, 1]) if min(power) > 1e-20 else None
    result = {"peak": float(np.max(np.abs(stereo))), "rms_lr": np.sqrt(power).tolist(),
              "dc_lr": stereo.mean(axis=0).tolist(), "correlation": corr,
              "mono_rms": float(np.sqrt(np.mean(mono**2))),
              "max_adjacent_difference": float(np.max(np.abs(np.diff(stereo, axis=0))))}
    if fundamental:
        spec = np.abs(np.fft.rfft(mono * np.hanning(len(mono))))**2
        hz = np.fft.rfftfreq(len(mono), 1 / fs)
        nearby = np.abs(hz - fundamental) <= 30
        carrier = np.abs(hz - fundamental) <= 2
        result["nearby_sideband_power_fraction"] = float(spec[nearby & ~carrier].sum() /
                                                            max(spec[nearby].sum(), 1e-30))
    return result


def experiment() -> dict:
    fs, count = 48000, 48000 * 2
    time = np.arange(count) / fs
    impulse = np.zeros(count)
    impulse[256] = 0.5
    cases = {"impulse": (impulse, None), "effect_sine_440": (0.2 * np.sin(2 * np.pi * 440 * time), 440),
             "source_A2": (source([45], fs, count), 110), "source_A4": (source([69], fs, count), 440),
             "source_A6": (source([93], fs, count), 1760),
             "source_chord": (source([57, 61, 64], fs, count), None),
             "slow_parameter": (source([69], fs, count), 440)}
    results = {}
    for name, (dry, frequency) in cases.items():
        wet_target = (0.5 + 0.5 * np.cos(np.pi * time / 2)) if name == "slow_parameter" else None
        result = {"dry": metrics(dry, fs, frequency), "modes": {}}
        for mode in range(3):
            effect = Ensemble(fs, mode)
            wet = effect.process(dry, wet_target)
            result["modes"][str(mode)] = {**metrics(wet, fs, frequency),
                "delay_range_samples": [effect.min_delay, effect.max_delay]}
        results[name] = result
    return {"schema": 1, "sample_rate": fs, "frames_per_case": count,
            "tool_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "numpy_version": np.__version__, "cases": results,
            "scope": "Own design self-consistency, no original render, no hardware benchmark; 2 s is not a full slow-LFO cycle"}


class Tests(unittest.TestCase):
    def test_phase_sharing_and_block_source(self):
        a = source([45, 57, 69], 48000, 1000)
        b = np.concatenate((source([45, 57, 69], 48000, 273), source([45, 57, 69], 48000, 727, 273)))
        np.testing.assert_array_equal(a, b)

    def test_envelope_time_and_release(self):
        bank = GateBank()
        bank.event("on", 69)
        for _ in range(1000):
            bank.tick(10000, 0.1, 0.2)
        self.assertAlmostEqual(bank.level[69], 1 - math.exp(-1), places=12)
        before = bank.level[69]
        bank.event("off", 69)
        for _ in range(2000):
            bank.tick(10000, 0.1, 0.2)
        self.assertAlmostEqual(bank.level[69] / before, math.exp(-1), places=12)

    def test_repeated_note_sustain_reset(self):
        b = GateBank()
        b.event("on", 69); b.event("on", 69); b.event("off", 69)
        self.assertEqual(b.held[69], 1)
        b.event("pedal_on"); b.event("off", 69)
        self.assertIn(69, b.pending)
        b.event("pedal_off")
        self.assertFalse(b.pending)
        b.event("reset")
        self.assertEqual(sum(b.level) + sum(b.held), 0)

    def test_block_continuity_and_parameter_smoothing(self):
        signal = source([57, 61, 64], 48000, 2000)
        target = np.linspace(0, 1, len(signal))
        for mode in range(3):
            a, b = Ensemble(mode=mode), Ensemble(mode=mode)
            expected = a.process(signal, target)
            pieces = []; start = 0
            for length in (1, 47, 3, 128, 511, 1310):
                pieces.append(b.process(signal[start:start + length], target[start:start + length]))
                start += length
            np.testing.assert_array_equal(expected, np.concatenate(pieces))

    def test_zero_and_dc_normalization(self):
        for mode in range(3):
            np.testing.assert_array_equal(Ensemble(mode=mode).process(np.zeros(2048)), np.zeros((2048, 2)))
            y = Ensemble(mode=mode).process(np.ones(3000))
            np.testing.assert_allclose(y[-100:], 0.8, atol=1e-14)

    def test_delay_bounds_full_modulation(self):
        # Evaluate a full sampled phase grid rather than assuming a 2 s sweep
        # covers the ~3.125 s slow oscillator.
        vals = np.array([parabola(k / 10000) for k in range(10000)])
        self.assertLessEqual(float(abs(vals).max()), 1)
        self.assertAlmostEqual(float(vals.mean()), 0, places=12)
        self.assertGreaterEqual(248 - 128 * float(abs(vals).max()), 120)
        for fs in (40000, 48000, 96000):
            e = Ensemble(fs, 2)
            e.process(np.ones(1000))
            self.assertGreater(e.min_delay, 0)
            self.assertLess(e.max_delay, len(e.ring) - 1)

    def test_reject_invalid_inputs(self):
        with self.assertRaises(ValueError): Ensemble(0)
        with self.assertRaises(ValueError): Ensemble(mode=3)
        with self.assertRaises(ValueError): Ensemble().process(np.array([math.nan]))
        with self.assertRaises(ValueError): GateBank().event("on", 128)
        with self.assertRaises(ValueError): source([69], 0, 10)
        with self.assertRaises(ValueError): source([69.5], 48000, 10)
        self.assertEqual(Ensemble().process(np.array([])).shape, (0, 2))

    def test_source_frequency_and_bandlimit(self):
        signal = source([69], 48000, 48000)
        power = abs(np.fft.rfft(signal))**2
        hz = np.fft.rfftfreq(len(signal), 1 / 48000)
        self.assertEqual(float(hz[np.argmax(power)]), 440.0)
        self.assertLess(float(power[hz > 0.45 * 48000].sum() / power.sum()), 1e-20)

    def test_impulse_has_no_feedback_tail(self):
        impulse = np.zeros(2048)
        impulse[256] = 0.5
        for mode in range(3):
            output = Ensemble(mode=mode).process(impulse)
            self.assertGreater(float(abs(output).max()), 0)
            np.testing.assert_array_equal(output[1024:], np.zeros((1024, 2)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--test", action="store_true")
    args = parser.parse_args()
    if args.test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(Tests)
        if not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful():
            raise SystemExit(1)
    if args.out:
        out = outside_git(args.out)
        with out.open("x", encoding="utf-8", newline="\n") as handle:
            handle.write(json.dumps(experiment(), indent=2, sort_keys=True) + "\n")
        print("PASS: own signal experiment saved externally")


if __name__ == "__main__":
    main()
