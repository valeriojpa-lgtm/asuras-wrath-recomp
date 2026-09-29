#!/usr/bin/env python3
"""T09 PE hardening audit for the built Asura executable."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

IMAGE_FILE_MACHINE_AMD64 = 0x8664
IMAGE_FILE_LARGE_ADDRESS_AWARE = 0x0020
IMAGE_NT_OPTIONAL_HDR64_MAGIC = 0x20B
IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA = 0x0020
IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE = 0x0040
IMAGE_DLLCHARACTERISTICS_NX_COMPAT = 0x0100
IMAGE_SUBSYSTEM_WINDOWS_GUI = 2


def read_pe(path: Path) -> dict[str, int | bool]:
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise ValueError("missing MZ header")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ValueError("missing PE signature")

    coff = pe_offset + 4
    machine, _sections, _timestamp, _symptr, _symbols, optional_size, characteristics = (
        struct.unpack_from("<HHIIIHH", data, coff)
    )
    optional = coff + 20
    magic = struct.unpack_from("<H", data, optional)[0]
    if magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC:
        raise ValueError(f"expected PE32+, got optional-header magic 0x{magic:04X}")
    if optional_size < 112:
        raise ValueError("optional header too small")

    subsystem = struct.unpack_from("<H", data, optional + 68)[0]
    dll_characteristics = struct.unpack_from("<H", data, optional + 70)[0]
    stack_reserve = struct.unpack_from("<Q", data, optional + 72)[0]
    stack_commit = struct.unpack_from("<Q", data, optional + 80)[0]
    heap_reserve = struct.unpack_from("<Q", data, optional + 88)[0]
    heap_commit = struct.unpack_from("<Q", data, optional + 96)[0]

    return {
        "machine": machine,
        "magic": magic,
        "characteristics": characteristics,
        "subsystem": subsystem,
        "dll_characteristics": dll_characteristics,
        "stack_reserve": stack_reserve,
        "stack_commit": stack_commit,
        "heap_reserve": heap_reserve,
        "heap_commit": heap_commit,
        "laa": bool(characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE),
        "dynamic_base": bool(
            dll_characteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE
        ),
        "nx": bool(dll_characteristics & IMAGE_DLLCHARACTERISTICS_NX_COMPAT),
        "high_entropy_va": bool(
            dll_characteristics & IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("exe", type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()

    pe = read_pe(args.exe)
    checks = {
        "AMD64": pe["machine"] == IMAGE_FILE_MACHINE_AMD64,
        "PE32+": pe["magic"] == IMAGE_NT_OPTIONAL_HDR64_MAGIC,
        "LargeAddressAware": pe["laa"],
        "ASLR": pe["dynamic_base"],
        "NX": pe["nx"],
        "HighEntropyVA": pe["high_entropy_va"],
        "WindowsGuiSubsystem": pe["subsystem"] == IMAGE_SUBSYSTEM_WINDOWS_GUI,
    }

    lines = [
        "ASURA'S WRATH - THESEUS T09 PE AUDIT",
        "=====================================",
        f"File: {args.exe}",
        f"Machine: 0x{int(pe['machine']):04X} (AMD64)",
        f"Optional header: 0x{int(pe['magic']):04X} (PE32+)",
        f"Characteristics: 0x{int(pe['characteristics']):04X}",
        f"Subsystem: {pe['subsystem']} (2 = Windows GUI)",
        f"DLL characteristics: 0x{int(pe['dll_characteristics']):04X}",
        f"Stack reserve: {pe['stack_reserve']} bytes",
        f"Stack commit: {pe['stack_commit']} bytes",
        f"Heap reserve: {pe['heap_reserve']} bytes",
        f"Heap commit: {pe['heap_commit']} bytes",
        "",
    ]
    lines.extend(f"{name}: {'PASS' if value else 'FAIL'}" for name, value in checks.items())
    text = "\n".join(lines) + "\n"
    print(text, end="")

    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")

    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
