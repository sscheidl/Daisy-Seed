"""Headless GUI tests of the Nebulae profile (simulated Nebulae on Daisy 0.2.0)."""
import json

import pytest

pytest.importorskip("PySide6")

from PySide6.QtCore import QPoint  # noqa: E402
from PySide6.QtWidgets import QApplication, QPushButton  # noqa: E402

from conftest import FakeClock  # noqa: E402
from daisy_diag.gui import main_window as mw  # noqa: E402
from daisy_diag.gui import theme  # noqa: E402
from daisy_diag.nebulae import Phase  # noqa: E402


@pytest.fixture(scope="module")
def app():
    application = QApplication.instance() or QApplication([])
    theme.apply(application)
    return application


@pytest.fixture
def neb(app):
    clock = FakeClock()
    window = mw.MainWindow(simulation=True, clock=clock, sim_device="nebulae")
    for timer in (window.poll_timer, window.panel_timer, window.port_timer):
        timer.stop()
    window.resize(1500, 900)
    window.control_tabs.setCurrentIndex(mw.TABS.index("Nebulae"))
    window.show()
    app.processEvents()

    def step(seconds=0.1):
        for _ in range(int(seconds / 0.01)):
            clock.advance(0.01)
            window._tick()
        window._refresh_panels()
        app.processEvents()

    def until(predicate, seconds=10.0):
        for _ in range(int(seconds / 0.1)):
            if predicate():
                return True
            step(0.1)
        return predicate()

    step(1.5)
    yield window, step, until
    window.close()


def test_performance_profile_shows_status_and_only_supported_actions(neb):
    window, step, _ = neb
    wf = window.nebulae
    assert wf.phase == Phase.PERFORMANCE
    assert "Performance" in window.neb_phase.text() and "SIMULATION" in window.neb_phase.text()
    assert window.neb_info["fw"].text() == "0.2.0"
    assert "USB-MIDI" in window.neb_info["link"].text()
    assert "bank 1 of 3" in window.neb_info["bank"].text()
    assert "not a bench measurement" in window.neb_info["cpu"].text()
    assert window.neb_share.isEnabled()
    assert not window.neb_return.isEnabled() and not window.neb_ack.isEnabled()
    assert not window.neb_eject_help.isEnabled()
    assert not window.neb_confirm.isVisible() and window.neb_warning.text() == ""
    # Diagnostics panels do not pretend a device: no CDC session in performance mode.
    assert not window.session.connected and not window.led_buttons["FOLLOW"].isEnabled()


def test_buttons_drive_share_eject_return(neb, monkeypatch):
    window, step, until = neb
    window.neb_share.click()
    assert until(lambda: window.nebulae.reader_state == "SHARED")
    assert "Windows owns the card" in window.neb_message.text()
    assert window.neb_eject_help.isEnabled() and not window.neb_return.isEnabled()
    shown = []
    monkeypatch.setattr(mw.QMessageBox, "information", lambda *a: shown.append(a[2]))
    window.neb_eject_help.click()
    assert shown and "Safely Remove Hardware" in shown[0]
    assert window.nebulae.reader_state == "SHARED"  # the help button ejects nothing
    # Capability gating in SD management: no LED or read test, reader controls present.
    assert not window.led_buttons["FOLLOW"].isEnabled() and not window.sd_button.isEnabled()
    window.cable.device.reader_eject()
    assert until(lambda: window.neb_return.isEnabled())
    assert "ejected" in window.neb_message.text()
    window.neb_return.click()
    assert until(lambda: window.nebulae.phase == Phase.PERFORMANCE)
    step(0.2)
    assert window.neb_share.isEnabled() and window.neb_sim.restarts == 2


def test_live_loop_warning_and_confirmation(neb):
    window, step, until = neb
    window.neb_sim.live = True
    step(1.5)
    assert "LOSES it" in window.neb_warning.text()
    window.neb_share.click()
    assert until(lambda: window.neb_confirm.isVisible())
    assert window.neb_cancel.isVisible() and not window.neb_share.isEnabled()
    window.neb_cancel.click()
    step()
    assert not window.neb_confirm.isVisible() and window.neb_sim.restarts == 0
    window.neb_share.click()
    assert until(lambda: window.neb_confirm.isVisible())
    window.neb_confirm.click()
    assert until(lambda: window.nebulae.phase == Phase.MANAGEMENT)


def test_refusal_and_interruption_messages(neb, monkeypatch):
    window, step, until = neb
    window.neb_sim.card = False
    step(1.5)
    assert not window.neb_share.isEnabled() and "no card" in window.neb_info["sd"].text()
    window.neb_sim.card = True
    step(1.5)
    window.neb_share.click()
    assert until(lambda: window.nebulae.reader_state == "SHARED")
    window.cable.device.reader_bus_reset()
    assert until(lambda: window.neb_ack.isEnabled())
    assert "quarantined" in window.neb_message.text() and not window.neb_return.isEnabled()
    monkeypatch.setattr(mw.QMessageBox, "warning", lambda *a: mw.QMessageBox.No)
    window.neb_ack.click()
    step(0.5)
    assert window.nebulae.reader_state == "INTERRUPTED"
    monkeypatch.setattr(mw.QMessageBox, "warning", lambda *a: mw.QMessageBox.Yes)
    window.neb_ack.click()
    assert until(lambda: window.nebulae.reader_state == "LOCAL")
    assert window.neb_return.isEnabled()


def test_long_texts_stay_inside_the_panel(neb):
    window, step, _ = neb
    window.resize(1280, 850)
    window.nebulae.error = "Very long firmware error " * 30
    window.nebulae.message = "Very long instruction " * 40
    window._refresh_nebulae()
    step()
    page = window.control_tabs.currentWidget()
    assert page.widget().width() <= page.viewport().width()
    for widget in page.widget().findChildren(QPushButton) + [window.neb_error, window.neb_message]:
        if widget.isVisible():
            left = widget.mapTo(page.viewport(), QPoint(0, 0)).x()
            assert left >= 0 and left + widget.width() <= page.viewport().width()


def test_report_contains_workflow(neb, monkeypatch, tmp_path):
    window, step, until = neb
    window.neb_share.click()
    assert until(lambda: window.nebulae.reader_state == "SHARED")
    target = tmp_path / "r.json"
    monkeypatch.setattr(mw.QFileDialog, "getSaveFileName", lambda *a: (str(target), "JSON (*.json)"))
    window._save_report()
    report = json.loads(target.read_text())
    assert report["nebulae_workflow"]["phase"] == "MANAGEMENT"
    assert report["nebulae_workflow"]["reader"] == "SHARED"
    assert report["device"]["app"] == "NebulaeOnDaisy" and report["nebulae"]["mode"] == "SDMGMT"


def test_diagnostics_simulation_has_no_nebulae(app):
    clock = FakeClock()
    window = mw.MainWindow(simulation=True, clock=clock)
    window.poll_timer.stop()
    for _ in range(200):
        clock.advance(0.01)
        window._tick()
    window._refresh_panels()
    assert window.nebulae.phase == Phase.NOT_FOUND and not window.neb_share.isEnabled()
    assert window.led_buttons["FOLLOW"].isEnabled()  # diagnostics unchanged
    window.close()
