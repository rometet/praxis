"""Unit tests use synthetic PE fixtures; they are never packaged as runtime DLLs."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("validator", ROOT / "tools/verify_runtime_package.py")
validator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(validator)


def pe_fixture(exports=(b"ll_mod_load", b"ll_memory_operator_overrided")):
    if isinstance(exports, (bytes, bytearray)):
        exports = (bytes(exports),)
    data = bytearray(1024)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, 0x84, 0x8664, 1, 0, 0, 0, 240, 0x2000)
    struct.pack_into("<H", data, 0x98, 0x20B)
    struct.pack_into("<I", data, 0x98 + 108, 16)
    struct.pack_into("<II", data, 0x98 + 112, 0x1000, 100)
    struct.pack_into("<IIII", data, 0x188 + 8, 512, 0x1000, 512, 512)
    struct.pack_into("<I", data, 512 + 24, len(exports))
    struct.pack_into("<I", data, 512 + 32, 0x1040)
    for index, export in enumerate(exports):
        name_rva = 0x1060 + index * 0x40
        struct.pack_into("<I", data, 576 + index * 4, name_rva)
        name_offset = 512 + (name_rva - 0x1000)
        data[name_offset:name_offset + len(export)] = export
    return data


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.package = Path(self.temp.name) / "PraxisCompanion"
        self.package.mkdir()
        for name in ("README.md", "LICENSE", "THIRD_PARTY_NOTICES.md"):
            (self.package / name).write_text("fixture\n")
        (self.package / "PraxisCompanion.dll").write_bytes(pe_fixture())
        (self.package / "config.example.json").write_bytes((ROOT / "config.example.json").read_bytes())
        manifest = (ROOT / "manifest.json").read_text()
        for key, value in {"modName": "PraxisCompanion", "modFile": "PraxisCompanion.dll", "modVersion": "0.1.0", "modPlatform": "client"}.items():
            manifest = manifest.replace("${" + key + "}", value)
        (self.package / "manifest.json").write_text(manifest)

    def test_valid_structure(self):
        result = validator.verify(self.package)
        self.assertEqual(result["runtime_game_test"], "NOT_RUN")
        self.assertEqual(result["levilamina_build_sdk"], "26.51.5")
        self.assertEqual(result["levilamina_runtime_guard"], "26.51.5")
        self.assertEqual(len(result["files"]["PraxisCompanion.dll"]), 64)

    def test_missing_dll(self):
        (self.package / "PraxisCompanion.dll").unlink()
        with self.assertRaises(ValueError):
            validator.verify(self.package)

    def test_object_is_not_dll(self):
        (self.package / "PraxisCompanion.dll").write_bytes(b"not a dll")
        with self.assertRaises(ValueError):
            validator.verify(self.package)

    def test_exe_is_not_dll(self):
        data = pe_fixture()
        struct.pack_into("<H", data, 0x96, 0)
        with self.assertRaises(ValueError):
            validator.dll_exports(bytes(data))

    def test_missing_mod_export(self):
        (self.package / "PraxisCompanion.dll").write_bytes(pe_fixture((b"ll_memory_operator_overrided",)))
        with self.assertRaises(ValueError):
            validator.verify(self.package)

    def test_missing_memory_operator_export(self):
        (self.package / "PraxisCompanion.dll").write_bytes(pe_fixture((b"ll_mod_load",)))
        with self.assertRaises(ValueError):
            validator.verify(self.package)

    def test_wrong_platform(self):
        path = self.package / "manifest.json"
        manifest = json.loads(path.read_text())
        manifest["platform"] = "server"
        path.write_text(json.dumps(manifest))
        with self.assertRaises(ValueError):
            validator.verify(self.package)

    def test_levilamina_loader_dependency_is_rejected(self):
        path = self.package / "manifest.json"
        clean = path.read_text()
        for version in ("26.51.5", "=26.51.5"):
            with self.subTest(version=version):
                manifest = json.loads(clean)
                manifest["dependencies"] = [{"name": "LeviLamina", "version": version}]
                path.write_text(json.dumps(manifest))
                with self.assertRaises(ValueError):
                    validator.verify(self.package)
                path.write_text(clean)

    def test_unexpanded_manifest(self):
        (self.package / "manifest.json").write_bytes((ROOT / "manifest.json").read_bytes())
        with self.assertRaises(ValueError):
            validator.verify(self.package)

    def test_foreign_mod_not_bundled(self):
        (self.package / "LHolo.dll").write_bytes(b"fixture")
        with self.assertRaises(ValueError):
            validator.verify(self.package)

    def test_truncated_pe(self):
        for size in (0, 2, 64, 128, 160, 512, 592):
            with self.subTest(size=size), self.assertRaises(ValueError):
                validator.dll_exports(bytes(pe_fixture()[:size]))


if __name__ == "__main__":
    unittest.main()
