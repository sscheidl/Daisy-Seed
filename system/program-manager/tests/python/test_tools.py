"""Host tool tests: synthetic ELF/BIN pairs through dsy_pack, package round trips,
catalog rules and the card checker including the root .bin rule."""
import hashlib
import json
import pathlib
import struct
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
TOOLS = HERE.parent.parent / "tools"
sys.path.insert(0, str(TOOLS))
import pmtools  # noqa: E402
import dsy_pack  # noqa: E402
import pm_card  # noqa: E402
import check_qspi_image  # noqa: E402


def make_elf(sections, entry):
    """Builds a minimal ARM ELF32 with the given sections and one PT_LOAD per
    PROGBITS section. sections: list of dicts(name, addr, size, nobits, lma=None, data=None)."""
    names = b"\0" + b"\0".join(s["name"].encode() for s in sections) + b"\0.shstrtab\0"
    name_off = {}
    off = 1
    for s in sections:
        name_off[s["name"]] = off
        off += len(s["name"]) + 1
    shstr_off = off
    body = bytearray()
    sec_offsets = {}
    for s in sections:
        if not s.get("nobits"):
            data = s.get("data") or bytes([0x11] * s["size"])
            sec_offsets[s["name"]] = len(body)
            body += data
        else:
            sec_offsets[s["name"]] = len(body)
    progbits = [s for s in sections if not s.get("nobits")]
    ehsize, phentsize, shentsize = 52, 32, 40
    phoff = ehsize
    data_off = phoff + phentsize * len(progbits)
    shstr_file_off = data_off + len(body)
    shoff = shstr_file_off + len(names)
    shnum = len(sections) + 2  # null + sections + shstrtab
    out = bytearray()
    out += b"\x7fELF\x01\x01\x01\0" + b"\0" * 8
    out += struct.pack("<HHIIIIIHHHHHH", 2, 40, 1, entry, phoff, shoff, 0x5000400, ehsize, phentsize,
                       len(progbits), shentsize, shnum, shnum - 1)
    for s in progbits:
        lma = s.get("lma", s["addr"])
        out += struct.pack("<IIIIIIII", 1, data_off + sec_offsets[s["name"]], s["addr"], lma, s["size"], s["size"], 5, 4)
    out += body
    out += names
    out += b"\0" * shentsize  # null section
    for s in sections:
        sh_type = 8 if s.get("nobits") else 1
        flags = 0x2 | (0x1 if s.get("nobits") or "data" in s["name"] else 0x4)
        out += struct.pack("<IIIIIIIIII", name_off[s["name"]], sh_type, flags, s["addr"],
                           data_off + sec_offsets[s["name"]], s["size"], 0, 0, 4, 0)
    out += struct.pack("<IIIIIIIIII", shstr_off, 3, 0, 0, shstr_file_off, len(names), 0, 0, 1, 0)
    return bytes(out)


def sram_image(text_size=1000, data_size=64, marker=b"TOYPIANO-FW-VERSION=0.4.0\0", noload=None):
    """A BOOT_SRAM-like ELF/BIN: vectors+text at 0x24000000, .data LMA after the text
    (VMA in DTCM), .bss in DTCM, optional NOLOAD section in the target."""
    vectors = struct.pack("<II", 0x20020000, 0x24000000 | 0x101)
    text = vectors + marker + bytes([0x22] * (text_size - len(vectors) - len(marker)))
    data = bytes([0x33] * data_size)
    sections = [
        {"name": ".isr_vector", "addr": 0x24000000, "size": 8, "data": vectors},
        {"name": ".text", "addr": 0x24000008, "size": text_size - 8, "data": text[8:]},
        {"name": ".data", "addr": 0x20000000, "size": data_size, "lma": 0x24000000 + text_size, "data": data},
        {"name": ".bss", "addr": 0x20000000 + data_size, "size": 300, "nobits": True},
        {"name": ".sram1_bss", "addr": 0x30000000, "size": 128, "nobits": True},
    ]
    if noload:
        sections.append({"name": noload[0], "addr": noload[1], "size": noload[2], "nobits": True})
    elf = make_elf(sections, 0x24000101)
    image = text + data
    return elf, image


class Args:
    def __init__(self, **kw):
        self.__dict__.update(kw)


class PackageTests(unittest.TestCase):
    def test_header_roundtrip_and_errors(self):
        payload = struct.pack("<II", 0x20020000, 0x24000101) + bytes(300)
        head = pmtools.build_header("toy-piano", "0.4.0", payload)
        self.assertEqual(len(head), 160)
        info = pmtools.verify_package(head + payload)
        self.assertEqual(info["product"], "toy-piano")
        self.assertEqual(info["payload_length"], 308)
        self.assertEqual(info["payload_sha256"], hashlib.sha256(payload).hexdigest())
        bad = bytearray(head + payload)
        bad[170] ^= 1
        with self.assertRaises(pmtools.PackageError) as cm:
            pmtools.verify_package(bytes(bad))
        self.assertEqual(cm.exception.code, "HASH")
        with self.assertRaises(pmtools.PackageError) as cm:
            pmtools.verify_package(head + payload + b"x")
        self.assertEqual(cm.exception.code, "FILESIZE")
        with self.assertRaises(pmtools.PackageError) as cm:
            pmtools.verify_package((head + payload)[:200])
        self.assertEqual(cm.exception.code, "FILESIZE")
        damaged = bytearray(head)
        damaged[20] = ord("x")
        with self.assertRaises(pmtools.PackageError) as cm:
            pmtools.parse_header(bytes(damaged))
        self.assertEqual(cm.exception.code, "HDRCRC")
        with self.assertRaises(pmtools.PackageError):
            pmtools.build_header("Toy", "0.4.0", payload)
        with self.assertRaises(pmtools.PackageError):
            pmtools.build_header("toy", "0.4.0", bytes(7))
        with self.assertRaises(pmtools.PackageError) as cm:
            pmtools.check_vectors(struct.pack("<II", 0x24000000, 0x24000101) + bytes(300))
        self.assertEqual(cm.exception.code, "VECSP")

    def test_check_image_accepts_good_and_rejects_bad(self):
        elf, image = sram_image()
        report = dsy_pack.check_image(elf, image, marker="TOYPIANO-FW-VERSION=", version="0.4.0")
        self.assertEqual(report["image_bytes"], len(image))
        self.assertEqual(report["marker_version"], "0.4.0")
        # BIN shorter than the load extent.
        with self.assertRaises(pmtools.PackageError) as cm:
            dsy_pack.check_image(elf, image[:-1])
        self.assertEqual(cm.exception.code, "BINSIZE")
        # Wrong marker version.
        with self.assertRaises(pmtools.PackageError) as cm:
            dsy_pack.check_image(elf, image, marker="TOYPIANO-FW-VERSION=", version="0.5.0")
        self.assertEqual(cm.exception.code, "MARKER")
        # NOLOAD inside the target must be declared.
        elf2, image2 = sram_image(noload=(".axi_bss", 0x24001000, 4096))
        with self.assertRaises(pmtools.PackageError) as cm:
            dsy_pack.check_image(elf2, image2)
        self.assertEqual(cm.exception.code, "NOLOAD")
        report = dsy_pack.check_image(elf2, image2, allowed_noload=(".axi_bss",))
        self.assertEqual(report["noload_in_target"], [".axi_bss"])
        # NOLOAD running past the target end.
        elf3, image3 = sram_image(noload=(".axi_bss", 0x24070000, 0x9000))
        with self.assertRaises(pmtools.PackageError) as cm:
            dsy_pack.check_image(elf3, image3, allowed_noload=(".axi_bss",))
        self.assertEqual(cm.exception.code, "REGION")
        # A QSPI dependency.
        elf4, image4 = sram_image(noload=(".qspiflash_bss", 0x90040000, 16))
        with self.assertRaises(pmtools.PackageError) as cm:
            dsy_pack.check_image(elf4, image4)
        self.assertEqual(cm.exception.code, "QSPISECTION")
        # Wrong entry.
        elf5 = bytearray(elf)
        struct.pack_into("<I", elf5, 24, 0x24000201)
        with self.assertRaises(pmtools.PackageError) as cm:
            dsy_pack.check_image(bytes(elf5), image)
        self.assertEqual(cm.exception.code, "ENTRY")
        # Image larger than 480 KiB is refused by size.
        elf6, image6 = sram_image(text_size=480 * 1024, data_size=64)
        with self.assertRaises(pmtools.PackageError) as cm:
            dsy_pack.check_image(elf6, image6)
        self.assertEqual(cm.exception.code, "LMA")

    def test_pack_and_card_flow(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            elf, image = sram_image()
            (tmp / "ToyPiano-v0.4.0.elf").write_bytes(elf)
            (tmp / "ToyPiano-v0.4.0.bin").write_bytes(image)
            rc = dsy_pack.main(["pack", "--elf", str(tmp / "ToyPiano-v0.4.0.elf"), "--bin", str(tmp / "ToyPiano-v0.4.0.bin"),
                                "--product", "toy-piano", "--version", "0.4.0", "--marker", "TOYPIANO-FW-VERSION=",
                                "--out", str(tmp / "out") + "/", "--report", str(tmp / "out" / "report.json")])
            self.assertEqual(rc, 0)
            pkg = tmp / "out" / "ToyPiano-v0.4.0.dsy"
            self.assertTrue(pkg.exists())
            self.assertEqual(pkg.stat().st_size, 160 + len(image))
            report = json.loads((tmp / "out" / "report.json").read_text())
            self.assertEqual(report["product"], "toy-piano")
            self.assertEqual(dsy_pack.main(["info", str(pkg)]), 0)
            # Card template.
            card = tmp / "card"
            self.assertEqual(pm_card.main(["init", str(card)]), 0)
            self.assertEqual(pm_card.main(["add", str(card), str(pkg), "--slot", "1", "--name", "Toy Piano", "--category", "instrument"]), 0)
            doc = pmtools.load_catalog(card / "DAISY" / "catalog.json")
            self.assertEqual(doc["generation"], 2)
            self.assertEqual(doc["programs"][0]["path"], "PROGRAMS/toy-piano/ToyPiano-v0.4.0.dsy")
            self.assertEqual(pm_card.main(["check", str(card)]), 0)
            # Same product again in another slot replaces the old entry.
            self.assertEqual(pm_card.main(["add", str(card), str(pkg), "--slot", "4"]), 0)
            doc = pmtools.load_catalog(card / "DAISY" / "catalog.json")
            self.assertEqual([e["slot"] for e in doc["programs"]], [4])
            # Root .bin rule.
            (card / "ToyPiano-v0.4.0.BIN").write_bytes(b"x")
            self.assertEqual(pm_card.main(["check", str(card)]), 1)
            (card / "ToyPiano-v0.4.0.BIN").unlink()
            (card / "notes.binary.txt").write_bytes(b"x")  # ".bin" substring: still refused
            self.assertEqual(pm_card.main(["check", str(card)]), 1)
            (card / "notes.binary.txt").unlink()
            (card / "DAISY" / "readme.bin").write_bytes(b"x")  # not in the root: allowed
            self.assertEqual(pm_card.main(["check", str(card)]), 0)
            # Catalog hash drift and missing file are reported.
            doc["programs"][0]["payload_sha256"] = "0" * 64
            (card / "DAISY" / "catalog.json").write_text(pmtools.dump_catalog(doc))
            self.assertEqual(pm_card.main(["check", str(card)]), 1)
            self.assertEqual(pm_card.main(["remove", str(card), "--slot", "4"]), 0)
            self.assertEqual(pm_card.main(["remove", str(card), "--slot", "4"]), 1)
            self.assertEqual(pm_card.main(["check", str(card)]), 0)
            # Slot limits and drive-root guard.
            self.assertEqual(pm_card.main(["add", str(card), str(pkg), "--slot", "16"]), 1)
            self.assertTrue(pm_card.is_drive_root(pathlib.Path(tmp.anchor)))

    def test_catalog_rules(self):
        base = {"format": 1, "generation": 3, "hardware": "seed3-desktop", "programs": []}
        entry = {"slot": 1, "id": "toy-piano", "name": "Toy Piano", "version": "0.4.0",
                 "path": "PROGRAMS/toy-piano/ToyPiano-v0.4.0.dsy", "payload_sha256": "ab" * 32}
        pmtools.validate_catalog({**base, "programs": [entry]})
        bad_cases = [
            ({**base, "format": 2}, "FORMAT"),
            ({**base, "generation": -1}, "GENERATION"),
            ({**base, "hardware": "seed2"}, "HARDWARE"),
            ({**base, "programs": [{**entry, "slot": 16}]}, "SLOT"),
            ({**base, "programs": [entry, {**entry, "id": "x"}]}, "DUPSLOT"),
            ({**base, "programs": [entry, {**entry, "slot": 2}]}, "DUPID"),
            ({**base, "programs": [{**entry, "id": "Toy"}]}, "ID"),
            ({**base, "programs": [{**entry, "name": "x" * 65}]}, "NAME"),
            ({**base, "programs": [{**entry, "name": "tab\there"}]}, "NAME"),
            ({**base, "programs": [{**entry, "category": "synth"}]}, "CATEGORY"),
            ({**base, "programs": [{**entry, "version": "1 b"}]}, "VERSION"),
            ({**base, "programs": [{**entry, "path": "PROGRAMS/../x.dsy"}]}, "PATH"),
            ({**base, "programs": [{**entry, "path": "SAMPLES/x.dsy"}]}, "PATH"),
            ({**base, "programs": [{**entry, "path": "PROGRAMS/a/.dsy"}]}, "PATH"),
            ({**base, "programs": [{**entry, "payload_sha256": "zz" * 32}]}, "HASH"),
            ({**base, "programs": [{**entry, "profile": "other"}]}, "PROFILE"),
            ({**base, "programs": [dict(entry, **{"slot": i}) if False else {**entry, "slot": i, "id": f"p{i}"} for i in range(1, 17)]}, "TOOMANY"),
            ({k: v for k, v in base.items() if k != "programs"}, "MISSING"),
        ]
        for doc, code in bad_cases:
            with self.assertRaises(pmtools.PackageError, msg=code) as cm:
                pmtools.validate_catalog(doc)
            self.assertEqual(cm.exception.code, code, doc)
        self.assertTrue(pmtools.valid_name("Träume – Ambient"))
        self.assertFalse(pmtools.valid_path("PROGRAMS/x/y.bin"))

    def test_qspi_checker_rejects_target_overlap(self):
        # A manager-like ELF: vectors/text in QSPI, .data LMA in QSPI and VMA in DTCM.
        vectors = struct.pack("<II", 0x20020000, 0x90040000 | 0x101)
        marker = b"PROGRAM-MANAGER-FW-VERSION=0.1.0\0"
        text = vectors + marker + bytes([0x44] * (2000 - len(vectors) - len(marker)))
        good = [
            {"name": ".isr_vector", "addr": 0x90040000, "size": 8, "data": vectors},
            {"name": ".text", "addr": 0x90040008, "size": 1992, "data": text[8:]},
            {"name": ".data", "addr": 0x20000000, "size": 64, "lma": 0x90040000 + 2000, "data": bytes(64)},
            {"name": ".bss", "addr": 0x20000040, "size": 1000, "nobits": True},
            {"name": ".pm_axi_bounce", "addr": 0x24078000, "size": 4096, "nobits": True},
            {"name": ".sram1_bss", "addr": 0x30000000, "size": 64, "nobits": True},
        ]
        image = text + bytes(64)
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            (tmp / "a.elf").write_bytes(make_elf(good, 0x90040101))
            (tmp / "a.bin").write_bytes(image)
            rc = self._run_checker(["--profile", "manager", "--version", "0.1.0", str(tmp / "a.elf"), str(tmp / "a.bin")])
            self.assertEqual(rc, 0)
            bad = good + [{"name": ".manager_bss", "addr": 0x24001000, "size": 16, "nobits": True}]
            (tmp / "b.elf").write_bytes(make_elf(bad, 0x90040101))
            rc = self._run_checker(["--version", "0.1.0", str(tmp / "b.elf"), str(tmp / "a.bin")])
            self.assertEqual(rc, 1)
            rc = self._run_checker(["--version", "0.2.0", str(tmp / "a.elf"), str(tmp / "a.bin")])
            self.assertEqual(rc, 1)

    @staticmethod
    def _run_checker(argv):
        old = sys.argv
        sys.argv = ["check_qspi_image.py"] + argv
        try:
            return check_qspi_image.main()
        finally:
            sys.argv = old


if __name__ == "__main__":
    unittest.main()
