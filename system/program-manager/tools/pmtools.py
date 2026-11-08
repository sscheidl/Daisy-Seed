"""Shared helpers for the Daisy Program Manager host tools: ELF32 reading, the .dsy
package header (format 1, mirrors src/pm_image.h) and the catalog rules (mirrors
src/pm_catalog.h). Standard library only.
"""
import hashlib
import json
import pathlib
import re
import struct
import zlib

HEADER_SIZE = 160
HEADER_VERSION = 1
MAGIC = b"DSYIMAGE"
HARDWARE_ID = "seed3-desktop"
PROFILE_ID = "axi-sram-480k-v1"
TARGET_BASE = 0x24000000
TARGET_SIZE = 480 * 1024
TARGET_END = TARGET_BASE + TARGET_SIZE
HELPER_START = TARGET_END
HELPER_END = 0x24080000
DTCM_START = 0x20000000
DTCM_END = 0x20020000
MIN_PAYLOAD = 8
CATALOG_FORMAT = 1
CATALOG_MAX_BYTES = 16 * 1024
CATALOG_MAX_PROGRAMS = 15
CATEGORIES = ("instrument", "effect", "sequencer", "other")
ID_RE = re.compile(r"^[a-z0-9-]{1,32}$")
VERSION_RE = re.compile(r"^[0-9A-Za-z.-]{1,16}$")
HASH_RE = re.compile(r"^[0-9a-fA-F]{64}$")
PATH_CHARS = re.compile(r"^[A-Za-z0-9._/-]+$")


class PackageError(Exception):
    """A package or catalog problem; `code` matches the firmware's error names."""

    def __init__(self, code, message):
        super().__init__(f"{code}: {message}")
        self.code = code


# ---- ELF32 ------------------------------------------------------------------------
SHF_ALLOC = 0x2
SHT_NOBITS = 8
PT_LOAD = 1


class Elf32:
    def __init__(self, data):
        if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
            raise ValueError("not a little-endian ELF32 file")
        (self.e_type, self.e_machine, _v, self.e_entry, e_phoff, e_shoff, _f, _eh, e_phentsize,
         e_phnum, e_shentsize, e_shnum, e_shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
        if self.e_machine != 40:
            raise ValueError("not an ARM ELF")
        self.data = data
        shdrs = [struct.unpack_from("<IIIIIIIIII", data, e_shoff + i * e_shentsize) for i in range(e_shnum)]
        strtab_off = shdrs[e_shstrndx][4] if shdrs else 0
        self.sections = []
        for (name, sh_type, flags, addr, offset, size, _link, _info, align, _entsize) in shdrs:
            end = data.index(b"\0", strtab_off + name)
            self.sections.append({
                "name": data[strtab_off + name:end].decode(), "type": sh_type, "flags": flags,
                "addr": addr, "offset": offset, "size": size, "align": align,
            })
        self.segments = []
        for i in range(e_phnum):
            (p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, _flags, _align) = struct.unpack_from(
                "<IIIIIIII", data, e_phoff + i * e_phentsize)
            self.segments.append({"type": p_type, "offset": p_offset, "vaddr": p_vaddr, "paddr": p_paddr,
                                  "filesz": p_filesz, "memsz": p_memsz})

    def allocated(self):
        return [s for s in self.sections if s["flags"] & SHF_ALLOC and s["size"] > 0]

    def lma(self, section):
        """Load address of a section through its PT_LOAD segment (equals VMA if none)."""
        for seg in self.segments:
            if seg["type"] != PT_LOAD or seg["memsz"] == 0:
                continue
            if seg["vaddr"] <= section["addr"] < seg["vaddr"] + seg["memsz"]:
                return seg["paddr"] + (section["addr"] - seg["vaddr"])
        return section["addr"]

    def symbols(self):
        out = {}
        symtab = next((s for s in self.sections if s["name"] == ".symtab"), None)
        strtab = next((s for s in self.sections if s["name"] == ".strtab"), None)
        if not symtab or not strtab:
            return out
        for i in range(symtab["size"] // 16):
            st_name, st_value, _size, _info, _other, _shndx = struct.unpack_from(
                "<IIIBBH", self.data, symtab["offset"] + i * 16)
            if st_name:
                end = self.data.index(b"\0", strtab["offset"] + st_name)
                out[self.data[strtab["offset"] + st_name:end].decode()] = st_value
        return out


# ---- .dsy header ------------------------------------------------------------------
def build_header(product, version, payload, hardware=HARDWARE_ID, profile=PROFILE_ID,
                 load_address=TARGET_BASE, flags=0):
    """Returns the 160-byte header for `payload` (bytes)."""
    for value, limit, what in ((hardware, 16, "hardware"), (profile, 24, "profile"),
                               (product, 32, "product"), (version, 16, "version")):
        if not value or len(value.encode()) >= limit:
            raise PackageError(what.upper(), f"{what} must be 1..{limit - 1} bytes")
    if not ID_RE.match(product):
        raise PackageError("PRODUCT", "product id must match [a-z0-9-]{1,32}")
    if not VERSION_RE.match(version):
        raise PackageError("VERSION", "version must match [0-9A-Za-z.-]{1,16}")
    if not MIN_PAYLOAD <= len(payload) <= TARGET_SIZE:
        raise PackageError("LENGTH", f"payload must be {MIN_PAYLOAD}..{TARGET_SIZE} bytes")
    head = bytearray(HEADER_SIZE)
    head[0:8] = MAGIC
    struct.pack_into("<HHI", head, 8, HEADER_VERSION, HEADER_SIZE, flags)
    head[16:32] = hardware.encode().ljust(16, b"\0")
    head[32:56] = profile.encode().ljust(24, b"\0")
    head[56:88] = product.encode().ljust(32, b"\0")
    head[88:104] = version.encode().ljust(16, b"\0")
    struct.pack_into("<III", head, 104, load_address, HEADER_SIZE, len(payload))
    head[116:148] = hashlib.sha256(payload).digest()
    struct.pack_into("<I", head, 156, zlib.crc32(bytes(head[:156])) & 0xFFFFFFFF)
    return bytes(head)


def _field(raw, what, pattern):
    if b"\0" not in raw:
        raise PackageError(what, "string field not terminated")
    text, rest = raw.split(b"\0", 1)
    if not text or rest.strip(b"\0"):
        raise PackageError(what, "empty field or bytes after the terminator")
    text = text.decode("ascii", errors="replace")
    if not pattern.match(text):
        raise PackageError(what, f"bad characters in {text!r}")
    return text


def parse_header(data):
    """Validates a header like the firmware does; returns a dict or raises PackageError."""
    if len(data) < HEADER_SIZE:
        raise PackageError("SHORT", "fewer than 160 bytes")
    head = bytes(data[:HEADER_SIZE])
    if head[:8] != MAGIC:
        raise PackageError("MAGIC", "not a .dsy package")
    if struct.unpack_from("<I", head, 156)[0] != zlib.crc32(head[:156]) & 0xFFFFFFFF:
        raise PackageError("HDRCRC", "header CRC mismatch (damaged or truncated header)")
    version, size, flags = struct.unpack_from("<HHI", head, 8)
    if version != HEADER_VERSION:
        raise PackageError("HDRVER", f"header version {version}")
    if size != HEADER_SIZE:
        raise PackageError("HDRSIZE", f"header size {size}")
    if flags != 0:
        raise PackageError("FLAGS", f"unknown flags {flags:#x}")
    hardware = _field(head[16:32], "HARDWARE", re.compile(r"^[a-z0-9A-Z.-]+$"))
    if hardware != HARDWARE_ID:
        raise PackageError("HARDWARE", hardware)
    profile = _field(head[32:56], "PROFILE", re.compile(r"^[a-z0-9A-Z.-]+$"))
    if profile != PROFILE_ID:
        raise PackageError("PROFILE", profile)
    product = _field(head[56:88], "PRODUCT", ID_RE)
    prod_version = _field(head[88:104], "VERSION", VERSION_RE)
    load_address, offset, length = struct.unpack_from("<III", head, 104)
    if load_address != TARGET_BASE:
        raise PackageError("LOADADDR", f"{load_address:#x}")
    if offset != HEADER_SIZE:
        raise PackageError("OFFSET", str(offset))
    if not MIN_PAYLOAD <= length <= TARGET_SIZE:
        raise PackageError("LENGTH", str(length))
    if head[148:156] != b"\0" * 8:
        raise PackageError("RESERVED", "reserved words not zero")
    return {"hardware": hardware, "profile": profile, "product": product, "version": prod_version,
            "load_address": load_address, "payload_offset": offset, "payload_length": length,
            "payload_sha256": head[116:148].hex()}


def check_vectors(payload):
    sp, reset = struct.unpack_from("<II", payload)
    if not (DTCM_START < sp <= DTCM_END) or sp % 8:
        raise PackageError("VECSP", f"initial stack pointer {sp:#x} not in DTCM / unaligned")
    if not reset & 1:
        raise PackageError("VECPC", "reset vector is not Thumb")
    entry = reset & ~1
    if not (TARGET_BASE <= entry < TARGET_BASE + len(payload)):
        raise PackageError("VECPC", f"reset vector {entry:#x} outside the image")
    return sp, reset


def verify_package(data):
    """Full check of a .dsy file; returns the header dict (with 'file_sha256')."""
    header = parse_header(data)
    if len(data) != HEADER_SIZE + header["payload_length"]:
        raise PackageError("FILESIZE", f"file has {len(data)} bytes, header says {HEADER_SIZE + header['payload_length']}")
    payload = data[HEADER_SIZE:]
    if hashlib.sha256(payload).hexdigest() != header["payload_sha256"]:
        raise PackageError("HASH", "payload SHA-256 mismatch")
    check_vectors(payload)
    header["file_sha256"] = hashlib.sha256(data).hexdigest()
    return header


# ---- catalog ----------------------------------------------------------------------
def valid_path(path):
    if not path or len(path) > 160 or not PATH_CHARS.match(path) or not path.startswith("PROGRAMS/"):
        return False
    if "//" in path or path.endswith("/") or not path.endswith(".dsy"):
        return False
    parts = path.split("/")
    if any(p.strip(".") == "" for p in parts):
        return False
    return len(parts[-1]) >= 5


def valid_name(name):
    if not name:
        return False
    try:
        raw = name.encode("utf-8")
    except UnicodeEncodeError:
        return False
    return len(raw) <= 64 and all(ord(c) >= 0x20 and ord(c) != 0x7F for c in name)


def validate_catalog(doc):
    """Validates a parsed catalog document (dict) with the firmware's rules.
    Returns the list of entries; raises PackageError."""
    if not isinstance(doc, dict):
        raise PackageError("SYNTAX", "root must be an object")
    for key in ("format", "generation", "hardware", "programs"):
        if key not in doc:
            raise PackageError("MISSING", f"missing key {key!r}")
    if doc["format"] != CATALOG_FORMAT or isinstance(doc["format"], bool):
        raise PackageError("FORMAT", str(doc["format"]))
    gen = doc["generation"]
    if isinstance(gen, bool) or not isinstance(gen, int) or not 0 <= gen <= 0xFFFFFFFF:
        raise PackageError("GENERATION", str(gen))
    if doc["hardware"] != HARDWARE_ID:
        raise PackageError("HARDWARE", str(doc["hardware"]))
    programs = doc["programs"]
    if not isinstance(programs, list):
        raise PackageError("PROGRAMS", "programs must be a list")
    if len(programs) > CATALOG_MAX_PROGRAMS:
        raise PackageError("TOOMANY", f"{len(programs)} entries, at most {CATALOG_MAX_PROGRAMS}")
    slots, ids = set(), set()
    for i, e in enumerate(programs):
        if not isinstance(e, dict):
            raise PackageError("PROGRAMS", f"entry {i} is not an object")
        for key in ("slot", "id", "name", "version", "path", "payload_sha256"):
            if key not in e:
                raise PackageError("MISSING", f"entry {i}: missing {key!r}")
        slot = e["slot"]
        if isinstance(slot, bool) or not isinstance(slot, int) or not 1 <= slot <= CATALOG_MAX_PROGRAMS:
            raise PackageError("SLOT", f"entry {i}: slot {slot!r}")
        if slot in slots:
            raise PackageError("DUPSLOT", f"slot {slot} twice")
        slots.add(slot)
        if not isinstance(e["id"], str) or not ID_RE.match(e["id"]):
            raise PackageError("ID", f"entry {i}: id {e['id']!r}")
        if e["id"] in ids:
            raise PackageError("DUPID", f"id {e['id']} twice")
        ids.add(e["id"])
        if not isinstance(e["name"], str) or not valid_name(e["name"]):
            raise PackageError("NAME", f"entry {i}: name {e['name']!r}")
        if e.get("category", "other") not in CATEGORIES:
            raise PackageError("CATEGORY", f"entry {i}: {e.get('category')!r}")
        if not isinstance(e["version"], str) or not VERSION_RE.match(e["version"]):
            raise PackageError("VERSION", f"entry {i}: version {e['version']!r}")
        if not isinstance(e["path"], str) or not valid_path(e["path"]):
            raise PackageError("PATH", f"entry {i}: path {e['path']!r}")
        if not isinstance(e["payload_sha256"], str) or not HASH_RE.match(e["payload_sha256"]):
            raise PackageError("HASH", f"entry {i}: payload_sha256")
        if "file_sha256" in e and not (isinstance(e["file_sha256"], str) and HASH_RE.match(e["file_sha256"])):
            raise PackageError("HASH", f"entry {i}: file_sha256")
        if e.get("profile", PROFILE_ID) != PROFILE_ID:
            raise PackageError("PROFILE", f"entry {i}: {e.get('profile')!r}")
    return programs


def load_catalog(path):
    data = pathlib.Path(path).read_bytes()
    if len(data) > CATALOG_MAX_BYTES:
        raise PackageError("TOOLARGE", f"{len(data)} bytes, at most {CATALOG_MAX_BYTES}")
    try:
        doc = json.loads(data.decode("utf-8-sig"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise PackageError("SYNTAX", str(exc)) from exc
    validate_catalog(doc)
    return doc


def dump_catalog(doc):
    text = json.dumps(doc, indent=2, ensure_ascii=False) + "\n"
    if len(text.encode("utf-8")) > CATALOG_MAX_BYTES:
        raise PackageError("TOOLARGE", "catalog exceeds 16 KiB")
    return text


def repo_root(start=None):
    """Repository root: the first parent of `start` (or this file) holding components/registry.json."""
    p = pathlib.Path(start or __file__).resolve()
    for parent in [p] + list(p.parents):
        if (parent / "components" / "registry.json").exists():
            return parent
    return None


def registry_product(root, project):
    """Product id, version, image prefix and marker of a BUILDABLE project from
    components/registry.json and its version source. Raises PackageError if unknown."""
    reg = json.loads((root / "components" / "registry.json").read_text(encoding="utf-8"))
    product = reg["products"].get(project)
    if product is None:
        raise PackageError("PRODUCT", f"{project} is not a product in components/registry.json")
    vspec = product.get("version")
    if vspec is None:
        comp = reg["components"][product["version_component"]]
        vspec = comp["version"]
    if vspec["type"] == "macro":
        text = (root / vspec["path"]).read_text(encoding="utf-8")
        m = re.search(re.escape(vspec["name"]) + r'\s+"([^"]+)"', text)
        if not m:
            raise PackageError("VERSION", f"{vspec['name']} not found in {vspec['path']}")
        version = m.group(1)
    elif vspec["type"] == "constants":
        text = (root / vspec["path"]).read_text(encoding="utf-8")
        parts = []
        for name in vspec["names"]:
            m = re.search(re.escape(name) + r"\s*=\s*(\d+)", text)
            if not m:
                raise PackageError("VERSION", f"{name} not found in {vspec['path']}")
            parts.append(m.group(1))
        version = ".".join(parts)
    else:
        raise PackageError("VERSION", f"unsupported version source {vspec['type']}")
    return {"id": project, "version": version, "image": product["image"], "marker": product["marker"],
            "directory": product["directory"]}
