"""Simulated Nebulae on Daisy 0.2.0: performance personality on a simulated USB-MIDI
port, SD management personality on the simulated CDC cable. Mirrors the firmware rules
(instruments/granular/src/control_sysex.h and admin.cpp); simulation only, no drive is
created on Windows.
"""
from __future__ import annotations

from . import protocol as p
from .nebulae import (APP_NAME, CMD_ERROR, CMD_SHARE, CMD_STATUS, REPLY_BIT, SYSEX_PREFIX,
                      SYSEX_VERSION, MidiLinkError)
from .simulator import SimulatedCable, SimulatedDevice

FIRMWARE = (0, 2, 0)
MIDI_PORT = "Daisy Seed Built In"
RESTART_S = 0.8  # simulated reset + bootloader skip + enumeration


class SimulatedNebulaeManagement(SimulatedDevice):
    """DDP side of the SD management personality."""

    def __init__(self, owner: "SimulatedNebulae") -> None:
        super().__init__(reader_supported=True, audio_supported=False)
        self.owner = owner
        self.inputs.sd_card = owner.card
        self.sd_filesystem = "EXFAT"

    def _neb(self, action: str) -> str:
        ret = 1 if self.reader_state == "LOCAL" else 0
        return (f"NEB mode=SDMGMT fw={'.'.join(map(str, FIRMWARE))}-sim card={int(self.inputs.sd_card)} "
                f"ret={ret} bank={self.owner.bank} slot={self.owner.slot} a={action}")

    def _send_sys(self, now: float) -> None:
        super()._send_sys(now)
        self._send(self._neb("STATUS"))

    def _handle(self, body: str, now: float) -> None:
        msg = p.parse_message(body)
        t = msg.type
        if t == "HELLO" and msg.get("proto") == str(self.protocol_version):
            self._expire(now)
            self._last_rx = now
            self._streaming = True
            self._last_sent = None
            self._send(f"HELLO proto={self.protocol_version} fw={'.'.join(map(str, FIRMWARE))}-sim "
                       f"app={APP_NAME} dev=seed3-desktop boot=BOOT_SRAM bl=v6_1 libdaisy=f715e48 "
                       "caps=btn,pot,potraw,sw17,sw18,sd,boot,sdmsc,nebmgmt sim=1")
            self._send_state(now)
            self._send_reader("STATUS")
            self._send(self._neb("STATUS"))
            return
        if t == "NEB":
            self._expire(now)
            self._last_rx = now
            action = msg.get("a")
            if not self._streaming:
                self._send("ERR c=NEB e=SESSION")
            elif action == "STATUS":
                self._send(self._neb("STATUS"))
            elif action == "PERFORM":
                if self.reader_state != "LOCAL":
                    self._send("ERR c=NEB e=STATE")
                else:
                    self._send(self._neb("PERFORM"))
                    self.owner.restart("PERF", now)
            else:
                self._send("ERR c=NEB e=ARG")
            return
        if t in ("LED", "SDTEST", "CVOUT", "TONE", "SCOPE"):
            self._expire(now)
            self._last_rx = now
            self._send(f"ERR c={t} e=UNSUPPORTED")
            return
        super()._handle(body, now)


class SimulatedMidiLink:
    def __init__(self, owner: "SimulatedNebulae") -> None:
        self.owner = owner
        self._pending: list[bytes] = []
        self.closed = False

    def _check(self) -> None:
        self.owner.advance()
        if self.closed or self.owner.personality != "PERF" or self.owner.restarting:
            raise MidiLinkError("simulated USB-MIDI device disconnected")

    def send(self, message: bytes) -> None:
        self._check()
        reply = self.owner.handle_sysex(message)
        if reply:
            self._pending.append(reply)

    def poll(self) -> list[bytes]:
        # A reply sent just before the reset (the SHARE ACK) still reaches the host.
        if self._pending and not self.closed:
            out, self._pending = self._pending, []
            return out
        self._check()
        return []

    def close(self) -> None:
        self.closed = True


class SimulatedNebulae:
    """Whole device. `cable` is the CDC side used by DeviceSession."""

    def __init__(self, clock) -> None:
        self._clock = clock
        self.personality = "PERF"
        self.restarting = False
        self._switch_at: float | None = None
        self._target = "PERF"
        self.card = True
        self.live = False
        self.recording = False
        self.loading = False
        self.bank, self.banks, self.slot = 1, 3, 1
        self.overruns = 0
        self.restarts = 0
        self.cable = SimulatedCable(lambda: SimulatedNebulaeManagement(self))
        self.cable.unplug()  # no COM port in the performance personality

    # ---- time and personality ---------------------------------------------------------
    def advance(self) -> None:
        if self._switch_at is not None and self._clock() >= self._switch_at:
            self._switch_at = None
            self.restarting = False
            self.personality = self._target
            self.live = self.recording = False  # RAM content is gone after any reset
            if self.personality == "MGMT":
                self.cable.plug()
                self.cable.device.inputs.sd_card = self.card

    def restart(self, target: str, now: float) -> None:
        self.restarts += 1
        self.restarting = True
        self._target = target
        self._switch_at = now + RESTART_S
        if target == "PERF":
            self.cable.unplug()

    def midi_ports(self) -> list[str]:
        self.advance()
        return [MIDI_PORT] if self.personality == "PERF" and not self.restarting else []

    def open_midi(self, name: str) -> SimulatedMidiLink:
        if name not in self.midi_ports():
            raise MidiLinkError(f"cannot open {name}")
        return SimulatedMidiLink(self)

    def cdc_ports(self):
        self.advance()
        return self.cable.ports()

    # ---- control SysEx, as instruments/granular/src/control_sysex.h ----------------------
    def handle_sysex(self, msg: bytes) -> bytes | None:
        if len(msg) < 7 or msg[:4] != SYSEX_PREFIX or msg[-1] != 0xF7:
            return None
        body = msg[4:-1]
        version, cmd = body[0], body[1]
        head = SYSEX_PREFIX + bytes([SYSEX_VERSION])
        if version != SYSEX_VERSION:
            return head + bytes([CMD_ERROR | REPLY_BIT, cmd, 1, 0xF7])
        if cmd == CMD_STATUS and len(body) == 2:
            flags = (int(self.card) | (2 if self.live else 0) | (4 if self.recording else 0)
                     | (8 if self.loading else 0) | (16 if self.overruns else 0))
            mask = (1 << self.banks) - 1
            data = [*FIRMWARE, 0, flags, self.bank, self.banks, self.bank, self.slot,
                    mask & 0x7F, mask >> 7 & 0x7F, mask >> 14 & 3, 0x7F, 0x7F, 3, 0,
                    self.overruns & 0x7F, self.overruns >> 7 & 0x7F, 35]
            return head + bytes([CMD_STATUS | REPLY_BIT, *data, 0xF7])
        if cmd == CMD_SHARE:
            if len(body) != 7 or body[2] > 1:
                return head + bytes([CMD_SHARE | REPLY_BIT, 4, 0, 0, 0, 0, 0xF7])
            confirm = body[2] == 1
            if self.recording or self.loading:
                result = 2
            elif not self.card:
                result = 3
            elif self.live and not confirm:
                result = 1
            else:
                result = 0
            if result == 0:
                self.restart("MGMT", self._clock())
            return head + bytes([CMD_SHARE | REPLY_BIT, result, *body[3:7], 0xF7])
        return head + bytes([CMD_ERROR | REPLY_BIT, cmd, 2, 0xF7])
