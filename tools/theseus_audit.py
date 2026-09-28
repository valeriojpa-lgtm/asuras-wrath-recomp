#!/usr/bin/env python3
"""T03 Theseus dependency audit.

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

REX_FILESYSTEM_PATTERNS = (
    re.compile(r"#\s*include\s*[<\"]rex/filesystem"),
    re.compile(r"\brex::filesystem::"),
    re.compile(r"\bHostPathDevice\b"),
    re.compile(r"\bDiscImageDevice\b"),
)


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return ""


def count_patterns(path: Path, patterns) -> int:
    text = read_text(path)
    return sum(len(pattern.findall(text)) for pattern in patterns)


def count_rex_refs(path: Path) -> int:
    return count_patterns(path, REX_PATTERNS)


def count_rex_filesystem_refs(path: Path) -> int:
    return count_patterns(path, REX_FILESYSTEM_PATTERNS)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default="out/theseus/THESEUS_STATUS.txt")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    src = repo / "src"
    platform = src / "platform"
    compat_bridge = src / "compat" / "rexglue_filesystem_bridge.cpp"
    compat_bridge_header = src / "compat" / "rexglue_filesystem_bridge.h"

    host_files = sorted(
        p for p in src.rglob("*")
        if p.is_file() and p.suffix.lower() in {".cpp", ".cc", ".cxx", ".h", ".hpp"}
    )
    platform_files = [p for p in host_files if platform in p.parents]

    counts = [(p, count_rex_refs(p)) for p in host_files]
    counts = [(p, n) for p, n in counts if n]
    platform_refs = sum(count_rex_refs(p) for p in platform_files)
    total_refs = sum(n for _, n in counts)

    fs_counts = [(p, count_rex_filesystem_refs(p)) for p in host_files]
    fs_counts = [(p, n) for p, n in fs_counts if n]
    allowed_fs_bridge = {compat_bridge, compat_bridge_header}
    fs_refs_outside_bridge = sum(
        n for p, n in fs_counts if p not in allowed_fs_bridge
    )
    fs_bridge_refs = sum(
        n for p, n in fs_counts if p in allowed_fs_bridge
    )

    boundary = "PASS" if platform_refs == 0 else "FAIL"
    filesystem_boundary = "PASS" if fs_refs_outside_bridge == 0 else "FAIL"

    lines = [
        "ASURA'S WRATH - THESEUS STATUS",
        "==============================",
        "Milestone: T03-native-filesystem",
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
        f"ReXGlue filesystem isolation: {filesystem_boundary}",
        f"ReXGlue filesystem refs outside compatibility bridge: {fs_refs_outside_bridge}",
        f"ReXGlue filesystem refs inside compatibility bridge: {fs_bridge_refs}",
        "",
        "Native host facilities (T03):",
        "  portable paths : native",
        "  config         : native",
        "  filesystem     : native",
        "",
        "Runtime service backends:",
        "  filesystem : native host / rexglue guest-path bridge",
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
        "T03 invariant:",
        "  Physical PC filesystem I/O is Theseus-owned.",
        "  ReXGlue filesystem usage is isolated to one temporary guest-path bridge.",
        "  ISO/GDFX import also remains in that compatibility bridge.",
        "  The bridge is not counted as removed until guest Xbox file ABI is replaced.",
        "",
    ]

    output = repo / args.out
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8")
    print(output.read_text(encoding="utf-8"))

    if platform_refs:
        print("ERROR: src/platform must remain ReXGlue-independent.", file=sys.stderr)
        return 2
    if fs_refs_outside_bridge:
        print(
            "ERROR: ReXGlue filesystem references escaped the T03 compatibility bridge.",
            file=sys.stderr,
        )
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
