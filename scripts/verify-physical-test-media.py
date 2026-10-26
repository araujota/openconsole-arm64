#!/usr/bin/env python3
"""Verify an OpenConsole physical-test ESP and bind returned logs to a receipt."""

import argparse
import hashlib
import json
import re
from pathlib import Path, PurePosixPath


EVIDENCE_LOGS = (
    "OPENCONSOLE-INVENTORY.LOG",
    "OPENCONSOLE-DRIVER-INVENTORY.LOG",
    "OPENCONSOLE-NETWORK-EVIDENCE.LOG",
    "OPENCONSOLE-AUDIO-EVIDENCE.LOG",
)
MANIFEST = "OPENCONSOLE-PAYLOAD-SHA256SUMS"
REQUIRED_PAYLOADS = (
    "EFI/BOOT/BOOTX64.EFI",
    "games/audio/game.efi",
    "games/physical-network/game.efi",
)
MANIFEST_LINE = re.compile(r"^([0-9a-f]{64})  (.+)$")
INVENTORY_END = b"OPENCONSOLE_IO_INVENTORY_END\n"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def safe_relative_path(raw: str) -> str:
    if raw.startswith("./"):
        raw = raw[2:]
    path = PurePosixPath(raw)
    if not raw or path.is_absolute() or ".." in path.parts or "." in path.parts:
        raise ValueError(f"unsafe manifest path: {raw!r}")
    return path.as_posix()


def regular_files(root: Path) -> set[str]:
    result = set()
    for path in root.rglob("*"):
        if path.is_symlink():
            raise ValueError(f"symlink is not permitted on physical media: {path}")
        if path.is_file():
            result.add(path.relative_to(root).as_posix())
    return result


def parse_manifest(path: Path) -> dict[str, str]:
    entries = {}
    for number, line in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        match = MANIFEST_LINE.fullmatch(line)
        if not match:
            raise ValueError(f"malformed manifest line {number}")
        digest, raw_name = match.groups()
        name = safe_relative_path(raw_name)
        if name in entries:
            raise ValueError(f"duplicate manifest path: {name}")
        entries[name] = digest
    if not entries:
        raise ValueError("payload manifest is empty")
    return entries


def verify(root: Path, collected: bool) -> dict:
    if not root.is_dir():
        raise ValueError(f"media root is not a directory: {root}")
    manifest_path = root / MANIFEST
    if not manifest_path.is_file() or manifest_path.is_symlink():
        raise ValueError(f"missing regular payload manifest: {MANIFEST}")
    entries = parse_manifest(manifest_path)
    files = regular_files(root)
    excluded = set(EVIDENCE_LOGS) | {MANIFEST}
    actual_payloads = files - excluded
    expected_payloads = set(entries)
    missing = sorted(expected_payloads - actual_payloads)
    extra = sorted(actual_payloads - expected_payloads)
    if missing or extra:
        raise ValueError(f"payload coverage mismatch; missing={missing}, extra={extra}")
    absent_required = sorted(set(REQUIRED_PAYLOADS) - expected_payloads)
    if absent_required:
        raise ValueError(f"required physical-test payloads absent: {absent_required}")
    mismatches = [name for name, expected in entries.items()
                  if sha256(root / name) != expected]
    if mismatches:
        raise ValueError(f"payload digest mismatch: {sorted(mismatches)}")

    evidence = {}
    for name in EVIDENCE_LOGS:
        path = root / name
        if not path.is_file() or path.is_symlink():
            raise ValueError(f"missing regular evidence log: {name}")
        size = path.stat().st_size
        if size != 16384:
            raise ValueError(f"{name} must remain exactly 16384 bytes, found {size}")
        data = path.read_bytes()
        evidence[name] = {
            "sha256": hashlib.sha256(data).hexdigest(),
            "nonzero_bytes": sum(byte != 0 for byte in data),
        }
    if collected:
        for name in EVIDENCE_LOGS[:2]:
            if INVENTORY_END not in (root / name).read_bytes():
                raise ValueError(f"returned inventory is incomplete: {name}")

    return {
        "schema": "openconsole.physical-media-receipt.v1",
        "mode": "collected" if collected else "preflight",
        "payload_count": len(entries),
        "payload_manifest_sha256": sha256(manifest_path),
        "payloads_verified": True,
        "inventory_stages_complete": collected,
        "evidence": evidence,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify copied OpenConsole physical media before or after boot")
    parser.add_argument("root", type=Path, help="mounted FAT32 ESP root")
    parser.add_argument("--collected", action="store_true",
                        help="require complete firmware and post-driver inventories")
    parser.add_argument("--output", type=Path, help="write a JSON receipt")
    args = parser.parse_args()
    try:
        receipt = verify(args.root, args.collected)
    except (OSError, UnicodeDecodeError, ValueError) as error:
        raise SystemExit(f"FAIL: {error}") from error
    encoded = json.dumps(receipt, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
