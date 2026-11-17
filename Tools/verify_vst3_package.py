#!/usr/bin/env python3
"""Validate and stage the seven Windows VST3 bundles produced by Skell Maw."""
from __future__ import annotations
import argparse, hashlib, json, os, re, shutil, struct, sys
from pathlib import Path
from typing import Any

EXPECTED_PLUGINS = ("Butterfly Delay", "Constrictor", "Dark Forest", "Hypnosis", "Night Bloom", "Serpents Maw", "Shed Skin")
EXPECTED_VERSION = "1.0.0"
EXPECTED_VENDOR = "Skell Audio"
EXPECTED_CLASSES = {"Audio Module Class", "Component Controller Class"}
CID_RE = re.compile(r"^[0-9A-Fa-f]{32}$")


class PackageValidationError(RuntimeError):
    pass


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


def validate_bundle(bundle: Path, expected_name: str, seen_cids: set[str], expected_version: str = EXPECTED_VERSION) -> dict[str, Any]:
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
        metadata = json.loads(normalized)
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise PackageValidationError(f"{metadata_path}: invalid JSON: {exc}") from exc
    if not isinstance(metadata, dict):
        raise PackageValidationError(f"{metadata_path}: JSON root must be an object")
    factory, classes = metadata.get("Factory Info") or {}, metadata.get("Classes")
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
        if category not in EXPECTED_CLASSES:
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
    seen_cids.update(local_cids)
    # JUCE currently emits trailing commas; stage a standards-compliant copy.
    metadata_path.write_text(normalized.rstrip() + "\n", encoding="utf-8", newline="\n")
    warnings = []
    if not factory.get("URL"):
        warnings.append("vendor URL is blank; owner decision required before commercial release")
    if not factory.get("E-Mail"):
        warnings.append("support email is blank; owner decision required before commercial release")
    return {"name": expected_name, "version": expected_version, "vendor": EXPECTED_VENDOR,
            "module": module.name, "module_bytes": module.stat().st_size,
            "class_ids": sorted(local_cids), "metadata_warnings": warnings}


def stage_package(source_dir: Path, package_dir: Path, expected_plugins: tuple[str, ...] = EXPECTED_PLUGINS,
                  expected_version: str = EXPECTED_VERSION) -> dict[str, Any]:
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
    if package_dir.exists():
        shutil.rmtree(package_dir)
    package_dir.mkdir(parents=True)
    seen_cids, plugins = set(), []
    for name in expected_plugins:
        staged = package_dir / f"{name}.vst3"
        shutil.copytree(by_name[staged.name][0], staged)
        plugins.append(validate_bundle(staged, name, seen_cids, expected_version))
    info = {
        "product": "Skell Maw", "release_status": "preview-only; not a commercial release",
        "format": "VST3", "platform": "Windows x86_64", "version": expected_version,
        "vendor": EXPECTED_VENDOR, "source_commit": os.environ.get("GITHUB_SHA", "local-unrecorded"),
        "source_ref": os.environ.get("GITHUB_REF", "local-unrecorded"),
        "workflow_run_id": os.environ.get("GITHUB_RUN_ID", "local-unrecorded"),
        "workflow_run_attempt": os.environ.get("GITHUB_RUN_ATTEMPT", "local-unrecorded"),
        "plugins": plugins,
    }
    (package_dir / "build-info.json").write_text(json.dumps(info, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    (package_dir / "PREVIEW-ONLY.txt").write_text(
        "Skell Maw Windows VST3 preview package.\nNot a commercial release or signed installer.\n"
        "Back up existing plugin installations before manual testing.\n"
        "Contains seven separate VST3 bundles; install only the bundles you intend to test.\n",
        encoding="utf-8", newline="\n")
    checksums = []
    for path in sorted(p for p in package_dir.rglob("*") if p.is_file()):
        checksums.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(package_dir).as_posix()}")
    (package_dir / "SHA256SUMS.txt").write_text("\n".join(checksums) + "\n", encoding="ascii", newline="\n")
    return info


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", required=True, type=Path)
    parser.add_argument("--package-dir", required=True, type=Path)
    args = parser.parse_args()
    try:
        report = stage_package(args.source_dir, args.package_dir)
    except PackageValidationError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    print(f"Validated {len(report['plugins'])} Windows VST3 bundles, version {report['version']}:")
    for plugin in report["plugins"]:
        print(f"  PASS {plugin['name']}: {plugin['module_bytes']:,} bytes; {len(plugin['class_ids'])} unique CIDs")
        for warning in plugin["metadata_warnings"]:
            print(f"  RELEASE WARNING {plugin['name']}: {warning}")
    print(f"Staged preview package: {args.package_dir}")
    print("Generated build-info.json and SHA256SUMS.txt; artifact is explicitly preview-only.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
