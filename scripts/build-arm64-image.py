#!/usr/bin/env python3
"""Build a deterministic ARM64 OpenConsole FAT suitcase for AAVMF."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PrivateKey

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ocg2
from verify_efi import inspect as inspect_efi


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(output: pathlib.Path, parent: pathlib.Path, child: pathlib.Path,
          jacket: pathlib.Path, size_mib: int) -> dict[str, object]:
    for tool in ("mkfs.vfat", "mmd", "mcopy"):
        if shutil.which(tool) is None:
            raise SystemExit(f"missing required tool: {tool}")
    inspect_efi(parent, "arm64", "application")
    inspect_efi(child, "arm64", "application")
    output.parent.mkdir(parents=True, exist_ok=True)
    manifest_source = ROOT / "examples/games/arm-input-probe/game.toml"
    with tempfile.TemporaryDirectory(prefix="openconsole-arm64-image-") as temporary:
        stage = pathlib.Path(temporary) / "root"
        game = stage / "games/arm-input-probe"
        boot = stage / "EFI/BOOT"
        packages = stage / "packages"
        boot.mkdir(parents=True)
        game.mkdir(parents=True)
        packages.mkdir(parents=True)
        shutil.copyfile(parent, boot / "BOOTAA64.EFI")
        shutil.copyfile(child, game / "game.efi")
        shutil.copyfile(manifest_source, game / "game.toml")
        shutil.copyfile(jacket, game / "jacket.ocj")
        key = Ed448PrivateKey.from_private_bytes(bytes(range(1, 58)))
        bundle = ocg2.pack(manifest_source, {
            "entry-arm64": child,
            "jacket-ocj1": jacket,
        }, key)
        (packages / "arm-input-probe.ocg").write_bytes(bundle)
        public = key.public_key().public_bytes(
            serialization.Encoding.Raw, serialization.PublicFormat.Raw)
        (packages / "development-publisher.ed448.pub").write_bytes(public)
        inventory = []
        for path in sorted(item for item in stage.rglob("*") if item.is_file()):
            if path.suffix.lower() == ".efi":
                inspect_efi(path, "arm64", "application")
            inventory.append({
                "path": path.relative_to(stage).as_posix(),
                "bytes": path.stat().st_size,
                "sha256": sha256(path),
            })
        (stage / "ARM64-INVENTORY.json").write_text(
            json.dumps({"schema": "openconsole.arm64-image.v1", "files": inventory},
                       indent=2, sort_keys=True) + "\n", encoding="utf-8")
        # Re-run the independent recursive gate after the stage is complete so a
        # later driver addition cannot bypass the per-file checks above.
        for path in (item for item in stage.rglob("*") if item.is_file() and
                     item.suffix.lower() == ".efi"):
            role = "driver" if "Drivers" in path.parts else "application"
            inspect_efi(path, "arm64", role)
        if output.exists():
            output.unlink()
        subprocess.run([
            "mkfs.vfat", "-C", "-F", "32", "--invariant", "-i", "4F434136",
            "-n", "OC-ARM64", str(output), str(size_mib * 1024),
        ], check=True, stdout=subprocess.DEVNULL)
        environment = {**os.environ, "MTOOLS_SKIP_CHECK": "1", "TZ": "UTC"}
        directories = sorted((item for item in stage.rglob("*") if item.is_dir()),
                             key=lambda item: (len(item.parts), item.as_posix()))
        for directory in directories:
            subprocess.run(["mmd", "-i", str(output),
                            "::/" + directory.relative_to(stage).as_posix()],
                           check=True, env=environment, stdout=subprocess.DEVNULL)
        for path in sorted(item for item in stage.rglob("*") if item.is_file()):
            subprocess.run(["mcopy", "-m", "-o", "-i", str(output), str(path),
                            "::/" + path.relative_to(stage).as_posix()],
                           check=True, env=environment, stdout=subprocess.DEVNULL)
        subprocess.run([sys.executable, str(ROOT / "scripts/normalize-fat.py"), str(output)],
                       check=True)
    return {"image": str(output), "bytes": output.stat().st_size, "sha256": sha256(output)}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--parent", type=pathlib.Path,
                        default=ROOT / "build/arm64/BOOTAA64-TEST.EFI")
    parser.add_argument("--child", type=pathlib.Path,
                        default=ROOT / "build/arm64/INPUT-PROBE-AA64.EFI")
    parser.add_argument("--jacket", type=pathlib.Path, default=ROOT / "build/jacket.ocj")
    parser.add_argument("--output", type=pathlib.Path,
                        default=ROOT / "build/arm64/openconsole-arm64-development.fat")
    parser.add_argument("--size-mib", type=int, default=64)
    arguments = parser.parse_args()
    if not 32 <= arguments.size_mib <= 2048:
        raise SystemExit("size must be 32..2048 MiB")
    print(json.dumps(build(arguments.output, arguments.parent, arguments.child,
                           arguments.jacket, arguments.size_mib), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
