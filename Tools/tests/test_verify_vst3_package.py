import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from Tools.verify_vst3_package import PackageValidationError, remove_trailing_commas, stage_package


class PackageVerifierTests(unittest.TestCase):
    def make_pe(self, path: Path, machine: int = 0x8664, optional_magic: int = 0x20B) -> None:
        data = bytearray(256)
        data[:2] = b"MZ"
        struct.pack_into("<I", data, 0x3C, 128)
        data[128:132] = b"PE\0\0"
        struct.pack_into("<H", data, 132, machine)
        struct.pack_into("<H", data, 152, optional_magic)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def make_bundle(self, root: Path, name: str, cids=None, machine=0x8664, optional_magic=0x20B) -> Path:
        bundle = root / f"{name}.vst3"
        resources = bundle / "Contents" / "Resources"
        module = bundle / "Contents" / "x86_64-win" / f"{name}.vst3"
        resources.mkdir(parents=True)
        if cids is None:
            cids = ["ABCDEF019182FAEB536B4175446B4672", "ABCDEF011234ABCD536B4175446B4672"]
        metadata = {
            "Name": name, "Version": "1.0.0",
            "Factory Info": {"Vendor": "Skell Audio", "URL": "", "E-Mail": ""},
            "Classes": [
                {"CID": cids[0], "Category": "Audio Module Class", "Name": name, "Vendor": "Skell Audio", "Version": "1.0.0"},
                {"CID": cids[1], "Category": "Component Controller Class", "Name": name, "Vendor": "Skell Audio", "Version": "1.0.0"},
            ],
        }
        raw = json.dumps(metadata, indent=2).replace('"E-Mail": ""', '"E-Mail": "",')
        resources.joinpath("moduleinfo.json").write_text(raw, encoding="utf-8")
        self.make_pe(module, machine, optional_magic)
        return bundle

    def test_trailing_comma_removal_is_string_aware(self):
        raw = '{"text":"literal ,} and \\"quoted\\"","nested":[1,2,],}'
        parsed = json.loads(remove_trailing_commas(raw))
        self.assertEqual(parsed["text"], 'literal ,} and "quoted"')
        self.assertEqual(parsed["nested"], [1, 2])

    def test_unterminated_string_is_rejected(self):
        with self.assertRaises(PackageValidationError):
            remove_trailing_commas('{"broken":"value}')

    def test_stages_valid_bundle_and_writes_provenance(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, package = root / "source", root / "package"
            self.make_bundle(source, "Test Plugin")
            report = stage_package(source, package, expected_plugins=("Test Plugin",))
            self.assertEqual(report["plugins"][0]["name"], "Test Plugin")
            self.assertTrue((package / "build-info.json").is_file())
            self.assertTrue((package / "SHA256SUMS.txt").is_file())
            self.assertTrue((package / "PREVIEW-ONLY.txt").is_file())
            staged = json.loads((package / "Test Plugin.vst3/Contents/Resources/moduleinfo.json").read_text())
            self.assertEqual(staged["Name"], "Test Plugin")

    def test_refuses_to_overwrite_existing_package(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, package = root / "source", root / "package"
            self.make_bundle(source, "Test Plugin")
            package.mkdir()
            sentinel = package / "keep.txt"
            sentinel.write_text("known-good package", encoding="utf-8")

            with self.assertRaisesRegex(PackageValidationError, "choose a new output path"):
                stage_package(source, package, expected_plugins=("Test Plugin",))

            self.assertEqual(sentinel.read_text(encoding="utf-8"), "known-good package")
            self.assertEqual(sorted(path.name for path in package.iterdir()), ["keep.txt"])

    def test_rejects_non_amd64_module(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_bundle(root / "source", "Test Plugin", machine=0x14C)
            with self.assertRaisesRegex(PackageValidationError, "expected AMD64"):
                stage_package(root / "source", root / "package", expected_plugins=("Test Plugin",))

    def test_rejects_pe32_not_pe32_plus(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_bundle(root / "source", "Test Plugin", optional_magic=0x10B)
            with self.assertRaisesRegex(PackageValidationError, r"expected PE32\+"):
                stage_package(root / "source", root / "package", expected_plugins=("Test Plugin",))

    def test_rejects_duplicate_architecture_module(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            bundle = self.make_bundle(root / "source", "Test Plugin")
            extra = bundle / "Contents" / "x86_64-linux" / "Test Plugin.vst3"
            extra.parent.mkdir(parents=True)
            extra.write_bytes(b"unexpected duplicate architecture payload")
            with self.assertRaisesRegex(PackageValidationError, "duplicate architecture modules"):
                stage_package(root / "source", root / "package", expected_plugins=("Test Plugin",))

    def test_rejects_duplicate_class_ids_across_bundles(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "source"
            self.make_bundle(source, "First")
            self.make_bundle(source, "Second")
            with self.assertRaisesRegex(PackageValidationError, "duplicate VST3 class ID"):
                stage_package(source, root / "package", expected_plugins=("First", "Second"))

    def test_rejects_missing_or_unexpected_bundle(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_bundle(root / "source", "Unexpected")
            with self.assertRaisesRegex(PackageValidationError, "inventory mismatch"):
                stage_package(root / "source", root / "package", expected_plugins=("Expected",))


if __name__ == "__main__":
    unittest.main()
