#!/usr/bin/env python3
"""T08 Theseus dependency audit.

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

    patch_script = read_text(repo / "tools" / "apply_win_startup_fix.py")
    save_bridge_markers = (
        "T04 ContentManager native save root",
        "T04 Theseus profile identity",
        "T04 auto-confirm first save creation",
        'REXCVAR_DEFINE_BOOL(asura_first_run, false, "Theseus"',
    )
    missing_save_markers = [
        marker for marker in save_bridge_markers if marker not in patch_script
    ]
    save_bridge_boundary = "PASS" if not missing_save_markers else "FAIL"

    input_bridge_markers = (
        "T05 Theseus cursor policy cvar",
        "T05 hide cursor on focused game window",
        "T05 restore cursor on focus loss",
    )
    missing_input_markers = [
        marker for marker in input_bridge_markers if marker not in patch_script
    ]
    input_bridge_boundary = "PASS" if not missing_input_markers else "FAIL"

    input_policy_header = read_text(repo / "src" / "platform" / "theseus_input.h")
    input_policy_boundary = (
        "PASS"
        if "class NativeInputPolicy" in input_policy_header
        and "struct InputBindings" in input_policy_header
        else "FAIL"
    )

    audio_policy_header = read_text(repo / "src" / "platform" / "theseus_audio.h")
    audio_policy_boundary = (
        "PASS"
        if "class NativeAudioPolicy" in audio_policy_header
        and "struct AudioPolicyState" in audio_policy_header
        else "FAIL"
    )
    audio_bridge_markers = (
        "T06 host audio app identity",
    )
    missing_audio_markers = [
        marker for marker in audio_bridge_markers if marker not in patch_script
    ]
    audio_bridge_boundary = "PASS" if not missing_audio_markers else "FAIL"

    timing_policy_header = read_text(repo / "src" / "platform" / "theseus_timing.h")
    timing_bridge_source = read_text(repo / "src" / "compat" / "theseus_timing_bridge.cpp")
    timing_policy_boundary = (
        "PASS"
        if "class NativeTimingPolicy" in timing_policy_header
        and "TimingPolicyState" in timing_policy_header
        else "FAIL"
    )
    timing_bridge_rex_refs = sum(
        len(pattern.findall(timing_bridge_source)) for pattern in REX_PATTERNS
    )
    timing_bridge_markers = (
        "T07 delegate ReXGlue clock surface to Theseus",
        "T07 delegate Windows host clock to Theseus",
    )
    missing_timing_markers = [
        marker for marker in timing_bridge_markers if marker not in patch_script
    ]
    timing_bridge_boundary = (
        "PASS"
        if not missing_timing_markers and timing_bridge_rex_refs == 0
        else "FAIL"
    )

    threading_service_header = read_text(
        repo / "src" / "platform" / "theseus_threading.h"
    )
    sync_bridge_source = read_text(
        repo / "src" / "compat" / "theseus_sync_bridge.cpp"
    )
    threading_policy_boundary = (
        "PASS"
        if "class NativeThreadingService" in threading_service_header
        and "host_sync_native" in threading_service_header
        else "FAIL"
    )
    sync_bridge_rex_refs = sum(
        len(pattern.findall(sync_bridge_source)) for pattern in REX_PATTERNS
    )
    sync_bridge_markers = (
        "T08 synchronization bridge include",
        "T08 delegate yield sleep and TLS to Theseus",
        "T08 delegate native handle lifetime to Theseus",
        "T08 delegate waits to Theseus",
        "T08 delegate events to Theseus",
        "T08 delegate semaphores to Theseus",
        "T08 delegate mutexes to Theseus",
        "T08 delegate one-shot timers to Theseus",
        "T08 delegate repeating timers to Theseus",
        "T08 delegate timer cancellation to Theseus",
        "T08 delegate manual timers to Theseus",
        "T08 delegate synchronization timers to Theseus",
    )
    missing_sync_markers = [
        marker for marker in sync_bridge_markers if marker not in patch_script
    ]
    sync_bridge_boundary = (
        "PASS"
        if not missing_sync_markers and sync_bridge_rex_refs == 0
        else "FAIL"
    )

    lines = [
        "ASURA'S WRATH - THESEUS STATUS",
        "==============================",
        "Milestone: T08-native-threading-sync",
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
        f"Save/profile compatibility bridge: {save_bridge_boundary}",
        f"Native input policy boundary: {input_policy_boundary}",
        f"Input compatibility bridge: {input_bridge_boundary}",
        f"Native audio policy boundary: {audio_policy_boundary}",
        f"Audio compatibility bridge: {audio_bridge_boundary}",
        "",
        "Native host facilities (T06):"
        "  portable paths : native",
        "  config         : native",
        "  filesystem     : native",
        "  saves/profile  : native",
        "  input policy   : native",
        "  audio policy   : native",
        "  timing         : native",
        "  threading sync : native",
        "",
        "Runtime service backends:",
        "  filesystem : native host / rexglue guest-path bridge",
        "  input      : native policy / rexglue physical-driver + XAM bridge",
        "  saves      : native host / rexglue XAM bridge",
        "  video      : guest Bink code / graphics path (not a standalone ReXGlue decoder)",
        "  audio      : native policy / rexglue XMA + SDL bridge",
        "  timing     : native clock / rexglue Xbox timing-export ABI bridge",
        "  threading  : native host sync / rexglue XThread+APC+DPC ABI bridge",
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
        "T08 invariant:",
        "  Host yield/sleep/TLS and synchronization waits are Theseus-owned.",
        "  Host Event/Semaphore/Mutex/WaitableTimer operations are Theseus-owned.",
        "  ReXGlue retains public rex::thread wrappers and Xbox kernel object ABI.",
        "  XThread lifecycle, APC/DPC and guest scheduling semantics remain compatibility code.",
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
    if missing_save_markers:
        print(
            "ERROR: T04 save/profile bridge markers missing: "
            + ", ".join(missing_save_markers),
            file=sys.stderr,
        )
        return 4
    if missing_input_markers or input_policy_boundary != "PASS":
        print(
            "ERROR: T05 native input policy/bridge is incomplete.",
            file=sys.stderr,
        )
        return 5
    if missing_audio_markers or audio_policy_boundary != "PASS":
        print(
            "ERROR: T06 native audio policy/bridge is incomplete.",
            file=sys.stderr,
        )
        return 6
    if (
        missing_timing_markers
        or timing_policy_boundary != "PASS"
        or timing_bridge_rex_refs != 0
    ):
        print(
            "ERROR: T07 native timing core/bridge is incomplete or contaminated.",
            file=sys.stderr,
        )
        return 7
    if (
        missing_sync_markers
        or threading_policy_boundary != "PASS"
        or sync_bridge_rex_refs != 0
    ):
        print(
            "ERROR: T08 native synchronization core/bridge is incomplete or contaminated.",
            file=sys.stderr,
        )
        return 8
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
