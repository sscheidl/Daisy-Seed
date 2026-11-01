"""Nebulae on Daisy 0.2.0 profile: control SysEx codec, share/return workflow over the
simulated USB-MIDI and CDC personalities, failure paths. Simulation only."""
import pytest

from conftest import FakeClock
from daisy_diag import nebulae as n
from daisy_diag import protocol as p
from daisy_diag.nebulae_sim import SimulatedNebulae, RESTART_S
from daisy_diag.session import ConnState, DeviceSession


# ---- SysEx codec (byte layout of instruments/granular/src/control_sysex.h) -----------
def test_requests_match_firmware_layout():
    assert n.status_request() == bytes([0xF0, 0x7D, 0x4E, 0x42, 0x01, 0x01, 0xF7])
    assert n.share_request(True, bytes([1, 2, 3, 4])) == bytes(
        [0xF0, 0x7D, 0x4E, 0x42, 0x01, 0x02, 0x01, 1, 2, 3, 4, 0xF7])
    with pytest.raises(ValueError):
        n.share_request(False, bytes([0x80, 0, 0, 0]))


def test_parse_status_reply():
    # Same bytes as the firmware test (test_control_sysex.cpp).
    data = [0, 2, 0, 0, 1 | 16, 2, 3, 2, 16, 7, 0, 0, 0x7F, 0x7F, 3, 4, 0x7F, 0x7F, 127]
    kind, st = n.parse_reply(bytes([0xF0, 0x7D, 0x4E, 0x42, 1, 0x41, *data, 0xF7]))
    assert kind == "STATUS"
    assert st.firmware == "0.2.0" and st.card and st.overrun and not st.live
    assert (st.bank, st.banks, st.playing_bank, st.playing_slot) == (2, 3, 2, 16)
    assert st.bank_mask == 7 and st.slot_mask == 0xFFFF and st.overruns == 16383
    assert st.load_error_text == "SD I/O error"


@pytest.mark.parametrize("raw", [
    b"", bytes([0xF0, 0x7D, 0x42, 0x4C, 1, 0x41, 0xF7]),                      # bootloader
    bytes([0xF0, 0x7D, 0x4E, 0x42, 2, 0x41] + [0] * 19 + [0xF7]),              # version
    bytes([0xF0, 0x7D, 0x4E, 0x42, 1, 0x41] + [0] * 18 + [0xF7]),              # length
    bytes([0xF0, 0x7D, 0x4E, 0x42, 1, 0x41] + [0, 2, 0, 1] + [0] * 15 + [0xF7]),  # personality
    bytes([0xF0, 0x7D, 0x4E, 0x42, 1, 0x42, 0, 1, 2, 0x90, 4, 0xF7]),          # status byte
    bytes([0xF0, 0x7D, 0x4E, 0x42, 1, 0x55, 0, 0xF7]),
])
def test_parse_rejects_foreign_or_malformed(raw):
    with pytest.raises(n.NebulaeError):
        n.parse_reply(raw)


def test_parse_share_and_error():
    assert n.parse_reply(bytes([0xF0, 0x7D, 0x4E, 0x42, 1, 0x42, 1, 9, 8, 7, 6, 0xF7])) == (
        "SHARE", ("CONFIRM_REQUIRED", bytes([9, 8, 7, 6])))
    assert n.parse_reply(bytes([0xF0, 0x7D, 0x4E, 0x42, 1, 0x7F, 0x33, 2, 0xF7])) == (
        "ERROR", (0x33, "UNKNOWN"))


# ---- workflow over the simulated device ----------------------------------------------
class Rig:
    def __init__(self):
        self.clock = FakeClock()
        self.dev = SimulatedNebulae(self.clock)
        self.session = DeviceSession(self.dev.cdc_ports, lambda port: self.dev.cable.open(port, self.clock),
                                     self.clock, simulated=True)
        self.tokens = iter(bytes([i, i + 1, i + 2, 0x83]) for i in range(1000))
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


def test_full_round_trip_share_eject_return():
    r = Rig()
    r.run(1.5)
    assert r.wf.phase == n.Phase.PERFORMANCE and r.wf.status.firmware == "0.2.0"
    assert r.wf.can_share and not r.session.connected  # no COM port in performance
    assert r.wf.request_share()
    assert r.until(lambda: r.wf.phase == n.Phase.RESTARTING, 1)
    assert r.dev.restarts == 1
    # The restart takes RESTART_S; the panel continues the share exactly once.
    assert r.until(lambda: r.wf.phase == n.Phase.MANAGEMENT, RESTART_S + 5)
    assert r.session.device.app == n.APP_NAME and "nebmgmt" in r.session.device.capabilities
    assert r.until(lambda: r.wf.reader_state == "SHARED", 5)
    assert not r.wf.ejected and not r.wf.can_return  # Windows owns the card
    assert not r.wf.return_to_performance()
    assert r.until(lambda: r.session.neb is not None and not r.session.neb.can_return, 3)
    r.dev.cable.device.reader_eject()  # Windows "Safely remove"
    assert r.until(lambda: r.wf.reader_state == "EJECTED", 3)
    assert r.wf.ejected and r.wf.can_return
    assert r.wf.return_to_performance()
    assert r.until(lambda: r.wf.phase == n.Phase.RETURNING, 3)
    assert r.until(lambda: r.wf.phase == n.Phase.PERFORMANCE, 10)
    assert r.dev.personality == "PERF" and r.dev.restarts == 2
    assert "rescanned" in r.wf.message


def test_live_loop_needs_confirmation_and_cancel_keeps_playing():
    r = Rig()
    r.dev.live = True
    r.run(1.5)
    assert r.wf.request_share()
    assert r.until(lambda: r.wf.phase == n.Phase.CONFIRM, 2)
    assert r.wf.needs_confirmation and "LOST" in r.wf.message
    assert r.dev.restarts == 0
    r.wf.cancel_confirmation()
    assert r.wf.phase == n.Phase.PERFORMANCE and r.dev.restarts == 0
    assert r.wf.request_share()
    assert r.until(lambda: r.wf.phase == n.Phase.CONFIRM, 2)
    assert r.wf.request_share(confirm=True)
    assert r.until(lambda: r.wf.phase == n.Phase.MANAGEMENT, 10)
    assert not r.dev.live  # RAM loop is gone after the restart (simulated)


@pytest.mark.parametrize("setup,error", [
    (lambda d: setattr(d, "recording", True), "recording"),
    (lambda d: setattr(d, "card", False), "No microSD"),
])
def test_refusals_change_nothing(setup, error):
    r = Rig()
    setup(r.dev)
    r.run(1.5)
    # The GUI disables the button; a forced request is refused by the firmware too.
    assert not r.wf.can_share
    r.wf._link.send(n.share_request(True, bytes([1, 2, 3, 4])))
    r.wf.phase = n.Phase.REQUESTING
    r.wf._token = bytes([1, 2, 3, 4])
    r.run(0.2)
    assert r.wf.phase == n.Phase.PERFORMANCE and error in r.wf.error and r.dev.restarts == 0


def test_share_timeout_and_lost_device():
    r = Rig()
    r.run(1.5)
    r.dev.handle_sysex = lambda message: None  # firmware stops answering
    r.wf.request_share()
    r.run(n.SHARE_TIMEOUT_S + 0.5)
    assert "No answer" in r.wf.error
    r.run(n.STATUS_TIMEOUT_S + 1)
    assert r.wf.phase == n.Phase.NOT_FOUND and r.wf.status is None


def test_restart_that_comes_back_in_performance_is_reported():
    r = Rig()
    r.run(1.5)
    r.wf.request_share()
    r.run(0.1)
    r.dev._target = "PERF"  # R1: boot intent lost, device boots performance
    assert r.until(lambda: r.wf.phase == n.Phase.PERFORMANCE, 10)
    assert "did not enter SD management" in r.wf.error


def test_restart_timeout():
    r = Rig()
    r.run(1.5)
    r.wf.request_share()
    r.run(0.1)
    r.dev._switch_at = 1e12  # never re-enumerates
    r.run(n.RESTART_TIMEOUT_S + 1)
    assert r.wf.phase == n.Phase.NOT_FOUND and "did not reappear" in r.wf.error


def test_interruption_is_quarantined_until_acknowledged():
    r = Rig()
    r.run(1.5)
    r.wf.request_share()
    assert r.until(lambda: r.wf.reader_state == "SHARED", 10)
    r.dev.cable.device.reader_bus_reset()
    assert r.until(lambda: r.wf.reader_state == "INTERRUPTED", 3)
    assert not r.wf.ejected and not r.wf.can_return and r.wf.can_acknowledge
    assert "quarantined" in r.wf.message
    r.run(3)
    assert r.wf.reader_state == "INTERRUPTED"  # no automatic reshare or return
    assert r.wf.acknowledge_interruption()
    assert r.until(lambda: r.wf.reader_state == "LOCAL", 3)
    assert r.wf.can_return and r.wf.return_to_performance()
    assert r.until(lambda: r.wf.phase == n.Phase.PERFORMANCE, 10)


def test_closing_the_link_never_returns_the_card():
    r = Rig()
    r.run(1.5)
    r.wf.request_share()
    assert r.until(lambda: r.wf.reader_state == "SHARED", 10)
    r.session.disconnect("GUI closed")
    assert r.dev.cable.device.reader_state == "SHARED"
    r.run(5)  # reconnects, queries STATUS, never repeats SHARE or RETURN
    assert r.wf.reader_state == "SHARED" and r.dev.personality == "MGMT"


def test_found_already_in_management_does_not_share():
    r = Rig()
    r.dev.personality = "MGMT"
    r.dev.cable.plug()
    r.dev.cable.device.inputs.sd_card = True
    r.run(3)
    assert r.wf.phase == n.Phase.MANAGEMENT and r.wf.reader_state == "LOCAL"
    assert r.wf.can_share_in_management
    r.run(2)
    assert r.wf.reader_state == "LOCAL"  # only an explicit request shares
    assert r.wf.request_share() and r.until(lambda: r.wf.reader_state == "SHARED", 3)


def test_unsupported_diagnostic_commands_are_rejected_in_management():
    r = Rig()
    r.dev.personality = "MGMT"
    r.dev.cable.plug()
    r.run(2)
    assert r.session.connected and "led" not in r.session.device.capabilities
    assert "sdread" not in r.session.device.capabilities  # the GUI offers no read test
    r.session.send("LED m=CHASE")
    r.run(0.2)
    assert r.session.state == ConnState.CONNECTED
