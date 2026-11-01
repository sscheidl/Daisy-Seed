"""Small measurement widgets: audio level bars, an audio snapshot oscilloscope and a
rolling CV/gate trace. All values are digital (dBFS, normalized CV), never volts."""
from __future__ import annotations

import math
from collections import deque

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QPainter, QPen
from PySide6.QtWidgets import QWidget

from .. import protocol as p
from . import theme

DB_FLOOR = -60.0  # bottom of the level scale


def dbfs(level: float) -> float | None:
    return None if level <= 0.0 else 20.0 * math.log10(level)


def format_dbfs(level: float) -> str:
    value = dbfs(level)
    return "−∞ dBFS" if value is None else f"{value:+.1f} dBFS"


class LevelBar(QWidget):
    """Horizontal peak meter with a slowly falling peak-hold mark."""

    def __init__(self, label: str, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.label = label
        self.level = 0.0  # 0..1 of full scale
        self.hold = 0.0
        self.active = False
        self.setMinimumHeight(22)
        self.setMinimumWidth(160)

    def set_level(self, level: float, active: bool = True) -> None:
        self.level = max(0.0, min(1.0, level))
        self.hold = self.level if self.level >= self.hold else self.hold * 0.92
        self.active = active
        self.update()

    @staticmethod
    def _fraction(level: float) -> float:
        value = dbfs(level)
        if value is None:
            return 0.0
        return max(0.0, min(1.0, (value - DB_FLOOR) / -DB_FLOOR))

    def paintEvent(self, _event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()
        label_w = 60
        bar = QRectF(label_w, 4, w - label_w - 70, h - 8)
        painter.setPen(theme.MUTED)
        painter.setFont(theme.font(theme.TECH_FONT, 8.5))
        painter.drawText(QRectF(0, 0, label_w - 6, h), int(Qt.AlignRight | Qt.AlignVCenter), self.label)
        painter.setPen(QPen(theme.PANEL_EDGE, 1))
        painter.setBrush(QColor("#141516"))
        painter.drawRoundedRect(bar, 2, 2)
        if self.active:
            frac = self._fraction(self.level)
            colour = theme.OK if dbfs(self.level) is None or dbfs(self.level) < -6 else (
                theme.WARN if dbfs(self.level) < -1 else theme.ERROR)
            painter.setPen(Qt.NoPen)
            painter.setBrush(colour)
            painter.drawRoundedRect(QRectF(bar.left(), bar.top(), bar.width() * frac, bar.height()), 2, 2)
            hold_x = bar.left() + bar.width() * self._fraction(self.hold)
            painter.setPen(QPen(theme.TEXT, 2))
            painter.drawLine(QPointF(hold_x, bar.top()), QPointF(hold_x, bar.bottom()))
        painter.setPen(theme.FAINT)
        for db in (-40, -20, -6):
            x = bar.left() + bar.width() * (db - DB_FLOOR) / -DB_FLOOR
            painter.drawLine(QPointF(x, bar.bottom() - 3), QPointF(x, bar.bottom()))
        painter.setPen(theme.TEXT if self.active else theme.MUTED)
        painter.setFont(theme.font(theme.MONO_FONT, 8.5))
        painter.drawText(QRectF(bar.right() + 6, 0, 64, h), int(Qt.AlignLeft | Qt.AlignVCenter),
                         format_dbfs(self.level) if self.active else "–")
        painter.end()


def measure(samples: list[tuple[int, int]], channel: int,
            sample_rate: int = p.AUDIO_SAMPLE_RATE) -> tuple[float | None, float]:
    """(frequency estimate from rising zero crossings or None, peak 0..1) of one channel."""
    values = [s[channel] / 32768.0 for s in samples]
    peak = max((abs(v) for v in values), default=0.0)
    if peak < 0.002:  # below -54 dBFS: zero crossings would be noise
        return None, peak
    crossings = [i for i in range(1, len(values)) if values[i - 1] < 0.0 <= values[i]]
    if len(crossings) < 2:
        return None, peak
    span = crossings[-1] - crossings[0]
    return (len(crossings) - 1) * sample_rate / span if span else None, peak


class ScopeWidget(QWidget):
    """Draws one audio snapshot (both channels) with time grid and measurements."""

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.samples: list[tuple[int, int]] | None = None
        self.sample_rate = p.AUDIO_SAMPLE_RATE
        self.measurements: list[tuple[float | None, float]] = [(None, 0.0), (None, 0.0)]
        self.setMinimumHeight(180)
        self.setMinimumWidth(240)

    def set_capture(self, samples: list[tuple[int, int]] | None) -> None:
        self.samples = samples
        self.measurements = [measure(samples, ch) if samples else (None, 0.0) for ch in range(2)]
        self.update()

    def summary(self) -> str:
        if not self.samples:
            return "No snapshot yet."
        parts = []
        for name, (freq, peak) in zip(("L", "R"), self.measurements):
            f = "–" if freq is None else f"{freq:.1f} Hz"
            parts.append(f"{name}: {f}, peak {format_dbfs(peak)}")
        ms = 1000.0 * len(self.samples) / self.sample_rate
        return f"{len(self.samples)} frames = {ms:.1f} ms · " + " · ".join(parts)

    def paintEvent(self, _event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        area = QRectF(8, 8, self.width() - 16, self.height() - 16)
        painter.setPen(QPen(theme.PANEL_EDGE, 1))
        painter.setBrush(QColor("#141516"))
        painter.drawRect(area)
        mid = area.center().y()
        painter.setPen(QPen(theme.FAINT, 1, Qt.DashLine))
        painter.drawLine(QPointF(area.left(), mid), QPointF(area.right(), mid))
        for k in range(1, 4):
            y = area.top() + area.height() * k / 4
            if k != 2:
                painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y))
        if not self.samples:
            painter.setPen(theme.MUTED)
            painter.setFont(theme.font(theme.TECH_FONT, 9))
            painter.drawText(area, int(Qt.AlignCenter), "No audio snapshot captured")
            painter.end()
            return
        n = len(self.samples)
        duration_ms = 1000.0 * n / self.sample_rate
        painter.setFont(theme.font(theme.TECH_FONT, 7.5))
        for k in range(5):
            x = area.left() + area.width() * k / 4
            painter.setPen(QPen(theme.FAINT, 1, Qt.DashLine))
            if 0 < k < 4:
                painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()))
            painter.setPen(theme.MUTED)
            label = f"{duration_ms * k / 4:.1f} ms"
            if k == 0:
                painter.drawText(QRectF(x + 2, area.bottom() - 14, 60, 12), int(Qt.AlignLeft), label)
            elif k == 4:
                painter.drawText(QRectF(x - 62, area.bottom() - 14, 60, 12), int(Qt.AlignRight), label)
            else:
                painter.drawText(QRectF(x - 30, area.bottom() - 14, 60, 12), int(Qt.AlignCenter), label)
        half = area.height() / 2 - 2
        for channel, colour in ((0, theme.OK), (1, theme.WARN)):
            painter.setPen(QPen(colour, 1.2))
            step = max(1, n // int(max(1, area.width() * 2)))
            points = [QPointF(area.left() + area.width() * i / (n - 1),
                              mid - self.samples[i][channel] / 32768.0 * half)
                      for i in range(0, n, step)]
            painter.drawPolyline(points)
        painter.setPen(theme.OK)
        painter.drawText(QRectF(area.left() + 6, area.top() + 4, 60, 12), int(Qt.AlignLeft), "L")
        painter.setPen(theme.WARN)
        painter.drawText(QRectF(area.left() + 20, area.top() + 4, 60, 12), int(Qt.AlignLeft), "R")
        painter.end()


class CvTraceWidget(QWidget):
    """Rolling trace of CV IN 1/2 (normalized), the gates and the CV OUT 1 echo over
    the last `window_s` seconds of received frames (50–100 Hz, not an audio scope)."""

    def __init__(self, window_s: float = 10.0, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.window_ms = int(window_s * 1000)
        self.frames: deque[tuple[int, float, float, bool, bool, float]] = deque(maxlen=2000)
        self.setMinimumHeight(150)
        self.setMinimumWidth(240)

    def push(self, time_ms: int, cv: list[float], gates: list[bool], cv_out: list[int]) -> None:
        self.frames.append((time_ms, cv[0], cv[1], gates[0], gates[1], cv_out[0] / p.DAC_MAX))

    def clear(self) -> None:
        self.frames.clear()
        self.update()

    def paintEvent(self, _event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        area = QRectF(8, 8, self.width() - 16, self.height() - 16)
        painter.setPen(QPen(theme.PANEL_EDGE, 1))
        painter.setBrush(QColor("#141516"))
        painter.drawRect(area)
        mid = area.center().y()
        painter.setPen(QPen(theme.FAINT, 1, Qt.DashLine))
        painter.drawLine(QPointF(area.left(), mid), QPointF(area.right(), mid))
        painter.setFont(theme.font(theme.TECH_FONT, 7.5))
        painter.setPen(theme.MUTED)
        painter.drawText(QRectF(area.left() + 4, area.top() + 2, 40, 12), int(Qt.AlignLeft), "+1")
        painter.drawText(QRectF(area.left() + 4, area.bottom() - 14, 40, 12), int(Qt.AlignLeft), "−1")
        painter.drawText(QRectF(area.right() - 80, area.bottom() - 14, 76, 12), int(Qt.AlignRight),
                         f"{self.window_ms // 1000} s")
        if len(self.frames) < 2:
            painter.setPen(theme.MUTED)
            painter.setFont(theme.font(theme.TECH_FONT, 9))
            painter.drawText(area, int(Qt.AlignCenter), "Waiting for CV frames")
            painter.end()
            return
        t_end = self.frames[-1][0]
        t_start = t_end - self.window_ms

        def x_of(t: int) -> float:
            return area.left() + area.width() * max(0.0, min(1.0, (t - t_start) / self.window_ms))

        half = area.height() / 2 - 2
        # Gates as shaded bands at the bottom (GATE 1) and top (GATE 2).
        painter.setPen(Qt.NoPen)
        for gate_index, band in ((0, QRectF(0, area.bottom() - 10, 0, 8)),
                                 (1, QRectF(0, area.top() + 2, 0, 8))):
            colour = QColor(theme.OK)
            colour.setAlphaF(0.55)
            painter.setBrush(colour)
            run_start = None
            prev_t = None
            for frame in self.frames:
                high = frame[3 + gate_index]
                if high and run_start is None:
                    run_start = frame[0]
                if not high and run_start is not None:
                    painter.drawRect(QRectF(x_of(run_start), band.top(), max(1.5, x_of(prev_t) - x_of(run_start)), band.height()))
                    run_start = None
                prev_t = frame[0]
            if run_start is not None:
                painter.drawRect(QRectF(x_of(run_start), band.top(), max(1.5, x_of(t_end) - x_of(run_start)), band.height()))
        # CV OUT 1 echo (normalized code) dashed, CV IN 1/2 solid.
        series = (
            (5, QPen(theme.MUTED, 1, Qt.DashLine), lambda v: mid - (2 * v - 1) * half),
            (1, QPen(theme.OK, 1.4), lambda v: mid - v * half),
            (2, QPen(theme.WARN, 1.4), lambda v: mid - v * half),
        )
        for index, pen, y_of in series:
            painter.setPen(pen)
            points = [QPointF(x_of(f[0]), y_of(f[index])) for f in self.frames if f[0] >= t_start]
            if len(points) >= 2:
                painter.drawPolyline(points)
        painter.setPen(theme.OK)
        painter.drawText(QRectF(area.right() - 150, area.top() + 2, 40, 12), int(Qt.AlignLeft), "CV 1")
        painter.setPen(theme.WARN)
        painter.drawText(QRectF(area.right() - 110, area.top() + 2, 40, 12), int(Qt.AlignLeft), "CV 2")
        painter.setPen(theme.MUTED)
        painter.drawText(QRectF(area.right() - 70, area.top() + 2, 66, 12), int(Qt.AlignLeft), "OUT 1")
        painter.end()
