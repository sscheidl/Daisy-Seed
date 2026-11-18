"""MIDI link on its own thread (P3 of the Nebulae first-contact test, 2026-11-08).

With the stalling Nebulae 0.2.1 image, WinMM threw on SysEx sends ("MidiOutWinMM::sendMessage:
error sending sysex message") or never completed them, and the panel died with the GUI thread
inside python-rtmidi. These tests use fake drivers that block or raise: no GUI-thread call may
wait in the driver, a lost device is reported, and the link comes back once the kit answers.
Real time (threads), short timeouts.
"""
import threading
import time

import pytest

from conftest import FakeClock
from daisy_diag import nebulae as n
from daisy_diag.nebulae_sim import SimulatedNebulae
from daisy_diag.session import DeviceSession

SYSEX_ERROR = "MidiOutWinMM::sendMessage: error sending sysex message."
STATUS = n.status_request()


class FakeDriver:
    """A blocking MidiLink like RtMidiLink on WinMM: send() can hang or raise."""

    def __init__(self):
        self.go = threading.Event()
        self.go.set()
        self.entered = threading.Event()
        self.closed = threading.Event()
        self.raise_on_send: Exception | None = None
        self.lock = threading.Lock()
        self.sent: list[bytes] = []
        self.inbox: list[bytes] = []
        self.close_thread: threading.Thread | None = None

    def send(self, message: bytes) -> None:
        self.entered.set()
        self.go.wait()
        if self.raise_on_send is not None:
            raise self.raise_on_send
        with self.lock:
            self.sent.append(bytes(message))

    def poll(self) -> list[bytes]:
        with self.lock:
            out, self.inbox = self.inbox, []
        return out

    def close(self) -> None:
        self.close_thread = threading.current_thread()
        self.closed.set()


def wait_for(predicate, seconds: float = 2.0) -> bool:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if predicate():
            return True
        time.sleep(0.002)
    return predicate()


def timed(call):
    start = time.monotonic()
    result = call()
    return result, time.monotonic() - start


def make_link(driver: FakeDriver, **kw) -> n.ThreadedMidiLink:
    kw.setdefault("send_timeout", 0.1)
    return n.ThreadedMidiLink(lambda: driver, **kw)


def test_messages_go_out_in_order_and_replies_come_back():
    driver = FakeDriver()
    link = make_link(driver)
    for i in range(5):
        link.send(bytes([0xF0, i, 0xF7]))
    assert wait_for(lambda: len(driver.sent) == 5)
    assert [m[1] for m in driver.sent] == [0, 1, 2, 3, 4]
    with driver.lock:
        driver.inbox.append(b"\xf0\x7d\x4e\x42\x01\x41\xf7")
    got: list[bytes] = []
    assert wait_for(lambda: got.extend(link.poll()) or got)
    assert got == [b"\xf0\x7d\x4e\x42\x01\x41\xf7"]
    link.close()
    assert driver.closed.wait(1) and driver.close_thread is not threading.main_thread()
    with pytest.raises(n.MidiLinkError):
        link.send(STATUS)


def test_stalled_send_never_blocks_and_is_reported():
    driver = FakeDriver()
    link = make_link(driver)
    driver.go.clear()  # the firmware stalls: the driver never completes the SysEx
    _, took = timed(lambda: link.send(STATUS))
    assert took < 0.02 and driver.entered.wait(1)
    assert link.poll() == []  # not yet overdue
    time.sleep(0.15)
    with pytest.raises(n.MidiLinkError, match="not answering: a MIDI send did not complete within 0.1 s"):
        link.poll()
    with pytest.raises(n.MidiLinkError, match="not answering"):
        link.send(STATUS)
    _, took = timed(link.close)
    assert took < 0.2 and link.stuck and not driver.closed.is_set()
    driver.go.set()  # the driver returns at last: the abandoned worker closes the port itself
    assert driver.closed.wait(1)
    assert wait_for(lambda: not link.stuck)


def test_send_that_raises_marks_the_link_lost():
    driver = FakeDriver()
    driver.raise_on_send = RuntimeError(SYSEX_ERROR)
    link = make_link(driver)
    link.send(STATUS)
    assert driver.closed.wait(1)  # the worker closed the port after the driver error
    with pytest.raises(n.MidiLinkError, match="error sending sysex message"):
        link.poll()
    link.close()


def test_queue_is_bounded_while_stalled():
    driver = FakeDriver()
    driver.go.clear()
    link = make_link(driver, send_timeout=60, max_queued=8)
    sent = 0
    start = time.monotonic()
    with pytest.raises(n.MidiLinkError, match="stopped taking messages"):
        for _ in range(100):
            link.send(STATUS)
            sent += 1
    assert time.monotonic() - start < 0.1
    assert 8 <= sent <= 9
    link.close()
    driver.go.set()
    assert driver.closed.wait(1)


def test_open_that_hangs_or_fails_is_bounded():
    gate = threading.Event()
    late = FakeDriver()

    def slow_open():
        gate.wait()
        return late

    start = time.monotonic()
    with pytest.raises(n.MidiLinkError, match="did not complete within 0.1 s"):
        n.ThreadedMidiLink(slow_open, open_timeout=0.1)
    assert time.monotonic() - start < 0.3
    gate.set()
    assert late.closed.wait(1)  # the late port is closed, never used

    def failing_open():
        raise n.MidiLinkError("cannot open MIDI port 'Daisy Seed Built In'")

    with pytest.raises(n.MidiLinkError, match="cannot open MIDI port"):
        n.ThreadedMidiLink(failing_open)


def test_background_scan_never_waits_for_a_hanging_enumeration():
    gate = threading.Event()
    calls = []

    def scan():
        calls.append(1)
        if len(calls) > 1:
            gate.wait()
        return ["Daisy Seed Built In"]

    ports = n.BackgroundScan(scan, wait=0.05)
    assert ports() == ["Daisy Seed Built In"]
    result, took = timed(ports)  # the second scan hangs: the previous result comes back
    assert result == ["Daisy Seed Built In"] and took < 0.2
    gate.set()

    def broken():
        raise OSError("winmm")

    with pytest.raises(n.MidiLinkError, match="scan failed"):
        n.BackgroundScan(broken, wait=0.5)()


# ---- the share workflow over a threaded link to the simulated kit ------------------------
class StallingKit:
    """The simulated Nebulae behind a driver that can stall. The simulator is not thread-safe:
    the worker touches it only under the rig's lock, never while stalled."""

    def __init__(self, rig):
        self.rig = rig
        self.go = threading.Event()
        self.go.set()

    def open(self, name: str) -> n.MidiLink:
        with self.rig.lock:
            inner = self.rig.dev.open_midi(name)
        kit = self

        class Driver:
            def send(self, message):
                kit.go.wait()
                with kit.rig.lock:
                    inner.send(message)

            def poll(self):
                with kit.rig.lock:
                    return inner.poll()

            def close(self):
                with kit.rig.lock:
                    inner.close()

        return n.ThreadedMidiLink(Driver, send_timeout=0.1)


class Rig:
    def __init__(self):
        self.lock = threading.RLock()  # the workflow opens links while the tick holds it
        self.clock = FakeClock()
        self.dev = SimulatedNebulae(self.clock)
        self.session = DeviceSession(self.dev.cdc_ports, lambda port: self.dev.cable.open(port, self.clock),
                                     self.clock, simulated=True)
        self.kit = StallingKit(self)
        self.wf = n.NebulaeWorkflow(self.session, self.dev.midi_ports, self.kit.open, self.clock)
        self.worst = 0.0

    def run(self, seconds: float, step: float = 0.02):
        # Virtual time runs about 4x real time, well inside STATUS_TIMEOUT_S per watchdog.
        for _ in range(int(seconds / step)):
            start = time.monotonic()
            with self.lock:
                self.clock.advance(step)
                self.session.poll()
                self.wf.poll()  # the GUI timer's work: may never wait in the driver
            self.worst = max(self.worst, time.monotonic() - start)
            time.sleep(0.005)

    def until(self, predicate, seconds: float):
        for _ in range(int(seconds / 0.02)):
            if predicate():
                return True
            self.run(0.02)
        return predicate()


def test_workflow_survives_a_stalled_kit_and_finds_it_again():
    r = Rig()
    assert r.until(lambda: r.wf.phase == n.Phase.PERFORMANCE and r.wf.status is not None, 5)
    events = []
    r.kit.go.clear()  # the firmware stalls
    assert r.until(lambda: r.wf.phase == n.Phase.NOT_FOUND, 5)
    events += r.wf.drain_events()
    assert "not answering" in r.wf.message
    assert any("not answering" in str(payload) for _, payload in events)
    # No GUI tick waited in the driver; closing waits at most MIDI_CLOSE_WAIT_S for a worker
    # that queues for the simulator's lock.
    assert r.worst < 0.2
    r.kit.go.set()  # power-cycled / reflashed: the kit answers again
    assert r.until(lambda: r.wf.phase == n.Phase.PERFORMANCE and r.wf.status is not None, 5)
    assert r.worst < 0.2
