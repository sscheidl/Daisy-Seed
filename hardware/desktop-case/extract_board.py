"""Extract mechanical datums with KiCad's bundled Python/pcbnew (read only)."""

import argparse
import hashlib
import json
from pathlib import Path

import pcbnew


def extract(path):
    if not path.is_file():
        raise FileNotFoundError(path)
    board = pcbnew.LoadBoard(str(path))
    mm = pcbnew.ToMM
    # Edge centre lines, not GetBoardEdgesBoundingBox (which includes line width).
    edges = [s for s in board.GetDrawings() if s.GetLayer() == pcbnew.Edge_Cuts]
    points = [p for s in edges for p in (s.GetStart(), s.GetEnd())]
    x0, y0 = min(p.x for p in points), min(p.y for p in points)
    x1, y1 = max(p.x for p in points), max(p.y for p in points)
    groups = {k: [] for k in ("mounts", "pots", "buttons", "leds", "toggles")}
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        group = next((g for prefix, g in (("H", "mounts"), ("VR", "pots"),
                     ("S", "buttons"), ("D", "leds"), ("SW", "toggles"))
                     if ref.startswith(prefix) and ref[len(prefix):].isdigit()
                     and (g != "leds" or int(ref[1:]) <= 16)), None)
        if group:
            pos = fp.GetPosition()
            item = dict(ref=ref, x=round(mm(pos.x - x0), 6),
                        y=round(mm(pos.y - y0), 6))
            if group == "mounts":
                item["drill"] = mm(next(iter(fp.Pads())).GetDrillSize().x)
            groups[group].append(item)
    for values in groups.values():
        values.sort(key=lambda v: int(''.join(c for c in v['ref'] if c.isdigit())))
    assert [len(groups[k]) for k in groups] == [4, 8, 16, 16, 2], 'Unexpected controls'
    return dict(width=mm(x1-x0), depth=mm(y1-y0),
                thickness=mm(board.GetDesignSettings().GetBoardThickness()),
                **groups)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("boards", nargs="+", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    layouts = [extract(p) for p in args.boards]
    assert all(x == layouts[0] for x in layouts), "Mechanical revisions differ"
    result = dict(source_commit="987297fb7c9a8c58721ef40abf6d6eedd1fcda2a",
                  source_repository="https://github.com/daisyaudio/Seed3-DevKit-Desktop",
                  origin="PCB rear-left; x right, y toward player; millimetres",
                  sources=[dict(file=p.name, sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                           for p in args.boards], **layouts[-1])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Compared {len(layouts)} revisions: {result['width']} x {result['depth']} mm")


if __name__ == "__main__":
    main()
