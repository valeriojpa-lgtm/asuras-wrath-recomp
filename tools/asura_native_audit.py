#!/usr/bin/env python3
"""Static integrity audit for the Asura's Wrath TU01 native recomp.

This tool intentionally distinguishes structural proof from heuristics:
- proven integrity failures exit non-zero;
- suspicious/heuristic findings are warnings or REVIEW candidates;
- nothing is auto-registered from a heuristic scan.

It only inspects project-owned/generated source. It never needs or modifies the
preserved XEX/XEXP.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Iterable

EXPECTED_PARTITIONS = 207
EXPECTED_TU_ENTRYPOINT = 0x82B75160
EXPECTED_TU_GENERATED_SHA256 = "29c9213ee11318b119dee60335b1dfe81f3dee740063c754b7967ac8c311b91a"

TU_FUNC_REMAPS = {
    0x829CF278: 0x829CF288,
    0x82C3E688: 0x82C3E698,
    0x82C544E0: 0x82C544F0,
}

# Historical runtime observation. This is deliberately REVIEW-only unless the
# static evidence proves more; CI must never turn a past crash address into an
# invented function.
KNOWN_REVIEW_TARGETS = {0x829C9BD0}

DECL_RE = re.compile(r"\bDECLARE_REX_FUNC\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)")
DEF_RE = re.compile(r"\bDEFINE_REX_FUNC\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)")
REGISTER_RE = re.compile(
    r"\bSetFunction\(\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)"
)
MAPPING_RE = re.compile(
    r"\{\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*([A-Za-z_][A-Za-z0-9_]*)\s*\}"
)
DIRECT_CALL_RE = re.compile(r"\bREX_CALL_FUNC\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)")
INDIRECT_LITERAL_RE = re.compile(
    r"\bREX_CALL_INDIRECT_FUNC\(\s*(0x[0-9A-Fa-f]+|\d+)\s*\)"
)
HEX_FUNC_RE = re.compile(r"^sub_([0-9A-Fa-f]{8})$")


def parse_int(text: str) -> int:
    return int(text, 0)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


class Audit:
    def __init__(self) -> None:
        self.checks: list[dict] = []
        self.candidates = {
            "PROVEN": [],
            "HIGH_CONFIDENCE": [],
            "REVIEW": [],
            "REJECT": [],
        }

    def add(self, severity: str, check: str, message: str, **data) -> None:
        item = {"severity": severity, "check": check, "message": message}
        if data:
            item["data"] = data
        self.checks.append(item)
        print(f"[{severity.upper():4}] {check}: {message}")

    def fail(self, check: str, message: str, **data) -> None:
        self.add("FAIL", check, message, **data)

    def warn(self, check: str, message: str, **data) -> None:
        self.add("WARN", check, message, **data)

    def ok(self, check: str, message: str, **data) -> None:
        self.add("PASS", check, message, **data)

    def info(self, check: str, message: str, **data) -> None:
        self.add("INFO", check, message, **data)

    def candidate(self, cls: str, address: int, reason: str, **data) -> None:
        item = {"address": f"0x{address:08X}", "reason": reason}
        if data:
            item.update(data)
        if item not in self.candidates[cls]:
            self.candidates[cls].append(item)

    @property
    def failed(self) -> bool:
        return any(x["severity"] == "FAIL" for x in self.checks)


def duplicates(values: Iterable) -> list:
    c = Counter(values)
    return sorted(k for k, n in c.items() if n > 1)


def parse_define(text: str, name: str) -> int | None:
    m = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+(0x[0-9A-Fa-f]+|\d+)(?:ull|ul|u|ll|l)?\b",
        text,
        re.MULTILINE,
    )
    return parse_int(m.group(1)) if m else None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project-root", default=".")
    ap.add_argument("--generated", default="generated/default")
    ap.add_argument("--report", default="out/audit/asura_native_audit.json")
    args = ap.parse_args()

    root = Path(args.project_root).resolve()
    generated = (root / args.generated).resolve()
    report_path = (root / args.report).resolve()
    audit = Audit()

    required = [
        generated / "asura_wrath_funcs.h",
        generated / "asura_wrath_init.cpp",
        generated / "asura_wrath_register.cpp",
        generated / "asura_wrath_pch.h",
        generated / "sources.cmake",
    ]
    missing = [str(p.relative_to(root)) for p in required if not p.is_file()]
    if missing:
        audit.fail("generated.required", "required generated files are missing", files=missing)
        write_report(audit, report_path, root, generated, {})
        return 1
    audit.ok("generated.required", "all required generated control files are present")

    partitions = sorted(generated.glob("asura_wrath_recomp.*.cpp"))
    if len(partitions) != EXPECTED_PARTITIONS:
        audit.fail(
            "generated.partitions",
            f"found {len(partitions)} recompilation partitions; expected {EXPECTED_PARTITIONS}",
        )
    else:
        audit.ok("generated.partitions", f"{len(partitions)} TU01 partitions")

    control = {p.name: read_text(p) for p in required}
    funcs_text = control["asura_wrath_funcs.h"]
    register_text = control["asura_wrath_register.cpp"]
    init_text = control["asura_wrath_init.cpp"]
    pch_text = control["asura_wrath_pch.h"]
    sources_text = control["sources.cmake"]

    declarations = set(DECL_RE.findall(funcs_text))
    definitions: set[str] = set()
    direct_calls: set[str] = set()
    indirect_literals: list[int] = []
    generated_text_by_file: dict[str, str] = {}
    trap_surface = {
        "unresolved_call": [],
        "unresolved_bctr": [],
        "unimplemented_insn": [],
        "empty_stub": [],
        "null_jump_case": [],
    }

    fatal_markers = {
        "unresolved_call": (
            'REX_FATAL("Unresolved call from',
            "// ERROR: unresolved bl target",
        ),
        "unresolved_bctr": (
            "unresolved at bctr",
            "classified as function but not in graph at bctr",
        ),
        "unimplemented_insn": (
            "REX_UNIMPLEMENTED(",
        ),
        "empty_stub": (
            "// STUB: Function at",
        ),
        "null_jump_case": (
            "__builtin_trap(); // ERROR - detected jump to null value",
        ),
    }

    for p in partitions:
        t = read_text(p)
        generated_text_by_file[p.name] = t
        definitions.update(DEF_RE.findall(t))
        direct_calls.update(DIRECT_CALL_RE.findall(t))
        indirect_literals.extend(parse_int(x) for x in INDIRECT_LITERAL_RE.findall(t))
        for category, markers in fatal_markers.items():
            for marker in markers:
                count = t.count(marker)
                if count:
                    trap_surface[category].append(
                        {"file": p.name, "marker": marker, "count": count}
                    )

    registrations_list = [
        (parse_int(addr), name) for addr, name in REGISTER_RE.findall(register_text)
    ]
    mappings_list = [
        (parse_int(addr), name)
        for addr, name in MAPPING_RE.findall(init_text)
        if parse_int(addr) != 0
    ]
    registrations = dict(registrations_list)
    mappings = dict(mappings_list)

    code_base = parse_define(pch_text, "REX_CODE_BASE")
    code_size = parse_define(pch_text, "REX_CODE_SIZE")
    image_base = parse_define(pch_text, "REX_IMAGE_BASE")
    image_size = parse_define(pch_text, "REX_IMAGE_SIZE")
    bounds = {
        "code_base": f"0x{code_base:08X}" if code_base is not None else None,
        "code_size": f"0x{code_size:X}" if code_size is not None else None,
        "image_base": f"0x{image_base:08X}" if image_base is not None else None,
        "image_size": f"0x{image_size:X}" if image_size is not None else None,
    }
    if None in (code_base, code_size, image_base, image_size):
        audit.fail("generated.bounds", "could not parse generated image/code bounds", **bounds)
    else:
        audit.ok("generated.bounds", "generated image/code bounds parsed", **bounds)

    # sources.cmake must describe exactly the partitions present on disk.
    source_names = set(re.findall(r"asura_wrath_recomp\.\d+\.cpp", sources_text))
    partition_names = {p.name for p in partitions}
    if source_names != partition_names:
        audit.fail(
            "generated.sources",
            "sources.cmake and generated partition set differ",
            missing_from_cmake=sorted(partition_names - source_names)[:32],
            missing_from_disk=sorted(source_names - partition_names)[:32],
        )
    else:
        audit.ok("generated.sources", "sources.cmake exactly matches partition set")

    dup_reg_addr = duplicates(a for a, _ in registrations_list)
    dup_map_addr = duplicates(a for a, _ in mappings_list)
    dup_reg_name = duplicates(n for _, n in registrations_list)
    if dup_reg_addr or dup_map_addr or dup_reg_name:
        audit.fail(
            "generated.duplicates",
            "duplicate generated registrations/mappings detected",
            registration_addresses=[f"0x{x:08X}" for x in dup_reg_addr],
            mapping_addresses=[f"0x{x:08X}" for x in dup_map_addr],
            registration_names=dup_reg_name,
        )
    else:
        audit.ok("generated.duplicates", "no duplicate registrations/mappings")

    if registrations != mappings:
        reg_only = sorted(set(registrations.items()) - set(mappings.items()))
        map_only = sorted(set(mappings.items()) - set(registrations.items()))
        audit.fail(
            "generated.register-map",
            "function registration and PPCFuncMappings differ",
            registration_only=[(f"0x{a:08X}", n) for a, n in reg_only[:64]],
            mapping_only=[(f"0x{a:08X}", n) for a, n in map_only[:64]],
        )
    else:
        audit.ok(
            "generated.register-map",
            f"{len(registrations)} registrations exactly match PPCFuncMappings",
        )

    bad_alignment = sorted(a for a in registrations if a & 3)
    if bad_alignment:
        audit.fail(
            "generated.alignment",
            "unaligned PPC function registrations",
            addresses=[f"0x{x:08X}" for x in bad_alignment],
        )
    else:
        audit.ok("generated.alignment", "all registered PPC addresses are 4-byte aligned")

    mismatched_named = []
    for addr, name in registrations.items():
        m = HEX_FUNC_RE.match(name)
        if m and int(m.group(1), 16) != addr:
            mismatched_named.append((addr, name))
    if mismatched_named:
        audit.fail(
            "generated.named-address",
            "sub_XXXXXXXX symbol/address mismatches found",
            items=[(f"0x{a:08X}", n) for a, n in mismatched_named[:64]],
        )
    else:
        audit.ok("generated.named-address", "all sub_XXXXXXXX names agree with their addresses")

    name_to_addr = {name: addr for addr, name in registrations.items()}
    xstart_addr = name_to_addr.get("xstart")
    if xstart_addr != EXPECTED_TU_ENTRYPOINT:
        audit.fail(
            "tu01.entrypoint",
            f"xstart is {fmt_addr(xstart_addr)}, expected 0x{EXPECTED_TU_ENTRYPOINT:08X}",
        )
    else:
        audit.ok("tu01.entrypoint", f"xstart registered at 0x{xstart_addr:08X}")

    missing_decl_for_def = sorted(definitions - declarations)
    if missing_decl_for_def:
        audit.fail(
            "generated.definition-declaration",
            "generated definitions without declarations",
            names=missing_decl_for_def[:128],
        )
    else:
        audit.ok(
            "generated.definition-declaration",
            f"all {len(definitions)} generated definitions are declared",
        )

    missing_reg_for_def = sorted(definitions - set(name_to_addr))
    unexpected_unregistered_defs = []
    direct_only_defs = []
    for name in missing_reg_for_def:
        m = HEX_FUNC_RE.match(name)
        if m and code_base is not None:
            addr = int(m.group(1), 16)
            if addr >= code_base:
                unexpected_unregistered_defs.append(name)
            else:
                direct_only_defs.append(name)
        else:
            # ReXGlue intentionally omits some named helpers below code_base
            # from PPCFuncMappings while still emitting bodies for direct calls.
            # Without an address-bearing name we cannot prove a table omission.
            direct_only_defs.append(name)

    if unexpected_unregistered_defs:
        audit.fail(
            "generated.definition-registration",
            "generated in-range definitions missing dispatcher registration",
            names=unexpected_unregistered_defs[:128],
        )
    else:
        audit.ok(
            "generated.definition-registration",
            "no provably in-range generated definition is missing registration",
            direct_only_count=len(direct_only_defs),
        )

    # Registrations without a generated body are legal only for explicit runtime
    # externals (imports/rexcrt) or entries below the generated code base. Any
    # other case is a structural integrity failure.
    allowed_external_prefixes = ("__imp__", "rexcrt_")
    unexpected_no_def = []
    expected_externals = []
    for addr, name in registrations.items():
        if name in definitions:
            continue
        if name.startswith(allowed_external_prefixes) or (
            code_base is not None and addr < code_base
        ):
            expected_externals.append((addr, name))
        else:
            unexpected_no_def.append((addr, name))
    if unexpected_no_def:
        audit.fail(
            "generated.registration-definition",
            "registrations without generated definition or recognized external classification",
            items=[(f"0x{a:08X}", n) for a, n in unexpected_no_def[:128]],
        )
    else:
        audit.ok(
            "generated.registration-definition",
            f"all registrations resolve to generated bodies or {len(expected_externals)} recognized externals",
        )

    missing_decl_for_reg = sorted(name for name in set(name_to_addr) if name not in declarations)
    if missing_decl_for_reg:
        audit.fail(
            "generated.registration-declaration",
            "registrations without declaration",
            names=missing_decl_for_reg[:128],
        )
    else:
        audit.ok("generated.registration-declaration", "every registration is declared")

    missing_direct = sorted(name for name in direct_calls if name not in declarations)
    if missing_direct:
        audit.fail(
            "generated.direct-calls",
            "direct call symbols absent from declarations",
            names=missing_direct[:128],
        )
    else:
        audit.ok(
            "generated.direct-calls",
            f"{len(direct_calls)} unique direct-call symbols resolve to declarations",
        )


    proven_traps = {
        key: value
        for key, value in trap_surface.items()
        if key != "null_jump_case" and value
    }
    if proven_traps:
        audit.fail(
            "generated.runtime-traps",
            "generated C++ contains deferred fatal/stub surfaces that must be resolved before runtime validation",
            categories=proven_traps,
        )
    else:
        audit.ok(
            "generated.runtime-traps",
            "no unresolved calls, unresolved bctr targets, unimplemented instructions, or empty generated stubs detected",
        )

    if trap_surface["null_jump_case"]:
        audit.warn(
            "generated.null-jump-cases",
            "jump-table code contains explicit null-target trap cases; these may be unreachable but remain a campaign-readiness risk",
            occurrences=trap_surface["null_jump_case"],
        )
    else:
        audit.ok(
            "generated.null-jump-cases",
            "no explicit null-target jump-table trap cases detected",
        )

    # Frozen TU01 map invariants: old retail starts must stay out, TU starts stay in.
    funcs_toml_path = root / "includes" / "funcs.toml"
    if funcs_toml_path.is_file():
        funcs_toml = read_text(funcs_toml_path)
        configured = {int(x, 16) for x in re.findall(r'"0x([0-9A-Fa-f]{8})"\s*=', funcs_toml)}
        bad_old = [old for old in TU_FUNC_REMAPS if old in configured]
        missing_new = [new for new in TU_FUNC_REMAPS.values() if new not in configured]
        if bad_old or missing_new:
            audit.fail(
                "tu01.func-map",
                "TU01 function-map frozen remaps are inconsistent",
                stale_retail=[f"0x{x:08X}" for x in bad_old],
                missing_tu=[f"0x{x:08X}" for x in missing_new],
            )
        else:
            audit.ok("tu01.func-map", "all three frozen TU01 remaps are preserved")
    else:
        audit.fail("tu01.func-map", "includes/funcs.toml missing")

    # Project-owned runtime source invariants.
    app_path = root / "src" / "asurawrath_app.h"
    patch_path = root / "tools" / "apply_win_startup_fix.py"
    if app_path.is_file():
        app = read_text(app_path)
        source_needles = {
            "expected TU01 entrypoint": "0x82B75160",
            "portable UserData": '"UserData"',
            "portable Data/Update": '"Data" / "Update"',
            "TU01 source version": "0x00000005",
            "TU01 target version": "0x00000105",
        }
        absent = [label for label, needle in source_needles.items() if needle not in app]
        if absent:
            audit.fail("runtime.tu01-source", "required TU01/portable invariants missing", items=absent)
        else:
            audit.ok("runtime.tu01-source", "TU01 entrypoint/version and portable path guards present")
    else:
        audit.fail("runtime.tu01-source", "src/asurawrath_app.h missing")

    launcher_path = root / "src" / "asura_launcher_win.cpp"
    if launcher_path.is_file():
        launcher = read_text(launcher_path)
        launcher_needles = {
            "resolution": "--window_width=",
            "window/fullscreen": "--fullscreen=",
            "renderer selection": "--gpu_backend=",
            "GPU adapter": "--d3d12_adapter=",
            "vsync": "--vsync=",
            "render scale": "--resolution_scale=",
            "language": "--user_language=",
            "country": "--user_country=",
        }
        absent = [label for label, needle in launcher_needles.items() if needle not in launcher]
        if absent:
            audit.fail(
                "launcher.options",
                "native launcher is missing required portable-PC controls",
                items=absent,
            )
        else:
            audit.ok(
                "launcher.options",
                "resolution, display mode, renderer/GPU, VSync, scaling and language controls are wired",
            )
        if "WriteBool" in launcher and "WriteInt" in launcher and "ReadBool" in launcher and "ReadInt" in launcher:
            audit.ok("launcher.persistence", "launcher settings persistence is present")
        else:
            audit.fail("launcher.persistence", "launcher settings persistence hooks are incomplete")
    else:
        audit.fail("launcher.source", "src/asura_launcher_win.cpp missing")

    # DLC is intentionally a readiness warning until native content discovery
    # and mount logic exists. Creating Data/DLC in the portable package is not
    # evidence that episodes are actually detected or mounted.
    app_source = read_text(app_path) if app_path.is_file() else ""
    if "Data/DLC" in app_source or "Data" + "/DLC" in app_source:
        audit.ok("dlc.readiness", "DLC source path discovery is present")
    else:
        audit.warn(
            "dlc.readiness",
            "Data/DLC is reserved in the portable layout, but native DLC discovery/mounting is not yet proven",
        )

    if patch_path.is_file():
        patch = read_text(patch_path)
        if "TryResolveBranchVeneer" not in patch or "opcode 18" not in patch:
            audit.fail("runtime.veneer", "conservative PPC branch-veneer resolver patch missing")
        else:
            audit.ok("runtime.veneer", "conservative PPC branch-veneer resolver present")

        if "Unregistered PPC target" not in patch or "words:" not in patch:
            audit.fail("runtime.diagnostics", "unknown-target PPC word dump patch missing")
        else:
            audit.ok("runtime.diagnostics", "unknown indirect target PPC dump present")

        manual_numeric_aliases = re.findall(
            r"SetFunction\(\s*(0x[0-9A-Fa-f]+|\d+)\s*,", patch
        )
        if manual_numeric_aliases:
            audit.fail(
                "runtime.aliases",
                "hard-coded numeric dispatcher aliases remain in runtime patch",
                addresses=manual_numeric_aliases,
            )
        else:
            audit.ok("runtime.aliases", "no hard-coded numeric dispatcher aliases detected")
    else:
        audit.fail("runtime.patch-source", "tools/apply_win_startup_fix.py missing")

    # Literal indirect calls are uncommon, but when present they can be classified
    # without guessing. Dynamic CTR targets require guest-image evidence and stay
    # outside automatic registration.
    for addr in sorted(set(indirect_literals)):
        if addr & 3:
            audit.candidate("REJECT", addr, "literal indirect target is not PPC-aligned")
        elif addr in registrations:
            audit.candidate(
                "PROVEN",
                addr,
                "literal indirect target exactly matches generated registration",
                symbol=registrations[addr],
            )
        elif code_base is not None and code_size is not None and not (
            code_base <= addr < code_base + code_size
        ):
            audit.candidate("REJECT", addr, "literal indirect target lies outside generated code range")
        else:
            audit.candidate("REVIEW", addr, "literal indirect target is in code range but unregistered")

    # Build a lightweight generated-source index so historical runtime targets
    # can be classified as internal labels when the emitted C++ proves it.
    for addr in sorted(KNOWN_REVIEW_TARGETS):
        if addr in registrations:
            audit.candidate(
                "PROVEN",
                addr,
                "historical runtime target is now statically registered",
                symbol=registrations[addr],
            )
            continue

        lower_entries = [(a, n) for a, n in registrations.items() if a < addr]
        upper_entries = [(a, n) for a, n in registrations.items() if a > addr]
        owner_addr, owner_name = max(lower_entries) if lower_entries else (None, None)
        next_addr, next_name = min(upper_entries) if upper_entries else (None, None)

        internal_label = f"loc_{addr:X}:"
        label_files = [
            name for name, text in generated_text_by_file.items()
            if internal_label in text
        ]

        owner_files = []
        if owner_name:
            owner_marker = f"DEFINE_REX_FUNC({owner_name})"
            owner_files = [
                name for name, text in generated_text_by_file.items()
                if owner_marker in text
            ]

        same_file_label = bool(
            label_files and owner_files and set(label_files) & set(owner_files)
        )

        neighbors = [
            (a, n)
            for a, n in registrations.items()
            if abs(a - addr) <= 0x100
        ]

        if same_file_label:
            audit.candidate(
                "HIGH_CONFIDENCE",
                addr,
                "unregistered runtime target is emitted as an internal C++ label inside the containing generated partition; do not alias blindly",
                owner=(f"0x{owner_addr:08X}", owner_name) if owner_addr is not None else None,
                next_entry=(f"0x{next_addr:08X}", next_name) if next_addr is not None else None,
                label=internal_label,
                files=sorted(set(label_files) & set(owner_files)),
                offset_from_owner=(addr - owner_addr) if owner_addr is not None else None,
            )
        else:
            audit.candidate(
                "REVIEW",
                addr,
                "historical runtime target remains unregistered; no alias inferred",
                owner=(f"0x{owner_addr:08X}", owner_name) if owner_addr is not None else None,
                next_entry=(f"0x{next_addr:08X}", next_name) if next_addr is not None else None,
                offset_from_owner=(addr - owner_addr) if owner_addr is not None else None,
                label_present=bool(label_files),
                owner_files=owner_files,
                label_files=label_files,
                neighbors=[(f"0x{a:08X}", n) for a, n in sorted(neighbors)],
            )

    audit.info(
        "indirect.classification",
        "candidate scan completed; only PROVEN candidates are eligible for future automatic action",
        counts={k: len(v) for k, v in audit.candidates.items()},
        dynamic_indirect_sites=sum(
            read_text(p).count("REX_CALL_INDIRECT_FUNC(") for p in partitions
        ),
    )

    cmake_path = root / "CMakeLists.txt"
    if cmake_path.is_file():
        cmake = read_text(cmake_path)
        if "-mavx2" in cmake:
            audit.warn(
                "portable.cpu",
                "host binary currently requires AVX2; consider runtime dispatch/baseline ISA for wider PC portability",
            )
        else:
            audit.ok("portable.cpu", "no unconditional AVX2 compiler requirement detected")

    metadata = {
        "expected_generated_sha256": EXPECTED_TU_GENERATED_SHA256,
        "partition_count": len(partitions),
        "declaration_count": len(declarations),
        "definition_count": len(definitions),
        "registration_count": len(registrations),
        "mapping_count": len(mappings),
        "direct_call_symbol_count": len(direct_calls),
        "literal_indirect_target_count": len(set(indirect_literals)),
        "trap_surface_counts": {
            key: sum(item["count"] for item in value)
            for key, value in trap_surface.items()
        },
        "bounds": bounds,
    }
    write_report(audit, report_path, root, generated, metadata)
    return 1 if audit.failed else 0


def fmt_addr(value: int | None) -> str:
    return "(missing)" if value is None else f"0x{value:08X}"


def write_report(audit: Audit, path: Path, root: Path, generated: Path, metadata: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "schema": 1,
        "project": "asuras-wrath-recomp",
        "scope": "TU01 native static integrity",
        "result": "FAIL" if audit.failed else "PASS",
        "project_root": str(root),
        "generated_root": str(generated),
        "metadata": metadata,
        "checks": audit.checks,
        "indirect_candidates": audit.candidates,
        "policy": {
            "auto_add": "PROVEN only",
            "high_confidence": "report, do not auto-add",
            "review": "report, do not auto-add",
            "reject": "never add",
        },
    }
    path.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")
    print(f"Audit report: {path}")
    print(f"RESULT: {payload['result']}")


if __name__ == "__main__":
    sys.exit(main())
