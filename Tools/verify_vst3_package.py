#!/usr/bin/env python3
"""Validate and stage the seven Windows VST3 bundles produced by Skell Maw."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import sys
from datetime import datetime
from pathlib import Path, PurePosixPath
from typing import Any
from urllib.parse import urlparse

EXPECTED_PLUGINS = ("Butterfly Delay", "Constrictor", "Dark Forest", "Hypnosis", "Night Bloom", "Serpents Maw", "Shed Skin")
EXPECTED_VERSION = "1.0.0"
EXPECTED_VENDOR = "Skell Audio"
EXPECTED_CLASSES = {"Audio Module Class", "Component Controller Class"}
CID_RE = re.compile(r"^[0-9A-Fa-f]{32}$")
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
EMAIL_RE = re.compile(r"^[^@\s]+@[^@\s]+\.[^@\s]+$")


class PackageValidationError(RuntimeError):
    pass


def _unique_json_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise PackageValidationError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def _reject_json_constant(value: str) -> None:
    raise PackageValidationError(f"non-standard JSON numeric constant {value!r}")


def strict_json_loads(text: str) -> Any:
    """Parse RFC 8259 JSON, rejecting duplicate keys and NaN/Infinity."""
    try:
        return json.loads(
            text,
            object_pairs_hook=_unique_json_object,
            parse_constant=_reject_json_constant,
        )
    except json.JSONDecodeError as exc:
        raise PackageValidationError(f"invalid JSON: {exc}") from exc


def remove_trailing_commas(text: str) -> str:
    """Remove only JSON trailing commas outside quoted strings."""
    out, in_string, escaped, i = [], False, False, 0
    while i < len(text):
        ch = text[i]
        if in_string:
            out.append(ch)
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == '"':
                in_string = False
            i += 1
            continue
        if ch == '"':
            in_string = True
            out.append(ch)
            i += 1
            continue
        if ch == ",":
            j = i + 1
            while j < len(text) and text[j].isspace():
                j += 1
            if j < len(text) and text[j] in "]}":
                i += 1
                continue
        out.append(ch)
        i += 1
    if in_string:
        raise PackageValidationError("moduleinfo.json contains an unterminated string")
    return "".join(out)


def require_amd64_pe(path: Path) -> None:
    with path.open("rb") as stream:
        dos = stream.read(64)
        if len(dos) < 64 or dos[:2] != b"MZ":
            raise PackageValidationError(f"{path}: missing DOS MZ header")
        offset = struct.unpack_from("<I", dos, 0x3C)[0]
        if offset < 64:
            raise PackageValidationError(f"{path}: invalid PE header offset {offset}")
        stream.seek(offset)
        header = stream.read(6)
        stream.seek(offset + 24)  # PE signature + 20-byte COFF header
        optional_magic = stream.read(2)
    if len(header) != 6 or header[:4] != b"PE\0\0":
        raise PackageValidationError(f"{path}: missing PE signature")
    machine = struct.unpack_from("<H", header, 4)[0]
    if machine != 0x8664:
        raise PackageValidationError(f"{path}: expected AMD64 0x8664, found 0x{machine:04x}")
    if optional_magic != b"\x0b\x02":
        value = struct.unpack("<H", optional_magic)[0] if len(optional_magic) == 2 else -1
        raise PackageValidationError(f"{path}: expected PE32+ optional header 0x20b, found 0x{value:04x}")


def _safe_manifest_file(base: Path, relative: Any, label: str) -> Path:
    if not isinstance(relative, str) or not relative.strip() or "\\" in relative:
        raise PackageValidationError(f"{label} must be a non-empty relative POSIX path")
    rel = PurePosixPath(relative)
    if rel.is_absolute() or any(part in ("", ".", "..") for part in rel.parts) or ":" in relative:
        raise PackageValidationError(f"{label} contains an unsafe path")
    candidate = base.joinpath(*rel.parts)
    cursor = base
    for part in rel.parts:
        cursor = cursor / part
        if cursor.is_symlink():
            raise PackageValidationError(f"{label} must not traverse a symlink: {relative}")
    if not candidate.is_file():
        raise PackageValidationError(f"{label} is missing or not a regular file: {relative}")
    if candidate.stat().st_size == 0:
        raise PackageValidationError(f"{label} must not be empty: {relative}")
    return candidate


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_commercial_approval(manifest_path: Path, expected_version: str = EXPECTED_VERSION) -> dict[str, Any]:
    """Validate an owner-supplied commercial-candidate record and its legal files.

    This is a declared approval record, not a cryptographic signature. It does not
    sign, publish, deploy, or authorize payment processing.
    """
    manifest_path = Path(manifest_path)
    if manifest_path.is_symlink() or not manifest_path.is_file():
        raise PackageValidationError("commercial approval manifest must be a regular, non-symlink file")
    raw = manifest_path.read_bytes()
    if not raw:
        raise PackageValidationError("commercial approval manifest is empty")
    try:
        approval = strict_json_loads(raw.decode("utf-8-sig"))
    except (UnicodeError, PackageValidationError) as exc:
        raise PackageValidationError(f"commercial approval manifest is invalid: {exc}") from exc
    if not isinstance(approval, dict):
        raise PackageValidationError("commercial approval manifest root must be an object")

    required = {
        "product", "version", "status", "approved_by", "approved_at", "approval_reference",
        "vendor_name", "vendor_url", "support_email", "license_file", "license_sha256",
        "third_party_notices",
    }
    missing = sorted(required - set(approval))
    unexpected = sorted(set(approval) - required)
    if missing or unexpected:
        raise PackageValidationError(
            f"commercial approval manifest schema mismatch; missing={missing}, unexpected={unexpected}"
        )
    if approval["product"] != "Skell Maw" or approval["version"] != expected_version:
        raise PackageValidationError("commercial approval product/version does not match this package")
    if approval["status"] != "approved" or approval["approved_by"] != "skellyruno":
        raise PackageValidationError("commercial candidate requires an explicit approved record from skellyruno")
    if not isinstance(approval["approval_reference"], str) or not approval["approval_reference"].strip():
        raise PackageValidationError("approval_reference must identify the owner's approval record")
    if not isinstance(approval["approved_at"], str) or not approval["approved_at"].strip():
        raise PackageValidationError("approved_at must be an ISO-8601 timestamp")
    try:
        approved_at = datetime.fromisoformat(approval["approved_at"].replace("Z", "+00:00"))
    except ValueError as exc:
        raise PackageValidationError("approved_at must be an ISO-8601 timestamp") from exc
    if approved_at.tzinfo is None or approved_at.utcoffset() is None:
        raise PackageValidationError("approved_at must include an explicit timezone")

    if approval["vendor_name"] != EXPECTED_VENDOR:
        raise PackageValidationError(
            f"approved vendor {approval['vendor_name']!r} does not match the compiled VST3 identity {EXPECTED_VENDOR!r}"
        )
    vendor_url = approval["vendor_url"]
    parsed_url = urlparse(vendor_url) if isinstance(vendor_url, str) else None
    if (
        parsed_url is None
        or parsed_url.scheme != "https"
        or not parsed_url.hostname
        or parsed_url.username is not None
        or parsed_url.password is not None
    ):
        raise PackageValidationError("vendor_url must be an HTTPS URL with a hostname and no embedded credentials")
    support_email = approval["support_email"]
    if not isinstance(support_email, str) or not EMAIL_RE.fullmatch(support_email):
        raise PackageValidationError("support_email must be a valid non-empty email address")

    base = manifest_path.parent.resolve()
    license_path = _safe_manifest_file(base, approval["license_file"], "license_file")
    license_digest = approval["license_sha256"]
    if not isinstance(license_digest, str) or not SHA256_RE.fullmatch(license_digest):
        raise PackageValidationError("license_sha256 must be a 64-character SHA-256 digest")
    actual_license_digest = _sha256_file(license_path)
    if actual_license_digest.lower() != license_digest.lower():
        raise PackageValidationError("owner-supplied LICENSE SHA-256 does not match the approval manifest")

    notices = approval["third_party_notices"]
    if not isinstance(notices, list) or not notices:
        raise PackageValidationError("third_party_notices must contain at least one verified notice")
    validated_notices = []
    package_paths: set[str] = set()
    for index, item in enumerate(notices):
        if not isinstance(item, dict) or set(item) != {"source", "package_path", "sha256"}:
            raise PackageValidationError(f"third_party_notices[{index}] must contain source, package_path, and sha256")
        source_path = _safe_manifest_file(base, item["source"], f"third_party_notices[{index}].source")
        package_path = item["package_path"]
        if not isinstance(package_path, str) or not package_path.strip() or "\\" in package_path:
            raise PackageValidationError(f"third_party_notices[{index}].package_path must be a safe relative POSIX path")
        rel = PurePosixPath(package_path)
        if not rel.parts or rel.is_absolute() or any(part in ("", ".", "..") for part in rel.parts) or ":" in package_path:
            raise PackageValidationError(f"third_party_notices[{index}].package_path contains an unsafe path")
        normalized_package_path = rel.as_posix()
        if normalized_package_path.casefold() in package_paths:
            raise PackageValidationError("third-party notice package paths must be unique, including case")
        package_paths.add(normalized_package_path.casefold())
        expected_digest = item["sha256"]
        if not isinstance(expected_digest, str) or not SHA256_RE.fullmatch(expected_digest):
            raise PackageValidationError(f"third_party_notices[{index}].sha256 must be a 64-character SHA-256 digest")
        actual_digest = _sha256_file(source_path)
        if actual_digest.lower() != expected_digest.lower():
            raise PackageValidationError(f"third-party notice hash mismatch for {item['source']}")
        validated_notices.append({
            "source_path": source_path,
            "package_path": normalized_package_path,
            "sha256": actual_digest,
        })

    return {
        "approval_reference": approval["approval_reference"].strip(),
        "approved_by": approval["approved_by"],
        "approved_at": approved_at.isoformat(),
        "vendor_name": approval["vendor_name"],
        "vendor_url": vendor_url,
        "support_email": support_email,
        "license_path": license_path,
        "license_sha256": actual_license_digest,
        "approval_manifest_sha256": hashlib.sha256(raw).hexdigest(),
        "notices": validated_notices,
    }


def validate_bundle(
    bundle: Path,
    expected_name: str,
    seen_cids: set[str],
    expected_version: str = EXPECTED_VERSION,
    commercial_approval: dict[str, Any] | None = None,
) -> dict[str, Any]:
    if not bundle.is_dir() or bundle.name != f"{expected_name}.vst3":
        raise PackageValidationError(f"{bundle}: unexpected VST3 bundle name")
    contents = bundle / "Contents"
    metadata_path = contents / "Resources" / "moduleinfo.json"
    binary_dir = contents / "x86_64-win"
    if not metadata_path.is_file():
        raise PackageValidationError(f"{bundle}: missing Resources/moduleinfo.json")
    if not binary_dir.is_dir():
        raise PackageValidationError(f"{bundle}: missing x86_64-win directory")
    modules = sorted(p for p in binary_dir.iterdir() if p.is_file() and p.suffix.lower() == ".vst3")
    all_modules = sorted(p for p in contents.rglob("*.vst3") if p.is_file())
    if len(modules) != 1 or len(all_modules) != 1 or all_modules[0] != modules[0]:
        raise PackageValidationError(f"{bundle}: expected exactly one module in x86_64-win and no duplicate architecture modules")
    if modules[0].name != f"{expected_name}.vst3":
        raise PackageValidationError(f"{bundle}: Windows module filename mismatch")
    module = modules[0]
    require_amd64_pe(module)
    try:
        normalized = remove_trailing_commas(metadata_path.read_text(encoding="utf-8-sig"))
        metadata = strict_json_loads(normalized)
    except (OSError, UnicodeError, PackageValidationError) as exc:
        raise PackageValidationError(f"{metadata_path}: invalid strict JSON: {exc}") from exc
    if not isinstance(metadata, dict):
        raise PackageValidationError(f"{metadata_path}: JSON root must be an object")
    factory, classes = metadata.get("Factory Info"), metadata.get("Classes")
    if not isinstance(factory, dict):
        raise PackageValidationError(f"{metadata_path}: Factory Info must be an object")
    if metadata.get("Name") != expected_name:
        raise PackageValidationError(f"{metadata_path}: plugin name mismatch")
    if metadata.get("Version") != expected_version:
        raise PackageValidationError(f"{metadata_path}: expected version {expected_version}")
    if factory.get("Vendor") != EXPECTED_VENDOR:
        raise PackageValidationError(f"{metadata_path}: vendor mismatch")
    if not isinstance(classes, list) or len(classes) != 2:
        raise PackageValidationError(f"{metadata_path}: expected exactly two VST3 classes")
    categories, local_cids = set(), set()
    for entry in classes:
        if not isinstance(entry, dict):
            raise PackageValidationError(f"{metadata_path}: class entry must be an object")
        category, cid = entry.get("Category"), entry.get("CID")
        if not isinstance(category, str) or category not in EXPECTED_CLASSES:
            raise PackageValidationError(f"{metadata_path}: unexpected class category {category!r}")
        if not isinstance(cid, str) or not CID_RE.fullmatch(cid):
            raise PackageValidationError(f"{metadata_path}: invalid class ID {cid!r}")
        cid = cid.upper()
        if cid in local_cids or cid in seen_cids:
            raise PackageValidationError(f"{metadata_path}: duplicate VST3 class ID {cid}")
        if entry.get("Name") != expected_name or entry.get("Vendor") != EXPECTED_VENDOR or entry.get("Version") != expected_version:
            raise PackageValidationError(f"{metadata_path}: class name/vendor/version mismatch")
        local_cids.add(cid)
        categories.add(category)
    if categories != EXPECTED_CLASSES:
        raise PackageValidationError(f"{metadata_path}: processor/controller class pair incomplete")

    warnings = []
    if commercial_approval is not None:
        factory["URL"] = commercial_approval["vendor_url"]
        factory["E-Mail"] = commercial_approval["support_email"]
    else:
        for key, label in (("URL", "vendor URL"), ("E-Mail", "support email")):
            value = factory.get(key, "")
            if value is not None and not isinstance(value, str):
                raise PackageValidationError(f"{metadata_path}: Factory Info {key} must be a string")
            if not value or not value.strip():
                warnings.append(f"{label} is blank; owner decision required before commercial release")

    seen_cids.update(local_cids)
    # Stage a standards-compliant copy; commercial contact fields are only added
    # when the explicit owner-supplied approval manifest has passed validation.
    metadata_path.write_text(
        json.dumps(metadata, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return {
        "name": expected_name,
        "version": expected_version,
        "vendor": EXPECTED_VENDOR,
        "module": module.name,
        "module_bytes": module.stat().st_size,
        "class_ids": sorted(local_cids),
        "metadata_warnings": warnings,
    }


def stage_package(
    source_dir: Path,
    package_dir: Path,
    expected_plugins: tuple[str, ...] = EXPECTED_PLUGINS,
    expected_version: str = EXPECTED_VERSION,
    commercial_release: bool = False,
    approval_manifest: Path | None = None,
) -> dict[str, Any]:
    if commercial_release and approval_manifest is None:
        raise PackageValidationError("--commercial-release requires --approval-manifest")
    if not commercial_release and approval_manifest is not None:
        raise PackageValidationError("--approval-manifest requires --commercial-release")
    approval = load_commercial_approval(approval_manifest, expected_version) if approval_manifest is not None else None

    if not source_dir.is_dir():
        raise PackageValidationError(f"source directory does not exist: {source_dir}")
    if source_dir.resolve() == package_dir.resolve():
        raise PackageValidationError("source and package directories must be different")
    found = sorted(p for p in source_dir.rglob("*.vst3") if p.is_dir())
    by_name = {}
    for bundle in found:
        by_name.setdefault(bundle.name, []).append(bundle)
    expected = {f"{name}.vst3" for name in expected_plugins}
    actual = set(by_name)
    missing, unexpected = sorted(expected - actual), sorted(actual - expected)
    duplicates = sorted(name for name, paths in by_name.items() if len(paths) != 1)
    if missing or unexpected or duplicates:
        raise PackageValidationError(f"VST3 inventory mismatch; missing={missing}, unexpected={unexpected}, duplicates={duplicates}")
    # Never destroy an earlier preview or known-good package during staging.
    # Use a fresh output path for each run so a failed/partial stage cannot
    # erase the last package that was available for inspection.
    if package_dir.exists() or package_dir.is_symlink():
        raise PackageValidationError(
            f"package destination already exists: {package_dir}; choose a new output path "
            "to preserve the existing package"
        )
    package_dir.mkdir(parents=True)
    seen_cids, plugins = set(), []
    for name in expected_plugins:
        staged = package_dir / f"{name}.vst3"
        shutil.copytree(by_name[staged.name][0], staged)
        plugins.append(validate_bundle(staged, name, seen_cids, expected_version, approval))

    commercial_metadata = None
    if approval is not None:
        license_target = package_dir / "LICENSE"
        shutil.copy2(approval["license_path"], license_target)
        notice_manifest = []
        for notice in approval["notices"]:
            target = package_dir / "THIRD_PARTY_NOTICES" / notice["package_path"]
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(notice["source_path"], target)
            notice_manifest.append({
                "path": target.relative_to(package_dir).as_posix(),
                "sha256": _sha256_file(target),
            })
        commercial_metadata = {
            "approval_reference": approval["approval_reference"],
            "approved_by": approval["approved_by"],
            "approved_at": approval["approved_at"],
            "vendor_name": approval["vendor_name"],
            "vendor_url": approval["vendor_url"],
            "support_email": approval["support_email"],
            "approval_manifest_sha256": approval["approval_manifest_sha256"],
            "license_sha256": _sha256_file(license_target),
            "third_party_notices": notice_manifest,
        }

    info = {
        "product": "Skell Maw",
        "release_status": "preview-only; not a commercial release",
        "commercial_candidate": commercial_metadata is not None,
        "format": "VST3",
        "platform": "Windows x86_64",
        "version": expected_version,
        "vendor": EXPECTED_VENDOR,
        "source_commit": os.environ.get("GITHUB_SHA", "local-unrecorded"),
        "source_ref": os.environ.get("GITHUB_REF", "local-unrecorded"),
        "pr_head_commit": os.environ.get("MAW_PR_HEAD_SHA", os.environ.get("GITHUB_SHA", "local-unrecorded")),
        "pr_head_ref": os.environ.get("MAW_PR_HEAD_REF", os.environ.get("GITHUB_REF", "local-unrecorded")),
        "workflow_run_id": os.environ.get("GITHUB_RUN_ID", "local-unrecorded"),
        "workflow_run_attempt": os.environ.get("GITHUB_RUN_ATTEMPT", "local-unrecorded"),
        "install_helper": "Tools/Install-SkellMawPreview.ps1",
        "rollback_helper": "Tools/Install-SkellMawPreview.ps1 -ManifestPath <install-manifest.json>",
        "plugins": plugins,
    }
    if commercial_metadata is not None:
        info["commercial_approval"] = commercial_metadata

    (package_dir / "build-info.json").write_text(
        json.dumps(info, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    if commercial_metadata is None:
        preview_notice = (
            "Skell Maw Windows VST3 preview package.\nNot a commercial release or signed installer.\n"
            "Back up existing plugin installations before manual testing.\n"
            "Contains seven separate VST3 bundles; install only the bundles you intend to test.\n"
            "Optional PowerShell 7 per-user install: pwsh -NoProfile -File .\\Tools\\Install-SkellMawPreview.ps1 -PackagePath .\n"
            "Rollback uses -ManifestPath with the install-manifest.json path printed after install.\n"
            "The helper is unsigned, does not run automatically, and refuses to overwrite changed bundles.\n"
        )
        release_section = (
            "LICENSE / DISTRIBUTION STATUS\n"
            "No final owner-approved license or attribution terms are included. Do not redistribute\n"
            "or use commercially until the owner approves those terms and the required vendor URL\n"
            "and support email are supplied.\n"
        )
        title = "SKELL MAW 1.0.0 — WINDOWS x64 VST3 PRIVATE PREVIEW"
    else:
        preview_notice = (
            "Skell Maw Windows VST3 preview-only package with owner-approved commercial metadata.\n"
            "The owner-supplied approval manifest, LICENSE and third-party notice hashes were validated.\n"
            "This remains preview-only, unsigned and unpublished; it does not authorize public distribution,\n"
            "payment processing, website deployment, or release publication.\n"
            "Use the per-user preview installer only for private engineering validation.\n"
        )
        release_section = (
            "LICENSE / DISTRIBUTION STATUS\n"
            "The owner-supplied LICENSE and listed THIRD_PARTY_NOTICES are included and hashed.\n"
            "The approval record is a declared manifest, not a cryptographic signature. This package\n"
            "remains unsigned and unpublished; signing, host/audio acceptance and final release approval\n"
            "are separate gates.\n"
        )
        title = "SKELL MAW 1.0.0 — WINDOWS x64 PRIVATE COMMERCIAL CANDIDATE"

    (package_dir / "PREVIEW-ONLY.txt").write_text(preview_notice, encoding="utf-8", newline="\n")
    install_guide = f"""{title}

This archive contains seven separate VST3 bundles. It is private engineering material,
not a signed installer or public release.

SAFE PER-USER INSTALL
1. Close FL Studio and every other audio host before changing plugin files.
2. Extract this archive to a temporary folder. Do not copy the ZIP into a scan path.
3. Prefer the optional PowerShell 7 helper from the extracted package root:
   pwsh -NoProfile -File .\\Tools\\Install-SkellMawPreview.ps1 -PackagePath .
4. The helper defaults to %LOCALAPPDATA%\\Programs\\Common\\VST3, validates the
   seven-bundle inventory, VST3 metadata/class-ID identity, AMD64 PE32+ module
   headers and SHA256SUMS.txt, backs up any existing same-named bundle, verifies
   the backup, and writes an install manifest.
5. Close hosts before install, then rescan and test only in a disposable project.
   The helper refuses system-wide Program Files installs and refuses to overwrite
   a bundle that changed after the preview was installed.

ROLLBACK
Run the same script with -ManifestPath <install-manifest.json> using the exact path
printed after installation. Rollback restores verified previous bundles. New preview
bundles are moved to a rollback archive rather than deleted. User-modified bundles
are left untouched.

MANUAL ALTERNATIVE
If you choose manual installation, first back up the entire existing bundle outside
all scanned VST3 directories. Prefer the per-user VST3 directory above; do not replace
a known-good system-wide plugin copy during this private preview.

INTEGRITY
- build-info.json records the build/merge commit and ref, PR head commit and ref, workflow run, and expected plugins.
- SHA256SUMS.txt covers the staged plugin payloads and package sidecars (it does not
  checksum itself). Verify the ZIP digest and every listed file before installation.

{release_section}
CI/pluginval does not imply FL Studio project/state recall, musical A/B, target-hardware
CPU/latency, or full UI acceptance.
"""
    (package_dir / "WINDOWS-VST3-INSTALL.txt").write_text(install_guide, encoding="utf-8", newline="\n")
    tools_dir = package_dir / "Tools"
    tools_dir.mkdir()
    for helper_name in ("Install-SkellMawPreview.ps1", "SkellMawPreviewInstall.psm1"):
        helper_source = Path(__file__).with_name(helper_name)
        if not helper_source.is_file():
            raise PackageValidationError(f"missing private-preview helper source: {helper_source}")
        shutil.copy2(helper_source, tools_dir / helper_name)
    checksums = []
    for path in sorted(p for p in package_dir.rglob("*") if p.is_file()):
        checksums.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(package_dir).as_posix()}")
    (package_dir / "SHA256SUMS.txt").write_text("\n".join(checksums) + "\n", encoding="ascii", newline="\n")
    return info


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", required=True, type=Path)
    parser.add_argument("--package-dir", required=True, type=Path)
    parser.add_argument(
        "--commercial-release",
        action="store_true",
        help="Stage a private, unsigned commercial candidate; requires an owner-supplied approval manifest.",
    )
    parser.add_argument(
        "--approval-manifest",
        type=Path,
        help="Owner-supplied JSON approval record with hashed LICENSE and third-party notices.",
    )
    args = parser.parse_args()
    try:
        report = stage_package(
            args.source_dir,
            args.package_dir,
            commercial_release=args.commercial_release,
            approval_manifest=args.approval_manifest,
        )
    except PackageValidationError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    print(f"Validated {len(report['plugins'])} Windows VST3 bundles, version {report['version']}:")
    for plugin in report["plugins"]:
        print(f"  PASS {plugin['name']}: {plugin['module_bytes']:,} bytes; {len(plugin['class_ids'])} unique CIDs")
        for warning in plugin["metadata_warnings"]:
            print(f"  RELEASE WARNING {plugin['name']}: {warning}")
    print(f"Staged private package: {args.package_dir}")
    print("Generated build-info.json and SHA256SUMS.txt; no signing, publishing, or deployment was performed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
