"""Validate a built package without loading game-dependent code into Python."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

EXPECTED_NAME = "PraxisCompanion"
EXPECTED_SDK = "26.51.5"


def dll_exports(data: bytes) -> set[str]:
    def unpack(fmt: str, offset: int) -> tuple:
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(data):
            raise ValueError("truncated PE file")
        return struct.unpack_from(fmt, data, offset)

    if data[:2] != b"MZ":
        raise ValueError("not a PE image")
    pe, = unpack("<I", 0x3C)
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("invalid PE signature")
    machine, sections, _, _, _, optional_size, characteristics = unpack("<HHIIIHH", pe + 4)
    if machine != 0x8664 or not characteristics & 0x2000:
        raise ValueError("expected an AMD64 DLL, not an object file or EXE")
    optional = pe + 24
    magic, = unpack("<H", optional)
    if magic != 0x20B or optional_size < 120:
        raise ValueError("expected PE32+ optional header")
    directories, = unpack("<I", optional + 108)
    if directories < 1:
        raise ValueError("PE image has no export directory")
    export_rva, export_size = unpack("<II", optional + 112)
    section_table = optional + optional_size

    def offset_of(rva: int) -> int:
        for index in range(sections):
            virtual_size, address, raw_size, raw = unpack("<IIII", section_table + index * 40 + 8)
            if address <= rva < address + max(virtual_size, raw_size):
                delta = rva - address
                if delta >= raw_size or raw + delta >= len(data):
                    raise ValueError("PE RVA outside file-backed section")
                return raw + delta
        raise ValueError("unmapped PE RVA")

    if not export_rva or export_size < 40:
        raise ValueError("DLL has no export table")
    exports = offset_of(export_rva)
    count, = unpack("<I", exports + 24)
    names_rva, = unpack("<I", exports + 32)
    if count > len(data) // 4:
        raise ValueError("invalid export count")
    names: set[str] = set()
    for index in range(count):
        name_rva, = unpack("<I", offset_of(names_rva + index * 4))
        start = offset_of(name_rva)
        end = data.find(b"\0", start, min(start + 4096, len(data)))
        if end < 0:
            raise ValueError("unterminated export name")
        names.add(data[start:end].decode("ascii"))
    return names


def verify(package: Path) -> dict:
    required = {"PraxisCompanion.dll", "manifest.json", "README.md", "config.example.json", "LICENSE", "THIRD_PARTY_NOTICES.md", "GPL-3.0.txt", "LICENSE_COMBINED.txt"}
    allowed = required | {"PraxisCompanion.pdb", "build-info.json"}
    if not package.is_dir():
        raise ValueError(f"package directory missing: {package}")
    present = {path.name for path in package.iterdir()}
    if required - present:
        raise ValueError(f"missing runtime files: {sorted(required - present)}")
    if present - allowed:
        raise ValueError(f"unexpected package files: {sorted(present - allowed)}")
    for name in present:
        path = package / name
        if not path.is_file() or path.is_symlink() or not path.stat().st_size:
            raise ValueError(f"not a nonempty regular package file: {name}")
    if "GNU GENERAL PUBLIC LICENSE" not in (package / "GPL-3.0.txt").read_text(encoding="utf-8-sig"):
        raise ValueError("missing full ChiyanMap GPL license")
    if "MPL 2.0 section 3.3" not in (package / "LICENSE_COMBINED.txt").read_text(encoding="utf-8-sig"):
        raise ValueError("missing combined-work and preserved MPL source-rights notice")
    text = (package / "manifest.json").read_text(encoding="utf-8-sig")
    if "${" in text:
        raise ValueError("manifest contains unexpanded template variables")
    manifest = json.loads(text)
    for field, value in {"name": EXPECTED_NAME, "entry": "PraxisCompanion.dll", "type": "native", "platform": "client", "version": "0.1.0"}.items():
        if manifest.get(field) != value:
            raise ValueError(f"invalid manifest {field}: {manifest.get(field)!r}")
    dependencies = manifest.get("dependencies", [])
    if not isinstance(dependencies, list):
        raise ValueError("manifest dependencies must be an array when present")
    if any(isinstance(dep, dict) and dep.get("name") == "LeviLamina" for dep in dependencies):
        raise ValueError("LeviLamina is the loader, not a regular mod dependency")
    dll = package / manifest["entry"]
    exports = dll_exports(dll.read_bytes())
    required_exports = {"ll_mod_load", "ll_memory_operator_overrided"}
    missing_exports = required_exports - exports
    if missing_exports:
        raise ValueError(f"DLL missing required exports: {sorted(missing_exports)}")
    example = json.loads((package / "config.example.json").read_text(encoding="utf-8-sig"))
    if example.get("version") != 2 or example.get("fullbright") is not False or example.get("zoom") is not False:
        raise ValueError("example config must default to all features OFF")
    if example.get("chunkBorder", {}).get("enabled") is not False:
        raise ValueError("example ChunkBorders config must default OFF")
    if any(example.get(name) is not False for name in ("shulkerPreview", "redstoneOverlay", "lightOverlay")):
        raise ValueError("example overlay and preview features must default OFF")
    if any(example.get("informationOptions", {}).get(name) is not False for name in ("infoHud", "durabilityHud", "bundlePreview", "durabilityInformation")):
        raise ValueError("Wave 1 must default OFF")
    no_render = example.get("noRender", {})
    if no_render.get("enabled") is not False or no_render.get("passes") != [False] * 11:
        raise ValueError("example NoRender config must default to all eleven passes OFF")
    return {
        "name": EXPECTED_NAME,
        "version": manifest["version"],
        "platform": "client-windows-x64",
        "levilamina_build_sdk": EXPECTED_SDK,
        "levilamina_runtime_guard": EXPECTED_SDK,
        "runtime_game_test": "NOT_RUN",
        "files": {name: hashlib.sha256((package / name).read_bytes()).hexdigest() for name in sorted(present - {"build-info.json"})},
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--policy-log", type=Path, required=True)
    args = parser.parse_args()
    try:
        if not re.fullmatch(r"[0-9a-fA-F]{40}", args.commit):
            raise ValueError("commit must be a full Git SHA")
        if not re.search(r"(?m)^Render policy checks: 651 PASS\s*$", args.policy_log.read_text(encoding="utf-8-sig")):
            raise ValueError("missing successful 651-check policy-test evidence")
        report = verify(args.package)
        report.update(commit=args.commit, policy_checks=651, policy_tests="PASS")
        (args.package / "build-info.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, indent=2))
        print("Runtime package validation: PASS")
        return 0
    except (ValueError, OSError, UnicodeError, struct.error) as error:
        print(f"Runtime package validation: FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
