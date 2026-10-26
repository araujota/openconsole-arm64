#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify candidate checksums, source inventory, FAT profile, and secret hygiene."""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import tarfile
import tempfile

SECRET_NAME = re.compile(r"(^|/)(\.env(?:\..*)?|[^/]*\.(?:pem|key))$", re.I)
SECRET_MARKERS = (b"-----BEGIN " + b"PRIVATE KEY-----",
                  b"-----BEGIN ENCRYPTED " + b"PRIVATE KEY-----",
                  b"CONVEX_DEPLOY_KEY" + b"=", b"VERCEL_TOKEN" + b"=")


def sha(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def reject_secret(path: str, data: bytes) -> None:
    if SECRET_NAME.search(path) or any(marker in data for marker in SECRET_MARKERS):
        raise SystemExit(f"forbidden secret-like release member: {path}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("candidate", type=pathlib.Path)
    args = parser.parse_args()
    root = args.candidate.resolve()
    expected = {}
    for line in (root / "SHA256SUMS").read_text(encoding="ascii").splitlines():
        value, name = line.split("  ", 1)
        if name in expected or "/" in name or "\\" in name:
            raise SystemExit("invalid checksum entry")
        expected[name] = value
    actual_names = {path.name for path in root.iterdir() if path.is_file() and path.name != "SHA256SUMS"}
    if set(expected) != actual_names:
        raise SystemExit("checksum inventory does not match candidate files")
    for name, value in expected.items():
        if sha(root / name) != value:
            raise SystemExit(f"checksum mismatch: {name}")
    manifest = json.loads((root / "release-manifest.json").read_text())
    archive_name = next(item["path"] for item in manifest["artifacts"] if item["role"] == "source")
    inventory = {item["path"]: item for item in manifest["sourceInventory"]}
    seen = {}
    with tarfile.open(root / archive_name, "r:gz") as archive:
        for member in archive.getmembers():
            parts = pathlib.PurePosixPath(member.name).parts
            if not member.isfile() or len(parts) < 2 or any(part in ("", ".", "..") for part in parts):
                raise SystemExit(f"invalid source archive member: {member.name}")
            rel = pathlib.PurePosixPath(*parts[1:]).as_posix()
            if rel in seen:
                raise SystemExit(f"duplicate source archive member: {rel}")
            data = archive.extractfile(member).read()
            reject_secret(rel, data)
            seen[rel] = {"path": rel, "bytes": len(data),
                         "sha256": hashlib.sha256(data).hexdigest()}
    if seen != inventory:
        raise SystemExit("source archive differs from release manifest")
    profile = json.loads((root / "image-profile.json").read_text())
    if profile.get("profile") != "development" or profile.get("testFixturesIncluded") is not False:
        raise SystemExit("candidate image profile is not a fixture-free development profile")
    image = next(root / item["path"] for item in manifest["artifacts"]
                 if item["role"] == "development-image")
    with tempfile.TemporaryDirectory(prefix="openconsole-release-verify-") as temp:
        extracted = pathlib.Path(temp) / "fat"
        extracted.mkdir()
        subprocess.run(["mcopy", "-s", "-i", str(image), "::*", str(extracted)], check=True)
        for path in extracted.rglob("*"):
            if path.is_file():
                reject_secret(path.relative_to(extracted).as_posix(), path.read_bytes())
        required = ("EFI/BOOT/BOOTX64.EFI", "EFI/OpenConsole/platform.ocp",
                    "EFI/OpenConsole/Trust/PlatformInventory.ed448.pub",
                    "EFI/OpenConsole/Trust/PublisherKeys.ocpk")
        for name in required:
            if not (extracted / name).is_file():
                raise SystemExit(f"missing image member: {name}")
    print(json.dumps({"verified": True, "version": manifest["version"],
                      "files": len(inventory), "imageSha256": sha(image)}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
