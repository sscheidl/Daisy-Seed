#!/usr/bin/env python3
"""CDC command-line client for the Daisy Program Manager (DDP framing, PM* commands).

  pm_cli.py ports                        list serial ports (no device is contacted)
  pm_cli.py [-p PORT] info               PMINFO (no session)
  pm_cli.py [-p PORT] list               PMLIST and one PMENT per occupied slot
  pm_cli.py [-p PORT] status             PMSTATUS
  pm_cli.py [-p PORT] start --slot N --id ID --hash HEX --gen G [--rid R] [--wait S]
  pm_cli.py [-p PORT] mode SELECT|DIAG
  pm_cli.py [-p PORT] rescan
  pm_cli.py [-p PORT] service --yes      BOOT: restart into the DFU bootloader
  pm_cli.py [-p PORT] raw "BODY" [...]   frames verbatim inside a session

Reading commands never change device state; start/mode/rescan/service/raw open a
host session (HELLO) and close it (BYE) unless the device restarts. Nothing is
sent to a device whose PMINFO/HELLO does not name a program manager. With several
Daisy ports and no -p the tool lists them and stops. Real ports need pyserial;
tests use a fake transport (tests/python/test_cli.py).
"""
import argparse
import binascii
import secrets
import sys
import time

PROTO = 1
USB_VID, USB_PID = 0x0483, 0x5740
MAX_BODY = 400
APP_PREFIX = "DaisyProgramManager"


class CliError(Exception):
    pass


def crc16(body):
    return binascii.crc_hqx(body.encode("ascii"), 0xFFFF)


def frame(body):
    if not body or len(body) > MAX_BODY or any(ord(c) < 0x20 or ord(c) > 0x7E or c in "$*" for c in body):
        raise CliError(f"invalid body {body!r}")
    return f"${body}*{crc16(body):04X}\n".encode("ascii")


class FrameParser:
    """Byte-wise receiver with the firmware's resynchronisation rules."""

    def __init__(self):
        self.buf = None
        self.bad = 0

    def feed(self, data):
        out = []
        for b in data:
            if b == 0x24:  # '$' starts (or restarts) a frame
                if self.buf:
                    self.bad += 1
                self.buf = bytearray()
                continue
            if self.buf is None:
                continue
            if b == 0x0A:
                text = self.buf.decode("ascii", errors="replace").rstrip("\r")
                self.buf = None
                star = text.rfind("*")
                body = text[:star] if star >= 0 else ""
                try:
                    ok = star >= 0 and len(text) - star == 5 and int(text[star + 1:], 16) == crc16(body) and body
                except ValueError:
                    ok = False
                if ok:
                    out.append(body)
                else:
                    self.bad += 1
                continue
            self.buf.append(b)
            if len(self.buf) > MAX_BODY + 6:
                self.buf = None
                self.bad += 1
        return out


def fields(body):
    parts = body.split(" ")
    out = {"_type": parts[0]}
    for p in parts[1:]:
        if "=" in p:
            k, v = p.split("=", 1)
            out[k] = v
    return out


def decode_name(text):
    out = bytearray()
    i = 0
    while i < len(text):
        if text[i] == "%" and i + 2 < len(text) + 1 and len(text) - i >= 3:
            try:
                out.append(int(text[i + 1:i + 3], 16))
                i += 3
                continue
            except ValueError:
                pass
        out.append(ord(text[i]))
        i += 1
    return out.decode("utf-8", errors="replace")


class SerialTransport:
    def __init__(self, port, baud=115200):
        try:
            import serial
        except ImportError as exc:
            raise CliError("pyserial is required for real ports: pip install pyserial") from exc
        self.ser = serial.Serial(port, baud, timeout=0.05)

    def write(self, data):
        self.ser.write(data)

    def read(self):
        return self.ser.read(4096)

    def close(self):
        self.ser.close()


def list_ports():
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise CliError("pyserial is required to list ports: pip install pyserial") from exc
    return [{"device": p.device, "vid": p.vid, "pid": p.pid, "description": p.description,
             "daisy": p.vid == USB_VID and p.pid == USB_PID} for p in list_ports.comports()]


class Device:
    def __init__(self, transport, out=sys.stdout, timeout=2.0, sleep=time.sleep, clock=time.monotonic):
        self.t = transport
        self.out = out
        self.timeout = timeout
        self.sleep = sleep
        self.clock = clock
        self.parser = FrameParser()
        self.pending = []
        self.session = False

    def send(self, body):
        self.t.write(frame(body))

    def recv(self, timeout):
        deadline = self.clock() + timeout
        while True:
            if self.pending:
                return self.pending.pop(0)
            data = self.t.read()
            if data:
                self.pending.extend(self.parser.feed(data))
                continue
            if self.clock() >= deadline:
                return None
            self.sleep(0.005)

    def ask(self, body, reply_types, timeout=None):
        """Sends `body`; returns (fields, reply) of the first reply of a wanted type or ERR."""
        self.send(body)
        deadline = self.clock() + (self.timeout if timeout is None else timeout)
        while True:
            left = deadline - self.clock()
            reply = self.recv(left) if left > 0 else None
            if reply is None:
                raise CliError(f"no reply to {body.split(' ')[0]}")
            f = fields(reply)
            if f["_type"] in reply_types or f["_type"] == "ERR":
                return f, reply

    def identify(self):
        f, reply = self.ask("PMINFO", ("PMINFO",))
        if f["_type"] != "PMINFO" or not f.get("app", "").startswith(APP_PREFIX):
            raise CliError(f"not a program manager: {reply}")
        return f

    def hello(self):
        f, reply = self.ask(f"HELLO proto={PROTO}", ("HELLO",))
        if f["_type"] != "HELLO":
            raise CliError(f"HELLO refused: {reply}")
        if f.get("proto") != str(PROTO):
            raise CliError(f"incompatible protocol {f.get('proto')}")
        if not f.get("app", "").startswith(APP_PREFIX):
            raise CliError(f"not a program manager: app={f.get('app')}")
        self.session = True
        return f

    def bye(self):
        if self.session:
            try:
                self.ask("BYE", ("ACK",), timeout=0.5)
            except CliError:
                pass
            self.session = False


def cmd_info(dev, args):
    for k, v in dev.identify().items():
        if k != "_type":
            print(f"{k:>8} = {v}", file=dev.out)
    return 0


def cmd_list(dev, args):
    dev.identify()
    f, reply = dev.ask("PMLIST", ("PMLIST",))
    if f["_type"] != "PMLIST":
        raise CliError(reply)
    print(f"catalog {f.get('cat')} gen {f.get('gen')} (file generation {f.get('cgen')}), {f.get('n')} program(s)",
          file=dev.out)
    mask = int(f.get("mask", "0"), 16)
    for slot in range(1, 16):
        if not mask & (1 << (slot - 1)):
            continue
        e, reply = dev.ask(f"PMLIST s={slot}", ("PMENT",))
        if e["_type"] != "PMENT":
            print(f"  slot {slot:>2}: {reply}", file=dev.out)
            continue
        print(f"  slot {slot:>2}  {e.get('id'):<24} {e.get('ver'):<10} {e.get('cat'):<10} "
              f"{decode_name(e.get('name', ''))}  hash={e.get('hash', '')[:16]}…", file=dev.out)
    print("  slot 16  diagnostics (resident)", file=dev.out)
    return 0


STATUS_KEYS = ("mode", "state", "e", "ie", "ve", "slot", "rid", "pct", "gen", "cat", "ce", "cd", "sd", "rootbin", "uerr")


def cmd_status(dev, args):
    dev.identify()
    f, reply = dev.ask("PMSTATUS", ("PMST",))
    if f["_type"] != "PMST":
        raise CliError(reply)
    print("  ".join(f"{k}={f.get(k, '?')}" for k in STATUS_KEYS), file=dev.out)
    return 0


def cmd_start(dev, args):
    h = args.hash.strip().lower()
    if len(h) < 16 or len(h) > 64 or len(h) % 2 or any(c not in "0123456789abcdef" for c in h):
        raise CliError("--hash must be 16..64 hex digits (even count), a prefix of the catalog payload hash")
    rid = args.rid if args.rid is not None else secrets.randbelow(0xFFFFFFFE) + 1
    dev.hello()
    try:
        f, reply = dev.ask(f"PMSTART slot={args.slot} id={args.id} hash={h} gen={args.gen} rid={rid}", ("ACK",))
        print(reply, file=dev.out)
        if f["_type"] != "ACK":
            return 1
        if f.get("dup") == "1":
            print("note: this request id was already accepted; nothing was restarted", file=dev.out)
        deadline = dev.clock() + args.wait
        last = None
        while dev.clock() < deadline:
            try:
                st, _ = dev.ask("PMSTATUS", ("PMST",), timeout=1.0)
            except CliError:
                print("device stopped answering: restart in progress (expected)", file=dev.out)
                dev.session = False
                return 0
            except Exception as exc:  # serial error after the reset
                print(f"port lost: {exc} (expected after the restart)", file=dev.out)
                dev.session = False
                return 0
            line = f"{st.get('state')} {st.get('pct')}% e={st.get('e')}"
            if line != last:
                print(line, file=dev.out)
                last = line
            if st.get("state") == "RESTARTING":
                dev.session = False
                return 0
            if st.get("state") == "ERROR":
                print(f"load failed: {st.get('e')} ie={st.get('ie')} ve={st.get('ve')}", file=dev.out)
                return 1
            dev.sleep(0.2)
        print("timeout waiting for the load; the device may still be loading", file=dev.out)
        return 1
    finally:
        dev.bye()


def session_command(body):
    def run(dev, args):
        dev.hello()
        try:
            f, reply = dev.ask(body(args), ("ACK",))
            print(reply, file=dev.out)
            return 0 if f["_type"] == "ACK" else 1
        finally:
            dev.bye()
    return run


def cmd_service(dev, args):
    if not args.yes:
        raise CliError("service restarts the kit into the DFU bootloader; pass --yes to confirm")
    dev.hello()
    f, reply = dev.ask("BOOT", ("ACK",))
    print(reply, file=dev.out)
    dev.session = False  # the device resets; no BYE
    return 0 if f["_type"] == "ACK" else 1


def cmd_raw(dev, args):
    dev.hello()
    try:
        for body in args.body:
            dev.send(body)
            deadline = dev.clock() + dev.timeout
            while dev.clock() < deadline:
                reply = dev.recv(deadline - dev.clock())
                if reply is None:
                    break
                print(reply, file=dev.out)
                if fields(reply)["_type"] in ("ACK", "ERR", "PMST", "PMINFO", "PMLIST", "PMENT", "PONG", "SD"):
                    break
        return 0
    finally:
        dev.bye()


def cmd_ports(args):
    for p in list_ports():
        vid = f"{p['vid']:04X}:{p['pid']:04X}" if p["vid"] is not None else "----:----"
        print(f"{p['device']:<8} {vid} {'daisy' if p['daisy'] else '':<6} {p['description']}", file=args.out)
    return 0


def choose_port(args):
    if args.port:
        return args.port
    daisy = [p["device"] for p in list_ports() if p["daisy"]]
    if len(daisy) == 1:
        return daisy[0]
    if not daisy:
        raise CliError("no Daisy CDC port (0483:5740) found; pass -p PORT")
    raise CliError("several Daisy CDC ports found, choose one with -p: " + ", ".join(daisy))


HANDLERS = {
    "info": cmd_info, "list": cmd_list, "status": cmd_status, "start": cmd_start,
    "mode": session_command(lambda a: f"PMMODE m={a.mode}"),
    "rescan": session_command(lambda a: "PMRESCAN"),
    "service": cmd_service, "raw": cmd_raw,
}


def main(argv=None, transport_factory=None, out=sys.stdout, sleep=time.sleep, clock=time.monotonic):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port")
    ap.add_argument("--timeout", type=float, default=2.0)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("ports", "info", "list", "status", "rescan"):
        sub.add_parser(name)
    p = sub.add_parser("start")
    p.add_argument("--slot", type=int, required=True)
    p.add_argument("--id", required=True)
    p.add_argument("--hash", required=True)
    p.add_argument("--gen", type=int, required=True)
    p.add_argument("--rid", type=int)
    p.add_argument("--wait", type=float, default=90.0)
    sub.add_parser("mode").add_argument("mode", choices=("SELECT", "DIAG"))
    sub.add_parser("service").add_argument("--yes", action="store_true")
    sub.add_parser("raw").add_argument("body", nargs="+")
    args = ap.parse_args(argv)
    args.out = out
    try:
        if args.cmd == "ports":
            return cmd_ports(args)
        transport = SerialTransport(choose_port(args)) if transport_factory is None else transport_factory(args.port)
        dev = Device(transport, out=out, timeout=args.timeout, sleep=sleep, clock=clock)
        try:
            return HANDLERS[args.cmd](dev, args)
        finally:
            transport.close()
    except CliError as exc:
        print(f"error: {exc}", file=out)
        return 2


if __name__ == "__main__":
    sys.exit(main())
