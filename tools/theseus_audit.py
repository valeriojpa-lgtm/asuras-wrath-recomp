#!/usr/bin/env python3
"""T01 Theseus dependency audit.

This intentionally audits only host-side source. Generated PPC guest code is
not counted: it is the preserved game logic, not the platform boundary.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys

REX_PATTERNS = (
    re.compile(r"#\s*include\s*[<\"]rex/"),
    re.compile(r"\brex::"),
    re.compile(r"\bREX[A-Z0-9_]+"),
)


def count_rex_refs(path: Path) -> int:
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return 0
    return sum(len(pattern.findall(text)) for pattern in REX_PATTERNS)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default="out/theseus/THESEUS_STATUS.txt")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    src = repo / "src"
    platform = src / "platform"

    host_files = sorted(
        p for p in src.rglob("*")
        if p.is_file() and p.suffix.lower() in {".cpp", ".cc", ".cxx", ".h", ".hpp"}
    )
    platform_files = [p for p in host_files if platform in p.parents]

    counts = [(p, count_rex_refs(p)) for p in host_files]
    counts = [(p, n) for p, n in counts if n]
    platform_refs = sum(count_rex_refs(p) for p in platform_files)
    total_refs = sum(n for _, n in counts)

    boundary = "PASS" if platform_refs == 0 else "FAIL"

    lines = [
        "ASURA'S WRATH - THESEUS STATUS",
        "==============================",
        "Milestone: T01-platform-bootstrap",
        "",
        "Portable contract:",
        "  Root/Data/Game/Content",
        "  Root/Data/Game/Cinematics",
        "  Root/Data/Game/Xbox360TOC.txt",
        "  Root/UserData/{cache,Config,Logs,Saves}",
        "",
        f"Theseus platform boundary: {boundary}",
        f"Direct ReXGlue refs inside src/platform: {platform_refs}",
        f"Direct ReXGlue refs in host-side src: {total_refs}",
        "",
        "Service backends (T01):",
        "  filesystem : rexglue",
        "  input      : rexglue",
        "  saves      : rexglue",
        "  video      : rexglue",
        "  audio      : rexglue",
        "  timing     : rexglue",
        "  threading  : rexglue",
        "  graphics   : rexglue",
        "",
        "Host files with direct ReXGlue references:",
    ]

    if counts:
        for path, count in counts:
            lines.append(f"  {path.relative_to(repo).as_posix():48} {count:5d}")
    else:
        lines.append("  (none)")

    lines += [
        "",
        "T01 invariant:",
        "  Platform boundary exists; runtime behavior remains RUN04-compatible.",
        "  Later milestones replace one backend at a time and keep A/B fallback.",
        "",
    ]

    output = repo / args.out
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8")
    print(output.read_text(encoding="utf-8"))

    if platform_refs:
        print("ERROR: src/platform must remain ReXGlue-independent.", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
