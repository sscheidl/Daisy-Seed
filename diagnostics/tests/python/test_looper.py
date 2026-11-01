"""Looper 0.5.0 profile: control SysEx codec, save-before-share, share/return workflow over
the simulated USB-MIDI and CDC personalities, and the profile probe that tells Nebulae and
the looper apart on one MIDI port. Simulation only."""
import pytest

from conftest import FakeClock
from daisy_diag import looper as lp
from daisy_diag import nebulae as n
from daisy_diag import protocol as p
from daisy_diag.looper_sim import SAVE_S, SimulatedLooper
from daisy_diag.nebulae_sim import RESTART_S, SimulatedNebulae
from daisy_diag.session import DeviceSession

PROFILES = (n.NEBULAE, lp.LOOPER)


# ---- SysEx codec (byte layout of instruments/looper/src/control_sysex.h) --------------
def test_requests_match_firmware_layout():
    assert lp.status_request() == bytes([0xF0, 0x7D, 0x4C, 0x50, 0x01, 0x01, 0xF7])
    assert lp.share_request(True, bytes([1, 2, 3, 4])) == bytes(
        [0xF0, 0x7D, 0x4C, 0x50, 0x01, 0x02, 0x01, 1, 2, 3, 4, 0xF7])
    with pytest.raises(ValueError):
        lp.share_request(False, bytes([0x80, 0, 0, 0]))


def test_parse_status_reply():
    # Same layout as tests/test_control.cpp of the firmware.
    ms = 87000
    data = [0, 5, 0, 0, 1 | 2 | 4 | 64, 1, 2, ms & 0x7F, ms >> 7 & 0x7F, ms >> 14 & 0x7F, 0x7F, 0x7F, 127]
    kind, st = lp.parse_reply(bytes([0xF0, 0x7D, 0x4C, 0x50, 1, 0x41, *data, 0xF7]))
    assert kind == "STATUS"
    assert st.firmware == "0.5.0" and st.card and st.loop and st.unsaved and st.overrun
    assert not st.saving and not st.busy and st.save_failed and st.fault_text == "write failed"
    assert st.loop_ms == 87000 and st.overruns == 16383 and st.peak_load == 127
    _, st = lp.parse_reply(bytes([0xF0, 0x7D, 0x4C, 0x50, 1, 0x41, 0, 5, 0, 0, 16 | 32, 0, 0,
                                  0, 0, 0, 0, 0, 0, 0xF7]))
    assert st.recording and st.importing and st.busy


@pytest.mark.parametrize("raw", [
    b"", bytes([0xF0, 0x7D, 0x4E, 0x42, 1, 0x41] + [0] * 19 + [0xF7]),          # Nebulae
    bytes([0xF0, 0x7D, 0x4C, 0x50, 2, 0x41] + [0] * 13 + [0xF7]),              # version
    bytes([0xF0, 0x7D, 0x4C, 0x50, 1, 0x41] + [0] * 12 + [0xF7]),              # length
    bytes([0xF0, 0x7D, 0x4C, 0x50, 1, 0x41] + [0, 5, 0, 1] + [0] * 9 + [0xF7]),  # personality
    bytes([0xF0, 0x7D, 0x4C, 0x50, 1, 0x42, 0, 1, 2, 0x90, 4, 0xF7]),          # status byte
    bytes([0xF0, 0x7D, 0x4C, 0x50, 1, 0x55, 0, 0xF7]),
])
def test_parse_rejects_foreign_or_malformed(raw):
    with pytest.raises(n.NebulaeError):
        lp.parse_reply(raw)


def test_parse_share_and_error():
    assert lp.parse_reply(bytes([0xF0, 0x7D, 0x4C, 0x50, 1, 0x42, 5, 9, 8, 7, 6, 0xF7])) == (
        "SHARE", ("SAVING", bytes([9, 8, 7, 6])))
    assert lp.parse_reply(bytes([0xF0, 0x7D, 0x4C, 0x50, 1, 0x7F, 0x33, 2, 0xF7])) == (
        "ERROR", (0x33, "UNKNOWN"))


def test_management_status_line():
    msg = p.parse_message("LOOPER mode=SDMGMT fw=0.5.0 card=1 ret=0 a=STATUS")
    info = p.parse_looper(msg)
    assert info.firmware == "0.5.0" and info.card and not info.can_return and info.action == "STATUS"
    with pytest.raises(p.ProtocolError):
        p.parse_looper(p.parse_message("LOOPER mode=PERF fw=0.5.0 a=STATUS"))
    with pytest.raises(p.ProtocolError):
        p.parse_looper(p.parse_message("LOOPER mode=SDMGMT fw=0.5.0 a=FROB"))


# ---- workflow over the simulated looper ----------------------------------------------
class Rig:
    def __init__(self, device=SimulatedLooper):
        self.clock = FakeClock()
        self.dev = device(self.clock)
        self.session = DeviceSession(self.dev.cdc_ports, lambda port: self.dev.cable.open(port, self.clock),
                                     self.clock, simulated=True)
        self.tokens = iter(bytes([i, i + 1, i + 2, 0x03]) for i in range(1000))
        self.wf = n.NebulaeWorkflow(self.session, self.dev.midi_ports, self.dev.open_midi, self.clock,
                                    token_source=lambda k: next(self.tokens), profiles=PROFILES)

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


def test_probe_finds_the_looper_and_still_finds_nebulae():
    r = Rig()
    r.run(1.5)
    assert r.wf.phase == n.Phase.PERFORMANCE and r.wf.profile is lp.LOOPER
    assert r.wf.status.firmware == "0.5.0" and r.wf.can_share
    neb = Rig(SimulatedNebulae)
    neb.run(1.5)
    assert neb.wf.phase == n.Phase.PERFORMANCE and neb.wf.profile is n.NEBULAE
    assert neb.wf.status.firmware == "0.2.0"


def test_full_round_trip_share_eject_return():
    r = Rig()
    r.run(1.5)
    assert r.wf.request_share()
    assert r.until(lambda: r.wf.phase == n.Phase.RESTARTING, 1)
    assert r.until(lambda: r.wf.phase == n.Phase.MANAGEMENT, RESTART_S + 5)
    assert r.session.device.app == lp.APP_NAME and "loopmgmt" in r.session.device.capabilities
    assert r.until(lambda: r.wf.reader_state == "SHARED", 5)
    assert "looper/loops" in r.wf.message
    assert r.until(lambda: r.session.looper is not None and not r.session.looper.can_return, 3)
    assert not r.wf.return_to_performance()
    r.dev.cable.device.reader_eject()
    assert r.until(lambda: r.wf.reader_state == "EJECTED", 3)
    assert r.wf.return_to_performance()
    assert r.until(lambda: r.wf.phase == n.Phase.RETURNING, 3)
    assert r.until(lambda: r.wf.phase == n.Phase.PERFORMANCE, 10)
    assert r.dev.personality == "PERF" and r.dev.restarts == 2
    assert "loop.wav" in r.wf.message and r.wf.profile is lp.LOOPER


def test_unsaved_loop_is_saved_before_the_share():
    r = Rig()
    r.dev.unsaved = True
    r.run(1.5)
    assert r.wf.status.unsaved and r.wf.can_share
    assert r.wf.request_share()
    assert r.until(lambda: r.wf.phase == n.Phase.SAVING, 1)
    assert "loop.wav" in r.wf.message and r.dev.saving and r.dev.restarts == 0
    # The workflow asks again until the firmware accepts; no confirmation is needed.
    assert r.until(lambda: r.wf.phase == n.Phase.RESTARTING, SAVE_S + 2)
    assert not r.dev.unsaved and r.dev.restarts == 1
    assert r.until(lambda: r.wf.reader_state == "SHARED", RESTART_S + 5)


def test_save_failure_needs_confirmation_and_cancel_keeps_playing():
    r = Rig()
    r.dev.unsaved = True
    r.dev.save_fails = True
    r.run(1.5)
    assert r.wf.request_share()
    assert r.until(lambda: r.wf.phase == n.Phase.CONFIRM, SAVE_S + 3)
    assert r.wf.needs_confirmation and "LOSES" in r.wf.message and r.dev.restarts == 0
    assert not r.wf.request_share()  # only an explicit confirmation continues
    r.wf.cancel_confirmation()
    assert r.wf.phase == n.Phase.PERFORMANCE and r.dev.restarts == 0
    assert r.wf.request_share()
    assert r.until(lambda: r.wf.phase == n.Phase.CONFIRM, 2)
    assert r.wf.request_share(confirm=True)
    assert r.until(lambda: r.wf.phase == n.Phase.MANAGEMENT, 10)


def test_saving_can_be_cancelled_and_times_out():
    r = Rig()
    r.dev.unsaved = True
    r.run(1.5)
    r.wf.request_share()
    assert r.until(lambda: r.wf.phase == n.Phase.SAVING, 1)
    r.wf.cancel_confirmation()
    assert r.wf.phase == n.Phase.PERFORMANCE and "nothing was shared" in r.wf.message
    r.run(2)
    assert r.dev.restarts == 0  # no further SHARE after the cancel
    # A save that never finishes: the panel gives up after SAVE_TIMEOUT_S.
    r.dev.unsaved = True
    r.dev.saving_until = 1e12
    assert r.wf.request_share()
    assert r.until(lambda: r.wf.phase == n.Phase.SAVING, 1)
    r.run(n.SAVE_TIMEOUT_S - 1, step=0.1)
    assert r.wf.phase == n.Phase.SAVING
    assert r.until(lambda: r.wf.phase == n.Phase.PERFORMANCE, 2)
    assert "not saved" in r.wf.error and r.dev.restarts == 0


@pytest.mark.parametrize("setup,error", [
    (lambda d: setattr(d, "recording", True), "recording"),
    (lambda d: setattr(d, "importing", True), "importing"),
    (lambda d: setattr(d, "card", False), "No microSD"),
])
def test_refusals_change_nothing(setup, error):
    r = Rig()
    setup(r.dev)
    r.run(1.5)
    assert not r.wf.can_share
    r.wf._link.send(lp.share_request(True, bytes([1, 2, 3, 4])))
    r.wf.phase = n.Phase.REQUESTING
    r.wf._token = bytes([1, 2, 3, 4])
    r.run(0.2)
    assert r.wf.phase == n.Phase.PERFORMANCE and error in r.wf.error and r.dev.restarts == 0


def test_found_already_in_management_does_not_share():
    r = Rig()
    r.dev.personality = "MGMT"
    r.dev.cable.plug()
    r.dev.cable.device.inputs.sd_card = True
    r.run(3)
    assert r.wf.phase == n.Phase.MANAGEMENT and r.wf.profile is lp.LOOPER
    assert r.wf.reader_state == "LOCAL" and r.session.looper is not None and r.session.neb is None
    r.run(2)
    assert r.wf.reader_state == "LOCAL"
    assert r.wf.return_to_performance()
    assert r.until(lambda: r.wf.phase == n.Phase.PERFORMANCE, 10)


def test_nebulae_message_is_rejected_by_the_looper():
    r = Rig()
    r.dev.personality = "MGMT"
    r.dev.cable.plug()
    r.run(2)
    r.session.send("NEB a=PERFORM")
    r.run(0.3)
    assert r.dev.personality == "MGMT" and r.dev.restarts == 0
