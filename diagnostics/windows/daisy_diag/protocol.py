"""Daisy Diagnostic Protocol (DDP) v1 - host implementation.

Specification: protocol/PROTOCOL.md. Mirrors protocol/ddp.cpp.
"""
from __future__ import annotations

import math
import re
from dataclasses import dataclass, field

PROTOCOL_VERSION = 1
MAX_BODY = 400
NUM_BUTTONS = 16
NUM_LEDS = 16
NUM_POTS = 8
LED_MODES = ("OFF", "ON", "MASK", "CHASE", "FOLLOW", "POTDIM")
CV_OUT_MODES = ("OFF", "FIX", "RAMP", "SAW")
NUM_CV = 2
NUM_GATES = 2
NUM_AUDIO = 2
DAC_MAX = 4095
# Audio extension (firmware 0.4.0, capability "audio").
TONE_MODES = ("OFF", "SINE")
TONE_CHANNELS = ("L", "R", "LR")
TONE_MIN_HZ, TONE_MAX_HZ = 20, 20000
TONE_MAX_AMPLITUDE = 5000  # 1/10000 of full scale, about -6 dBFS
TONE_DEFAULT_AMPLITUDE = 1000
SCOPE_MIN_FRAMES, SCOPE_MAX_FRAMES, SCOPE_DEFAULT_FRAMES = 256, 2048, 1024
SCOPE_CHUNK_FRAMES = 40
AUDIO_SAMPLE_RATE = 48000
USB_VID = 0x0483
USB_PID = 0x5740


class ProtocolError(ValueError):
    """A frame was valid but its content could not be interpreted."""


U32_MAX = 0xFFFFFFFF
# Python's int() also accepts '+1', '-1', ' 1' and '1_0'; the protocol does not.
_DEC = re.compile(r"[0-9]{1,10}")
_SIGNED_DEC = re.compile(r"-?[0-9]{1,10}")
_HEX4 = re.compile(r"[0-9A-Fa-f]{4}")
_SIGNED_CV = re.compile(r"-?[0-9]{1,5}")
_GATE_BITS = re.compile(r"[01]{2}")
_PRESS_COUNTS = re.compile(r"[0-9A-Fa-f]{32}")


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no final XOR)."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def is_body_char(byte: int) -> bool:
    return 0x20 <= byte <= 0x7E and byte not in (0x24, 0x2A)  # '$', '*'


def build_frame(body: str) -> bytes:
    raw = body.encode("ascii")
    if not raw or len(raw) > MAX_BODY or not all(is_body_char(b) for b in raw):
        raise ValueError(f"invalid DDP body: {body!r}")
    return b"$" + raw + b"*" + f"{crc16(raw):04X}".encode("ascii") + b"\n"


class FrameParser:
    """Byte-wise receiver with resynchronisation (see PROTOCOL.md, receiver rules)."""

    _HEX = frozenset(b"0123456789ABCDEFabcdef")

    def __init__(self) -> None:
        self.ok = 0
        self.bad_crc = 0
        self.bad_format = 0
        self.overflow = 0
        self._state = "idle"
        self._body = bytearray()
        self._crc = bytearray()

    @property
    def errors(self) -> int:
        return self.bad_crc + self.bad_format + self.overflow

    def feed(self, data: bytes) -> list[str]:
        bodies: list[str] = []
        for byte in data:
            body = self._feed_byte(byte)
            if body is not None:
                bodies.append(body)
        return bodies

    def _feed_byte(self, byte: int) -> str | None:
        if byte == 0x24:  # '$'
            if self._state != "idle":
                self.bad_format += 1
            self._state = "body"
            self._body.clear()
            return None
        state = self._state
        if state == "idle":
            return None
        if state == "body":
            if byte == 0x2A:  # '*'
                self._state = "crc"
                self._crc.clear()
            elif not is_body_char(byte):
                self.bad_format += 1
                self._state = "idle"
            elif len(self._body) >= MAX_BODY:
                self.overflow += 1
                self._state = "idle"
            else:
                self._body.append(byte)
            return None
        if state == "crc":
            if len(self._crc) < 4:
                if byte in self._HEX:
                    self._crc.append(byte)
                else:
                    self.bad_format += 1
                    self._state = "idle"
                return None
            if byte == 0x0D:
                self._state = "cr"
                return None
            if byte == 0x0A:
                return self._finish()
            self.bad_format += 1
            self._state = "idle"
            return None
        # state == "cr"
        if byte == 0x0A:
            return self._finish()
        self.bad_format += 1
        self._state = "idle"
        return None

    def _finish(self) -> str | None:
        self._state = "idle"
        if not self._body:
            self.bad_format += 1
            return None
        if int(self._crc.decode("ascii"), 16) != crc16(bytes(self._body)):
            self.bad_crc += 1
            return None
        self.ok += 1
        return self._body.decode("ascii")


@dataclass
class Message:
    type: str
    fields: dict[str, str]

    def get(self, key: str, default: str | None = None) -> str | None:
        return self.fields.get(key, default)

    def int(self, key: str, low: int = 0, high: int = U32_MAX, signed: bool = False) -> int:
        """Strict decimal field: digits only (optional '-' if signed), within low..high."""
        raw = self.fields.get(key)
        if raw is None or not (_SIGNED_DEC if signed else _DEC).fullmatch(raw):
            raise ProtocolError(f"{self.type}: field {key!r} missing or invalid")
        value = int(raw)
        if not low <= value <= high:
            raise ProtocolError(f"{self.type}: field {key!r}={value} outside {low}..{high}")
        return value

    def hex4(self, key: str) -> int:
        raw = self.fields.get(key)
        if raw is None or not _HEX4.fullmatch(raw):
            raise ProtocolError(f"{self.type}: field {key!r} must be 4 hex digits")
        return int(raw, 16)


def parse_message(body: str) -> Message:
    tokens = [t for t in body.split(" ") if t]
    if not tokens:
        raise ProtocolError("empty message")
    fields: dict[str, str] = {}
    for token in tokens[1:]:
        key, sep, value = token.partition("=")
        if sep:
            fields[key] = value
    return Message(tokens[0], fields)


def format_message(msg_type: str, **fields: object) -> str:
    parts = [msg_type] + [f"{k}={v}" for k, v in fields.items()]
    return " ".join(parts)


@dataclass
class CvGateState:
    """CV IN, GATE IN and CV OUT fields of an ST message (firmware 0.3.0 and later)."""

    cv: list[float] = field(default_factory=lambda: [0.0] * NUM_CV)  # -1.0 .. +1.0
    cv_raw: list[int] = field(default_factory=lambda: [0] * NUM_CV)  # 0 .. 65535
    gates: list[bool] = field(default_factory=lambda: [False] * NUM_GATES)
    gate_counts: list[int] = field(default_factory=lambda: [0] * NUM_GATES)
    cv_out_mode: str = "OFF"
    cv_out: list[int] = field(default_factory=lambda: [0] * NUM_CV)  # DAC codes written


@dataclass
class BoardState:
    """Complete panel state as reported by an ST message."""

    seq: int = 0
    time_ms: int = 0
    buttons: list[bool] = field(default_factory=lambda: [False] * NUM_BUTTONS)
    press_counts: list[int] = field(default_factory=lambda: [0] * NUM_BUTTONS)
    pots: list[float] = field(default_factory=lambda: [0.0] * NUM_POTS)  # 0.0 .. 1.0
    pots_raw: list[int] = field(default_factory=lambda: [0] * NUM_POTS)  # 0 .. 65535
    sw17: str = "D"  # 'U' / 'D'
    sw18: str = "C"  # 'U' / 'C' / 'D'
    sd_card: bool = False
    jack_left: bool = False
    jack_right: bool = False
    led_mode: str = "FOLLOW"
    leds: list[bool] = field(default_factory=lambda: [False] * NUM_LEDS)
    led_brightness: int = 100
    cvgate: CvGateState | None = None  # None: firmware without CV/gate support


def _mask_bits(mask: int, count: int) -> list[bool]:
    return [bool(mask >> i & 1) for i in range(count)]


def _int_list(msg: Message, key: str, count: int, low: int, high: int) -> list[int]:
    raw = msg.get(key)
    if raw is None:
        raise ProtocolError(f"ST: field {key!r} missing")
    parts = raw.split(",")
    if not all(_DEC.fullmatch(v) for v in parts):
        raise ProtocolError(f"ST: field {key!r} invalid")
    values = [int(v) for v in parts]
    if len(values) != count or any(not low <= v <= high for v in values):
        raise ProtocolError(f"ST: field {key!r} must have {count} values in {low}..{high}")
    return values


def _flag(msg: Message, key: str) -> bool:
    value = msg.get(key)
    if value not in ("0", "1"):
        raise ProtocolError(f"ST: field {key!r} must be 0 or 1")
    return value == "1"


_CVGATE_KEYS = ("cv", "cvr", "g", "gc", "co")


def _parse_cvgate(msg: Message) -> CvGateState | None:
    present = [k for k in _CVGATE_KEYS if k in msg.fields]
    if not present:
        return None
    if len(present) != len(_CVGATE_KEYS):
        raise ProtocolError("ST: CV/gate fields must be complete")
    cv_parts = msg.fields["cv"].split(",")
    if len(cv_parts) != NUM_CV or not all(_SIGNED_CV.fullmatch(v) for v in cv_parts):
        raise ProtocolError("ST: field 'cv' invalid")
    cv = [int(v) for v in cv_parts]
    if any(not -10000 <= v <= 10000 for v in cv):
        raise ProtocolError("ST: field 'cv' out of range")
    gates = msg.fields["g"]
    if not _GATE_BITS.fullmatch(gates):
        raise ProtocolError("ST: field 'g' must be two 0/1 digits")
    gc = msg.hex4("gc")
    co = msg.fields["co"].split(",")
    if len(co) != 1 + NUM_CV or co[0] not in CV_OUT_MODES or not all(
            _DEC.fullmatch(v) for v in co[1:]):
        raise ProtocolError("ST: field 'co' invalid")
    codes = [int(v) for v in co[1:]]
    if any(v > DAC_MAX for v in codes):
        raise ProtocolError("ST: field 'co' code out of range")
    return CvGateState(
        cv=[v / 10000.0 for v in cv],
        cv_raw=_int_list(msg, "cvr", NUM_CV, 0, 65535),
        gates=[c == "1" for c in gates],
        gate_counts=[gc >> 8 & 0xFF, gc & 0xFF],
        cv_out_mode=co[0],
        cv_out=codes,
    )


def parse_state(msg: Message) -> BoardState:
    if msg.type != "ST":
        raise ProtocolError(f"expected ST, got {msg.type}")
    pc = msg.get("pc", "")
    if not _PRESS_COUNTS.fullmatch(pc):
        raise ProtocolError("ST: field 'pc' must have 32 hex digits")
    counts = [int(pc[i:i + 2], 16) for i in range(0, len(pc), 2)]
    sw17 = msg.get("s17")
    sw18 = msg.get("s18")
    if sw17 not in ("U", "D") or sw18 not in ("U", "C", "D"):
        raise ProtocolError("ST: invalid switch position")
    led_mode = msg.get("lm")
    if led_mode not in LED_MODES:
        raise ProtocolError(f"ST: unknown LED mode {led_mode!r}")
    buttons = msg.hex4("b")
    leds = msg.hex4("l")
    return BoardState(
        seq=msg.int("n"),
        time_ms=msg.int("t"),
        buttons=_mask_bits(buttons, NUM_BUTTONS),
        press_counts=counts,
        pots=[v / 10000.0 for v in _int_list(msg, "p", NUM_POTS, 0, 10000)],
        pots_raw=_int_list(msg, "r", NUM_POTS, 0, 65535),
        sw17=sw17,
        sw18=sw18,
        sd_card=_flag(msg, "sd"),
        jack_left=_flag(msg, "jl"),
        jack_right=_flag(msg, "jr"),
        led_mode=led_mode,
        leds=_mask_bits(leds, NUM_LEDS),
        led_brightness=msg.int("br", 0, 100),
        cvgate=_parse_cvgate(msg),
    )


def format_state(state: BoardState) -> str:
    """Inverse of parse_state; produces exactly the firmware's ST layout."""
    def mask(bits: list[bool]) -> str:
        return f"{sum(1 << i for i, on in enumerate(bits) if on):04X}"

    return (
        f"ST n={state.seq} t={state.time_ms} b={mask(state.buttons)} "
        f"pc={''.join(f'{c & 0xFF:02X}' for c in state.press_counts)} "
        f"p={','.join(str(pot_to_int(v)) for v in state.pots)} "
        f"r={','.join(str(v) for v in state.pots_raw)} "
        f"s17={state.sw17} s18={state.sw18} sd={int(state.sd_card)} "
        f"jl={int(state.jack_left)} jr={int(state.jack_right)} "
        f"lm={state.led_mode} l={mask(state.leds)} br={state.led_brightness}"
        + ("" if state.cvgate is None else _format_cvgate(state.cvgate))
    )


def _format_cvgate(c: CvGateState) -> str:
    return (
        f" cv={','.join(str(cv_to_int(v)) for v in c.cv)}"
        f" cvr={','.join(str(v) for v in c.cv_raw)}"
        f" g={''.join('1' if g else '0' for g in c.gates)}"
        f" gc={''.join(f'{n & 0xFF:02X}' for n in c.gate_counts)}"
        f" co={c.cv_out_mode},{','.join(str(v) for v in c.cv_out)}"
    )


def cv_to_int(value: float) -> int:
    """Same rounding as diag::NormalizeCv in the firmware."""
    if value != value:
        return 0
    if value <= -1.0:
        return -10000
    if value >= 1.0:
        return 10000
    return int(value * 10000.0 + 0.5) if value >= 0 else -int(-value * 10000.0 + 0.5)


def pot_to_int(value: float) -> int:
    """Same rounding as diag::NormalizePot in the firmware."""
    if not value > 0.0:
        return 0
    if value >= 1.0:
        return 10000
    return int(value * 10000.0 + 0.5)


@dataclass
class DeviceInfo:
    protocol: int
    firmware: str
    app: str
    device: str
    boot: str
    bootloader: str
    libdaisy: str
    capabilities: list[str]
    simulated: bool = False


def parse_hello(msg: Message) -> DeviceInfo:
    return DeviceInfo(
        protocol=msg.int("proto"),
        firmware=msg.get("fw", "?"),
        app=msg.get("app", "?"),
        device=msg.get("dev", "?"),
        boot=msg.get("boot", "?"),
        bootloader=msg.get("bl", "?"),
        libdaisy=msg.get("libdaisy", "?"),
        capabilities=[c for c in msg.get("caps", "").split(",") if c],
        simulated=msg.get("sim") == "1",
    )


@dataclass
class SdInfo:
    status: str  # NONE / MOUNTED / ERROR
    fresult: int
    fs_type: str = "-"
    size_mb: int = 0
    entries: int = 0


@dataclass(frozen=True)
class ReaderInfo:
    state: str
    sectors: int
    busy: bool
    io: str | None = None
    fault: int | None = None
    he: int | None = None
    lba: int | None = None
    ms: int | None = None

    @property
    def diagnostic_text(self) -> str:
        if self.io is None:
            return ""
        faults = ("OK", "unavailable", "start failed", "DMA error", "DMA timeout",
                  "programming timeout", "buffer alignment", "card status error")
        return (f"Last I/O: {self.io}; {faults[self.fault]}; HAL 0x{self.he:08X}; "
                f"sector {self.lba}; elapsed {self.ms} ms.")


def parse_reader(msg: Message) -> ReaderInfo:
    state = msg.get("state")
    if state not in ("LOCAL", "STARTING", "SHARED", "EJECTED", "INTERRUPTED", "ERROR"):
        raise ProtocolError(f"READER: invalid state {state!r}")
    if msg.get("a") not in ("STATUS", "SHARE", "RETURN"):
        raise ProtocolError("READER: invalid or missing action")
    keys = ("io", "fault", "he", "lba", "ms")
    present = [key in msg.fields for key in keys]
    diagnostics = {}
    if any(present):
        if not all(present) or msg.get("io") not in ("NONE", "READ", "WRITE", "SYNC"):
            raise ProtocolError("READER: invalid or incomplete I/O diagnostics")
        diagnostics = dict(io=msg.get("io"), fault=msg.int("fault", 0, 7),
                           he=msg.int("he"), lba=msg.int("lba"), ms=msg.int("ms"))
    return ReaderInfo(state, msg.int("sectors"), bool(msg.int("busy", 0, 1)), **diagnostics)


FRESULT_TEXT = {
    0: "FR_OK",
    1: 'FR_DISK_ERR (read error)',
    2: "FR_INT_ERR",
    3: 'FR_NOT_READY (card not responding)',
    4: "FR_NO_FILE",
    5: "FR_NO_PATH",
    9: "FR_INVALID_OBJECT",
    11: "FR_INVALID_DRIVE",
    12: "FR_NOT_ENABLED",
    13: 'FR_NO_FILESYSTEM (no FAT/exFAT)',
    15: "FR_TIMEOUT",
}


def parse_sd(msg: Message) -> SdInfo:
    status = msg.get("st")
    if status not in ("NONE", "MOUNTED", "ERROR"):
        raise ProtocolError(f"SD: unknown status {status!r}")
    info = SdInfo(status=status, fresult=msg.int("res", -1, 255, signed=True))
    if status == "MOUNTED":
        info.fs_type = msg.get("fs", "?")
        info.size_mb = msg.int("mb")
        info.entries = msg.int("ent")
    return info


@dataclass
class SysInfo:
    uptime_s: int
    control_rate_hz: int
    tx_ok: int
    tx_busy: int
    tx_drop: int
    rx_ok: int
    rx_bad: int
    rate_hz: int


def parse_sys(msg: Message) -> SysInfo:
    return SysInfo(
        uptime_s=msg.int("up"),
        control_rate_hz=msg.int("cbhz"),
        tx_ok=msg.int("txok"),
        tx_busy=msg.int("txbusy"),
        tx_drop=msg.int("txdrop"),
        rx_ok=msg.int("rxok"),
        rx_bad=msg.int("rxbad"),
        rate_hz=msg.int("rate", 10, 100),
    )


# ---- Host -> firmware commands ---------------------------------------------------

def cmd_hello() -> str:
    return f"HELLO proto={PROTOCOL_VERSION}"


def cmd_ping(n: int) -> str:
    return f"PING n={n & 0xFFFFFFFF}"


def cmd_led(mode: str, mask: int | None = None, brightness: int | None = None,
            step_ms: int | None = None) -> str:
    if mode not in LED_MODES:
        raise ValueError(f"unknown LED mode {mode!r}")
    body = f"LED m={mode}"
    if mask is not None:
        body += f" mask={mask & 0xFFFF:04X}"
    if brightness is not None:
        body += f" br={max(0, min(100, int(brightness)))}"
    if step_ms is not None:
        body += f" ms={max(20, min(2000, int(step_ms)))}"
    return body


def cmd_cvout(mode: str, code1: int | None = None, code2: int | None = None,
              period_ms: int | None = None) -> str:
    if mode not in CV_OUT_MODES:
        raise ValueError(f"unknown CV OUT mode {mode!r}")
    body = f"CVOUT m={mode}"
    if code1 is not None:
        body += f" c1={max(0, min(DAC_MAX, int(code1)))}"
    if code2 is not None:
        body += f" c2={max(0, min(DAC_MAX, int(code2)))}"
    if period_ms is not None:
        body += f" ms={max(50, min(10000, int(period_ms)))}"
    return body


def cmd_rate(hz: int) -> str:
    return f"RATE hz={max(10, min(100, int(hz)))}"


def cmd_tone(mode: str, freq_hz: int | None = None, amplitude: int | None = None,
             channels: str | None = None) -> str:
    if mode not in TONE_MODES:
        raise ValueError(f"unknown tone mode {mode!r}")
    if channels is not None and channels not in TONE_CHANNELS:
        raise ValueError(f"unknown tone channels {channels!r}")
    body = f"TONE m={mode}"
    if freq_hz is not None:
        body += f" f={max(TONE_MIN_HZ, min(TONE_MAX_HZ, int(freq_hz)))}"
    if amplitude is not None:
        body += f" a={max(0, min(TONE_MAX_AMPLITUDE, int(amplitude)))}"
    if channels is not None:
        body += f" ch={channels}"
    return body


def cmd_scope(frames: int | None = None, trigger: bool | None = None) -> str:
    body = "SCOPE"
    if frames is not None:
        body += f" n={max(SCOPE_MIN_FRAMES, min(SCOPE_MAX_FRAMES, int(frames)))}"
    if trigger is not None:
        body += f" t={1 if trigger else 0}"
    return body


# ---- Audio extension: AU levels and SC snapshot chunks -----------------------------

@dataclass
class AudioLevels:
    """Peak levels since the previous AU frame, 0.0..1.0 of digital full scale."""
    inputs: list[float]
    outputs: list[float]
    tone_mode: str
    tone_freq_hz: int
    tone_amplitude: int  # 1/10000 FS
    tone_channels: str

    @staticmethod
    def dbfs(level: float) -> float | None:
        """Peak in dBFS, or None for digital silence."""
        if level <= 0.0:
            return None
        return 20.0 * math.log10(level)


def parse_audio(msg: Message) -> AudioLevels:
    if msg.type != "AU":
        raise ProtocolError(f"expected AU, got {msg.type}")
    levels = [msg.int(key, 0, 10000) / 10000.0 for key in ("il", "ir", "ol", "or")]
    tone = msg.get("tone", "").split(",")
    if (len(tone) != 4 or tone[0] not in TONE_MODES or not _DEC.fullmatch(tone[1])
            or not _DEC.fullmatch(tone[2]) or tone[3] not in TONE_CHANNELS):
        raise ProtocolError("AU: field 'tone' invalid")
    freq, amplitude = int(tone[1]), int(tone[2])
    if not TONE_MIN_HZ <= freq <= TONE_MAX_HZ or amplitude > TONE_MAX_AMPLITUDE:
        raise ProtocolError("AU: tone frequency or amplitude out of range")
    return AudioLevels(inputs=levels[:2], outputs=levels[2:], tone_mode=tone[0],
                       tone_freq_hz=freq, tone_amplitude=amplitude, tone_channels=tone[3])


@dataclass
class ScopeChunk:
    index: int
    chunks: int
    frames: int
    samples: list[tuple[int, int]]  # (left, right) int16 per stereo frame


_HEX_RUN = re.compile(r"(?:[0-9A-Fa-f]{8})*")


def parse_scope_chunk(msg: Message) -> ScopeChunk:
    if msg.type != "SC":
        raise ProtocolError(f"expected SC, got {msg.type}")
    index = msg.int("i", 0, 65535)
    chunks = msg.int("k", 1, 65535)
    frames = msg.int("n", SCOPE_MIN_FRAMES, SCOPE_MAX_FRAMES)
    data = msg.get("d", "")
    if index >= chunks or not _HEX_RUN.fullmatch(data):
        raise ProtocolError("SC: invalid chunk index or sample data")
    first = index * SCOPE_CHUNK_FRAMES
    expected = max(0, min(SCOPE_CHUNK_FRAMES, frames - first))
    if len(data) != expected * 8 or (chunks - 1) * SCOPE_CHUNK_FRAMES >= frames:
        raise ProtocolError("SC: sample count does not match n/i/k")

    def s16(text: str) -> int:
        value = int(text, 16)
        return value - 65536 if value >= 32768 else value

    samples = [(s16(data[k:k + 4]), s16(data[k + 4:k + 8])) for k in range(0, len(data), 8)]
    return ScopeChunk(index=index, chunks=chunks, frames=frames, samples=samples)


class ScopeAssembler:
    """Collects SC chunks of one capture; complete() returns the full stereo capture."""

    def __init__(self) -> None:
        self.reset()

    def reset(self) -> None:
        self.frames = 0
        self.chunks = 0
        self._parts: dict[int, list[tuple[int, int]]] = {}

    def add(self, chunk: ScopeChunk) -> list[tuple[int, int]] | None:
        if chunk.index == 0 or chunk.frames != self.frames or chunk.chunks != self.chunks:
            self.reset()
            self.frames, self.chunks = chunk.frames, chunk.chunks
        self._parts[chunk.index] = chunk.samples
        if len(self._parts) < self.chunks:
            return None
        capture = [sample for i in range(self.chunks) for sample in self._parts[i]]
        self.reset()
        return capture if len(capture) == chunk.frames else None
