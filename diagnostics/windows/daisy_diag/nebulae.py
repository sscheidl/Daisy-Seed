"""SD sharing workflow of the control panel for firmwares with SD management:
Nebulae on Daisy 0.2.0 and Looper 0.5.0 (profiles below; common/sdshare/README.md).

Two transports, never at the same time (instruments/granular/docs/V0_2_ARCHITECTURE.md):

* Performance personality: class-compliant USB-MIDI. The panel speaks the control
  SysEx ``F0 7D <p0> <p1> ...`` with the firmware's prefix (Nebulae ``4E 42``,
  Looper ``4C 50``): STATUS and SHARE. There is no COM port in this mode.
* SD management personality: USB CDC (DDP v1, handled by ``DeviceSession``) plus USB
  mass storage. HELLO reports the firmware in ``app=``.

The Looper keeps its loop on the card: its SHARE answers PENDING, saves the loop first
and restarts afterwards; only a failed save needs the user's confirmation.

Qt-free and clock-injected, like ``session.py``. The panel never infers an eject: only
the firmware's ``READER state=EJECTED`` means Windows ejected the card cleanly.
"""
from __future__ import annotations

import os
import time
from dataclasses import dataclass, asdict
from enum import Enum
from typing import Callable, Iterable, Protocol

from .session import DeviceSession

@dataclass(frozen=True)
class Profile:
    key: str      # "nebulae" | "looper"
    name: str     # shown in the panel
    app: str      # HELLO app= of the SD management personality
    prefix: bytes  # two bytes after F0 7D


NEBULAE = Profile("nebulae", "Nebulae on Daisy", "NebulaeOnDaisy", b"NB")
LOOPER = Profile("looper", "Looper", "Looper", b"LP")
PROFILES = (NEBULAE, LOOPER)
APPS = {p.app: p for p in PROFILES}

APP_NAME = NEBULAE.app  # compatibility (app 0.5.0 API)
SYSEX_PREFIX = bytes([0xF0, 0x7D]) + NEBULAE.prefix
SYSEX_VERSION = 0x01
CMD_STATUS = 0x01
CMD_SHARE = 0x02
CMD_ERROR = 0x7F
REPLY_BIT = 0x40
SHARE_RESULTS = {0: "ACCEPTED", 1: "CONFIRM_REQUIRED", 2: "BUSY", 3: "NO_CARD", 4: "BAD_REQUEST",
                 5: "PENDING"}
ERROR_CODES = {1: "VERSION", 2: "UNKNOWN", 3: "LENGTH"}
LOAD_ERRORS = {0: "none", 1: "missing file", 2: "invalid WAV", 3: "file too large",
               4: "SD I/O error", 5: "no card"}

STATUS_INTERVAL_S = 1.0     # performance: STATUS poll while connected
STATUS_TIMEOUT_S = 3.0      # performance: link considered lost without a reply
PROBE_TIMEOUT_S = 1.0       # a MIDI port that does not answer STATUS is not Nebulae
SHARE_TIMEOUT_S = 2.0       # SHARE reply
RESTART_TIMEOUT_S = 20.0    # bootloader skip + USB enumeration on Windows
SAVE_TIMEOUT_S = 130.0      # Looper: saving before the restart (firmware gives up at 120 s)
STORE_FAULTS = {0: "none", 1: "card", 2: "write error (card full?)", 3: "busy", 4: "no files", 5: "file"}
SCAN_INTERVAL_S = 1.0


class NebulaeError(ValueError):
    """Malformed or foreign SysEx."""


def _head(profile: Profile) -> bytes:
    return bytes([0xF0, 0x7D]) + profile.prefix


def status_request(profile: Profile = NEBULAE) -> bytes:
    return _head(profile) + bytes([SYSEX_VERSION, CMD_STATUS, 0xF7])


def share_request(confirm: bool, token: bytes, profile: Profile = NEBULAE) -> bytes:
    if len(token) != 4 or any(b > 0x7F for b in token):
        raise ValueError("token must be four 7-bit bytes")
    return _head(profile) + bytes([SYSEX_VERSION, CMD_SHARE, 1 if confirm else 0]) + token + b"\xF7"


@dataclass(frozen=True)
class PerfStatus:
    firmware: str
    card: bool
    live: bool
    recording: bool
    loading: bool
    overrun: bool
    scanning: bool
    bank: int
    banks: int
    playing_bank: int
    playing_slot: int
    bank_mask: int
    slot_mask: int
    load_error: int
    overruns: int
    peak_load: int

    profile: str = "nebulae"

    @property
    def load_error_text(self) -> str:
        return LOAD_ERRORS.get(self.load_error, f"code {self.load_error}")

    @property
    def blocked(self) -> bool:  # SHARE would answer BUSY
        return self.recording or self.loading

    @property
    def volatile(self) -> bool:  # content that the restart loses
        return self.live


@dataclass(frozen=True)
class LooperStatus:
    firmware: str
    card: bool
    loop: bool
    busy: bool
    saving: bool
    importing: bool
    in_sync: bool
    overrun: bool
    share_pending: bool
    share_failed: bool
    loop_seconds: float
    fault: int
    overruns: int
    peak_load: int
    profile: str = "looper"

    @property
    def fault_text(self) -> str:
        return STORE_FAULTS.get(self.fault, f"code {self.fault}")

    @property
    def blocked(self) -> bool:
        return self.busy or self.importing

    @property
    def volatile(self) -> bool:  # the loop is saved before the restart; only undo is lost
        return False


def _mask(b: bytes) -> int:
    return b[0] | b[1] << 7 | (b[2] & 0x03) << 14


def reply_profile(message: bytes) -> Profile | None:
    for profile in PROFILES:
        if message[:4] == _head(profile):
            return profile
    return None


def parse_reply(message: bytes) -> tuple[str, object]:
    """("STATUS", PerfStatus | LooperStatus) | ("SHARE", (result, token)) | ("ERROR", (cmd, code))."""
    profile = reply_profile(message) if len(message) >= 8 else None
    if profile is None or message[-1] != 0xF7:
        raise NebulaeError("not a SysEx reply of a known firmware")
    body = message[4:-1]
    if any(b > 0x7F for b in body):
        raise NebulaeError("status byte inside SysEx")
    if body[0] != SYSEX_VERSION:
        raise NebulaeError(f"unsupported protocol version {body[0]}")
    command, data = body[1], body[2:]
    if command == CMD_STATUS | REPLY_BIT and profile is LOOPER:
        if len(data) != 12:
            raise NebulaeError("Looper STATUS reply has the wrong length")
        if data[3] != 0:
            raise NebulaeError("STATUS from an unknown personality")
        a, b = data[4], data[5]
        return "STATUS", LooperStatus(
            firmware=f"{data[0]}.{data[1]}.{data[2]}", card=bool(a & 1), loop=bool(a & 2),
            busy=bool(a & 4), saving=bool(a & 8), importing=bool(a & 16), in_sync=bool(a & 32),
            overrun=bool(a & 64), share_pending=bool(b & 1), share_failed=bool(b & 2),
            loop_seconds=(data[6] | data[7] << 7) / 10.0, fault=data[8],
            overruns=data[9] | data[10] << 7, peak_load=data[11])
    if command == CMD_STATUS | REPLY_BIT:
        if len(data) != 19:
            raise NebulaeError("STATUS reply has the wrong length")
        if data[3] != 0:
            raise NebulaeError("STATUS from an unknown personality")
        flags = data[4]
        return "STATUS", PerfStatus(
            firmware=f"{data[0]}.{data[1]}.{data[2]}", card=bool(flags & 1), live=bool(flags & 2),
            recording=bool(flags & 4), loading=bool(flags & 8), overrun=bool(flags & 16),
            scanning=bool(flags & 32), bank=data[5], banks=data[6], playing_bank=data[7],
            playing_slot=data[8], bank_mask=_mask(data[9:12]), slot_mask=_mask(data[12:15]),
            load_error=data[15], overruns=data[16] | data[17] << 7, peak_load=data[18])
    if command == CMD_SHARE | REPLY_BIT:
        if len(data) != 5:
            raise NebulaeError("SHARE reply has the wrong length")
        return "SHARE", (SHARE_RESULTS.get(data[0], f"CODE{data[0]}"), bytes(data[1:5]))
    if command == CMD_ERROR | REPLY_BIT:
        if len(data) != 2:
            raise NebulaeError("ERROR reply has the wrong length")
        return "ERROR", (data[0], ERROR_CODES.get(data[1], f"CODE{data[1]}"))
    raise NebulaeError(f"unknown reply command 0x{command:02X}")


class MidiLink(Protocol):
    def send(self, message: bytes) -> None: ...
    def poll(self) -> list[bytes]: ...  # complete SysEx messages received since last call
    def close(self) -> None: ...


class MidiLinkError(OSError):
    pass


def list_midi_ports() -> list[str]:
    """MIDI output ports that may be a Daisy (identified later by STATUS)."""
    try:
        import rtmidi  # python-rtmidi; optional: without it only SD management works
    except ImportError:
        return []
    out = rtmidi.MidiOut()
    try:
        return [name for name in out.get_ports() if "daisy" in name.lower()]
    finally:
        out.delete()


class RtMidiLink:
    """python-rtmidi in/out pair with the same port name stem. SysEx is enabled."""

    def __init__(self, port_name: str) -> None:
        try:
            import rtmidi
        except ImportError as exc:
            raise MidiLinkError("python-rtmidi is not installed") from exc
        self._out = rtmidi.MidiOut()
        self._in = rtmidi.MidiIn()
        try:
            outs = self._out.get_ports()
            ins = self._in.get_ports()
            stem = port_name.rsplit(" ", 1)[0] if port_name[-1:].isdigit() else port_name
            out_index = outs.index(port_name)
            in_index = next(i for i, name in enumerate(ins) if name.startswith(stem[:20]))
            self._out.open_port(out_index)
            self._in.open_port(in_index)
            self._in.ignore_types(sysex=False, timing=True, active_sense=True)
        except (ValueError, StopIteration, rtmidi.SystemError) as exc:
            self.close()
            raise MidiLinkError(f"cannot open MIDI port {port_name!r}: {exc}") from exc
        self._sysex = bytearray()

    def send(self, message: bytes) -> None:
        try:
            self._out.send_message(list(message))
        except Exception as exc:  # driver gone (device restarted)
            raise MidiLinkError(str(exc)) from exc

    def poll(self) -> list[bytes]:
        result: list[bytes] = []
        while True:
            try:
                item = self._in.get_message()
            except Exception as exc:
                raise MidiLinkError(str(exc)) from exc
            if item is None:
                return result
            data = item[0]
            for byte in data:
                if byte == 0xF0:
                    self._sysex = bytearray([byte])
                elif self._sysex:
                    self._sysex.append(byte)
                    if byte == 0xF7:
                        result.append(bytes(self._sysex))
                        self._sysex = bytearray()

    def close(self) -> None:
        for port in (getattr(self, "_in", None), getattr(self, "_out", None)):
            try:
                if port is not None:
                    port.close_port()
                    port.delete()
            except Exception:
                pass


class Phase(Enum):
    NOT_FOUND = "No Nebulae found"
    PERFORMANCE = "Performance (USB-MIDI)"
    CONFIRM = "Confirmation required"
    REQUESTING = "Requesting SD management …"
    SAVING = "Saving the loop before the restart …"
    RESTARTING = "Restarting into SD management …"
    MANAGEMENT = "SD management (USB CDC + mass storage)"
    RETURNING = "Returning to performance …"


def _token(source: Callable[[int], bytes]) -> bytes:
    return bytes(b & 0x7F for b in source(4))


class NebulaeWorkflow:
    """Drives the share/return workflow. Call ``poll()`` regularly (GUI timer)."""

    def __init__(self, session: DeviceSession,
                 scan_midi: Callable[[], Iterable[str]] = list_midi_ports,
                 open_midi: Callable[[str], MidiLink] = RtMidiLink,
                 clock: Callable[[], float] = time.monotonic,
                 token_source: Callable[[int], bytes] = os.urandom) -> None:
        self.session = session
        self._scan = scan_midi
        self._open = open_midi
        self._clock = clock
        self._tokens = token_source
        self.phase = Phase.NOT_FOUND
        self.status: PerfStatus | LooperStatus | None = None
        self.profile: Profile | None = None  # which firmware answered (or is in management)
        self.port: str | None = None
        self.message = ""
        self.error = ""
        self._link: MidiLink | None = None
        self._probing = False
        self._next_scan = 0.0
        self._last_reply = 0.0
        self._last_status_sent = 0.0
        self._deadline: float | None = None
        self._token = b""
        self._share_after_restart = False
        self._perform_after_return = False
        self._failed_ports: dict[str, float] = {}
        self._events: list[tuple[str, object]] = []

    # ---- queries for the GUI ------------------------------------------------------------
    @property
    def management_connected(self) -> bool:
        s = self.session
        return bool(s.connected and s.device and s.device.app in APPS)

    @property
    def reader_state(self) -> str | None:
        if not self.management_connected or self.session.reader is None:
            return None
        return self.session.reader.state

    @property
    def can_share(self) -> bool:
        return (self.phase in (Phase.PERFORMANCE, Phase.CONFIRM) and self._link is not None
                and self.status is not None and self.status.card and not self.status.blocked)

    @property
    def needs_confirmation(self) -> bool:
        return self.phase == Phase.CONFIRM

    @property
    def can_share_in_management(self) -> bool:
        st = self.session.board
        return (self.management_connected and self.reader_state == "LOCAL" and st is not None
                and st.sd_card and not self.session.reader_pending)

    @property
    def ejected(self) -> bool:
        return self.reader_state == "EJECTED"  # firmware truth only

    @property
    def can_return(self) -> bool:
        return (self.management_connected and not self.session.reader_pending
                and self.phase != Phase.RETURNING and self.reader_state in ("EJECTED", "LOCAL")
                and not (self.session.reader is not None and self.session.reader.busy))

    @property
    def can_acknowledge(self) -> bool:
        return (self.management_connected and not self.session.reader_pending
                and self.reader_state in ("INTERRUPTED", "ERROR"))

    def drain_events(self) -> list[tuple[str, object]]:
        events, self._events = self._events, []
        return events

    def snapshot(self) -> dict:
        return {"phase": self.phase.name, "port": self.port, "message": self.message,
                "error": self.error, "reader": self.reader_state,
                "profile": self.profile.key if self.profile else None,
                "status": asdict(self.status) if self.status else None}

    # ---- actions ----------------------------------------------------------------------
    def request_share(self, confirm: bool = False) -> bool:
        """SD für Windows freigeben. Nebulae answers CONFIRM_REQUIRED while a Live loop exists;
        the Looper answers PENDING (saves first) and CONFIRM_REQUIRED only after a failed save.
        The GUI shows the warning and calls again with confirm."""
        if self.management_connected:
            if not self.can_share_in_management:
                return False
            return self.session.request_reader("SHARE")
        if not self.can_share or (self.phase == Phase.CONFIRM and not confirm):
            return False
        self._token = _token(self._tokens)
        try:
            self._link.send(share_request(confirm, self._token, self.profile or NEBULAE))
        except OSError as exc:
            self._lost(f"MIDI send failed ({exc})")
            return False
        self.phase = Phase.REQUESTING
        self._deadline = self._clock() + SHARE_TIMEOUT_S
        self.message = "Waiting for the firmware to accept SD management …"
        return True

    def cancel_confirmation(self) -> None:
        if self.phase == Phase.CONFIRM:
            self.phase = Phase.PERFORMANCE
            self.message = f"Cancelled: {self._name()} keeps playing; nothing was changed."

    def acknowledge_interruption(self) -> bool:
        """Explicit acknowledgement of an interrupted Windows access (no data recovery)."""
        if not self.can_acknowledge:
            return False
        return self.session.request_reader("RETURN")

    def return_to_performance(self) -> bool:
        """Zurück zu Nebulae: only after Windows ejected (or from LOCAL)."""
        if not self.can_return:
            return False
        if self.reader_state == "EJECTED":
            if not self.session.request_reader("RETURN"):
                return False
            self._perform_after_return = True
            self.message = "Card returned by the firmware; restarting into performance …"
            return True
        return self._send_perform()

    # ---- polling ----------------------------------------------------------------------
    def poll(self) -> None:
        now = self._clock()
        self._poll_management(now)
        self._poll_midi(now)
        if self._deadline is not None and now > self._deadline:
            self._timeout()

    def _send_perform(self) -> bool:
        if not self.session.send("NEB a=PERFORM"):
            return False
        self._perform_after_return = False
        self.phase = Phase.RETURNING
        self._deadline = self._clock() + RESTART_TIMEOUT_S
        self.message = f"{self._name()} restarts into performance and reads the card again …"
        self._event("log", f"{self._name()}: return to performance requested")
        return True

    def _name(self) -> str:
        return self.profile.name if self.profile else "The instrument"

    def _poll_management(self, now: float) -> None:
        if not self.management_connected:
            return
        self.profile = APPS[self.session.device.app]
        if self.phase in (Phase.NOT_FOUND, Phase.RESTARTING, Phase.PERFORMANCE, Phase.SAVING):
            if self.phase == Phase.RESTARTING:
                self._event("log", f"{self._name()}: SD management connected over USB CDC")
            elif self.phase == Phase.NOT_FOUND:
                self._event("log", f"{self._name()} found in SD management mode")
            self._close_link()
            self.phase = Phase.MANAGEMENT
            self._deadline = None
            self.message = ""
        if self.phase != Phase.MANAGEMENT:
            return
        state = self.reader_state
        if self._share_after_restart and state == "LOCAL" and self.can_share_in_management:
            # The user asked for the share before the restart; continue it exactly once.
            self._share_after_restart = False
            if self.session.request_reader("SHARE"):
                self.message = "Opening the card for Windows …"
        if self._perform_after_return and state == "LOCAL" and not self.session.reader_pending:
            self._send_perform()
            return
        self.message = {
            None: "Waiting for the card-reader status …",
            "LOCAL": f"Card is with {self._name()} (not shared). Share it with Windows or return to performance.",
            "STARTING": "Preparing the card for Windows …",
            "SHARED": "Windows owns the card. Copy, rename or delete samples, then use "
                      "„Safely Remove Hardware“ / Eject in Windows.",
            "EJECTED": f"Windows ejected the card and all writes completed. You can return to {self._name()}.",
            "INTERRUPTED": "Access was interrupted (USB reset, card removed or I/O error). Windows may "
                           "have lost pending changes. The card stays quarantined until you acknowledge.",
            "ERROR": "Card I/O failed. Windows may have lost pending changes. The card stays "
                     "quarantined until you acknowledge.",
        }.get(state, self.message)

    def _poll_midi(self, now: float) -> None:
        if self.phase == Phase.MANAGEMENT:
            if not self.session.connected:
                # CDC gone without a requested return: ownership unknown, keep looking.
                self.message = "SD management connection lost; card ownership is unknown."
            return
        if self._link is None:
            # While RESTARTING keep scanning: a device that comes back on USB-MIDI did
            # not enter SD management (boot intent lost, risk R1) and is reported.
            if self.phase == Phase.REQUESTING:
                return
            if now >= self._next_scan:
                self._next_scan = now + SCAN_INTERVAL_S
                self._try_open(now)
            return
        try:
            messages = self._link.poll()
        except OSError as exc:
            self._lost(f"MIDI connection lost ({exc})")
            return
        for message in messages:
            try:
                kind, payload = parse_reply(message)
            except NebulaeError:
                continue  # other SysEx on the same port
            self._last_reply = now
            self._handle(kind, payload, now)
        if self._link is None:
            return
        if self._probing and now - self._last_status_sent > PROBE_TIMEOUT_S:
            self._failed_ports[self.port or ""] = now + 5.0
            self._close_link()
            return
        if self.phase in (Phase.PERFORMANCE, Phase.CONFIRM, Phase.SAVING):
            if now - self._last_reply > STATUS_TIMEOUT_S:
                self._lost(f"{self._name()} stopped answering on USB-MIDI")
                return
            if now - self._last_status_sent >= STATUS_INTERVAL_S:
                self._send_status(now)

    def _try_open(self, now: float) -> None:
        try:
            ports = list(self._scan())
        except Exception as exc:
            self.error = f"MIDI port scan failed: {exc}"
            return
        for name in ports:
            if self._failed_ports.get(name, 0.0) > now:
                continue
            try:
                link = self._open(name)
            except OSError as exc:
                self._failed_ports[name] = now + 5.0
                self.error = str(exc)
                continue
            self._link = link
            self.port = name
            self._probing = True
            self._last_reply = now
            self._send_status(now)
            return

    def _send_status(self, now: float) -> None:
        # While probing an unknown port, ask every known firmware; each one ignores the
        # others' prefixes. Afterwards only the identified firmware is asked.
        profiles = (self.profile,) if self.profile and not self._probing else PROFILES
        try:
            for profile in profiles:
                self._link.send(status_request(profile))
            self._last_status_sent = now
        except OSError as exc:
            self._lost(f"MIDI send failed ({exc})")

    def _handle(self, kind: str, payload, now: float) -> None:
        if kind == "STATUS":
            self._probing = False
            self.profile = NEBULAE if payload.profile == "nebulae" else LOOPER
            self.status = payload
            if self.phase == Phase.SAVING:
                if payload.share_failed:  # the Looper could not save: ask the user
                    self.phase = Phase.CONFIRM
                    self._deadline = None
                    self.error = f"Saving the loop failed ({payload.fault_text})."
                    self.message = ("The current loop could not be saved to the card. Sharing anyway "
                                    "restarts the Looper and LOSES the unsaved change; loop.wav keeps "
                                    "the last saved loop. Confirm, or cancel and free space first.")
                    self._event("log", f"Looper: {self.error}")
                return
            if self.phase in (Phase.NOT_FOUND, Phase.RETURNING, Phase.RESTARTING):
                self.error = ""  # a fresh connection; refusals and timeouts stay visible otherwise
                if self.phase == Phase.RETURNING:
                    self._event("log", f"{self._name()} is back in performance; the card was read again")
                    self.message = ("Back in performance. Banks were rescanned from the card."
                                    if self.profile is NEBULAE else
                                    "Back in performance. The loop was recalled from the card.")
                elif self.phase == Phase.RESTARTING:
                    # R1: the device came back in performance instead of SD management.
                    self.error = (f"{self._name()} restarted but did not enter SD management "
                                  "(boot intent not kept). Nothing was shared.")
                    self._share_after_restart = False
                    self._event("log", self.error)
                else:
                    self._event("log", f"{self._name()} {payload.firmware} found on {self.port}")
                self.phase = Phase.PERFORMANCE
                self._deadline = None
        elif kind == "SHARE":
            result, token = payload
            if self.phase != Phase.REQUESTING or token != self._token:
                return  # stale or foreign reply
            self._deadline = None
            if result == "ACCEPTED":
                self._restarting(now)
            elif result == "PENDING":
                self.phase = Phase.SAVING
                self._share_after_restart = True
                self._deadline = now + SAVE_TIMEOUT_S
                self.message = ("The Looper saves the current loop to the card first, then stops audio "
                                "and restarts into SD management …")
                self._event("log", "Looper: saving the loop before SD management")
            elif result == "CONFIRM_REQUIRED":
                self.phase = Phase.CONFIRM
                self.message = ("A recorded Live loop exists only in RAM and will be LOST by the "
                                "restart. Confirm to continue, or cancel and keep playing."
                                if self.profile is not LOOPER else
                                "The last save failed. Sharing anyway LOSES the unsaved change; "
                                "loop.wav keeps the last saved loop. Confirm or cancel.")
            else:
                self.phase = Phase.PERFORMANCE
                busy = ("Nebulae is recording or loading a sample." if self.profile is not LOOPER
                        else "The Looper is recording, resampling or importing.")
                self.error = {
                    "BUSY": f"{busy} Stop it and try again.",
                    "NO_CARD": "No microSD card in the slot.",
                }.get(result, f"Request refused ({result}).")
                self._event("log", f"{self._name()} refused SD management: {result}")
        elif kind == "ERROR":
            command, code = payload
            self.error = f"Firmware rejected command 0x{command:02X}: {code}"
            if self.phase == Phase.REQUESTING:
                self.phase = Phase.PERFORMANCE
                self._deadline = None

    def _restarting(self, now: float) -> None:
        self.phase = Phase.RESTARTING
        self._share_after_restart = True
        self._deadline = now + RESTART_TIMEOUT_S
        self.message = (f"{self._name()} stops audio and restarts into SD management; Windows will "
                        "install/open the drive …")
        self._event("log", f"{self._name()} accepted SD management; restarting")
        self._close_link()

    def _timeout(self) -> None:
        phase = self.phase
        self._deadline = None
        if phase == Phase.REQUESTING:
            self.phase = Phase.PERFORMANCE
            self.error = "No answer to the share request; nothing was changed."
        elif phase == Phase.SAVING:
            self.phase = Phase.PERFORMANCE
            self._share_after_restart = False
            self.error = (f"The Looper did not finish saving within {SAVE_TIMEOUT_S:.0f} s; "
                          "nothing was shared.")
        elif phase == Phase.RESTARTING:
            self.phase = Phase.NOT_FOUND
            self._share_after_restart = False
            self.error = (f"{self._name()} did not reappear in SD management within "
                          f"{RESTART_TIMEOUT_S:.0f} s. Check the USB cable; power-cycling "
                          "always starts the performance mode.")
        elif phase == Phase.RETURNING:
            self.phase = Phase.NOT_FOUND
            self.error = (f"{self._name()} did not reappear on USB-MIDI within "
                          f"{RESTART_TIMEOUT_S:.0f} s.")
        self._event("log", f"{self._name()}: {self.error}")

    def _lost(self, reason: str) -> None:
        self._close_link()
        if self.phase == Phase.SAVING:  # the save finished and the Looper restarted
            self._restarting(self._clock())
            return
        if self.phase in (Phase.PERFORMANCE, Phase.CONFIRM, Phase.REQUESTING):
            self.phase = Phase.NOT_FOUND
            self.status = None
            self.message = reason
            self._event("log", f"{self._name()}: {reason}")

    def _close_link(self) -> None:
        if self._link is not None:
            try:
                self._link.close()
            except Exception:
                pass
        self._link = None
        self._probing = False

    def _event(self, kind: str, payload: object) -> None:
        self._events.append((kind, payload))
