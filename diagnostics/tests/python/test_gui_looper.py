"""Headless GUI tests of the Looper profile in the SD share tab (simulated Looper 0.5.0)."""
import json

import pytest

pytest.importorskip("PySide6")

from PySide6.QtWidgets import QApplication  # noqa: E402

from conftest import FakeClock  # noqa: E402
from daisy_diag.gui import main_window as mw  # noqa: E402
from daisy_diag.gui import theme  # noqa: E402
from daisy_diag.looper import LOOPER  # noqa: E402
from daisy_diag.nebulae import Phase  # noqa: E402


@pytest.fixture(scope="module")
def app():
    application = QApplication.instance() or QApplication([])
    theme.apply(application)
    return application


@pytest.fixture
def loop(app):
    clock = FakeClock()
    window = mw.MainWindow(simulation=True, clock=clock, sim_device="looper")
    for timer in (window.poll_timer, window.panel_timer, window.port_timer):
        timer.stop()
    window.resize(1500, 900)
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


def test_looper_profile_in_the_sd_share_tab(loop):
    window, step, _ = loop
    assert window.control_tabs.tabText(window.control_tabs.currentIndex()) == "SD share"
    wf = window.nebulae
    assert wf.phase == Phase.PERFORMANCE and wf.profile is LOOPER
    assert window.neb_info["device"].text() == "Looper"
    assert window.neb_info["fw"].text() == "0.5.0"
    assert window.neb_row_labels["bank"].text() == "Loop:"
    assert "12.5 s, saved in looper/loop.wav" in window.neb_info["bank"].text()
    assert window.neb_return.text() == "Return to the Looper"
    assert window.neb_share.isEnabled() and not window.neb_return.isEnabled()
    assert window.neb_warning.text() == "" and not window.neb_cancel.isVisible()


def test_save_then_share_eject_return(loop):
    window, step, until = loop
    window.neb_sim.unsaved = True
    step(1.5)
    assert "not saved yet" in window.neb_info["bank"].text()
    window.neb_share.click()
    assert until(lambda: window.nebulae.phase == Phase.SAVING, 2)
    assert window.neb_cancel.isVisible() and not window.neb_share.isEnabled()
    assert "loop.wav" in window.neb_message.text()
    assert until(lambda: window.nebulae.reader_state == "SHARED")
    assert "looper/loops" in window.neb_message.text()
    assert "reloaded from looper/loop.wav" in window.neb_info["bank"].text()
    window.cable.device.reader_eject()
    assert until(lambda: window.neb_return.isEnabled())
    window.neb_return.click()
    assert until(lambda: window.nebulae.phase == Phase.PERFORMANCE)
    assert window.neb_sim.restarts == 2


def test_failed_save_warning_and_confirmation(loop):
    window, step, until = loop
    window.neb_sim.unsaved = True
    window.neb_sim.save_fails = True
    step(1.5)
    window.neb_share.click()
    assert until(lambda: window.neb_confirm.isVisible(), 5)
    assert window.neb_confirm.text() == "Share without the unsaved changes"
    assert "LOSES the unsaved changes" in window.neb_warning.text()
    window.neb_cancel.click()
    step()
    assert not window.neb_confirm.isVisible() and window.neb_sim.restarts == 0
    window.neb_share.click()
    assert until(lambda: window.neb_confirm.isVisible(), 5)
    window.neb_confirm.click()
    assert until(lambda: window.nebulae.phase == Phase.MANAGEMENT)


def test_report_names_the_profile(loop, monkeypatch, tmp_path):
    window, step, until = loop
    window.neb_share.click()
    assert until(lambda: window.nebulae.reader_state == "SHARED")
    target = tmp_path / "r.json"
    monkeypatch.setattr(mw.QFileDialog, "getSaveFileName", lambda *a: (str(target), "JSON (*.json)"))
    window._save_report()
    report = json.loads(target.read_text())
    assert report["nebulae_workflow"]["profile"] == "Looper"
    assert report["device"]["app"] == "LooperOnDaisy" and report["looper"]["mode"] == "SDMGMT"
    assert report["app_version"] == "0.6.0"
