"""Simulated Looper 0.5.0: performance personality on a simulated USB-MIDI port, SD
management personality on the simulated CDC cable. Mirrors the firmware rules
(instruments/looper/src/control_sysex.h, sd_admin.cpp, loop_store SaveSoon); simulation
only, no drive is created on Windows.
"""
from __future__ import annotations

from . import protocol as p
from .looper import APP_NAME, SYSEX_PREFIX
from .nebulae import CMD_ERROR, CMD_SHARE, CMD_STATUS, REPLY_BIT, SYSEX_VERSION
from .nebulae_sim import SimulatedNebulae, SimulatedNebulaeManagement
from .simulator import SimulatedCable

FIRMWARE = (0, 5, 0)
SAVE_S = 0.6  # simulated write of the loop to looper/loop.wav


class SimulatedLooperManagement(SimulatedNebulaeManagement):
    """DDP side of the looper's SD management personality."""

    def __init__(self, owner: "SimulatedLooper") -> None:
        super().__init__(owner)
        self.sd_filesystem = "FAT32"

    def _neb(self, action: str) -> str:  # the status line the base class sends with SYS
        return self._looper(action)

    def _looper(self, action: str) -> str:
        ret = 1 if self.reader_state == "LOCAL" else 0
        return (f"LOOPER mode=SDMGMT fw={'.'.join(map(str, FIRMWARE))}-sim "
                f"card={int(self.inputs.sd_card)} ret={ret} a={action}")

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
                       "caps=btn,pot,potraw,sw17,sw18,sd,boot,sdmsc,loopmgmt sim=1")
            self._send_state(now)
            self._send_reader("STATUS")
            self._send(self._looper("STATUS"))
            return
        if t == "LOOPER":
            self._expire(now)
            self._last_rx = now
            action = msg.get("a")
            if not self._streaming:
                self._send("ERR c=LOOPER e=SESSION")
            elif action == "STATUS":
                self._send(self._looper("STATUS"))
            elif action == "PERFORM":
                if self.reader_state != "LOCAL":
                    self._send("ERR c=LOOPER e=STATE")
                else:
                    self._send(self._looper("PERFORM"))
                    self.owner.restart("PERF", now)
            else:
                self._send("ERR c=LOOPER e=ARG")
            return
        if t == "NEB":
            self._expire(now)
            self._last_rx = now
            self._send("ERR c=NEB e=UNKNOWN")
            return
        super()._handle(body, now)


class SimulatedLooper(SimulatedNebulae):
    """Whole looper. `cable` is the CDC side used by DeviceSession."""

    def __init__(self, clock) -> None:
        super().__init__(clock)
        self.loop_ms = 12500
        self.loop = True
        self.unsaved = False
        self.saving_until: float | None = None
        self.save_fails = False  # the card cannot be written
        self.save_failed = False  # the last save failed (firmware: Fault::Write)
        self.importing = False
        self.cable = SimulatedCable(lambda: SimulatedLooperManagement(self))
        self.cable.unplug()

    def advance(self) -> None:
        super().advance()
        if self.saving_until is not None and self._clock() >= self.saving_until:
            self.saving_until = None
            self.save_failed = self.save_fails
            if not self.save_fails:
                self.unsaved = False
        if self.personality == "MGMT":
            self.unsaved = False  # after a reset the loop is the one in loop.wav

    @property
    def saving(self) -> bool:
        return self.saving_until is not None

    # ---- control SysEx, as instruments/looper/src/control_sysex.h -------------------------
    def handle_sysex(self, msg: bytes) -> bytes | None:
        self.advance()
        if len(msg) < 7 or msg[:4] != SYSEX_PREFIX or msg[-1] != 0xF7:
            return None  # e.g. Nebulae's STATUS while the panel probes the port
        body = msg[4:-1]
        version, cmd = body[0], body[1]
        head = SYSEX_PREFIX + bytes([SYSEX_VERSION])
        if version != SYSEX_VERSION:
            return head + bytes([CMD_ERROR | REPLY_BIT, cmd, 1, 0xF7])
        if cmd == CMD_STATUS and len(body) == 2:
            flags = (int(self.card) | (2 if self.loop else 0) | (4 if self.unsaved else 0)
                     | (8 if self.saving else 0) | (16 if self.recording else 0)
                     | (32 if self.importing else 0) | (64 if self.overruns else 0))
            failed = self.save_failed
            ms = self.loop_ms if self.loop else 0
            data = [*FIRMWARE, 0, flags, int(failed), 2 if failed else 0,
                    ms & 0x7F, ms >> 7 & 0x7F, ms >> 14 & 0x7F,
                    self.overruns & 0x7F, self.overruns >> 7 & 0x7F, 22]
            return head + bytes([CMD_STATUS | REPLY_BIT, *data, 0xF7])
        if cmd == CMD_SHARE:
            if len(body) != 7 or body[2] > 1:
                return head + bytes([CMD_SHARE | REPLY_BIT, 4, 0, 0, 0, 0, 0xF7])
            confirm = body[2] == 1
            if self.recording or self.importing:
                result = 2
            elif not self.card:
                result = 3
            elif self.unsaved and not confirm:
                if self.save_failed and not self.saving:
                    result = 1
                else:
                    result = 5
                    if not self.saving:
                        self.saving_until = self._clock() + SAVE_S
            else:
                result = 0
            if result == 0:
                self.saving_until = None
                self.restart("MGMT", self._clock())
            return head + bytes([CMD_SHARE | REPLY_BIT, result, *body[3:7], 0xF7])
        return head + bytes([CMD_ERROR | REPLY_BIT, cmd, 2, 0xF7])
