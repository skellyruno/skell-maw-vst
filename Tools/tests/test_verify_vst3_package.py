import hashlib
import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from Tools.verify_vst3_package import (
    PackageValidationError,
    remove_trailing_commas,
    stage_package,
    strict_json_loads,
)


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
            "Name": name,
            "Version": "1.0.0",
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

    def make_approval_manifest(
        self,
        root: Path,
        *,
        status: str = "approved",
        vendor_name: str = "Skell Audio",
        vendor_url: str = "https://example.invalid/skell",
        support_email: str = "release@example.invalid",
    ) -> Path:
        license_path = root / "owner-approved-license.txt"
        license_path.write_text("Temporary unit-test license fixture; not a real Skell license.\n", encoding="utf-8")
        notice_path = root / "notices" / "juce" / "LICENSE.txt"
        notice_path.parent.mkdir(parents=True, exist_ok=True)
        notice_path.write_text("Temporary unit-test third-party notice fixture.\n", encoding="utf-8")
        approval = {
            "product": "Skell Maw",
            "version": "1.0.0",
            "status": status,
            "approved_by": "skellyruno",
            "approved_at": "2026-11-19T12:00:00Z",
            "approval_reference": "TEST-APPROVAL-ONLY",
            "vendor_name": vendor_name,
            "vendor_url": vendor_url,
            "support_email": support_email,
            "license_file": "owner-approved-license.txt",
            "license_sha256": hashlib.sha256(license_path.read_bytes()).hexdigest(),
            "third_party_notices": [
                {
                    "source": "notices/juce/LICENSE.txt",
                    "package_path": "juce/LICENSE.txt",
                    "sha256": hashlib.sha256(notice_path.read_bytes()).hexdigest(),
                }
            ],
        }
        manifest = root / "commercial-approval-test.json"
        manifest.write_text(json.dumps(approval, indent=2), encoding="utf-8")
        return manifest

    def checksum_map(self, package: Path) -> dict[str, str]:
        result = {}
        for line in (package / "SHA256SUMS.txt").read_text(encoding="ascii").splitlines():
            digest, filename = line.split("  ", 1)
            result[filename] = digest
        return result

    def test_trailing_comma_removal_is_string_aware(self):
        raw = '{"text":"literal ,} and \\\\"quoted\\\\"","nested":[1,2,],}'
        parsed = json.loads(remove_trailing_commas(raw))
        self.assertEqual(parsed["text"], 'literal ,} and "quoted"')
        self.assertEqual(parsed["nested"], [1, 2])

    def test_unterminated_string_is_rejected(self):
        with self.assertRaises(PackageValidationError):
            remove_trailing_commas('{"broken":"value}')

    def test_strict_json_rejects_duplicate_keys_and_non_finite_numbers(self):
        with self.assertRaisesRegex(PackageValidationError, "duplicate JSON key"):
            strict_json_loads('{"Factory Info":{"Vendor":"Skell Audio","Vendor":"Other"}}')
        with self.assertRaisesRegex(PackageValidationError, "non-standard JSON numeric constant"):
            strict_json_loads('{"Version":NaN}')

    def test_bundle_metadata_rejects_duplicate_keys(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            bundle = self.make_bundle(root / "source", "Test Plugin")
            metadata_path = bundle / "Contents" / "Resources" / "moduleinfo.json"
            raw = metadata_path.read_text(encoding="utf-8")
            raw = raw.replace('"Name": "Test Plugin",', '"Name": "Wrong",\n  "Name": "Test Plugin",', 1)
            metadata_path.write_text(raw, encoding="utf-8")
            with self.assertRaisesRegex(PackageValidationError, "duplicate JSON key"):
                stage_package(root / "source", root / "package", expected_plugins=("Test Plugin",))

    def test_bundle_metadata_rejects_non_object_factory_info(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            bundle = self.make_bundle(root / "source", "Test Plugin")
            metadata_path = bundle / "Contents" / "Resources" / "moduleinfo.json"
            metadata = json.loads(remove_trailing_commas(metadata_path.read_text(encoding="utf-8")))
            metadata["Factory Info"] = []
            metadata_path.write_text(json.dumps(metadata), encoding="utf-8")
            with self.assertRaisesRegex(PackageValidationError, "Factory Info must be an object"):
                stage_package(root / "source", root / "package", expected_plugins=("Test Plugin",))

    def test_bundle_metadata_rejects_non_finite_numbers(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            bundle = self.make_bundle(root / "source", "Test Plugin")
            metadata_path = bundle / "Contents" / "Resources" / "moduleinfo.json"
            raw = remove_trailing_commas(metadata_path.read_text(encoding="utf-8"))
            raw = raw.replace('"Version": "1.0.0"', '"Version": NaN', 1)
            metadata_path.write_text(raw, encoding="utf-8")
            with self.assertRaisesRegex(PackageValidationError, "non-standard JSON numeric constant"):
                stage_package(root / "source", root / "package", expected_plugins=("Test Plugin",))

    def test_stages_valid_bundle_and_writes_provenance(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, package = root / "source", root / "package"
            self.make_bundle(source, "Test Plugin")
            with patch.dict(os.environ, {"MAW_PR_HEAD_SHA": "test-pr-head", "MAW_PR_HEAD_REF": "refs/heads/test-branch"}):
                report = stage_package(source, package, expected_plugins=("Test Plugin",))
            self.assertEqual(report["plugins"][0]["name"], "Test Plugin")
            self.assertEqual(report["pr_head_commit"], "test-pr-head")
            self.assertEqual(report["pr_head_ref"], "refs/heads/test-branch")
            self.assertTrue((package / "build-info.json").is_file())
            self.assertTrue((package / "SHA256SUMS.txt").is_file())
            manifest = json.loads((package / "build-info.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["pr_head_commit"], "test-pr-head")
            self.assertEqual(manifest["pr_head_ref"], "refs/heads/test-branch")
            self.assertTrue((package / "PREVIEW-ONLY.txt").is_file())
            install_guide = package / "WINDOWS-VST3-INSTALL.txt"
            self.assertTrue(install_guide.is_file())
            guide_text = install_guide.read_text(encoding="utf-8")
            guide_bytes = install_guide.read_bytes()
            self.assertIn(b"\n", guide_bytes)
            self.assertNotIn(b"\r\n", guide_bytes)
            self.assertIn("ROLLBACK", guide_text)
            self.assertIn("private engineering material", guide_text.lower())
            self.assertIn("not a signed installer", guide_text.lower())
            self.assertIn("VST3 metadata/class-ID identity", guide_text)
            self.assertIn("AMD64 PE32+ module", guide_text)
            checksum_lines = self.checksum_map(package)
            self.assertEqual(
                hashlib.sha256(install_guide.read_bytes()).hexdigest(),
                checksum_lines["WINDOWS-VST3-INSTALL.txt"],
            )
            staged = json.loads((package / "Test Plugin.vst3/Contents/Resources/moduleinfo.json").read_text())
            self.assertTrue((package / "Tools/Install-SkellMawPreview.ps1").is_file())
            self.assertTrue((package / "Tools/SkellMawPreviewInstall.psm1").is_file())
            self.assertIn("Tools/Install-SkellMawPreview.ps1", checksum_lines)
            self.assertIn("Tools/SkellMawPreviewInstall.psm1", checksum_lines)
            self.assertEqual(staged["Name"], "Test Plugin")
            self.assertEqual(manifest["release_status"], "preview-only; not a commercial release")
            self.assertIn("vendor URL is blank", manifest["plugins"][0]["metadata_warnings"][0])

    def test_commercial_mode_requires_an_approval_manifest(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_bundle(root / "source", "Test Plugin")
            with self.assertRaisesRegex(PackageValidationError, "requires --approval-manifest"):
                stage_package(root / "source", root / "package", expected_plugins=("Test Plugin",), commercial_release=True)
            self.assertFalse((root / "package").exists())

    def test_commercial_mode_rejects_pending_owner_approval(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_bundle(root / "source", "Test Plugin")
            approval = self.make_approval_manifest(root, status="pending")
            with self.assertRaisesRegex(PackageValidationError, "explicit approved record"):
                stage_package(
                    root / "source",
                    root / "package",
                    expected_plugins=("Test Plugin",),
                    commercial_release=True,
                    approval_manifest=approval,
                )
            self.assertFalse((root / "package").exists())

    def test_commercial_mode_rejects_vendor_mismatch_and_bad_contact(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_bundle(root / "source", "Test Plugin")
            approval = self.make_approval_manifest(root, vendor_name="Skelly Audio")
            with self.assertRaisesRegex(PackageValidationError, "does not match the compiled VST3 identity"):
                stage_package(root / "source", root / "package-vendor", expected_plugins=("Test Plugin",), commercial_release=True, approval_manifest=approval)
            approval = self.make_approval_manifest(root, vendor_url="http://example.invalid", support_email="not-an-email")
            with self.assertRaisesRegex(PackageValidationError, "vendor_url must be an HTTPS URL"):
                stage_package(root / "source", root / "package-contact", expected_plugins=("Test Plugin",), commercial_release=True, approval_manifest=approval)

    def test_commercial_candidate_copies_and_hashes_owner_legal_files(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, package = root / "source", root / "package"
            self.make_bundle(source, "Test Plugin")
            approval = self.make_approval_manifest(root)
            report = stage_package(
                source,
                package,
                expected_plugins=("Test Plugin",),
                commercial_release=True,
                approval_manifest=approval,
            )
            self.assertEqual(report["release_status"], "preview-only; not a commercial release")
            self.assertTrue(report["commercial_candidate"])
            manifest = json.loads((package / "build-info.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["commercial_approval"]["approval_reference"], "TEST-APPROVAL-ONLY")
            self.assertEqual(manifest["commercial_approval"]["approved_by"], "skellyruno")
            self.assertEqual(manifest["release_status"], "preview-only; not a commercial release")
            self.assertTrue(manifest["commercial_candidate"])
            self.assertEqual(manifest["plugins"][0]["metadata_warnings"], [])
            self.assertTrue((package / "LICENSE").is_file())
            self.assertTrue((package / "THIRD_PARTY_NOTICES/juce/LICENSE.txt").is_file())
            self.assertEqual((package / "LICENSE").read_text(encoding="utf-8"), (root / "owner-approved-license.txt").read_text(encoding="utf-8"))
            staged_metadata = json.loads((package / "Test Plugin.vst3/Contents/Resources/moduleinfo.json").read_text(encoding="utf-8"))
            self.assertEqual(staged_metadata["Factory Info"]["URL"], "https://example.invalid/skell")
            self.assertEqual(staged_metadata["Factory Info"]["E-Mail"], "release@example.invalid")
            self.assertIn("unsigned and unpublished", (package / "PREVIEW-ONLY.txt").read_text(encoding="utf-8"))
            self.assertIn("not a cryptographic signature", (package / "WINDOWS-VST3-INSTALL.txt").read_text(encoding="utf-8"))
            checksum_lines = self.checksum_map(package)
            for relative in ("LICENSE", "THIRD_PARTY_NOTICES/juce/LICENSE.txt"):
                self.assertEqual(hashlib.sha256((package / relative).read_bytes()).hexdigest(), checksum_lines[relative])

    def test_commercial_candidate_rejects_tampered_license(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "source"
            self.make_bundle(source, "Test Plugin")
            approval = self.make_approval_manifest(root)
            (root / "owner-approved-license.txt").write_text("changed after approval\n", encoding="utf-8")
            with self.assertRaisesRegex(PackageValidationError, "LICENSE SHA-256 does not match"):
                stage_package(source, root / "package", expected_plugins=("Test Plugin",), commercial_release=True, approval_manifest=approval)

    def test_commercial_candidate_rejects_empty_notice_package_path(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "source"
            self.make_bundle(source, "Test Plugin")
            approval = self.make_approval_manifest(root)
            data = json.loads(approval.read_text(encoding="utf-8"))
            data["third_party_notices"][0]["package_path"] = "."
            approval.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaisesRegex(PackageValidationError, "package_path contains an unsafe path"):
                stage_package(source, root / "package", expected_plugins=("Test Plugin",), commercial_release=True, approval_manifest=approval)

    def test_commercial_candidate_rejects_duplicate_manifest_keys(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "source"
            self.make_bundle(source, "Test Plugin")
            approval = self.make_approval_manifest(root)
            approval.write_text('{"product":"Skell Maw","product":"Skell Maw"}', encoding="utf-8")
            with self.assertRaisesRegex(PackageValidationError, "duplicate JSON key"):
                stage_package(source, root / "package", expected_plugins=("Test Plugin",), commercial_release=True, approval_manifest=approval)

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

    def test_release_configuration_is_vst3_only_and_windows_first(self):
        root = Path(__file__).resolve().parents[2]
        cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
        workflow = (root / ".github/workflows/build.yml").read_text(encoding="utf-8")
        self.assertIn("set(MAW_FORMATS VST3)", cmake)
        self.assertNotIn("list(APPEND MAW_FORMATS AU)", cmake)
        self.assertNotIn("Standalone", cmake)
        self.assertNotIn("CMAKE_OSX", cmake)
        self.assertIn("runs-on: windows-latest", workflow)
        self.assertNotIn("macos-latest", workflow)


if __name__ == "__main__":
    unittest.main()
