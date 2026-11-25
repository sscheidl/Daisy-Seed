#!/usr/bin/env python3
"""Check a rename commit against an approved map and its pinned base."""

import argparse
from difflib import SequenceMatcher
from fnmatch import fnmatchcase
from functools import lru_cache
from itertools import zip_longest
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys


DOC_SUFFIXES = {".md", ".html", ".pdf"}
LINK = re.compile(
    r"(?<!\\)\[[^\]\n]*\]\([ \t]*(?:<([^>\n]+)>|([^\s<>()]+))"
    r"""(?=[ \t]*(?:(?:"[^"]*"|'[^']*'|\([^)]*\))[ \t]*)?\))"""
    r"|^ {0,3}\[[^\]\n]+\]:[ \t]*(?:<([^>\n]+)>|([^\s<>]+))"
    r"""|(?i:<a\b[^>]*\bhref[ \t]*=[ \t]*)(?:"([^"]+)"|'([^']+)')"""
)
CODE_SPAN = re.compile(r"(?<!`)(`+)(?!`).*?(?<!`)\1(?!`)")
FENCE = re.compile(r"^ {0,3}(`{3,}|~{3,})(.*)$")


def git(*args):
    return subprocess.check_output(("git", *args))


def tree(ref):
    commit = git("rev-parse", "--verify", "--end-of-options", f"{ref}^{{commit}}").decode().strip()
    entries = {}
    for entry in git("ls-tree", "-rz", commit).split(b"\0")[:-1]:
        metadata, path = entry.split(b"\t", 1)
        mode, _, sha = metadata.decode().split()
        entries[path.decode("utf-8", "surrogateescape")] = mode, sha
    return entries


@lru_cache(maxsize=64)
def blob(entry):
    return git("cat-file", "blob", entry[1]) if entry and entry[0] != "160000" else b""


def move(path, directories):
    for old in sorted(directories, key=len, reverse=True):
        if path.startswith(old + "/"):
            return directories[old] + path[len(old):]
    return path


def glob_matches(path, pattern):
    parts, globs = path.split("/"), pattern.split("/")

    def match(i, j):
        if j == len(globs):
            return i == len(parts)
        if globs[j] == "**":
            return any(match(k, j + 1) for k in range(i, len(parts) + 1))
        return i < len(parts) and fnmatchcase(parts[i], globs[j]) and match(i + 1, j + 1)

    return match(0, 0)


def substitutions(renames, keep=()):
    # Each alternative consumes an original occurrence once. An occurrence can
    # also stay as it was, for example a variable with the same name as a CI key.
    rules = [(re.escape(item), None) for item in sorted(keep, key=len, reverse=True)]
    for old, new in sorted(renames.get("directories", {}).items(), key=lambda pair: -len(pair[0])):
        rules.append((re.escape(old), new))
    for kind in ("project_ids", "image_prefixes", "ci_suite_keys"):
        for old, new in sorted(renames.get(kind, {}).items(), key=lambda pair: -len(pair[0])):
            start, end = r"(?<![\w-])", r"(?![\w-])"
            if kind == "project_ids":
                start = r"(?:(?<![\w-])|(?<=PROJECT_DIR_)|(?<=PROJECT_ARGS_)|(?<=PROJECT_STATUS_))"
            if kind == "image_prefixes":
                end = r"(?:(?![\w-])|(?=-v(?:[0-9<{$\s'\"]|$)))"
            rules.append((start + re.escape(old) + end, new))
    if not rules:
        return re.compile(r"(?!)"), []
    return re.compile("|".join(f"({pattern})" for pattern, _ in rules)), [new for _, new in rules]


def line_pattern(line, rules):
    pattern, replacements = rules
    start = 0
    pieces = []
    for match in pattern.finditer(line):
        original = re.escape(match[0])
        replacement = replacements[match.lastindex - 1]
        pieces.append(re.escape(line[start:match.start()]))
        pieces.append(original if replacement is None else f"(?:{original}|{re.escape(replacement)})")
        start = match.end()
    return "".join(pieces) + re.escape(line[start:])


def historical_patterns(lines, rules):
    fence = ""
    for line in lines:
        marker = FENCE.match(line)
        if fence or marker:
            if not fence:
                fence = marker[1]
            elif marker and marker[1].startswith(fence) and not marker[2].strip():
                fence = ""
            yield re.escape(line)
            continue
        code = [match.span() for match in CODE_SPAN.finditer(line)]
        start = 0
        pieces = []
        for link in LINK.finditer(line):
            left, _ = link.span(link.lastindex)
            target = re.split(r"[?#]", link[link.lastindex], maxsplit=1)[0]
            if (re.match(r"[/\\]|[a-zA-Z][\w+.-]*:", target)
                    or any(begin <= link.start() < end for begin, end in code)):
                continue
            pieces.extend((re.escape(line[start:left]), line_pattern(target, rules)))
            start = left + len(target)
        yield "".join(pieces) + re.escape(line[start:])


def lint(mapping, head_ref):
    base, head = tree(mapping["base_commit"]), tree(head_ref)
    rename = mapping["rename"]
    directories = rename["directories"]
    base_paths = {}
    errors = []

    def report(path, line, reason):
        errors.append(f"{path}:{line}: {reason}")

    for old in base:
        path = move(old, directories)
        if path in base_paths:
            report(path, 1, f"rename collides with {base_paths[path]} and {old}")
        base_paths[path] = old
        if path != old and path not in head:
            report(path, 1, f"missing required rename of {old}")
    for path in head:
        if move(path, directories) != path:
            report(path, 1, "file remains under an old directory")

    rules = substitutions(rename, mapping.get("keep", ()))
    link_rules = substitutions({"directories": directories})
    historical = set(mapping["historical_docs"])
    allowances = [(entry["path"], re.compile(entry["pattern"])) for entry in mapping["allowed_changes"]]
    frozen = mapping["frozen"]

    for path in sorted(base_paths.keys() | head.keys()):
        old, new = base.get(base_paths.get(path)), head.get(path)
        if old == new:
            continue
        if any(glob_matches(path, pattern) for pattern in frozen):
            report(path, 1, "frozen file changed, was added or was removed")
        is_historical = path in historical
        if not is_historical and PurePosixPath(path).suffix.lower() in DOC_SUFFIXES:
            continue
        if (old and new and old[0] != new[0]) or any(entry and entry[0] == "160000" for entry in (old, new)):
            report(path, 1, "file mode or submodule changed")
            continue
        old_data, new_data = blob(old), blob(new)
        if b"\0" in old_data or b"\0" in new_data:
            report(path, 1, "binary file changed")
            continue
        if bool(old) != bool(new) and not (old_data or new_data):
            report(path, 1, "empty file added or removed")
            continue

        allowed = [pattern for glob, pattern in allowances if glob_matches(path, glob)] if not is_historical else []
        before, after = (
            [(i, line) for i, line in enumerate(data.decode("utf-8", "surrogateescape").splitlines(keepends=True), 1)
             if not any(pattern.search(line.rstrip("\r\n")) for pattern in allowed)]
            for data in (old_data, new_data)
        )
        old_lines = [line for _, line in before]
        if is_historical:
            old_patterns = list(historical_patterns(old_lines, link_rules))
        else:
            old_patterns = [line_pattern(line, rules) for line in old_lines]
        new_lines = [line for _, line in after]
        # Substitutions preserve line count. Align by position so newly
        # identical lines can't be mistaken for insertions or deletions.
        if len(old_lines) == len(new_lines):
            changes = [("replace", 0, len(old_lines), 0, len(new_lines))]
        else:
            changes = SequenceMatcher(None, old_lines, new_lines, autojunk=False).get_opcodes()
        for tag, i, j, k, end in changes:
            if tag == "equal":
                continue
            for a, b in zip_longest(range(i, j), range(k, end)):
                if a is not None and b is not None and (
                    old_lines[a] == new_lines[b] or re.fullmatch(old_patterns[a], new_lines[b])
                ):
                    continue
                number = after[b][0] if b is not None else before[a][0]
                reason = "historical edit outside a rename in a link target" if is_historical else "edit outside the rename map"
                report(path, number, reason)

    for entry in mapping["protected"]:
        path, needle, count = entry["path"], entry["string"].encode("utf-8"), entry["count"]
        old_path = base_paths.get(path, path)
        for side, file_path, entries in (("base", old_path, base), ("head", path, head)):
            data = blob(entries.get(file_path))
            found = data.count(needle)
            if file_path not in entries or found != count:
                offset = data.find(needle)
                number = 1 + data.count(b"\n", 0, offset) if offset >= 0 else 1
                report(file_path, number, f"{side} protected {entry['string']!r}: expected {count}, found {found}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", type=Path, required=True)
    parser.add_argument("--head", required=True)
    args = parser.parse_args()
    try:
        errors = lint(json.loads(args.map.read_bytes()), args.head)
    except (OSError, ValueError, KeyError, re.error, subprocess.CalledProcessError) as error:
        print(f"lint_diff.py: {error}", file=sys.stderr)
        return 1
    for error in errors:
        print(error, file=sys.stderr)
    if not errors:
        print(f"PASS rename diff at {args.head}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
