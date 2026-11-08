"""pm_cli against a scripted fake device: framing/CRC, identity check, read-only
commands, start flow with polling and duplicates, errors, service confirmation."""
import io
import pathlib
import sys
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent / "tools"))
import pm_cli  # noqa: E402

HASH = "3F9A1C2E0B7D4466" + "A" * 48


class FakeDevice:
    """Answers PM* and DDP frames like the firmware (happy path, configurable)."""

    def __init__(self, app="DaisyProgramManager", loading_steps=3):
        self.app = app
        self.sent = []          # bodies received from the CLI
        self.rx = bytearray()   # bytes the CLI will read
        self.parser = pm_cli.FrameParser()
        self.session = False
        self.loading = -1
        self.loading_steps = loading_steps
        self.rids = set()
        self.closed = False
        self.gen = 1

    def reply(self, body):
        self.rx += pm_cli.frame(body)

    def write(self, data):
        for body in self.parser.feed(data):
            self.sent.append(body)
            self.handle(body)

    def read(self):
        data = bytes(self.rx)
        self.rx = bytearray()
        return data

    def close(self):
        self.closed = True

    def handle(self, body):
        f = pm_cli.fields(body)
        t = f["_type"]
        if t == "HELLO":
            self.session = True
            self.reply(f"HELLO proto=1 fw=0.1.0 app={self.app} dev=seed3-desktop boot=BOOT_QSPI bl=v6_1 libdaisy=f715e48 caps=btn,pm,pmsd")
        elif t == "PMINFO":
            self.reply(f"PMINFO app={self.app} fw=0.1.0 profile=manager build=abc1234 mode=SELECT gen={self.gen} cat=OK cgen=9 n=2 sd=1 rootbin=0 slots=15")
        elif t == "PMLIST" and "s" not in f:
            self.reply(f"PMLIST gen={self.gen} cgen=9 n=2 mask=0005 cat=OK")
        elif t == "PMLIST":
            slot = f["s"]
            if slot == "1":
                self.reply(f"PMENT gen={self.gen} slot=1 id=toy-piano name=Toy%20Piano%20%C3%A4 cat=instrument ver=0.4.0 hash={HASH}")
            elif slot == "3":
                self.reply(f"PMENT gen={self.gen} slot=3 id=reverb-fx name=Ambient%20Reverb cat=effect ver=0.3.1 hash={'B' * 64}")
            else:
                self.reply("ERR c=PMLIST e=NOENTRY")
        elif t == "PMSTATUS":
            if self.loading < 0:
                self.reply(f"PMST mode=SELECT state=IDLE e=OK ie=OK ve=OK slot=0 rid=0 pct=0 gen={self.gen} cat=OK ce=OK cd=0 sd=1 rootbin=0 uerr=OK")
            else:
                self.loading += 1
                if self.loading >= self.loading_steps:
                    self.reply(f"PMST mode=LOADING state=RESTARTING e=OK ie=OK ve=OK slot=1 rid={self.rid} pct=100 gen={self.gen} cat=OK ce=OK cd=0 sd=1 rootbin=0 uerr=OK")
                else:
                    self.reply(f"PMST mode=LOADING state=LOADING e=OK ie=OK ve=OK slot=1 rid={self.rid} pct={self.loading * 30} gen={self.gen} cat=OK ce=OK cd=0 sd=1 rootbin=0 uerr=OK")
        elif t == "PMSTART":
            if not self.session:
                self.reply("ERR c=PMSTART e=SESSION")
            elif f["gen"] != str(self.gen):
                self.reply("ERR c=PMSTART e=GENERATION")
            elif f["rid"] in self.rids:
                self.reply(f"ACK c=PMSTART rid={f['rid']} dup=1 state=LOADING e=OK")
            elif not HASH.lower().startswith(f["hash"].lower()):
                self.reply("ERR c=PMSTART e=HASH")
            else:
                self.rids.add(f["rid"])
                self.rid = f["rid"]
                self.loading = 0
                self.reply(f"ACK c=PMSTART rid={f['rid']} slot={f['slot']}")
        elif t == "PMMODE":
            self.reply(f"ACK c=PMMODE m={f['m']}" if self.session else "ERR c=PMMODE e=SESSION")
        elif t == "PMRESCAN":
            self.gen += 1
            self.reply("ACK c=PMRESCAN")
        elif t == "BOOT":
            self.reply("ACK c=BOOT" if self.session else "ERR c=BOOT e=SESSION")
        elif t == "BYE":
            self.session = False
            self.reply("ACK c=BYE")
        elif t == "LED":
            self.reply("ERR c=LED e=MODE")
        else:
            self.reply(f"ERR c={t} e=UNKNOWN")


class CliTests(unittest.TestCase):
    def run_cli(self, argv, device=None):
        device = device or FakeDevice()
        out = io.StringIO()
        clock = [0.0]

        def sleep(s):
            clock[0] += s

        rc = pm_cli.main(["-p", "FAKE"] + argv, transport_factory=lambda port: device, out=out,
                         sleep=sleep, clock=lambda: clock[0])
        return rc, out.getvalue(), device

    def test_framing(self):
        self.assertEqual(pm_cli.frame("PING n=1"), b"$PING n=1*" + f"{pm_cli.crc16('PING n=1'):04X}".encode() + b"\n")
        self.assertEqual(pm_cli.crc16("123456789"), 0x29B1)
        p = pm_cli.FrameParser()
        good = pm_cli.frame("PONG n=1")
        self.assertEqual(p.feed(b"junk" + good[:4]) + p.feed(good[4:]), ["PONG n=1"])
        bad = bytearray(good)
        bad[-3] ^= 1
        self.assertEqual(p.feed(bytes(bad)), [])
        self.assertEqual(p.bad, 1)
        self.assertEqual(p.feed(b"$ST a=1$" + good[1:]), ["PONG n=1"])  # restart on '$'
        self.assertEqual(pm_cli.decode_name("Toy%20Piano%20%C3%A4"), "Toy Piano ä")
        with self.assertRaises(pm_cli.CliError):
            pm_cli.frame("has $ dollar")

    def test_read_only_commands_send_no_state_change(self):
        rc, out, dev = self.run_cli(["info"])
        self.assertEqual(rc, 0)
        self.assertIn("app = DaisyProgramManager", out)
        self.assertEqual(dev.sent, ["PMINFO"])
        rc, out, dev = self.run_cli(["list"])
        self.assertEqual(rc, 0)
        self.assertIn("toy-piano", out)
        self.assertIn("Toy Piano ä", out)
        self.assertIn("slot  3", out)
        self.assertEqual(dev.sent, ["PMINFO", "PMLIST", "PMLIST s=1", "PMLIST s=3"])
        for body in dev.sent:
            self.assertFalse(body.startswith(("HELLO", "PMSTART", "BOOT", "PMMODE")))
        rc, out, dev = self.run_cli(["status"])
        self.assertEqual(rc, 0)
        self.assertIn("mode=SELECT", out)
        self.assertTrue(dev.closed)

    def test_refuses_other_devices(self):
        rc, out, dev = self.run_cli(["info"], FakeDevice(app="DaisyDiagnostic"))
        self.assertEqual(rc, 2)
        self.assertIn("not a program manager", out)
        rc, out, dev = self.run_cli(["mode", "DIAG"], FakeDevice(app="DaisyDiagnostic"))
        self.assertEqual(rc, 2)
        self.assertNotIn("PMMODE m=DIAG", dev.sent)

    def test_start_flow(self):
        rc, out, dev = self.run_cli(["start", "--slot", "1", "--id", "toy-piano", "--hash", HASH[:16], "--gen", "1", "--rid", "77", "--wait", "30"])
        self.assertEqual(rc, 0, out)
        self.assertEqual(dev.sent[:2], ["HELLO proto=1", f"PMSTART slot=1 id=toy-piano hash={HASH[:16].lower()} gen=1 rid=77"])
        self.assertIn("RESTARTING", out)
        self.assertEqual(dev.sent.count("PMSTART slot=1 id=toy-piano hash=" + HASH[:16].lower() + " gen=1 rid=77"), 1)
        self.assertNotIn("BYE", dev.sent)  # the device restarts; no BYE
        # Same rid again: duplicate, nothing restarted.
        rc, out, dev2 = self.run_cli(["start", "--slot", "1", "--id", "toy-piano", "--hash", HASH[:16], "--gen", "1", "--rid", "77", "--wait", "1"], dev)
        self.assertIn("dup=1", out)
        # Stale generation and wrong hash are reported as errors.
        rc, out, _ = self.run_cli(["start", "--slot", "1", "--id", "toy-piano", "--hash", HASH[:16], "--gen", "2", "--rid", "78"])
        self.assertEqual(rc, 1)
        self.assertIn("e=GENERATION", out)
        rc, out, _ = self.run_cli(["start", "--slot", "1", "--id", "toy-piano", "--hash", "0" * 16, "--gen", "1", "--rid", "79"])
        self.assertEqual(rc, 1)
        self.assertIn("e=HASH", out)
        rc, out, _ = self.run_cli(["start", "--slot", "1", "--id", "toy-piano", "--hash", "xyz", "--gen", "1"])
        self.assertEqual(rc, 2)

    def test_session_commands_and_service(self):
        rc, out, dev = self.run_cli(["mode", "DIAG"])
        self.assertEqual(rc, 0)
        self.assertEqual(dev.sent, ["HELLO proto=1", "PMMODE m=DIAG", "BYE"])
        rc, out, dev = self.run_cli(["rescan"])
        self.assertEqual(dev.sent, ["HELLO proto=1", "PMRESCAN", "BYE"])
        rc, out, dev = self.run_cli(["service"])
        self.assertEqual(rc, 2)
        self.assertEqual(dev.sent, [])  # refused without --yes, nothing sent
        rc, out, dev = self.run_cli(["service", "--yes"])
        self.assertEqual(rc, 0)
        self.assertEqual(dev.sent, ["HELLO proto=1", "BOOT"])
        rc, out, dev = self.run_cli(["raw", "LED m=ON", "PING n=5"])
        self.assertIn("ERR c=LED e=MODE", out)
        self.assertEqual(dev.sent[-1], "BYE")

    def test_timeout(self):
        class Silent(FakeDevice):
            def handle(self, body):
                pass
        rc, out, _ = self.run_cli(["--timeout", "0.5", "info"], Silent())
        self.assertEqual(rc, 2)
        self.assertIn("no reply", out)


if __name__ == "__main__":
    unittest.main()
