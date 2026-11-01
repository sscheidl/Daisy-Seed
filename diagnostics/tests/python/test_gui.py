"""Headless GUI tests in simulation mode (QT_QPA_PLATFORM=offscreen)."""
import pytest

pytest.importorskip("PySide6")

from PySide6.QtCore import QPoint, Qt  # noqa: E402
from PySide6.QtTest import QTest  # noqa: E402
from PySide6.QtWidgets import QApplication, QLabel  # noqa: E402

from conftest import FakeClock  # noqa: E402
from daisy_diag import protocol as p  # noqa: E402
from daisy_diag.gui import board as board_mod  # noqa: E402
from daisy_diag.gui import main_window as mw  # noqa: E402
from daisy_diag.gui import theme  # noqa: E402
from daisy_diag.session import ConnState  # noqa: E402


@pytest.fixture(scope="module")
def app():
    application = QApplication.instance() or QApplication([])
    theme.apply(application)
    return application


@pytest.fixture
def sim(app):
    clock = FakeClock()
    window = mw.MainWindow(simulation=True, clock=clock)
    for timer in (window.poll_timer, window.panel_timer, window.port_timer):
        timer.stop()
    window.resize(1500, 900)
    window.show()
    app.processEvents()

    def step(seconds=0.1):
        for _ in range(int(seconds / 0.005)):
            clock.advance(0.005)
            window._tick()
        window._refresh_panels()
        app.processEvents()

    step(0.3)
    yield window, step
    window.close()


def widget_point(window, board_point):
    pt = window.board.to_widget(board_point)
    return QPoint(round(pt.x()), round(pt.y()))


def test_starts_in_simulation_and_is_marked(sim):
    window, _ = sim
    assert window.session.state == ConnState.CONNECTED
    assert window.sim_banner.isVisible() and "SIMULATION" in window.sim_banner.text()
    assert "SIMULATION" in window.windowTitle()
    assert window.status_pill.text() == "Connected (Simulation)"
    assert window.info["fw"].text() == "DaisyDiagnostic 0.4.0-sim"
    assert window.info["proto"].text().startswith("DDP v1")
    assert window.board.simulated and window.board.interactive


def test_reader_controls_follow_ownership_and_never_create_a_drive(sim):
    window, step = sim
    assert "no Windows drive" in window.reader_status.text()
    assert not window.reader_share.isEnabled()
    window.cable.device.inputs.sd_card = True
    step()
    assert window.reader_share.isEnabled()
    window.reader_share.click()
    step()
    assert "Windows owns" in window.reader_status.text()
    assert not window.sd_button.isEnabled() and not window.reader_return.isEnabled()
    window.sim_reader_eject.click()
    step()
    assert window.reader_return.isEnabled() and not window.sd_button.isEnabled()
    window.reader_return.click()
    step()
    assert window.sd_button.isEnabled() and window.reader_share.isEnabled()


def test_interrupted_reader_return_requires_acknowledgement(sim, monkeypatch):
    window, step = sim
    window.cable.device.inputs.sd_card = True
    step()
    window.reader_share.click()
    step()
    window.sim_reader_reset.click()
    step()
    assert "interrupted" in window.reader_status.text()
    monkeypatch.setattr(mw.QMessageBox, "warning", lambda *args: mw.QMessageBox.No)
    window.reader_return.click()
    step()
    assert window.session.reader.state == "INTERRUPTED"
    monkeypatch.setattr(mw.QMessageBox, "warning", lambda *args: mw.QMessageBox.Yes)
    window.reader_return.click()
    step()
    assert window.session.reader.state == "LOCAL"


def test_reader_unavailable_for_protected_firmware(sim):
    window, step = sim
    window.cable.device.inputs.sd_card = True
    step()
    window.session.device.capabilities.remove("sdmsc")
    window._refresh_panels()
    assert "unavailable" in window.reader_status.text()
    assert not window.reader_share.isEnabled() and not window.reader_return.isEnabled()
    assert window.sd_button.isEnabled()


@pytest.mark.parametrize("index", range(p.NUM_BUTTONS))
def test_clicking_each_button_on_board(sim, index):
    window, step = sim
    pos = widget_point(window, board_mod.button_rect(index).center())
    QTest.mousePress(window.board, Qt.LeftButton, Qt.NoModifier, pos)
    step(0.05)
    assert window.board.state.buttons[index]
    assert window.board.state.leds[index]  # FOLLOW mode
    assert window.counter_labels[index].text() == f"S{index + 1}: 1"
    QTest.mouseRelease(window.board, Qt.LeftButton, Qt.NoModifier, pos)
    step(0.05)
    assert not any(window.board.state.buttons)


@pytest.mark.parametrize("index", range(p.NUM_POTS))
def test_dragging_each_pot(sim, index):
    window, step = sim
    c = board_mod.pot_center(index)
    # Pointer straight up = 50 %, then to the right (3 o'clock) = 83.3 %
    QTest.mousePress(window.board, Qt.LeftButton, Qt.NoModifier,
                     widget_point(window, c + board_mod.QPointF(0, -30)))
    step(0.05)
    assert window.board.state.pots[index] == pytest.approx(0.5, abs=0.02)
    QTest.mouseMove(window.board, widget_point(window, c + board_mod.QPointF(30, 0)))
    QTest.mouseRelease(window.board, Qt.LeftButton, Qt.NoModifier,
                       widget_point(window, c + board_mod.QPointF(30, 0)))
    step(0.05)
    assert window.board.state.pots[index] == pytest.approx(225 / 270, abs=0.02)
    assert window.pot_table.item(index, 1).text().endswith("%")
    assert window.board.pot_min[index] == pytest.approx(0.0, abs=1e-6)
    assert window.board.pot_max[index] == pytest.approx(225 / 270, abs=0.02)


def test_toggle_switches_by_click(sim):
    window, step = sim
    for center, attr, positions in ((board_mod.SW18_POS, "sw18", {"U": -25, "C": 0, "D": 25}),
                                    (board_mod.SW17_POS, "sw17", {"U": -25, "D": 25})):
        for pos, dy in positions.items():
            QTest.mouseClick(window.board, Qt.LeftButton, Qt.NoModifier,
                             widget_point(window, center + board_mod.QPointF(0, dy)))
            step(0.05)
            assert getattr(window.board.state, attr) == pos
    assert window.in_labels["sw17"].text().startswith("D")


def test_leds_follow_buttons_by_default_and_test_patterns_are_explicit(sim):
    window, step = sim
    assert window.board.state.led_mode == "FOLLOW"
    assert window.led_buttons["FOLLOW"].isChecked()
    assert set(window.led_buttons) == {"FOLLOW", "OFF", "ON", "CHASE", "POTDIM"}  # no per-LED mask
    window.led_buttons["ON"].click()
    step(0.05)
    assert window.board.state.led_mode == "ON" and all(window.board.state.leds)
    window.led_buttons["OFF"].click()
    step(0.05)
    assert not any(window.board.state.leds)
    # Clicking an LED on the board is not a control any more: nothing changes.
    QTest.mouseClick(window.board, Qt.LeftButton, Qt.NoModifier,
                     widget_point(window, board_mod.led_rect(6).center()))
    step(0.05)
    assert window.board.state.led_mode == "OFF" and not any(window.board.state.leds)
    window.led_buttons["CHASE"].click()
    step(0.05)
    assert window.board.state.led_mode == "CHASE" and sum(window.board.state.leds) == 1
    assert window.led_buttons["CHASE"].isChecked()
    window.led_buttons["FOLLOW"].click()
    step(0.05)
    pos = widget_point(window, board_mod.button_rect(3).center())
    QTest.mousePress(window.board, Qt.LeftButton, Qt.NoModifier, pos)
    step(0.05)
    assert window.board.state.led_mode == "FOLLOW"
    assert [i for i, on in enumerate(window.board.state.leds) if on] == [3]
    QTest.mouseRelease(window.board, Qt.LeftButton, Qt.NoModifier, pos)
    step(0.05)
    assert not any(window.board.state.leds)


def test_cv_min_max_cover_every_received_frame(sim):
    window, step = sim
    window.sim_cv_patch.setChecked(True)
    window.cv_period_spin.setValue(200)
    window.cv_mode_buttons["SAW"].click()
    step(0.1)
    window._reset_minmax()
    step(3.0)  # 150 frames at 50 Hz; a 10 Hz panel refresh would alias to one phase
    assert window.cv_min[0] is not None and window.cv_min[0] < 0.15
    assert window.cv_max[0] > 0.85
    assert window.cv_table.item(0, 3).text() == f"{window.cv_min[0]:+.3f}"
    assert window.cv_table.item(0, 4).text() == f"{window.cv_max[0]:+.3f}"
    window.cv_mode_buttons["OFF"].click()
    step(0.05)


def test_saved_report_has_no_stale_sd_result_after_card_removal_or_unplug(sim, monkeypatch,
                                                                          tmp_path):
    import json
    window, step = sim
    destination = tmp_path / "report.json"
    monkeypatch.setattr(mw.QFileDialog, "getSaveFileName",
                        lambda *args: (str(destination), "JSON (*.json)"))
    window.sim_sd_card.setChecked(True)
    step()
    window.sd_button.click()
    step()
    assert window.sd_result is not None and window.sd_result.status == "MOUNTED"
    window.save_report.click()
    assert json.loads(destination.read_text(encoding="utf-8"))["sd"]["status"] == "MOUNTED"
    window.sim_sd_card.setChecked(False)
    step()
    assert window.sd_status.text() == "No card"
    window.save_report.click()
    report = json.loads(destination.read_text(encoding="utf-8"))
    assert report["sd"] is None and report["board"]["sd_card"] is False
    window.sim_sd_card.setChecked(True)
    step()
    window.sd_button.click()
    step()
    window.sim_plug.click()
    step(0.2)
    window.save_report.click()
    report = json.loads(destination.read_text(encoding="utf-8"))
    assert report["connected"] is False
    assert report["sd"] is None and report["device"] is None and report["port"] is None


def test_sd_test_switches_an_active_cv_out_off_first(sim, monkeypatch):
    window, step = sim
    window.sim_sd_card.setChecked(True)
    window.cv_code_spins[0].setValue(2000)
    window.cv_mode_buttons["FIX"].click()
    step()
    assert window.board.state.cvgate.cv_out_mode == "FIX"
    monkeypatch.setattr(mw.QMessageBox, "question", lambda *args: mw.QMessageBox.No)
    window.sd_button.click()
    step()
    assert window.board.state.cvgate.cv_out_mode == "FIX" and window.sd_result is None
    monkeypatch.setattr(mw.QMessageBox, "question", lambda *args: mw.QMessageBox.Yes)
    window.sd_button.click()
    step()
    assert window.board.state.cvgate.cv_out_mode == "OFF"
    assert window.sd_status.text().startswith("Filesystem readable (FAT32")


def test_sd_states(sim):
    window, step = sim
    assert window.sd_status.text() == 'No card'
    assert not window.sd_button.isEnabled()
    QTest.mouseClick(window.board, Qt.LeftButton, Qt.NoModifier,
                     widget_point(window, board_mod.SD_RECT.center()))
    step(0.05)
    assert window.sd_status.text() == 'Card detected'
    assert window.sd_button.isEnabled()
    window.sd_button.click()
    step(0.05)
    assert window.sd_status.text().startswith('Filesystem readable (FAT32')
    window.sim_sd_fs.setChecked(False)
    window.sd_button.click()
    step(0.05)
    assert window.sd_status.text().startswith('Error: FR_NO_FILESYSTEM')
    window.sim_sd_card.setChecked(False)
    step(0.05)
    assert window.sd_status.text() == 'No card'


def test_jack_detection_click(sim):
    window, step = sim
    x, _ = board_mod.AUDIO_JACKS["in_l"]
    QTest.mouseClick(window.board, Qt.LeftButton, Qt.NoModifier,
                     widget_point(window, board_mod.QPointF(x, 40)))
    step(0.05)
    assert window.board.state.jack_left and not window.board.state.jack_right
    assert window.in_labels["jl"].text() == 'Plug detected'


def test_usb_unplug_and_reconnect(sim):
    window, step = sim
    window.sim_plug.click()
    step(0.1)
    assert window.status_pill.text() == 'Disconnected'
    assert window.board.state is None
    assert not window.led_buttons["ON"].isEnabled()
    window.sim_plug.click()
    step(1.5)
    assert window.status_pill.text() == "Connected (Simulation)"
    assert window.info["reconnects"].text() == "1"


def test_frozen_firmware_shows_connection_error(sim):
    window, step = sim
    window.sim_freeze.setChecked(True)
    step(1.7)
    assert window.status_pill.text() == 'Connection error'
    window.sim_freeze.setChecked(False)
    step(2.5)
    assert window.session.connected


def test_board_marks_unimplemented_connectors(sim):
    window, _ = sim
    tip = window.board.tooltip_for(("midi", 0))
    assert 'Not implemented' in tip
    assert not [w for w in window.findChildren(QLabel) if w.objectName() == "niBadge"]


def test_led_tooltip_states_no_optical_feedback(sim):
    window, _ = sim
    assert 'No electrical feedback' in window.board.tooltip_for(("led", 0))


def test_hardware_mode_without_device(app, monkeypatch):
    monkeypatch.setattr(mw, "list_serial_ports", lambda: [])
    clock = FakeClock()
    window = mw.MainWindow(simulation=False, clock=clock)
    for timer in (window.poll_timer, window.panel_timer, window.port_timer):
        timer.stop()
    for _ in range(50):
        clock.advance(0.05)
        window._tick()
    window._refresh_panels()
    assert window.sim_banner.isHidden()
    assert window.session.state == ConnState.SEARCHING
    assert window.status_pill.text() == 'Disconnected'
    assert "No Daisy" in window.status_detail.text()
    assert window.board.state is None
    tabs = [window.control_tabs.tabText(i) for i in range(window.control_tabs.count())]
    assert tabs == list(mw.TABS)  # no simulation tab in hardware mode
    window.close()


def test_cv_gate_panel_and_board(sim):
    window, step = sim
    dev = window.cable.device
    dev.set_cv(0, 0.25)
    dev.set_cv(1, -0.5)
    step(0.05)
    cg = window.board.state.cvgate
    assert cg is not None and cg.cv == pytest.approx([0.25, -0.5])
    assert window.cv_table.item(0, 1).text() == "+0.2500"
    assert window.cv_table.item(1, 1).text() == "-0.5000"
    # gate click on the board (simulation) and edge counter
    QTest.mouseClick(window.board, Qt.LeftButton, Qt.NoModifier,
                     widget_point(window, board_mod.CV_JACKS["gate_in_2"][0]))
    step(0.05)
    assert window.board.state.cvgate.gates == [False, True]
    assert window.gate_labels[1].text() == "GATE 2: HIGH · rising edges 1"
    dev.pulse_gate(0)
    step(0.05)
    assert window.board.state.cvgate.gate_counts[0] == 1
    assert "CV IN 1: +0.2500" in window.board.tooltip_for(("cv_in_1", 0))
    assert "Not implemented" not in window.board.tooltip_for(("cv", 0))


def test_cv_out_controls(sim):
    window, step = sim
    window.cv_code_spins[0].setValue(1000)
    window.cv_code_spins[1].setValue(4095)
    window.cv_mode_buttons["FIX"].click()
    step(0.05)
    cg = window.board.state.cvgate
    assert (cg.cv_out_mode, cg.cv_out) == ("FIX", [1000, 4095])
    assert window.cv_mode_buttons["FIX"].isChecked()
    window.cv_period_spin.setValue(200)
    window.cv_mode_buttons["SAW"].click()
    step(0.05)
    assert window.board.state.cvgate.cv_out_mode == "SAW"
    window.sim_cv_patch.setChecked(True)
    step(0.05)
    cg = window.board.state.cvgate
    assert cg.cv[0] == pytest.approx(cg.cv_out[0] / 4095, abs=1e-3)
    window.cv_mode_buttons["OFF"].click()
    step(0.05)
    assert window.board.state.cvgate.cv_out == [0, 0]


def test_reader_diagnostics_display_and_saved_report(sim, monkeypatch, tmp_path):
    import json
    window, _ = sim
    window.session._handle('READER state=ERROR sectors=123 busy=0 a=STATUS io=WRITE fault=7 he=32 lba=777 ms=1000', window.session._clock())
    window._refresh_panels()
    assert 'card status error' in window.reader_status.text()
    assert 'WRITE' in window.reader_status.text() and '777' in window.reader_status.text()
    destination = tmp_path / 'diagnostics.json'
    monkeypatch.setattr(mw.QFileDialog, 'getSaveFileName', lambda *args: (str(destination),'JSON (*.json)'))
    window.save_report.click()
    report = json.loads(destination.read_text(encoding='utf-8'))
    assert report['simulated'] is True
    assert report['reader']['fault'] == 7 and report['reader']['he'] == 32
    assert report['reader']['io'] == 'WRITE' and report['reader']['lba'] == 777
    assert report['reader']['ms'] == 1000 and report['captured_at']


def test_control_tabs_are_categories_with_report_always_reachable(sim):
    window, step = sim
    tabs = [window.control_tabs.tabText(i) for i in range(window.control_tabs.count())]
    assert tabs == list(mw.TABS) + [mw.SIM_TAB]
    assert tabs == ["USB", "Buttons", "Pots", "CV / Gate", "Audio", "SD card", "Nebulae", "Log",
                    "Simulation"]
    expectations = {
        "USB": [window.port_combo, window.boot_button],
        "Buttons": [window.counter_labels[15], window.led_buttons["FOLLOW"]],
        "Pots": [window.pot_table, window.in_labels["sw18"]],
        "CV / Gate": [window.cv_table, window.cv_apply, window.gate_labels[1]],
        "Audio": [window.level_bars["il"], window.tone_buttons["SINE"], window.scope_button],
        "SD card": [window.sd_button, window.reader_share],
        "Nebulae": [window.neb_phase, window.neb_share, window.neb_return],
        "Log": [window.log],
        "Simulation": [window.sim_plug, window.sim_cv_patch],
    }
    for index, title in enumerate(tabs):
        window.control_tabs.setCurrentIndex(index)
        step()
        for widget in expectations[title]:
            assert widget.isVisible(), (title, widget)
        assert window.save_report.isVisible()
        assert window.control_tabs.tabToolTip(index)


def test_switching_control_tabs_preserves_reader_and_cv_output(sim):
    window, step = sim
    window.cable.device.inputs.sd_card = True
    step()
    window.reader_share.click()
    step()
    window.cv_code_spins[0].setValue(1234)
    window.cv_mode_buttons['FIX'].click()
    step()
    for i in range(window.control_tabs.count()):
        window.control_tabs.setCurrentIndex(i)
        step()
        assert window.session.reader.state == 'SHARED'
        assert window.board.state.cvgate.cv_out_mode == 'FIX'
        assert window.board.state.cvgate.cv_out == [1234, 0]
        assert 'OUT 1 = 1234' in window.cv_out_status.text()
        assert 'OUT 2 = 0' in window.cv_out_status.text()


@pytest.mark.parametrize('width', [1280, 1560])
def test_control_pages_fit_width_and_keep_buttons_inside_viewport(sim, width):
    window, step = sim
    window.resize(width, 850)
    step()
    bar = window.control_tabs.tabBar()
    assert bar.tabRect(bar.count() - 1).right() <= bar.width()  # all tabs visible, no scrolling
    for i in range(window.control_tabs.count()):
        window.control_tabs.setCurrentIndex(i)
        step()
        page = window.control_tabs.currentWidget()
        assert page.widget().width() <= page.viewport().width()
        from PySide6.QtWidgets import QPushButton
        for button in page.widget().findChildren(QPushButton):
            if not button.isVisible():
                continue
            left = button.mapTo(page.viewport(), QPoint(0, 0)).x()
            assert left >= 0
            assert left + button.width() <= page.viewport().width()
