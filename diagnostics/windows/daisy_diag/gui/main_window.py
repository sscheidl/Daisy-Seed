"""Main window: board view on the left, categorised control panel on the right."""
from __future__ import annotations

import math
import time
import json
from pathlib import Path
from datetime import datetime

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (QAbstractItemView, QAbstractScrollArea, QButtonGroup, QCheckBox,
                               QComboBox, QFormLayout, QGridLayout, QGroupBox, QHBoxLayout,
                               QHeaderView, QLabel, QMainWindow, QPlainTextEdit, QPushButton,
                               QScrollArea, QSlider, QSpinBox, QSplitter, QTableWidget,
                               QTableWidgetItem, QVBoxLayout, QWidget, QMessageBox, QFileDialog,
                               QTabWidget, QSizePolicy)

from .. import __version__
from .. import protocol as p
from ..session import ConnState, DeviceSession, SerialLink, list_serial_ports
from ..simulator import SimulatedCable
from . import theme
from .board import BoardWidget, SdDisplay
from .scope import CvTraceWidget, LevelBar, ScopeWidget

# Firmware LED modes offered by the GUI. FOLLOW is the firmware default: every LED
# D1–D16 lights while its button S1–S16 is held. The others are test patterns;
# POTDIM (firmware 0.4.0) lights all LEDs with VR1 as brightness control.
LED_MODE_BUTTONS = (
    ("FOLLOW", 'Buttons → LEDs'),
    ("OFF", 'All off'),
    ("ON", 'All on'),
    ("CHASE", 'Chase'),
    ("POTDIM", 'VR1 dims'),
)

CV_OUT_BUTTONS = (("OFF", "Off"), ("FIX", "Fixed"), ("RAMP", "Ramp"), ("SAW", "Saw"))
TONE_LEVELS_DBFS = (-6, -12, -20, -30, -40)  # amplitude presets; -6 dBFS is the firmware cap
SCOPE_FRAME_CHOICES = (256, 512, 1024, 2048)

# Control-panel categories, in tab order, with short labels so all tabs fit in the
# 480 px panel without scroll buttons. The simulation tab is appended in simulation mode.
TABS = ("USB", "Buttons", "Pots", "CV / Gate", "Audio", "SD card", "Log")
TAB_TOOLTIPS = {
    "USB": "USB connection, firmware and protocol versions, transport statistics",
    "Buttons": "Buttons S1–S16 (press counts) and LEDs D1–D16 (test patterns, brightness)",
    "Pots": "Potentiometers VR1–VR8, toggle switches, plug and card detection",
    "CV / Gate": "CV inputs with rolling trace, gate inputs and the CV OUT test signal",
    "Audio": "Audio peak meters, test tone generator and input snapshot (firmware 0.4.0)",
    "SD card": "microSD read test and Windows card-reader ownership",
    "Log": "Event log",
}


def _amplitude_for_dbfs(db: int) -> int:
    return min(p.TONE_MAX_AMPLITUDE, int(round(10 ** (db / 20.0) * 10000)))
SIM_TAB = "Simulation"

STATE_COLORS = {
    ConnState.CONNECTED: theme.OK,
    ConnState.HANDSHAKE: theme.TEXT,
    ConnState.SEARCHING: theme.FAINT,
    ConnState.ERROR: theme.ERROR,
    ConnState.INCOMPATIBLE: theme.WARN,
}
STATE_TEXT = {
    ConnState.CONNECTED: 'Connected',
    ConnState.HANDSHAKE: 'Connecting …',
    ConnState.SEARCHING: 'Disconnected',
    ConnState.ERROR: 'Connection error',
    ConnState.INCOMPATIBLE: 'Incompatible protocol',
}


def _value_label(text: str = "–") -> QLabel:
    label = QLabel(text)
    label.setTextInteractionFlags(Qt.TextSelectableByMouse)
    label.setWordWrap(True)
    label.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Preferred)
    return label


def _hint(text: str) -> QLabel:
    label = QLabel(text)
    label.setObjectName("hint")
    label.setWordWrap(True)
    return label


def _value_table(row_names: list[str]) -> QTableWidget:
    """Read-only table with Value / Raw ADC / Min / Max columns sized to its contents."""
    table = QTableWidget(len(row_names), 5)
    table.setHorizontalHeaderLabels(["", 'Value', 'Raw ADC', "Min", "Max"])
    table.verticalHeader().setVisible(False)
    table.setEditTriggers(QAbstractItemView.NoEditTriggers)
    table.setSelectionMode(QAbstractItemView.NoSelection)
    table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
    for i, name in enumerate(row_names):
        table.setItem(i, 0, QTableWidgetItem(name))
        for c in range(1, 5):
            item = QTableWidgetItem("–")
            item.setTextAlignment(Qt.AlignRight | Qt.AlignVCenter)
            table.setItem(i, c, item)
    # Height follows the font metrics instead of a fixed pixel count, so larger system
    # fonts never clip the last row behind an inner scroll bar.
    table.verticalHeader().setSectionResizeMode(QHeaderView.ResizeToContents)
    table.setVerticalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
    table.setSizeAdjustPolicy(QAbstractScrollArea.AdjustToContents)
    table.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
    return table


class MainWindow(QMainWindow):
    def __init__(self, simulation: bool = False, port: str | None = None, clock=time.monotonic):
        super().__init__()
        self._clock = clock
        self.cable: SimulatedCable | None = None
        self.session: DeviceSession | None = None
        self.paused = False
        self.sd_result: p.SdInfo | None = None
        self._last_sd_card: bool | None = None
        self._port_override = port
        self.cv_min: list[float | None] = [None] * p.NUM_CV
        self.cv_max: list[float | None] = [None] * p.NUM_CV
        self.resize(1560, 940)

        self._build_ui()
        self._start_session(simulation)

        self.poll_timer = QTimer(self)
        self.poll_timer.setTimerType(Qt.PreciseTimer)
        self.poll_timer.timeout.connect(self._tick)
        self.poll_timer.start(10)
        self.panel_timer = QTimer(self)
        self.panel_timer.timeout.connect(self._refresh_panels)
        self.panel_timer.start(100)
        self.port_timer = QTimer(self)
        self.port_timer.timeout.connect(self._refresh_ports)
        self.port_timer.start(2000)

    # ---- UI construction ---------------------------------------------------------------
    def _build_ui(self) -> None:
        central = QWidget()
        outer = QVBoxLayout(central)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)
        outer.addWidget(self._build_header())
        self.sim_banner = QLabel('SIMULATION – all displayed values are simulated; no Daisy Seed3 is connected')
        self.sim_banner.setObjectName("simBanner")
        self.sim_banner.setAlignment(Qt.AlignCenter)
        outer.addWidget(self.sim_banner)

        split = QSplitter(Qt.Horizontal)
        left = QWidget()
        left_layout = QVBoxLayout(left)
        left_layout.setContentsMargins(12, 12, 6, 12)
        self.board = BoardWidget()
        self.board.clock = self._clock
        left_layout.addWidget(self.board, 1)
        split.addWidget(left)

        controls = QWidget()
        controls_layout = QVBoxLayout(controls)
        controls_layout.setContentsMargins(0, 4, 10, 10)
        controls_layout.setSpacing(8)
        self.control_tabs = QTabWidget()
        self.control_tabs.setMinimumWidth(480)
        self.control_pages: dict[str, QScrollArea] = {}
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(1000)
        self.log.setMinimumHeight(200)
        pages = {
            "USB": [self._build_connection_group()],
            "Buttons": [self._build_counter_group(), self._build_led_group()],
            "Pots": [self._build_pot_group(), self._build_inputs_group()],
            "CV / Gate": [self._build_cvgate_group()],
            "Audio": [self._build_audio_group()],
            "SD card": [self._build_sd_group()],
            "Log": [self._build_log_group()],
        }
        for title in TABS:
            index = self.control_tabs.addTab(
                self._make_page(title, pages[title], grow_last=title == "Log"), title)
            self.control_tabs.setTabToolTip(index, TAB_TOOLTIPS[title])
        self.control_tabs.tabBar().setExpanding(False)
        self.sim_group = self._build_sim_group()
        self.sim_page = self._make_page(SIM_TAB, [self.sim_group])
        controls_layout.addWidget(self.control_tabs, 1)
        self.save_report = QPushButton("Save diagnostic report …")
        self.save_report.clicked.connect(self._save_report)
        controls_layout.addWidget(self.save_report)
        split.addWidget(controls)
        split.setStretchFactor(0, 1)
        split.setSizes([1040, 520])
        outer.addWidget(split, 1)
        self.setCentralWidget(central)

        self.board.buttonChanged.connect(self._sim_button)
        self.board.potChanged.connect(self._sim_pot)
        self.board.sw17Changed.connect(lambda pos: self._sim_device() and self._sim_device().set_sw17(pos))
        self.board.sw18Changed.connect(lambda pos: self._sim_device() and self._sim_device().set_sw18(pos))
        self.board.sdToggled.connect(self._sim_toggle_sd)
        self.board.jackToggled.connect(self._sim_toggle_jack)
        self.board.gateToggled.connect(self._sim_toggle_gate)
        self.board.cvChanged.connect(
            lambda i, v: self._sim_device() and self._sim_device().set_cv(i, v))

    def _make_page(self, title: str, groups: list[QWidget], grow_last: bool = False) -> QScrollArea:
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        panel = QWidget()
        panel.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Preferred)
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(6, 0, 6, 8)
        layout.setSpacing(4)
        for k, group in enumerate(groups):
            layout.addWidget(group, 1 if grow_last and k == len(groups) - 1 else 0)
        if not grow_last:
            layout.addStretch(1)
        scroll.setWidget(panel)
        self.control_pages[title] = scroll
        return scroll

    def _build_header(self) -> QWidget:
        header = QWidget()
        header.setObjectName("header")
        header.setAttribute(Qt.WA_StyledBackground, True)
        row = QHBoxLayout(header)
        row.setContentsMargins(16, 8, 16, 8)
        title = QLabel("Daisy Seed3 – Hardware Diagnostic Suite")
        title.setObjectName("appTitle")
        version = QLabel(f"v{__version__}")
        version.setObjectName("appVersion")
        row.addWidget(title)
        row.addSpacing(10)
        row.addWidget(version)
        row.addStretch(1)
        self.mode_hw = QPushButton("Hardware (USB)")
        self.mode_sim = QPushButton("Simulation")
        for b in (self.mode_hw, self.mode_sim):
            b.setCheckable(True)
            b.setObjectName("segment")
            row.addWidget(b)
        self.mode_group = QButtonGroup(self)
        self.mode_group.addButton(self.mode_hw)
        self.mode_group.addButton(self.mode_sim)
        self.mode_hw.clicked.connect(lambda: self._start_session(False))
        self.mode_sim.clicked.connect(lambda: self._start_session(True))
        row.addStretch(1)
        self.status_dot = QLabel()
        self.status_dot.setFixedSize(10, 10)
        row.addWidget(self.status_dot)
        row.addSpacing(6)
        self.status_pill = QLabel('Disconnected')
        self.status_pill.setObjectName("statusText")
        row.addWidget(self.status_pill)
        row.addSpacing(14)
        self.connect_button = QPushButton('Disconnect')
        self.connect_button.clicked.connect(self._toggle_pause)
        row.addWidget(self.connect_button)
        return header

    def _build_connection_group(self) -> QGroupBox:
        box = QGroupBox('USB connection')
        layout = QVBoxLayout(box)
        self.status_detail = _hint("")
        layout.addWidget(self.status_detail)

        port_row = QHBoxLayout()
        port_row.addWidget(QLabel("Port:"))
        self.port_combo = QComboBox()
        self.port_combo.addItem('Automatic (VID 0483 / PID 5740)', None)
        self.port_combo.currentIndexChanged.connect(self._port_selected)
        port_row.addWidget(self.port_combo, 1)
        layout.addLayout(port_row)

        form = QFormLayout()
        form.setLabelAlignment(Qt.AlignRight)
        form.setRowWrapPolicy(QFormLayout.WrapLongRows)
        self.info = {}
        for key, label in (("port", "Port"), ("fw", "Firmware"), ("proto", 'Protocol'),
                           ("boot", 'Application / bootloader'), ("rate", 'State rate'),
                           ("frames", 'Frames OK / errors'), ("lost", 'Lost frames'),
                           ("rtt", 'Round-trip time (PING)'), ("cb", 'Control update rate'),
                           ("uptime", 'Firmware uptime'), ("fwstats", "Firmware TX / RX"),
                           ("reconnects", 'Reconnections')):
            self.info[key] = _value_label()
            form.addRow(label + ":", self.info[key])
        layout.addLayout(form)
        rate_row = QHBoxLayout()
        rate_row.addWidget(QLabel('Stream rate:'))
        self.rate_combo = QComboBox()
        for hz in (20, 50, 100):
            self.rate_combo.addItem(f"{hz} Hz", hz)
        self.rate_combo.setCurrentIndex(1)
        self.rate_combo.activated.connect(
            lambda _i: self.session and self.session.send(p.cmd_rate(self.rate_combo.currentData())))
        rate_row.addWidget(self.rate_combo)
        rate_row.addStretch(1)
        layout.addLayout(rate_row)
        self.boot_button = QPushButton("Firmware update mode …")
        self.boot_button.setToolTip("Restarts the kit into the Daisy bootloader without BOOT/RESET. "
                                    "Then flash with the Daisy Web Programmer or make program-dfu.")
        self.boot_button.clicked.connect(self._enter_bootloader)
        layout.addWidget(self.boot_button)
        return box

    def _build_sim_group(self) -> QGroupBox:
        box = QGroupBox("Simulated kit")
        layout = QVBoxLayout(box)
        layout.addWidget(_hint('Click the board to press buttons, drag knobs or turn them with the '
                               'mouse wheel, select toggle positions, and toggle the microSD card '
                               'or audio input plugs. Nothing here reaches real hardware.'))
        grid = QGridLayout()
        self.sim_plug = QPushButton('Unplug USB cable')
        self.sim_plug.clicked.connect(self._sim_toggle_cable)
        self.sim_freeze = QPushButton('Freeze firmware')
        self.sim_freeze.setCheckable(True)
        self.sim_freeze.toggled.connect(self._sim_set_freeze)
        self.sim_noise = QPushButton('Send corrupt frame')
        self.sim_noise.clicked.connect(self._sim_corrupt)
        grid.addWidget(self.sim_plug, 0, 0)
        grid.addWidget(self.sim_freeze, 0, 1)
        grid.addWidget(self.sim_noise, 1, 0)
        layout.addLayout(grid)
        self.sim_sd_card = QCheckBox('microSD card inserted')
        self.sim_sd_card.toggled.connect(self._sim_set_sd)
        self.sim_sd_fs = QCheckBox('Card has a readable FAT32 filesystem')
        self.sim_sd_fs.setChecked(True)
        self.sim_sd_fs.toggled.connect(
            lambda on: self._sim_device() and setattr(self._sim_device(), "sd_filesystem",
                                                      "FAT32" if on else None))
        self.sim_jl = QCheckBox('Left audio input plugged in')
        self.sim_jl.toggled.connect(lambda on: self._sim_set_jack("left", on))
        self.sim_jr = QCheckBox('Right audio input plugged in')
        self.sim_jr.toggled.connect(lambda on: self._sim_set_jack("right", on))
        self.sim_cv_patch = QCheckBox('Patch cables CV OUT 1/2 → CV IN 1/2')
        self.sim_cv_patch.setToolTip('Simulation assumption: DAC code 0–4095 appears as '
                                     'CV IN 0.0–+1.0. The real scaling is not measured.')
        self.sim_cv_patch.toggled.connect(
            lambda on: self._sim_device() and setattr(self._sim_device(), "cv_patched", on))
        self.sim_audio_patch = QCheckBox('Patch cables AUDIO OUT → AUDIO IN (test tone loopback)')
        self.sim_audio_patch.toggled.connect(
            lambda on: self._sim_device() and setattr(self._sim_device(), "audio_patched", on))
        self.sim_audio_noise = QCheckBox('Noise on the audio inputs (−10 dBFS)')
        self.sim_audio_noise.toggled.connect(
            lambda on: self._sim_device() and setattr(self._sim_device(), "input_noise",
                                                      0.316 if on else 0.0))
        for w in (self.sim_sd_card, self.sim_sd_fs, self.sim_jl, self.sim_jr, self.sim_cv_patch,
                  self.sim_audio_patch, self.sim_audio_noise):
            layout.addWidget(w)
        self.sim_reader_eject = QPushButton("Simulate Windows eject")
        self.sim_reader_eject.clicked.connect(
            lambda: self._sim_device() and self._sim_device().reader_eject())
        layout.addWidget(self.sim_reader_eject)
        self.sim_reader_reset = QPushButton("Simulate USB bus reset")
        self.sim_reader_reset.clicked.connect(
            lambda: self._sim_device() and self._sim_device().reader_bus_reset())
        layout.addWidget(self.sim_reader_reset)
        return box

    def _build_led_group(self) -> QGroupBox:
        box = QGroupBox('LEDs D1–D16')
        layout = QVBoxLayout(box)
        layout.addWidget(_hint('Default: each LED lights while its button is held '
                               '(firmware mode Buttons → LEDs). The test patterns below '
                               'override that until Buttons → LEDs is selected again. '
                               '"VR1 dims" lights all LEDs and takes the brightness from VR1 '
                               '(hands-on pot and PWM check, firmware 0.4.0).'))
        row = QHBoxLayout()
        self.led_buttons: dict[str, QPushButton] = {}
        self.led_group = QButtonGroup(self)
        for mode, text in LED_MODE_BUTTONS:
            b = QPushButton(text)
            b.setCheckable(True)
            b.setObjectName("segment")
            b.clicked.connect(lambda _c=False, m=mode: self._send_led(m))
            self.led_group.addButton(b)
            self.led_buttons[mode] = b
            row.addWidget(b)
        layout.addLayout(row)
        sliders = QFormLayout()
        self.br_slider = QSlider(Qt.Horizontal)
        self.br_slider.setRange(0, 100)
        self.br_slider.setValue(100)
        self.br_slider.sliderReleased.connect(lambda: self._send_led(self._current_led_mode()))
        self.br_label = QLabel("100 %")
        self.br_slider.valueChanged.connect(lambda v: self.br_label.setText(f"{v} %"))
        br_row = QHBoxLayout()
        br_row.addWidget(self.br_slider, 1)
        br_row.addWidget(self.br_label)
        sliders.addRow('Brightness:', br_row)
        self.chase_slider = QSlider(Qt.Horizontal)
        self.chase_slider.setRange(20, 1000)
        self.chase_slider.setValue(120)
        self.chase_label = QLabel("120 ms")
        self.chase_slider.valueChanged.connect(lambda v: self.chase_label.setText(f"{v} ms"))
        self.chase_slider.sliderReleased.connect(lambda: self._send_led(self._current_led_mode()))
        ch_row = QHBoxLayout()
        ch_row.addWidget(self.chase_slider, 1)
        ch_row.addWidget(self.chase_label)
        sliders.addRow('Chase step:', ch_row)
        layout.addLayout(sliders)
        layout.addWidget(_hint('The board view shows the <b>commanded</b> LED state. LEDs have no '
                               'electrical feedback; confirm on the physical board.'))
        return box

    def _build_sd_group(self) -> QGroupBox:
        box = QGroupBox("microSD")
        layout = QVBoxLayout(box)
        self.sd_status = QLabel('No data')
        self.sd_status.setStyleSheet("font-size: 12pt; font-weight: 700;")
        layout.addWidget(self.sd_status)
        self.sd_detail = _hint("")
        layout.addWidget(self.sd_detail)
        self.sd_button = QPushButton('Start read test (read-only)')
        self.sd_button.clicked.connect(self._sd_test)
        layout.addWidget(self.sd_button)
        layout.addWidget(_hint('The test mounts the filesystem and reads the root directory '
                               'without writing or formatting. The firmware blocks during the '
                               'test (5 s read budget plus initialization; the host allows up to '
                               '20 s), so streaming pauses and an active CV OUT signal is switched '
                               'Off first.'))
        self.reader_status = QLabel("Card reader: checking firmware capability …")
        self.reader_status.setWordWrap(True)
        layout.addWidget(self.reader_status)
        self.reader_share = QPushButton("Share SD card with Windows")
        self.reader_share.clicked.connect(self._share_reader)
        layout.addWidget(self.reader_share)
        self.reader_return = QPushButton("Return card to diagnostics")
        self.reader_return.clicked.connect(self._return_reader)
        layout.addWidget(self.reader_return)
        return box

    def _build_audio_group(self) -> QGroupBox:
        box = QGroupBox('Audio (firmware 0.4.0)')
        layout = QVBoxLayout(box)
        self.audio_status = _hint("")
        layout.addWidget(self.audio_status)
        self.level_bars = {key: LevelBar(label) for key, label in
                           (("il", "IN L"), ("ir", "IN R"), ("ol", "OUT L"), ("or", "OUT R"))}
        for bar in self.level_bars.values():
            layout.addWidget(bar)
        layout.addWidget(_hint('Peak levels per 100 ms in dBFS (digital full scale), not volts. '
                               'OUT is the commanded test tone, IN what the codec receives.'))

        tone_label = QLabel('Test tone')
        tone_label.setObjectName("sectionLabel")
        layout.addWidget(tone_label)
        self.tone_status = _value_label()
        layout.addWidget(self.tone_status)
        row = QHBoxLayout()
        self.tone_group = QButtonGroup(self)
        self.tone_buttons: dict[str, QPushButton] = {}
        for mode, text in (("OFF", "Off"), ("SINE", "Sine")):
            b = QPushButton(text)
            b.setCheckable(True)
            b.setObjectName("segment")
            b.clicked.connect(lambda _c=False, m=mode: self._send_tone(m))
            self.tone_group.addButton(b)
            self.tone_buttons[mode] = b
            row.addWidget(b)
        layout.addLayout(row)
        form = QFormLayout()
        self.tone_freq = QSpinBox()
        self.tone_freq.setRange(p.TONE_MIN_HZ, p.TONE_MAX_HZ)
        self.tone_freq.setValue(1000)
        self.tone_freq.setSuffix(" Hz")
        form.addRow("Frequency:", self.tone_freq)
        self.tone_level = QComboBox()
        for db in TONE_LEVELS_DBFS:
            self.tone_level.addItem(f"{db} dBFS", _amplitude_for_dbfs(db))
        self.tone_level.setCurrentIndex(TONE_LEVELS_DBFS.index(-20))
        form.addRow("Level:", self.tone_level)
        self.tone_channels = QComboBox()
        for ch, text in (("LR", "Left + right"), ("L", "Left only"), ("R", "Right only")):
            self.tone_channels.addItem(text, ch)
        form.addRow("Channels:", self.tone_channels)
        layout.addLayout(form)
        self.tone_apply = QPushButton('Apply tone settings')
        self.tone_apply.clicked.connect(lambda: self._send_tone(self._current_tone_mode()))
        layout.addWidget(self.tone_apply)
        layout.addWidget(_hint('Sine on AUDIO OUT, capped at −6 dBFS by the firmware. It stops '
                               'with the session (BYE, timeout, closed app), before a blocking '
                               'SD test, and whenever the firmware main loop stalls (1 s '
                               'watchdog in the audio callback). Check the input range of '
                               'connected equipment first.'))

        scope_label = QLabel('Input snapshot (oscilloscope)')
        scope_label.setObjectName("sectionLabel")
        layout.addWidget(scope_label)
        self.scope_widget = ScopeWidget()
        layout.addWidget(self.scope_widget)
        self.scope_summary = _value_label("No snapshot yet.")
        layout.addWidget(self.scope_summary)
        scope_row = QHBoxLayout()
        self.scope_frames = QComboBox()
        for n in SCOPE_FRAME_CHOICES:
            self.scope_frames.addItem(f"{n} frames ({1000.0 * n / p.AUDIO_SAMPLE_RATE:.1f} ms)", n)
        self.scope_frames.setCurrentIndex(SCOPE_FRAME_CHOICES.index(1024))
        scope_row.addWidget(self.scope_frames, 1)
        self.scope_trigger = QCheckBox("Trigger on rising edge")
        self.scope_trigger.setChecked(True)
        scope_row.addWidget(self.scope_trigger)
        layout.addLayout(scope_row)
        self.scope_button = QPushButton('Capture snapshot')
        self.scope_button.clicked.connect(self._capture_scope)
        layout.addWidget(self.scope_button)
        layout.addWidget(_hint('One window of 5–43 ms from the audio inputs, transferred as '
                               'frames; frequency from rising zero crossings, peak in dBFS. '
                               'With a cable from AUDIO OUT to AUDIO IN and the tone on, this '
                               'is the audio loopback check.'))
        return box

    def _build_log_group(self) -> QGroupBox:
        box = QGroupBox('Event log')
        layout = QVBoxLayout(box)
        layout.addWidget(self.log, 1)
        return box

    def _save_report(self) -> None:
        if self.session is None:
            return
        name, _ = QFileDialog.getSaveFileName(self, "Save diagnostic report",
                                              "DaisyDiagnostic.json", "JSON (*.json)")
        if not name:
            return
        report = self.session.snapshot()
        report["captured_at"] = datetime.now().astimezone().isoformat()
        report["app_version"] = __version__
        try:
            Path(name).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        except OSError as exc:
            QMessageBox.warning(self, "Report could not be saved", str(exc))

    def _build_inputs_group(self) -> QGroupBox:
        box = QGroupBox('Toggle switches and plug detection')
        form = QFormLayout(box)
        self.in_labels = {}
        for key, label in (("sw17", 'SW17 (2 positions)'), ("sw18", 'SW18 (3 positions)'),
                           ("jl", 'Left audio input'), ("jr", 'Right audio input'),
                           ("sd", 'microSD detection')):
            self.in_labels[key] = _value_label()
            form.addRow(label + ":", self.in_labels[key])
        return box

    def _build_pot_group(self) -> QGroupBox:
        box = QGroupBox('Potentiometers VR1–VR8')
        layout = QVBoxLayout(box)
        self.pot_table = _value_table([f"VR{i + 1}" for i in range(p.NUM_POTS)])
        layout.addWidget(self.pot_table)
        reset = QPushButton('Reset min/max (pots and CV)')
        reset.clicked.connect(self._reset_minmax)
        layout.addWidget(reset)
        return box

    def _build_cvgate_group(self) -> QGroupBox:
        box = QGroupBox('CV / Gate')
        layout = QVBoxLayout(box)
        self.cvgate_status = _hint("")
        layout.addWidget(self.cvgate_status)
        self.cv_table = _value_table([f"CV IN {i + 1}" for i in range(p.NUM_CV)])
        layout.addWidget(self.cv_table)
        gates = QHBoxLayout()
        self.gate_labels: list[QLabel] = []
        for i in range(p.NUM_GATES):
            label = QLabel(f"GATE {i + 1}: –")
            label.setWordWrap(True)
            label.setObjectName("valueLabel")
            gates.addWidget(label)
            self.gate_labels.append(label)
        layout.addLayout(gates)
        self.cv_trace = CvTraceWidget()
        layout.addWidget(self.cv_trace)
        layout.addWidget(_hint('Rolling trace of the last 10 s of received frames (50–100 Hz, '
                               'ADC scan 6–13 ms): CV IN 1/2 solid, CV OUT 1 echo dashed, gates '
                               'as bands. Min/Max cover every received frame since the last '
                               'reset (Pots → Reset min/max). Gate edges count rising edges '
                               'modulo 256.'))

        out_label = QLabel('CV OUT test signal')
        out_label.setObjectName("sectionLabel")
        layout.addWidget(out_label)
        self.cv_out_status = _value_label()
        layout.addWidget(self.cv_out_status)
        row = QHBoxLayout()
        self.cv_mode_group = QButtonGroup(self)
        self.cv_mode_buttons: dict[str, QPushButton] = {}
        for mode, text in CV_OUT_BUTTONS:
            b = QPushButton(text)
            b.setCheckable(True)
            b.setObjectName("segment")
            b.clicked.connect(lambda _c=False, m=mode: self._send_cvout(m))
            self.cv_mode_group.addButton(b)
            self.cv_mode_buttons[mode] = b
            row.addWidget(b)
        layout.addLayout(row)
        form = QFormLayout()
        self.cv_code_spins: list[QSpinBox] = []
        for i in range(p.NUM_CV):
            spin = QSpinBox()
            spin.setRange(0, p.DAC_MAX)
            spin.setValue(0)
            spin.setSuffix(" / 4095")
            spin.setToolTip('DAC code for the fixed mode')
            form.addRow(f"CV OUT {i + 1} code:", spin)
            self.cv_code_spins.append(spin)
        self.cv_period_spin = QSpinBox()
        self.cv_period_spin.setRange(50, 10000)
        self.cv_period_spin.setSingleStep(50)
        self.cv_period_spin.setValue(1000)
        self.cv_period_spin.setSuffix(" ms")
        form.addRow('Ramp/saw period:', self.cv_period_spin)
        layout.addLayout(form)
        self.cv_apply = QPushButton('Apply CV OUT settings')
        self.cv_apply.clicked.connect(lambda: self._send_cvout(self._current_cv_mode()))
        layout.addWidget(self.cv_apply)
        layout.addWidget(_hint('CV IN is normalized −1…+1; CV OUT uses DAC codes 0–4095. '
                               'Volts are not displayed because the analog scaling has not been '
                               'measured. Ramp is triangular; output 2 is inverse in Ramp/Saw.'))
        return box

    def _build_counter_group(self) -> QGroupBox:
        box = QGroupBox('Buttons S1–S16')
        layout = QVBoxLayout(box)
        layout.addWidget(_hint('Press counts since power-up (modulo 256); a held button is '
                               'highlighted and lights its LED on the board.'))
        grid = QGridLayout()
        self.counter_labels: list[QLabel] = []
        for i in range(p.NUM_BUTTONS):
            label = QLabel(f"S{i + 1}: –")
            label.setAlignment(Qt.AlignCenter)
            label.setMinimumWidth(42)
            grid.addWidget(label, i // 4, i % 4)
            self.counter_labels.append(label)
        layout.addLayout(grid)
        return box

    # ---- session handling ----------------------------------------------------------------
    def _start_session(self, simulation: bool) -> None:
        if self.session is not None:
            self.session.disconnect('Mode changed')
        self.paused = False
        self.connect_button.setText('Disconnect')
        self.sd_result = None
        self._reset_minmax()
        if simulation:
            self.cable = SimulatedCable()
            cable = self.cable
            self.session = DeviceSession(cable.ports, lambda port: cable.open(port, self._clock),
                                         self._clock, simulated=True)
            self._sync_sim_controls()
        else:
            self.cable = None
            self.session = DeviceSession(list_serial_ports, SerialLink, self._clock,
                                         port_override=self._port_override)
        self.mode_sim.setChecked(simulation)
        self.mode_hw.setChecked(not simulation)
        self.sim_banner.setVisible(simulation)
        sim_index = self.control_tabs.indexOf(self.sim_page)
        if simulation and sim_index < 0:
            sim_index = self.control_tabs.addTab(self.sim_page, SIM_TAB)
            self.control_tabs.setTabToolTip(sim_index, "Simulated kit: cable, card, plugs, faults")
        elif not simulation and sim_index >= 0:
            self.control_tabs.removeTab(sim_index)
        self.port_combo.setEnabled(not simulation)
        self.board.interactive = simulation
        self.board.simulated = simulation
        self.setWindowTitle(f"Daisy Seed3 Hardware Diagnostics {__version__}"
                            + (" – SIMULATION" if simulation else ""))
        self._log('Mode: ' + ('Simulation (no hardware)' if simulation
                               else 'Hardware over USB CDC'))
        self._refresh_ports()
        self._update_status()

    def _toggle_pause(self) -> None:
        if self.session is None:
            return
        if self.paused:
            self.paused = False
            self.connect_button.setText('Disconnect')
            self._log('Automatic connection resumed')
        else:
            self.session.disconnect()
            self.paused = True
            self.connect_button.setText('Connect')
        self._update_status()

    def _refresh_ports(self) -> None:
        if self.cable is not None or self.port_combo.view().isVisible():
            return
        try:
            ports = list_serial_ports()
        except Exception:
            return
        current = self.port_combo.currentData()
        wanted = [None] + [port.device for port in ports]
        existing = [self.port_combo.itemData(i) for i in range(self.port_combo.count())]
        if wanted == existing:
            return
        self.port_combo.blockSignals(True)
        self.port_combo.clear()
        self.port_combo.addItem('Automatic (VID 0483 / PID 5740)', None)
        for port in ports:
            mark = " ★" if port.is_daisy_candidate else ""
            self.port_combo.addItem(f"{port.device} – {port.description}{mark}", port.device)
        if current is not None and self.port_combo.findData(current) < 0:
            # Keep the user's fixed choice visible until the port returns, instead of
            # silently showing "Automatic" while the session still waits for it.
            self.port_combo.addItem(f"{current} – not present", current)
        self.port_combo.setCurrentIndex(max(0, self.port_combo.findData(current)))
        self.port_combo.blockSignals(False)

    def _port_selected(self, _index: int) -> None:
        self._port_override = self.port_combo.currentData()
        if self.cable is None:
            self._start_session(False)

    def _tick(self) -> None:
        if self.session is None:
            return
        if not self.paused:
            self.session.poll()
        board_changed = False
        for kind, payload in self.session.drain_events():
            if kind == "board":
                board_changed = True
                self._track_extremes(payload)
            elif kind == "state":
                self._update_status()
            elif kind == "connected":
                info: p.DeviceInfo = payload
                self.sd_result = None
                self._reset_minmax()
                self._log(f"Connected to {self.session.port}: {info.app} {info.firmware}, "
                          f"Protocol {info.protocol}, Bootloader {info.bootloader}"
                          + (" (SIMULATION)" if info.simulated else ""))
            elif kind == "disconnected":
                board_changed = True
                self.sd_result = None
            elif kind == "sd":
                self.sd_result = payload
                if payload is not None:
                    self._log(f"microSD test: {self._sd_text()[0]}")
            elif kind == "reader":
                self.sd_result = None
                if payload is not None:
                    self._log(f"Card reader: {payload.state}")
            elif kind == "audio":
                self._apply_audio(payload)
            elif kind == "scope":
                self.scope_widget.set_capture(payload)
                self.scope_summary.setText(self.scope_widget.summary())
                self._log(f"Audio snapshot: {self.scope_widget.summary()}")
            elif kind == "bootloader":
                # ACK c=BOOT confirms the accepted request, not the DFU entry itself.
                self._log("Request accepted: the kit restarts into the Daisy bootloader. Once it "
                          "appears as DFU device, flash with the Daisy Web Programmer or make program-dfu.")
            elif kind == "log":
                self._log(str(payload))
        if board_changed:
            self._apply_board()

    def _track_extremes(self, st: p.BoardState) -> None:
        """Min/Max over every received frame, not only the one visible at refresh time."""
        for i, v in enumerate(st.pots):
            lo, hi = self.board.pot_min[i], self.board.pot_max[i]
            self.board.pot_min[i] = v if lo is None else min(lo, v)
            self.board.pot_max[i] = v if hi is None else max(hi, v)
        if st.cvgate is not None:
            for i, v in enumerate(st.cvgate.cv):
                lo, hi = self.cv_min[i], self.cv_max[i]
                self.cv_min[i] = v if lo is None else min(lo, v)
                self.cv_max[i] = v if hi is None else max(hi, v)
            self.cv_trace.push(st.time_ms, st.cvgate.cv, st.cvgate.gates, st.cvgate.cv_out)

    def _apply_audio(self, levels: p.AudioLevels) -> None:
        for key, value in zip(("il", "ir", "ol", "or"), (*levels.inputs, *levels.outputs)):
            self.level_bars[key].set_level(value)
        amplitude = levels.tone_amplitude / 10000.0
        db = "−∞" if amplitude <= 0 else f"{20 * math.log10(amplitude):+.1f}"
        self.tone_status.setText(
            "Firmware tone: off" if levels.tone_mode == "OFF" else
            f"Firmware tone: {levels.tone_mode} {levels.tone_freq_hz} Hz, {db} dBFS, "
            f"{levels.tone_channels}")
        button = self.tone_buttons.get(levels.tone_mode)
        if button is not None and not button.isChecked():
            button.setChecked(True)

    def _apply_board(self) -> None:
        st = self.session.board if self.session else None
        if st is not None:
            if self._last_sd_card and not st.sd_card:
                self.sd_result = None  # card removed: an old test result is no longer valid
            self._last_sd_card = st.sd_card
        self.board.state = st
        self.board.connected = self.session is not None and self.session.connected
        text, color = self._sd_text()
        self.board.sd_display = SdDisplay(text, color)
        self.board.update()

    def _sd_text(self) -> tuple[str, QColor]:
        st = self.session.board if self.session else None
        if st is None:
            return 'No data', theme.MUTED
        if not st.sd_card:
            return 'No card', theme.MUTED
        reader = self.session.reader if self.session.reader_supported else None
        if reader and reader.state != "LOCAL":
            return {
                "STARTING": ("Preparing card reader …", theme.TEXT),
                "SHARED": ("Shared with Windows", theme.WARN),
                "EJECTED": ("Ejected by Windows", theme.OK),
                "INTERRUPTED": ("Card access interrupted", theme.ERROR),
                "ERROR": ("Card I/O error", theme.ERROR),
            }[reader.state]
        if self.session.sd_test_pending:
            return 'Read test in progress …', theme.TEXT
        r = self.sd_result
        if r is None:
            return 'Card detected', theme.TEXT
        if r.status == "MOUNTED":
            size = f"{r.size_mb / 1024:.1f} GB" if r.size_mb >= 1024 else f"{r.size_mb} MB"
            return f"Filesystem readable ({r.fs_type}, {size}, {r.entries} entries)", theme.OK
        if r.status == "NONE":
            return 'No card (test)', theme.MUTED
        return f"Error: {p.FRESULT_TEXT.get(r.fresult, f'FRESULT {r.fresult}')}", theme.ERROR

    # ---- periodic panel refresh ----------------------------------------------------------
    def _update_status(self) -> None:
        s = self.session
        if s is None:
            return
        state = s.state
        text = 'Paused' if self.paused else STATE_TEXT[state]
        if s.simulated and state == ConnState.CONNECTED:
            text += " (Simulation)"
        color = theme.FAINT if self.paused else STATE_COLORS[state]
        self.status_pill.setText(text)
        self.status_dot.setStyleSheet(f"background: {color.name()}; border-radius: 5px;")
        self.status_detail.setText('Disconnected manually' if self.paused
                                   else s.status_detail)

    def _refresh_panels(self) -> None:
        s = self.session
        if s is None:
            return
        self._update_status()
        dev = s.device if s.connected else None
        self.info["port"].setText(s.port if s.connected and s.port else "–")
        self.info["fw"].setText(f"{dev.app} {dev.firmware}" if dev else "–")
        self.info["proto"].setText(f"DDP v{dev.protocol} (Host: v{p.PROTOCOL_VERSION})" if dev else
                                   f"– (Host: v{p.PROTOCOL_VERSION})")
        self.info["boot"].setText(f"{dev.boot} / {dev.bootloader}" if dev else "–")
        self.info["rate"].setText(f"{s.state_rate_hz:.0f} Hz" if s.connected else "–")
        self.info["frames"].setText(f"{s.frames_ok} / {s.frame_errors + s.content_errors}")
        self.info["lost"].setText(str(s.lost_frames))
        self.info["rtt"].setText(f"{s.rtt_ms:.0f} ms" if s.rtt_ms is not None and s.connected else "–")
        sysinfo = s.sys if s.connected else None
        self.info["cb"].setText(f"{sysinfo.control_rate_hz} Hz (target 1000)" if sysinfo else "–")
        self.info["uptime"].setText(f"{sysinfo.uptime_s} s" if sysinfo else "–")
        self.info["fwstats"].setText(
            f"TX {sysinfo.tx_ok} (busy {sysinfo.tx_busy}, dropped {sysinfo.tx_drop}) / "
            f"RX {sysinfo.rx_ok} (errors {sysinfo.rx_bad})" if sysinfo else "–")
        self.info["reconnects"].setText(str(s.reconnects))
        # The combo shows the firmware's actual stream rate, not the last request.
        reported_rate = getattr(sysinfo, "rate_hz", None)
        if reported_rate is not None and not self.rate_combo.view().isVisible():
            index = self.rate_combo.findData(reported_rate)
            if index >= 0 and index != self.rate_combo.currentIndex():
                self.rate_combo.blockSignals(True)
                self.rate_combo.setCurrentIndex(index)
                self.rate_combo.blockSignals(False)

        st = s.board if s.connected else None
        enabled = st is not None
        for b in self.led_buttons.values():
            b.setEnabled(enabled)
        self.br_slider.setEnabled(enabled)
        self.chase_slider.setEnabled(enabled)
        reader = s.reader if s.reader_supported else None
        local = not s.reader_supported or (reader is not None and reader.state == "LOCAL")
        self.sd_button.setEnabled(enabled and st.sd_card and not s.sd_test_pending
                                  and not s.reader_pending and local)
        self.boot_button.setEnabled(bool(enabled and s.bootloader_supported and local
                                         and not s.sd_test_pending and not s.reader_pending))
        self.reader_share.setEnabled(bool(enabled and st.sd_card and reader and
                                          reader.state == "LOCAL" and not reader.busy
                                          and not s.sd_test_pending and not s.reader_pending))
        self.reader_return.setEnabled(bool(enabled and reader and not reader.busy and
                                           reader.state in ("EJECTED", "INTERRUPTED", "ERROR")
                                           and not s.reader_pending))
        self.reader_return.setText("Acknowledge interrupted access" if reader and
                                  reader.state in ("INTERRUPTED", "ERROR") else
                                  "Return card to diagnostics")
        if not s.connected:
            reader_text = "Card reader: disconnected; card ownership is unknown."
        elif not s.reader_supported:
            reader_text = "Card reader unavailable: install the CardReader firmware variant."
        elif s.reader_pending:
            reader_text = "Card reader: waiting for firmware …"
        elif reader is None:
            reader_text = "Card reader: state unknown; waiting for firmware status."
        else:
            reader_text = {
                "LOCAL": "Card available for diagnostics. Sharing enables Windows read/write access.",
                "STARTING": "Preparing the card for Windows …",
                "SHARED": "Windows owns the card. Eject it in Windows before returning it here.",
                "EJECTED": "Windows eject received and card writes completed. You can return the card.",
                "INTERRUPTED": "Access was interrupted. Windows may have lost pending changes. Close card files before acknowledging.",
                "ERROR": "Card I/O failed. Windows may have lost pending changes. Close card files before acknowledging.",
            }[reader.state]
        if s.simulated:
            reader_text += " Simulation only: no Windows drive is created."
        if reader and reader.diagnostic_text and reader.io != "NONE":
            reader_text += "\n" + reader.diagnostic_text
        self.reader_status.setText(reader_text)
        self.rate_combo.setEnabled(enabled)
        if st is not None:
            btn = self.led_buttons.get(st.led_mode)
            if btn is not None and not btn.isChecked():
                btn.setChecked(True)
            elif btn is None:  # MASK was set by another host: no button represents it
                self._uncheck_all(self.led_group, self.led_buttons.values())
            if not self.br_slider.isSliderDown():
                self.br_slider.setValue(st.led_brightness)
        else:
            self._uncheck_all(self.led_group, self.led_buttons.values())

        text, color = self._sd_text()
        self.sd_status.setText(text)
        self.sd_status.setStyleSheet(f"font-size: 12pt; font-weight: 700; color: {color.name()};")
        self.sd_detail.setText(
            "" if st is None else
            ('SD read test is unavailable until the card is returned.' if st.sd_card and not local else
             'Card detected on D7 (SDMMC_DETECT). Read test available.' if st.sd_card and
             self.sd_result is None else
             'The slot reports no card.' if not st.sd_card else ""))

        def show(key: str, value: str, active: bool = False, ok: bool = False) -> None:
            self.in_labels[key].setText(value)
            color = theme.OK if ok else theme.TEXT
            self.in_labels[key].setStyleSheet(f"color: {color.name()}; font-weight: 700;"
                                              if active else f"color: {theme.MUTED.name()};")

        if st is None:
            for key in self.in_labels:
                show(key, "–")
        else:
            show("sw17", {"U": 'U (up according to libDaisy)', "D": 'D (down according to libDaisy)'}[st.sw17],
                 st.sw17 == "U")
            show("sw18", {"U": 'U (up)', "C": 'C (center / OFF)', "D": 'D (down)'}[st.sw18],
                 st.sw18 != "C")
            show("jl", 'Plug detected' if st.jack_left else 'unplugged', st.jack_left, ok=True)
            show("jr", 'Plug detected' if st.jack_right else 'unplugged', st.jack_right, ok=True)
            show("sd", 'Card detected' if st.sd_card else 'no card', st.sd_card, ok=True)

        for i in range(p.NUM_POTS):
            lo, hi = self.board.pot_min[i], self.board.pot_max[i]
            cells = ("–", "–", "–", "–") if st is None else (
                f"{st.pots[i] * 100:.1f} %", str(st.pots_raw[i]),
                "–" if lo is None else f"{lo * 100:.1f} %",
                "–" if hi is None else f"{hi * 100:.1f} %")
            for c, txt in enumerate(cells, start=1):
                self.pot_table.item(i, c).setText(txt)
        self._refresh_cvgate(st)
        for i, label in enumerate(self.counter_labels):
            if st is None:
                label.setText(f"S{i + 1}: –")
                label.setStyleSheet("")
            else:
                label.setText(f"S{i + 1}: {st.press_counts[i]}")
                label.setStyleSheet(f"color: {theme.TEXT.name()}; font-weight: 700;"
                                    f"background: #3a3d41;" if st.buttons[i] else "")

        audio = s.audio_supported and st is not None
        for w in (*self.tone_buttons.values(), self.tone_freq, self.tone_level,
                  self.tone_channels, self.tone_apply, self.scope_frames, self.scope_trigger):
            w.setEnabled(audio)
        self.scope_button.setEnabled(audio and not s.scope_pending)
        if not s.connected:
            self.audio_status.setText("")
            for bar in self.level_bars.values():
                bar.set_level(0.0, active=False)
            self._uncheck_all(self.tone_group, self.tone_buttons.values())
            self.tone_status.setText("–")
        elif not s.audio_supported:
            self.audio_status.setText('The connected firmware has no audio extension '
                                      '(firmware 0.4.0 or later required).')
            for bar in self.level_bars.values():
                bar.set_level(0.0, active=False)
        else:
            self.audio_status.setText("Live peak meters, test tone and input snapshot.")

        if self.cable is not None:
            self.sim_plug.setText('Plug in USB cable' if not self.cable.plugged
                                  else 'Unplug USB cable')
            self._sync_sim_controls()
        if not s.connected and self.board.state is not None:
            self._apply_board()
        elif self.board.flash_pending():
            self.board.update()  # let a gate-ring flash decay even without new frames

    @staticmethod
    def _uncheck_all(group: QButtonGroup, buttons) -> None:
        group.setExclusive(False)
        for b in buttons:
            b.setChecked(False)
        group.setExclusive(True)

    # ---- LED commands -------------------------------------------------------------------
    def _current_led_mode(self) -> str:
        st = self.session.board if self.session else None
        return st.led_mode if st and st.led_mode in self.led_buttons else "FOLLOW"

    def _send_led(self, mode: str) -> None:
        if self.session is None:
            return
        ok = self.session.set_led(mode, brightness=self.br_slider.value(),
                                  step_ms=self.chase_slider.value())
        if ok:
            self._log(f"LED mode requested: {mode}")

    def _enter_bootloader(self) -> None:
        s = self.session
        if s is None or not s.bootloader_supported:
            return
        answer = QMessageBox.question(self, "Firmware update mode",
            "The kit restarts into the Daisy bootloader: the diagnostic firmware stops and the "
            "USB connection ends. Then flash the new firmware with the Daisy Web Programmer or "
            "make program-dfu; the bootloader waits without a timeout. Continue?",
            QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
        if answer == QMessageBox.Yes and s.request_bootloader():
            self._log("Firmware update mode requested")

    def _sd_test(self) -> None:
        s = self.session
        if s is None:
            return
        if self._current_cv_mode() != "OFF":
            # The firmware holds the last DAC code while it blocks in the SD test and
            # cannot switch CV OUT off meanwhile; ask, then send OFF ahead of SDTEST so
            # both are executed in wire order before the test starts.
            answer = QMessageBox.question(self, "CV OUT is active",
                "The SD read test blocks the firmware, which would freeze the CV OUT test "
                "signal at its current level for the duration of the test. Switch CV OUT "
                "Off and start the read test?",
                QMessageBox.Yes | QMessageBox.No, QMessageBox.Yes)
            if answer != QMessageBox.Yes:
                return
            self._send_cvout("OFF")
        if s.request_sd_test():
            self._log('microSD read test requested')

    def _share_reader(self) -> None:
        if self.session and self.session.request_reader("SHARE"):
            self.sd_result = None
            self._log("SD card sharing requested; eject in Windows before returning the card")

    def _return_reader(self) -> None:
        s = self.session
        if s is None or s.reader is None:
            return
        if s.reader.state in ("INTERRUPTED", "ERROR"):
            answer = QMessageBox.warning(self, "Interrupted SD access",
                "Windows may have lost pending changes. Close all files and applications "
                "using this card. Acknowledging does not recover unsaved data or confirm "
                "a clean eject. Return the card to diagnostics?",
                QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
            if answer != QMessageBox.Yes:
                return
        if s.request_reader("RETURN"):
            self.sd_result = None
            self._log("SD card return requested")

    def _reset_minmax(self) -> None:
        self.board.pot_min = [None] * p.NUM_POTS
        self.board.pot_max = [None] * p.NUM_POTS
        self.cv_min = [None] * p.NUM_CV
        self.cv_max = [None] * p.NUM_CV
        if hasattr(self, "cv_trace"):
            self.cv_trace.clear()
        self.board.update()

    # ---- audio --------------------------------------------------------------------------
    def _current_tone_mode(self) -> str:
        a = self.session.audio if self.session else None
        return a.tone_mode if a is not None else "OFF"

    def _send_tone(self, mode: str) -> None:
        s = self.session
        if s is None:
            return
        if s.request_tone(mode, self.tone_freq.value(), self.tone_level.currentData(),
                          self.tone_channels.currentData()):
            self._log(f"Test tone requested: {mode} {self.tone_freq.value()} Hz "
                      f"{self.tone_level.currentText()} {self.tone_channels.currentData()}")

    def _capture_scope(self) -> None:
        s = self.session
        if s is None:
            return
        if s.request_scope(self.scope_frames.currentData(), self.scope_trigger.isChecked()):
            self._log(f"Audio snapshot requested: {self.scope_frames.currentText()}")

    # ---- CV / gate ----------------------------------------------------------------------
    def _current_cv_mode(self) -> str:
        st = self.session.board if self.session else None
        if st is not None and st.cvgate is not None:
            return st.cvgate.cv_out_mode
        return "OFF"

    def _send_cvout(self, mode: str) -> None:
        if self.session is None:
            return
        body = p.cmd_cvout(mode, self.cv_code_spins[0].value(), self.cv_code_spins[1].value(),
                           self.cv_period_spin.value())
        if self.session.send(body):
            self._log(f"CV OUT requested: {mode}")

    def _refresh_cvgate(self, st) -> None:
        cg = st.cvgate if st is not None else None
        self.cv_out_status.setText(
            f"Reported DAC codes: OUT 1 = {cg.cv_out[0]}, OUT 2 = {cg.cv_out[1]} "
            f"({cg.cv_out_mode})" if cg is not None else "No CV OUT data")
        enabled = cg is not None
        for b in self.cv_mode_buttons.values():
            b.setEnabled(enabled)
        for w in (*self.cv_code_spins, self.cv_period_spin, self.cv_apply):
            w.setEnabled(enabled)
        if st is None:
            self.cvgate_status.setText("")
        elif cg is None:
            self.cvgate_status.setText('The connected firmware reports no CV/gate data '
                                       '(firmware 0.3.0 or later required).')
        else:
            self.cvgate_status.setText("Live CV inputs and gate counters. Values are normalized, not volts.")
        self.cv_trace.update()
        for i in range(p.NUM_CV):
            lo, hi = self.cv_min[i], self.cv_max[i]
            cells = ("–", "–", "–", "–") if cg is None else (
                f"{cg.cv[i]:+.4f}", str(cg.cv_raw[i]),
                "–" if lo is None else f"{lo:+.3f}", "–" if hi is None else f"{hi:+.3f}")
            for c, txt in enumerate(cells, start=1):
                self.cv_table.item(i, c).setText(txt)
        for i, label in enumerate(self.gate_labels):
            if cg is None:
                label.setText(f"GATE {i + 1}: –")
                label.setStyleSheet("")
            else:
                label.setText(f"GATE {i + 1}: {'HIGH' if cg.gates[i] else 'LOW'} · "
                              f"rising edges {cg.gate_counts[i]}")
                label.setStyleSheet(f"color: {theme.TEXT.name()}; font-weight: 700;"
                                    if cg.gates[i] else f"color: {theme.MUTED.name()};")
        if cg is not None:
            button = self.cv_mode_buttons.get(cg.cv_out_mode)
            if button is not None and not button.isChecked():
                button.setChecked(True)
        else:
            self._uncheck_all(self.cv_mode_group, self.cv_mode_buttons.values())

    # ---- simulation controls -------------------------------------------------------------
    def _sim_toggle_gate(self, index: int) -> None:
        dev = self._sim_device()
        if dev:
            dev.set_gate(index, not dev.inputs.gates[index])

    def _sim_device(self):
        return self.cable.device if self.cable is not None else None

    def _sim_button(self, index: int, pressed: bool) -> None:
        if self._sim_device():
            self._sim_device().set_button(index, pressed)

    def _sim_pot(self, index: int, value: float) -> None:
        if self._sim_device():
            self._sim_device().set_pot(index, value)

    def _sim_toggle_sd(self) -> None:
        dev = self._sim_device()
        if dev:
            dev.inputs.sd_card = not dev.inputs.sd_card

    def _sim_set_sd(self, on: bool) -> None:
        dev = self._sim_device()
        if dev:
            dev.inputs.sd_card = on

    def _sim_toggle_jack(self, side: str) -> None:
        dev = self._sim_device()
        if dev:
            attr = "jack_left" if side == "left" else "jack_right"
            setattr(dev.inputs, attr, not getattr(dev.inputs, attr))

    def _sim_set_jack(self, side: str, on: bool) -> None:
        dev = self._sim_device()
        if dev:
            setattr(dev.inputs, "jack_left" if side == "left" else "jack_right", on)

    def _sim_toggle_cable(self) -> None:
        if self.cable is None:
            return
        if self.cable.plugged:
            self.cable.unplug()
            self._log('Simulation: USB cable unplugged (board powered off)')
        else:
            self.cable.plug()
            self.cable.device.responsive = not self.sim_freeze.isChecked()
            self._log('Simulation: USB cable plugged in (firmware restarting)')

    def _sim_set_freeze(self, frozen: bool) -> None:
        if self.cable is not None:
            self.cable.device.responsive = not frozen
            self._log("Simulation: Firmware " + ('unresponsive' if frozen else 'responsive again'))

    def _sim_corrupt(self) -> None:
        if self.cable is not None:
            self.cable.device.corrupt_next_frame = True
            self._log('Simulation: the next frame will have an invalid CRC')

    def _sync_sim_controls(self) -> None:
        dev = self._sim_device()
        if dev is None:
            return
        for widget, value in ((self.sim_sd_card, dev.inputs.sd_card),
                              (self.sim_jl, dev.inputs.jack_left),
                              (self.sim_jr, dev.inputs.jack_right)):
            if widget.isChecked() != value:
                widget.blockSignals(True)
                widget.setChecked(value)
                widget.blockSignals(False)

    # ---- misc ----------------------------------------------------------------------------
    def _log(self, text: str) -> None:
        self.log.appendPlainText(f"{datetime.now():%H:%M:%S}  {text}")

    def closeEvent(self, event) -> None:
        if self.session is not None:
            self.session.disconnect('Application closed')
        super().closeEvent(event)
