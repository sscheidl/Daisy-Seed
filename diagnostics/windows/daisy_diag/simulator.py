"""Simulated Seed3 Desktop Dev Kit running the diagnostic firmware's DDP behaviour.

The simulator speaks the real wire protocol (frames with CRC), so the GUI and the
connection logic run exactly the same code paths as with hardware. Every HELLO
from the simulator carries ``sim=1`` and the GUI labels all data as simulated.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from . import protocol as p

HOST_TIMEOUT_S = 3.0
MIN_FRAME_SPACING_S = 0.010


class LinkError(OSError):
    'Raised by links when the device vanished (USB unplugged, port closed).'


@dataclass
class SimulatedInputs:
    buttons: list[bool] = field(default_factory=lambda: [False] * p.NUM_BUTTONS)
    press_counts: list[int] = field(default_factory=lambda: [0] * p.NUM_BUTTONS)
    pots: list[float] = field(default_factory=lambda: [0.0] * p.NUM_POTS)
    sw17: str = "D"
    sw18: str = "C"
    sd_card: bool = False
    jack_left: bool = False
    jack_right: bool = False
    cv: list[float] = field(default_factory=lambda: [0.0] * p.NUM_CV)  # -1.0 .. +1.0
    gates: list[bool] = field(default_factory=lambda: [False] * p.NUM_GATES)
    gate_counts: list[int] = field(default_factory=lambda: [0] * p.NUM_GATES)

    def copy(self) -> "SimulatedInputs":
        return SimulatedInputs(list(self.buttons), list(self.press_counts), list(self.pots),
                               self.sw17, self.sw18, self.sd_card, self.jack_left,
                               self.jack_right, list(self.cv), list(self.gates),
                               list(self.gate_counts))

    def digital(self) -> tuple:
        return (tuple(self.buttons), tuple(self.press_counts), self.sw17, self.sw18,
                self.sd_card, self.jack_left, self.jack_right, tuple(self.gates),
                tuple(self.gate_counts))


class SimulatedDevice:
    """Device side of DDP v1, modelled after firmware/src."""

    def __init__(self, protocol_version: int = p.PROTOCOL_VERSION, reader_supported: bool = True,
                 audio_supported: bool = True) -> None:
        self.protocol_version = protocol_version
        self.reader_supported = reader_supported
        self.audio_supported = audio_supported  # False models firmware before 0.4.0
        self.reader_state = "LOCAL"
        self.in_bootloader = False  # BOOT accepted: the firmware has reset into the bootloader
        self.bootloader_entries = 0
        self.reader_sectors = 0
        self.reader_init_duration = 0.0
        self.inputs = SimulatedInputs()
        # SD card content for SDTEST: file system type, or None for "no file system".
        self.sd_filesystem: str | None = "FAT32"
        self.sd_size_mb = 30436
        self.sd_entries = 12
        self.led_mode = "FOLLOW"
        self.led_mask = 0
        self.led_brightness = 100
        self.chase_ms = 120
        self.cv_mode = "OFF"
        self.cv_code = [0, 0]
        self.cv_period_ms = 1000
        # Simulated patch cables CV OUT n -> CV IN n. Code 0..4095 is shown as CV IN
        # 0.0..+1.0; this is a simulation assumption, not the hardware's scaling.
        self.cv_patched = False
        # Audio extension (0.4.0): test tone, peak meters, snapshot capture. With
        # `audio_patched` the tone is fed back to both inputs (simulation assumption).
        self.tone_mode = "OFF"
        self.tone_freq_hz = 1000
        self.tone_amplitude = p.TONE_DEFAULT_AMPLITUDE
        self.tone_channels = "LR"
        self.audio_patched = False
        self.input_noise = 0.0  # peak of a pseudo-random input signal, 0.0..1.0
        self._last_audio = -1e9
        self._scope_pending: tuple[int, bool] | None = None  # (frames, trigger)
        self.rate_hz = 50
        self.responsive = True  # False: behaves like a hung firmware (no output)
        self.corrupt_next_frame = False
        self.sd_test_duration = 0.0  # seconds the firmware main loop blocks in SDTEST
        self._busy_until = -1e9
        # Like the firmware's 1 KiB USB receive ring: bytes arriving while the main loop
        # is blocked wait here and are processed afterwards; excess bytes are lost.
        self._rx_pending = bytearray()
        self.rx_overflow = 0
        self._keepalive_after_block = False
        self._parser = p.FrameParser()
        self._out = bytearray()
        self._streaming = False
        self._last_rx = -1e9
        self._last_state = -1e9
        self._last_sys = -1e9
        self._last_sent: tuple | None = None
        self._seq = 0
        self._boot_time: float | None = None
        self.tx_ok = 0

    # ---- control surface used by the GUI / tests --------------------------------
    def set_button(self, index: int, pressed: bool) -> None:
        if pressed and not self.inputs.buttons[index]:
            self.inputs.press_counts[index] = (self.inputs.press_counts[index] + 1) & 0xFF
        self.inputs.buttons[index] = pressed

    def set_pot(self, index: int, value: float) -> None:
        self.inputs.pots[index] = min(1.0, max(0.0, value))

    def set_cv(self, index: int, value: float) -> None:
        self.inputs.cv[index] = min(1.0, max(-1.0, value))

    def set_gate(self, index: int, high: bool) -> None:
        if high and not self.inputs.gates[index]:
            self.inputs.gate_counts[index] = (self.inputs.gate_counts[index] + 1) & 0xFF
        self.inputs.gates[index] = high

    def pulse_gate(self, index: int) -> None:
        """A trigger shorter than one state frame: only the counter changes."""
        self.set_gate(index, True)
        self.set_gate(index, False)

    def cv_out(self, now: float) -> list[int]:
        """DAC codes as diag::ComputeCvOut computes them."""
        if self.cv_mode == "OFF":
            return [0, 0]
        if self.cv_mode == "FIX":
            return list(self.cv_code)
        period = max(1, self.cv_period_ms)
        phase = int(self._uptime(now) * 1000) % period
        if self.cv_mode == "SAW":
            value = phase * p.DAC_MAX // max(1, period - 1)
        else:  # RAMP: triangle
            half = max(1, period // 2)
            value = (phase * p.DAC_MAX // half if phase < half
                     else (period - phase) * p.DAC_MAX // (period - half))
        value = min(p.DAC_MAX, value)
        return [value, p.DAC_MAX - value]

    def set_sw17(self, position: str) -> None:
        assert position in ("U", "D")
        self.inputs.sw17 = position

    def set_sw18(self, position: str) -> None:
        assert position in ("U", "C", "D")
        self.inputs.sw18 = position

    # ---- host <-> device byte streams ----------------------------------------------
    RX_RING_SIZE = 1024

    def receive(self, data: bytes, now: float) -> None:
        self._boot(now)
        free = self.RX_RING_SIZE - len(self._rx_pending)
        self._rx_pending += data[:free]
        self.rx_overflow += max(0, len(data) - free)
        self._process_rx(now)

    def _process_rx(self, now: float) -> None:
        if now >= self._busy_until and self._keepalive_after_block:
            # As in the firmware: the blocked time does not count as host silence.
            self._keepalive_after_block = False
            if self._streaming:
                self._last_rx = now
        # Byte by byte, like the firmware main loop: an SDTEST blocks everything after it.
        while self._rx_pending and now >= self._busy_until:
            byte = bytes(self._rx_pending[:1])
            del self._rx_pending[:1]
            for body in self._parser.feed(byte):
                self._handle(body, now)

    def output(self, now: float) -> bytes:
        """Returns all bytes the device sent since the previous call."""
        self._boot(now)
        if not self.inputs.sd_card and self.reader_state == "SHARED":
            self.reader_state = "INTERRUPTED"
        if now < self._busy_until:
            return b""  # main loop blocked in the SD driver: nothing is sent
        self._process_rx(now)
        if self.responsive:
            self._run_main_loop(now)
        data = bytes(self._out)
        self._out.clear()
        return data if self.responsive else b""

    # ---- internals -----------------------------------------------------------------
    def _boot(self, now: float) -> None:
        if self._boot_time is None:
            self._boot_time = now

    def _uptime(self, now: float) -> float:
        return 0.0 if self._boot_time is None else now - self._boot_time

    def _send(self, body: str) -> None:
        frame = bytearray(p.build_frame(body))
        if self.corrupt_next_frame:
            frame[1] ^= 0x01  # still printable, CRC no longer matches
            self.corrupt_next_frame = False
        self._out += frame
        self.tx_ok += 1

    def host_active(self, now: float) -> bool:
        self._expire(now)
        return self._streaming

    def _expire(self, now: float) -> None:
        if self._streaming and now - self._last_rx >= HOST_TIMEOUT_S:
            self._streaming = False  # only a new HELLO restarts the stream
            self.cv_mode = "OFF"  # as in the firmware: no voltage without a session
            self.tone_mode = "OFF"
            self._scope_pending = None

    def computed_leds(self, now: float) -> int:
        if self.led_mode == "OFF":
            return 0
        if self.led_mode in ("ON", "POTDIM"):
            return 0xFFFF
        if self.led_mode == "MASK":
            return self.led_mask
        if self.led_mode == "CHASE":
            elapsed_ms = int(self._uptime(now) * 1000)
            return 1 << (elapsed_ms // self.chase_ms % p.NUM_LEDS)
        return sum(1 << i for i, b in enumerate(self.inputs.buttons) if b)

    def effective_brightness(self) -> int:
        """LED brightness in percent: the setting, or VR1 in POTDIM (diag::LedBrightness)."""
        if self.led_mode != "POTDIM":
            return self.led_brightness
        return min(100, (p.pot_to_int(self.inputs.pots[0]) + 50) // 100)

    # ---- audio extension --------------------------------------------------------
    def tone_sample(self, index: int) -> float:
        import math
        if self.tone_mode != "SINE":
            return 0.0
        return (self.tone_amplitude / 10000.0) * math.sin(
            2.0 * math.pi * self.tone_freq_hz * index / p.AUDIO_SAMPLE_RATE)

    def input_sample(self, channel: int, index: int) -> float:
        """Simulated input: the patched-back tone plus deterministic pseudo noise."""
        value = self.tone_sample(index) if self.audio_patched and (
            self.tone_channels == "LR" or self.tone_channels == ("L", "R")[channel]) else 0.0
        if self.input_noise > 0.0:
            # Deterministic, cheap pseudo-random sequence (no RNG state to keep).
            h = (index * 2654435761 + channel * 40503) & 0xFFFFFFFF
            value += self.input_noise * ((h >> 8) / float(1 << 24) * 2.0 - 1.0)
        return max(-1.0, min(1.0, value))

    def audio_levels(self) -> p.AudioLevels:
        out = (self.tone_amplitude / 10000.0) if self.tone_mode == "SINE" else 0.0
        outs = [out if self.tone_channels in ("L", "LR") else 0.0,
                out if self.tone_channels in ("R", "LR") else 0.0]
        ins = [min(1.0, max(abs(self.input_sample(ch, i)) for i in range(0, 480, 7)))
               for ch in range(p.NUM_AUDIO)]
        return p.AudioLevels(inputs=ins, outputs=outs, tone_mode=self.tone_mode,
                             tone_freq_hz=self.tone_freq_hz, tone_amplitude=self.tone_amplitude,
                             tone_channels=self.tone_channels)

    def _send_audio(self) -> None:
        a = self.audio_levels()
        self._send(f"AU il={p.pot_to_int(a.inputs[0])} ir={p.pot_to_int(a.inputs[1])} "
                   f"ol={p.pot_to_int(a.outputs[0])} or={p.pot_to_int(a.outputs[1])} "
                   f"tone={a.tone_mode},{a.tone_freq_hz},{a.tone_amplitude},{a.tone_channels}")

    def _send_scope(self, frames: int, trigger: bool) -> None:
        """The whole capture at once; the firmware spreads the chunks over a few steps."""
        start = 0
        if trigger:  # rising zero crossing of the left input, like the firmware
            prev = self.input_sample(0, 0)
            for i in range(1, 480):
                cur = self.input_sample(0, i)
                if prev < 0.0 <= cur:
                    start = i
                    break
                prev = cur
        chunks = (frames + p.SCOPE_CHUNK_FRAMES - 1) // p.SCOPE_CHUNK_FRAMES
        for k in range(chunks):
            parts = []
            for f in range(k * p.SCOPE_CHUNK_FRAMES, min(frames, (k + 1) * p.SCOPE_CHUNK_FRAMES)):
                for ch in range(p.NUM_AUDIO):
                    v = int(round(self.input_sample(ch, start + f) * 32767.0))
                    parts.append(f"{max(-32768, min(32767, v)) & 0xFFFF:04X}")
            self._send(f"SC i={k} k={chunks} n={frames} d={''.join(parts)}")

    def _handle_tone(self, msg: p.Message) -> None:
        mode = msg.get("m")
        try:
            if mode not in p.TONE_MODES:
                raise ValueError
            f = self._opt_int(msg, "f", self.tone_freq_hz, p.TONE_MIN_HZ, p.TONE_MAX_HZ)
            a = self._opt_int(msg, "a", self.tone_amplitude, 0, p.TONE_MAX_AMPLITUDE)
            ch = msg.get("ch", self.tone_channels)
            if ch not in p.TONE_CHANNELS:
                raise ValueError
        except ValueError:
            self._send("ERR c=TONE e=ARG")
            return
        self.tone_mode, self.tone_freq_hz, self.tone_amplitude, self.tone_channels = mode, f, a, ch
        self._send(f"ACK c=TONE m={mode} f={f} a={a} ch={ch}")

    def _handle_scope(self, msg: p.Message) -> None:
        try:
            n = self._opt_int(msg, "n", p.SCOPE_DEFAULT_FRAMES, p.SCOPE_MIN_FRAMES, p.SCOPE_MAX_FRAMES)
            t = self._opt_int(msg, "t", 1, 0, 1)
        except ValueError:
            self._send("ERR c=SCOPE e=ARG")
            return
        if self._scope_pending is not None:
            self._send("ERR c=SCOPE e=BUSY")
            return
        chunks = (n + p.SCOPE_CHUNK_FRAMES - 1) // p.SCOPE_CHUNK_FRAMES
        self._scope_pending = (n, t == 1)
        self._send(f"ACK c=SCOPE n={n} t={t} k={chunks}")

    @staticmethod
    def _opt_int(msg: p.Message, key: str, default: int, low: int, high: int) -> int:
        raw = msg.get(key)
        if raw is None:
            return default
        if not raw.isdigit() or not low <= int(raw) <= high:  # firmware: digits only
            raise ValueError
        return int(raw)

    def _handle(self, body: str, now: float) -> None:
        self._expire(now)
        self._last_rx = now
        msg = p.parse_message(body)
        t = msg.type
        if t == "HELLO":
            proto = msg.get("proto")
            if proto is None or not proto.isdigit():
                self._send(f"ERR c={t} e=ARG")
            elif int(proto) != self.protocol_version:
                self._send(f"ERR c={t} e=PROTO")
            else:
                self._streaming = True
                self._last_sent = None
                self._send(
                    f"HELLO proto={self.protocol_version} "
                    f"fw={'0.4.0' if self.audio_supported else '0.3.2'}-sim app=DaisyDiagnostic "
                    "dev=seed3-desktop boot=BOOT_SRAM bl=v6_1 libdaisy=f715e48 "
                    "caps=btn,pot,potraw,sw17,sw18,sd,sdread,jack,led,boot,cvin,gate,cvout"
                    + (",audio" if self.audio_supported else "")
                    + (",sdmsc" if self.reader_supported else "") + " sim=1")
                self._send_state(now)
                if self.audio_supported:
                    self._send_audio()
                    self._last_audio = now
        elif t == "PING":
            n = msg.get("n")
            self._send(f"PONG n={n}" if n and n.isdigit() else f"ERR c={t} e=ARG")
        elif t == "GET":
            self._send_state(now)
        elif t == "RATE":
            hz = msg.get("hz")
            if hz and hz.isdigit() and 10 <= int(hz) <= 100:
                self.rate_hz = int(hz)
                self._send(f"ACK c=RATE hz={self.rate_hz}")
            else:
                self._send(f"ERR c={t} e=ARG")
        elif t == "LED":
            self._handle_led(msg)
        elif t == "CVOUT":
            self._handle_cvout(msg)
        elif t in ("TONE", "SCOPE") and not self.audio_supported:
            self._send(f"ERR c={t} e=UNKNOWN")  # older firmware has no such command
        elif t == "TONE":
            if not self._streaming:
                self._send("ERR c=TONE e=SESSION")
            else:
                self._handle_tone(msg)
        elif t == "SCOPE":
            if not self._streaming:
                self._send("ERR c=SCOPE e=SESSION")
            else:
                self._handle_scope(msg)
        elif t == "SDTEST":
            if self.reader_state != "LOCAL":
                self._send("ERR c=SDTEST e=BUSY")
                return
            if self.inputs.sd_card:
                self._busy_until = now + self.sd_test_duration
                self._keepalive_after_block = True
            self.cv_mode = "OFF"  # firmware 0.4.0 silences outputs before it blocks
            self.tone_mode = "OFF"
            self._send(self._sd_result())
        elif t == "READER":
            action = msg.get("a")
            if not self.reader_supported:
                self._send("ERR c=READER e=UNSUPPORTED")
            elif not self._streaming:
                self._send("ERR c=READER e=SESSION")
            elif action not in ("STATUS", "SHARE", "RETURN"):
                self._send("ERR c=READER e=ARG")
            elif action == "SHARE":
                if self.reader_state != "LOCAL" or not self.inputs.sd_card:
                    self._send("ERR c=READER e=STATE")
                else:
                    self.reader_state = "SHARED"
                    self.reader_sectors = self.sd_size_mb * 2048
                    self._busy_until = now + self.reader_init_duration
                    self._keepalive_after_block = True
                    self._send_reader(action)
            elif action == "RETURN":
                if self.reader_state not in ("EJECTED", "INTERRUPTED", "ERROR"):
                    self._send("ERR c=READER e=STATE")
                else:
                    self.reader_state = "LOCAL"
                    self.reader_sectors = 0
                    self._send_reader(action)
            else:
                self._send_reader(action)
        elif t == "BOOT":
            if not self._streaming:
                self._send("ERR c=BOOT e=SESSION")
            elif self.reader_state != "LOCAL":
                self._send("ERR c=BOOT e=BUSY")
            else:
                self._send("ACK c=BOOT")
                self.in_bootloader = True
                self.bootloader_entries += 1
        elif t == "BYE":
            self._streaming = False
            self.cv_mode = "OFF"
            self.tone_mode = "OFF"
            self._scope_pending = None
            self._send("ACK c=BYE")
        else:
            self._send(f"ERR c={t[:12]} e=UNKNOWN")

    def _handle_led(self, msg: p.Message) -> None:
        mode = msg.get("m")
        try:
            if mode not in p.LED_MODES:
                raise ValueError
            mask = int(msg.get("mask"), 16) if msg.get("mask") is not None else self.led_mask
            br = int(msg.get("br")) if msg.get("br") is not None else self.led_brightness
            ms = int(msg.get("ms")) if msg.get("ms") is not None else self.chase_ms
            if not (0 <= mask <= 0xFFFF and 0 <= br <= 100 and 20 <= ms <= 2000):
                raise ValueError
        except ValueError:
            self._send("ERR c=LED e=ARG")
            return
        self.led_mode, self.led_mask, self.led_brightness, self.chase_ms = mode, mask, br, ms
        self._send(f"ACK c=LED m={mode} mask={mask:04X} br={br} ms={ms}")

    def _handle_cvout(self, msg: p.Message) -> None:
        if not self._streaming:
            self._send("ERR c=CVOUT e=SESSION")
            return
        mode = msg.get("m")

        def opt(key: str, current: int, low: int, high: int) -> int:
            raw = msg.get(key)
            if raw is None:
                return current
            if not raw.isdigit() or len(raw) > 11 or not low <= int(raw) <= high:
                raise ValueError
            return int(raw)

        try:
            if mode not in p.CV_OUT_MODES:
                raise ValueError
            c1 = opt("c1", self.cv_code[0], 0, p.DAC_MAX)
            c2 = opt("c2", self.cv_code[1], 0, p.DAC_MAX)
            ms = opt("ms", self.cv_period_ms, 50, 10000)
        except ValueError:
            self._send("ERR c=CVOUT e=ARG")
            return
        self.cv_mode, self.cv_code, self.cv_period_ms = mode, [c1, c2], ms
        self._send(f"ACK c=CVOUT m={mode} c1={c1} c2={c2} ms={ms}")

    def _sd_result(self) -> str:
        if not self.inputs.sd_card:
            return "SD st=NONE res=-1"
        if self.sd_filesystem is None:
            return "SD st=ERROR res=13"
        return (f"SD st=MOUNTED res=0 fs={self.sd_filesystem} mb={self.sd_size_mb} "
                f"ent={self.sd_entries}")

    def _send_reader(self, action: str = "STATUS") -> None:
        if self.reader_supported:
            self._send(f"READER state={self.reader_state} sectors={self.reader_sectors} busy=0 a={action}")

    def reader_eject(self) -> None:
        """Model a successful Windows eject. Does not emulate the USB MSC stack."""
        if self.reader_state == "SHARED":
            self.reader_state = "EJECTED"
            self._send_reader()

    def reader_bus_reset(self) -> None:
        """An externally powered board can lose its USB link without rebooting."""
        if self.reader_state == "SHARED":
            self.reader_state = "INTERRUPTED"
        self._send_reader()

    def state(self, now: float) -> p.BoardState:
        leds = self.computed_leds(now)
        return p.BoardState(
            seq=self._seq,
            time_ms=int(self._uptime(now) * 1000) & 0xFFFFFFFF,
            buttons=list(self.inputs.buttons),
            press_counts=list(self.inputs.press_counts),
            pots=list(self.inputs.pots),
            pots_raw=[min(65535, int(v * 65535 + 0.5)) for v in self.inputs.pots],
            sw17=self.inputs.sw17,
            sw18=self.inputs.sw18,
            sd_card=self.inputs.sd_card,
            jack_left=self.inputs.jack_left,
            jack_right=self.inputs.jack_right,
            led_mode=self.led_mode,
            leds=[bool(leds >> i & 1) for i in range(p.NUM_LEDS)],
            led_brightness=self.effective_brightness(),
            cvgate=self._cvgate(now),
        )

    def _cvgate(self, now: float) -> p.CvGateState:
        out = self.cv_out(now)
        cv = [o / p.DAC_MAX for o in out] if self.cv_patched else list(self.inputs.cv)
        return p.CvGateState(
            cv=[p.cv_to_int(v) / 10000.0 for v in cv],
            # libDaisy's bipolar CV inputs are inverted: raw 0 reads +1.0, raw 65535 reads
            # -1.0 (hardware trace 2026-10-31: cv=+0.9906 at cvr=307).
            cv_raw=[max(0, min(65535, int((1.0 - v) / 2.0 * 65535 + 0.5))) for v in cv],
            gates=list(self.inputs.gates),
            gate_counts=list(self.inputs.gate_counts),
            cv_out_mode=self.cv_mode,
            cv_out=out,
        )

    def _send_state(self, now: float) -> None:
        self._send(p.format_state(self.state(now)))
        self._seq = (self._seq + 1) & 0xFFFFFFFF
        self._last_state = now
        self._last_sent = self.inputs.digital()
        self._send_sys(now)

    def _send_sys(self, now: float) -> None:
        self._send(f"SYS up={int(self._uptime(now))} cbhz=1000 txok={self.tx_ok} txbusy=0 "
                   f"txdrop=0 rxok={self._parser.ok} "
                   f"rxbad={self._parser.errors + self.rx_overflow} host=1 rate={self.rate_hz}")
        self._last_sys = now

    def _run_main_loop(self, now: float) -> None:
        if not self.host_active(now):
            return
        period = 1.0 / self.rate_hz
        changed = (self._last_sent != self.inputs.digital()
                   and now - self._last_state >= MIN_FRAME_SPACING_S)
        if now - self._last_state >= period - 1e-6 or changed:
            self._send(p.format_state(self.state(now)))
            self._seq = (self._seq + 1) & 0xFFFFFFFF
            # Keep the nominal rate even if the host polls with a coarse timer.
            behind = now - self._last_state
            self._last_state = now if changed or behind >= 2 * period else self._last_state + period
            self._last_sent = self.inputs.digital()
        if now - self._last_sys >= 1.0 - 1e-6:
            self._send_sys(now)
            self._send_reader()
        if self.audio_supported and now - self._last_audio >= 0.1 - 1e-6:
            self._send_audio()
            self._last_audio = now
        if self._scope_pending is not None:
            frames, trigger = self._scope_pending
            self._scope_pending = None
            self._send_scope(frames, trigger)


class SimulatedLink:
    """Link to a SimulatedDevice; behaves like an opened serial port."""

    def __init__(self, device: SimulatedDevice, clock, cable: "SimulatedCable") -> None:
        self.device = device
        self._clock = clock
        self._cable = cable
        self._generation = cable.generation
        self.closed = False

    def _check(self) -> None:
        if self.closed or not self._cable.plugged or self._generation != self._cable.generation:
            raise LinkError("simulated USB device disconnected")

    def read(self) -> bytes:
        self._check()
        data = self.device.output(self._clock())
        if self.device.in_bootloader:
            # The ACK is out; the CDC port disappears with the reset (DFU is no COM port).
            # Plugging in again models the restart after flashing.
            self._cable.unplug()
        return data

    def write(self, data: bytes) -> None:
        self._check()
        self.device.receive(data, self._clock())

    def close(self) -> None:
        self.closed = True


class SimulatedCable:
    """Models the J7 USB cable. Unplugging also powers the board down (J7 supplies it)."""

    PORT_NAME = "SIM"

    def __init__(self, device_factory=SimulatedDevice) -> None:
        self._factory = device_factory
        self.device = device_factory()
        self.plugged = True
        self.generation = 0

    def unplug(self) -> None:
        self.plugged = False
        self.generation += 1

    def plug(self) -> None:
        if not self.plugged:
            # The firmware restarts: inputs keep their physical position, state resets.
            old = self.device.inputs
            self.device = self._factory()
            self.device.inputs = old.copy()
            self.device.inputs.press_counts = [0] * p.NUM_BUTTONS
            self.plugged = True

    def ports(self):
        from .session import PortInfo
        if not self.plugged:
            return []
        return [PortInfo(self.PORT_NAME, p.USB_VID, p.USB_PID, 'Simulated Daisy Seed3 (J7)')]

    def open(self, port: str, clock) -> SimulatedLink:
        if port != self.PORT_NAME or not self.plugged:
            raise LinkError(f"cannot open {port}")
        return SimulatedLink(self.device, clock, self)
