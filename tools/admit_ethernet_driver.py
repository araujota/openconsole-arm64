#!/usr/bin/env python3
"""Verify and stage a user-acquired Ethernet EFI driver entirely offline."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

import ethernet_driver_manifest as manifests
from verify_efi import VerificationError, inspect as inspect_efi


class AdmissionError(ValueError):
    pass


def admit(manifest_path: Path, artifact_path: Path, public_key_path: Path,
          root: Path) -> dict:
    document = json.loads(manifest_path.read_text(encoding="utf-8"))
    payload = manifests.verify(document, manifests.public_key(public_key_path))
    if payload["acquisition"] != "user-acquired":
        raise AdmissionError("manifest is not authorized for user-acquired admission")
    artifact = payload["artifact"]
    raw = artifact_path.read_bytes()
    if len(raw) != artifact["bytes"]:
        raise AdmissionError("artifact byte length differs from signed manifest")
    digest = hashlib.sha256(raw).hexdigest()
    if digest != artifact["sha256"]:
        raise AdmissionError("artifact digest differs from signed manifest")
    try:
        inspect_efi(artifact_path, payload["architecture"], "driver")
    except VerificationError as error:
        raise AdmissionError(str(error)) from error

    destination = root / artifact["path"]
    manifest_destination = destination.with_suffix(destination.suffix + ".oced.json")
    if destination.exists() or manifest_destination.exists():
        raise AdmissionError("refusing to overwrite an existing admitted driver")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="openconsole-ethernet-admit-",
                                     dir=destination.parent) as temporary:
        stage = Path(temporary)
        staged_artifact = stage / destination.name
        staged_manifest = stage / manifest_destination.name
        shutil.copyfile(artifact_path, staged_artifact)
        staged_manifest.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n",
                                   encoding="utf-8")
        os.replace(staged_manifest, manifest_destination)
        os.replace(staged_artifact, destination)
    return {
        "architecture": payload["architecture"],
        "bytes": len(raw),
        "manifestId": payload["manifestId"],
        "path": artifact["path"],
        "sha256": digest,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("artifact", type=Path)
    parser.add_argument("public_key", type=Path)
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(admit(args.manifest, args.artifact, args.public_key,
                               args.root), sort_keys=True))
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(json.dumps({"code": "OCED_ADMISSION_REJECTED", "message": str(error)},
                         sort_keys=True), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
