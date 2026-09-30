#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GENERATED = ROOT / "generated" / "default"

# Exact guest-code replacements from Xenia Canary's
# "Resolution Scaling Fix / Disable All Post-Processing" for title 43430817.
# Applied to generated C++ only for this diagnostic build; source XEX remains untouched.
PATCHES = (
    (0x8273F440, 0x8273F610, "ctx.r5.s64 = 1;", "li r5,1"),
    (0x8273F440, 0x8273F76C, "ctx.r5.s64 = 0;", "li r5,0"),
    (0x8273F880, 0x8273F8D8, "ctx.r4.s64 = 4;", "li r4,4"),
    (0x82741EB0, 0x82741ED4, "ctx.r5.s64 = 0;", "li r5,0"),
    (0x82741EB0, 0x82741EF0, "(void)0;", "nop"),
)

FUNC_RE = re.compile(r"DEFINE_REX_FUNC\(sub_([0-9A-Fa-f]{8})\)")
INSTR_RE = re.compile(r"^\s*//\s+(.+?)\s*$")
LABEL_RE = re.compile(r"^\s*loc_[0-9A-Fa-f]+:\s*$")


def find_function_file(function_address: int) -> tuple[Path, list[str], int]:
    needle = f"DEFINE_REX_FUNC(sub_{function_address:08X})"
    for path in GENERATED.glob("asura_wrath_recomp.*.cpp"):
        lines = path.read_text(encoding="utf-8", errors="strict").splitlines()
        for i, line in enumerate(lines):
            if needle in line:
                return path, lines, i
    raise RuntimeError(f"Function 0x{function_address:08X} not found")


def locate_instruction(lines: list[str], function_line: int, function_address: int,
                       target_address: int) -> tuple[int, int, str]:
    address = function_address
    i = function_line + 1
    while i < len(lines):
        line = lines[i]
        if i > function_line + 1 and line.startswith("DEFINE_REX_FUNC("):
            break
        match = INSTR_RE.match(line)
        if match:
            if address == target_address:
                start = i
                end = i + 1
                while end < len(lines):
                    if INSTR_RE.match(lines[end]) or LABEL_RE.match(lines[end]):
                        break
                    if lines[end].strip() == "}":
                        break
                    end += 1
                return start, end, match.group(1)
            address += 4
        i += 1
    raise RuntimeError(
        f"Instruction 0x{target_address:08X} not found in function "
        f"0x{function_address:08X}"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--report-only", action="store_true")
    args = parser.parse_args()

    grouped: dict[Path, list[tuple[int, int, str, str, int]]] = {}

    for function_address, target_address, replacement, replacement_asm in PATCHES:
        path, lines, function_line = find_function_file(function_address)
        start, end, original_asm = locate_instruction(
            lines, function_line, function_address, target_address
        )
        print(
            f"0x{target_address:08X}: {path.name}:{start + 1}: "
            f"{original_asm} -> {replacement_asm}"
        )
        grouped.setdefault(path, []).append(
            (start, end, replacement, replacement_asm, target_address)
        )

    if args.report_only:
        return 0

    for path, patches in grouped.items():
        lines = path.read_text(encoding="utf-8", errors="strict").splitlines()
        # Replace backwards so line numbers remain valid.
        for start, end, replacement, replacement_asm, target_address in sorted(
            patches, reverse=True
        ):
            indent = re.match(r"^(\s*)", lines[start]).group(1)
            new_lines = [
                f"{indent}// THESEUS T11.2 diagnostic @ 0x{target_address:08X}: {replacement_asm}",
                f"{indent}{replacement}",
            ]
            lines[start:end] = new_lines
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    print("Applied Asura's Wrath scaled-resolution postprocess diagnostic patch.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
