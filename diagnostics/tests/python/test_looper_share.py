"""Looper 0.5.0 profile of the SD sharing workflow: the loop is saved before the restart
(PENDING), a failed save needs confirmation. Simulation only."""
from conftest import FakeClock
from daisy_diag import nebulae as n
from daisy_diag.nebulae_sim import RESTART_S, SimulatedLooper
from daisy_diag.session import DeviceSession


class Rig:
    def __init__(self):
        self.clock = FakeClock()
        self.dev = SimulatedLooper(self.clock)
        self.session = DeviceSession(self.dev.cdc_ports, lambda port: self.dev.cable.open(port, self.clock),
                                     self.clock, simulated=True)
        self.tokens = iter(bytes([i, 1, 2, 3]) for i in range(1000))
        self.wf = n.NebulaeWorkflow(self.session, self.dev.midi_ports, self.dev.open_midi, self.clock,
                                    token_source=lambda k: next(self.tokens))

    def run(self, seconds: float, step: float = 0.02):
        for _ in range(int(seconds / step)):
            self.clock.advance(step)
            self.session.poll()
            self.wf.poll()

    def until(self, predicate, seconds: float = 10.0):
        for _ in range(int(seconds / 0.02)):
            if predicate():
                return True
            self.run(0.02)
        return predicate()


def test_looper_status_codec():
    data = [0, 5, 0, 0, 1 | 2 | 32 | 64, 1, 871 & 0x7F, 871 >> 7, 2, 0x7F, 0x7F, 127]
    kind, st = n.parse_reply(bytes([0xF0, 0x7D, 0x4C, 0x50, 1, 0x41, *data, 0xF7]))
    assert kind == "STATUS" and isinstance(st, n.LooperStatus)
    assert st.firmware == "0.5.0" and st.card and st.loop and st.in_sync and st.overrun
    assert st.share_pending and not st.share_failed and st.loop_seconds == 87.1
    assert st.fault_text.startswith("write error") and st.overruns == 16383 and st.peak_load == 127
    assert n.status_request(n.LOOPER) == bytes([0xF0, 0x7D, 0x4C, 0x50, 1, 1, 0xF7])
    assert n.parse_reply(bytes([0xF0, 0x7D, 0x4C, 0x50, 1, 0x42, 5, 1, 2, 3, 4, 0xF7])) == (
        "SHARE", ("PENDING", bytes([1, 2, 3, 4])))


def test_looper_saves_first_then_restarts_and_returns():
    r = Rig()
    r.run(1.5)
    assert r.wf.phase == n.Phase.PERFORMANCE and r.wf.profile is n.LOOPER
    assert r.wf.status.firmware == "0.5.0" and not r.wf.status.in_sync
    assert r.wf.request_share()
    assert r.until(lambda: r.wf.phase == n.Phase.SAVING, 1)
    assert "saves the current loop" in r.wf.message and r.dev.restarts == 0
    assert r.until(lambda: r.wf.phase == n.Phase.RESTARTING, 5)
    assert r.dev.restarts == 1 and r.dev.in_sync
    assert r.until(lambda: r.wf.reader_state == "SHARED", RESTART_S + 5)
    assert r.session.device.app == "Looper" and r.wf.profile is n.LOOPER
    assert "Looper" in r.wf.message or "Windows owns the card" in r.wf.message
    r.dev.cable.device.reader_eject()
    assert r.until(lambda: r.wf.can_return, 3)
    assert r.wf.return_to_performance()
    assert r.until(lambda: r.wf.phase == n.Phase.PERFORMANCE, 10)
    assert "recalled from the card" in r.wf.message


def test_in_sync_loop_restarts_without_saving():
    r = Rig()
    r.dev.in_sync = True
    r.run(1.5)
    assert r.wf.request_share()
    assert r.until(lambda: r.wf.phase == n.Phase.RESTARTING, 1)
    assert r.dev.restarts == 1


def test_failed_save_needs_confirmation():
    r = Rig()
    r.dev.save_fails = True
    r.run(1.5)
    assert r.wf.request_share()
    assert r.until(lambda: r.wf.phase == n.Phase.CONFIRM, 5)
    assert "could not be saved" in r.wf.message and "write error" in r.wf.error
    assert r.wf.needs_confirmation and r.dev.restarts == 0
    r.wf.cancel_confirmation()
    assert r.wf.phase == n.Phase.PERFORMANCE and r.dev.restarts == 0
    assert r.wf.request_share()
    assert r.until(lambda: r.wf.phase == n.Phase.CONFIRM, 2)  # firmware: CONFIRM_REQUIRED now
    assert not r.wf.request_share()  # without confirm nothing is sent
    assert r.wf.request_share(confirm=True)
    assert r.until(lambda: r.wf.phase == n.Phase.MANAGEMENT, 10)


def test_busy_and_no_card():
    r = Rig()
    r.dev.recording = True
    r.run(1.5)
    assert not r.wf.can_share  # the GUI disables the button
    r.dev.recording = False
    r.dev.card = False
    r.run(1.5)
    assert not r.wf.can_share and r.dev.restarts == 0


def test_saving_that_never_ends_times_out():
    r = Rig()
    r.run(1.5)
    r.dev.SAVE_S = 1e9
    assert r.wf.request_share()
    assert r.until(lambda: r.wf.phase == n.Phase.SAVING, 1)
    r.run(n.SAVE_TIMEOUT_S + 2, step=0.25)
    assert r.wf.phase == n.Phase.PERFORMANCE and "did not finish saving" in r.wf.error
    assert r.dev.restarts == 0
