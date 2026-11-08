"""Looper (firmware 0.5.0): device profile for the SD share workflow of the control panel.

Same two personalities as Nebulae on Daisy (``nebulae.py``), with the looper's own
control SysEx ``F0 7D 4C 50 ...`` (instruments/looper/docs/SD_MANAGEMENT.md):

* Performance: class-compliant USB-MIDI, STATUS and SHARE. The restart reloads the loop
  from ``looper/loop.wav``, so SHARE first answers SAVING while the firmware writes the
  current loop; the workflow asks again until the card is current.
* SD management: USB CDC (DDP v1) + mass storage, HELLO ``app=LooperOnDaisy``, the
  ``LOOPER`` message for status and the return to performance.
"""
from __future__ import annotations

from dataclasses import dataclass

from .nebulae import (CMD_ERROR, CMD_SHARE, CMD_STATUS, ERROR_CODES, REPLY_BIT, SYSEX_VERSION,
                      DeviceProfile, NebulaeError)

APP_NAME = "LooperOnDaisy"
SYSEX_PREFIX = bytes([0xF0, 0x7D, 0x4C, 0x50])
SHARE_RESULTS = {0: "ACCEPTED", 1: "CONFIRM_REQUIRED", 2: "BUSY", 3: "NO_CARD", 4: "BAD_REQUEST",
                 5: "SAVING"}
FAULTS = {0: "none", 1: "card not ready", 2: "write failed", 3: "looper busy", 4: "no WAV files",
          5: "file rejected"}
STATUS_LENGTH = 13


def status_request() -> bytes:
    return SYSEX_PREFIX + bytes([SYSEX_VERSION, CMD_STATUS, 0xF7])


def share_request(confirm: bool, token: bytes) -> bytes:
    if len(token) != 4 or any(b > 0x7F for b in token):
        raise ValueError("token must be four 7-bit bytes")
    return SYSEX_PREFIX + bytes([SYSEX_VERSION, CMD_SHARE, 1 if confirm else 0]) + token + b"\xF7"


@dataclass(frozen=True)
class LooperStatus:
    firmware: str
    card: bool
    loop: bool
    unsaved: bool
    saving: bool
    recording: bool
    importing: bool
    overrun: bool
    save_failed: bool
    fault: int
    loop_ms: int
    overruns: int
    peak_load: int

    @property
    def busy(self) -> bool:
        return self.recording or self.importing

    @property
    def fault_text(self) -> str:
        return FAULTS.get(self.fault, f"code {self.fault}")


def parse_reply(message: bytes) -> tuple[str, object]:
    """("STATUS", LooperStatus) | ("SHARE", (result, token)) | ("ERROR", (command, code))."""
    if len(message) < 8 or message[:4] != SYSEX_PREFIX or message[-1] != 0xF7:
        raise NebulaeError("not a looper SysEx reply")
    body = message[4:-1]
    if any(b > 0x7F for b in body):
        raise NebulaeError("status byte inside SysEx")
    if body[0] != SYSEX_VERSION:
        raise NebulaeError(f"unsupported protocol version {body[0]}")
    command, data = body[1], body[2:]
    if command == CMD_STATUS | REPLY_BIT:
        if len(data) != STATUS_LENGTH:
            raise NebulaeError("STATUS reply has the wrong length")
        if data[3] != 0:
            raise NebulaeError("STATUS from an unknown personality")
        flags = data[4]
        return "STATUS", LooperStatus(
            firmware=f"{data[0]}.{data[1]}.{data[2]}", card=bool(flags & 1), loop=bool(flags & 2),
            unsaved=bool(flags & 4), saving=bool(flags & 8), recording=bool(flags & 16),
            importing=bool(flags & 32), overrun=bool(flags & 64), save_failed=bool(data[5] & 1),
            fault=data[6], loop_ms=data[7] | data[8] << 7 | data[9] << 14,
            overruns=data[10] | data[11] << 7, peak_load=data[12])
    if command == CMD_SHARE | REPLY_BIT:
        if len(data) != 5:
            raise NebulaeError("SHARE reply has the wrong length")
        return "SHARE", (SHARE_RESULTS.get(data[0], f"CODE{data[0]}"), bytes(data[1:5]))
    if command == CMD_ERROR | REPLY_BIT:
        if len(data) != 2:
            raise NebulaeError("ERROR reply has the wrong length")
        return "ERROR", (data[0], ERROR_CODES.get(data[1], f"CODE{data[1]}"))
    raise NebulaeError(f"unknown reply command 0x{command:02X}")


LOOPER = DeviceProfile(
    name="Looper",
    app=APP_NAME,
    capability="loopmgmt",
    perform_command="LOOPER a=PERFORM",
    status_request=status_request,
    share_request=share_request,
    parse_reply=parse_reply,
    texts={
        "busy": "The looper is recording, overdubbing, resampling or importing a WAV file. "
                "Stop it and try again.",
        "confirm": "The loop could not be saved to the card (the last write failed). Sharing "
                   "restarts the looper and LOSES the unsaved changes; looper/loop.wav keeps the "
                   "last saved loop. Confirm to continue, or cancel and keep playing.",
        "back": "Back in performance. The loop was reloaded from looper/loop.wav.",
        "returning": "The looper restarts into performance and reloads looper/loop.wav …",
        "local": "Card is with the looper (not shared). Share it with Windows or return to "
                 "performance.",
        "shared": "Windows owns the card. Copy WAV files into looper/loops or back up "
                  "looper/loop.wav, then use „Safely Remove Hardware“ / Eject in Windows.",
        "ejected": "Windows ejected the card and all writes completed. You can return to the looper.",
        "saving": "Saving the current loop to looper/loop.wav before the restart …",
    },
)
