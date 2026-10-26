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
import platform_inventory as ocp
import ethernet_catalog as oec
from verify_efi import inspect as inspect_efi


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(output: pathlib.Path, parent: pathlib.Path, child: pathlib.Path,
          jacket: pathlib.Path, network_stack: pathlib.Path,
          input_driver: pathlib.Path, crypto_driver: pathlib.Path,
          audio_driver: pathlib.Path,
          size_mib: int) -> dict[str, object]:
    for tool in ("mkfs.vfat", "mmd", "mcopy"):
        if shutil.which(tool) is None:
            raise SystemExit(f"missing required tool: {tool}")
    inspect_efi(parent, "arm64", "application")
    inspect_efi(child, "arm64", "application")
    network_drivers = sorted(network_stack.glob("*.efi"))
    if not network_drivers:
        raise SystemExit(f"missing ARM64 network stack: {network_stack}")
    for driver in network_drivers:
        inspect_efi(driver, "arm64", "driver")
    native_drivers = {
        "Input/UsbMouseDxe.efi": input_driver,
        "Crypto/OpenConsoleCryptoDxe.efi": crypto_driver,
        "Audio/AudioDxe.efi": audio_driver,
    }
    for path, driver in native_drivers.items():
        if not driver.is_file():
            raise SystemExit(f"missing ARM64 {path} driver: {driver}")
        inspect_efi(driver, "arm64", "driver")
    output.parent.mkdir(parents=True, exist_ok=True)
    manifest_source = ROOT / "examples/games/arm-input-probe/game.toml"
    with tempfile.TemporaryDirectory(prefix="openconsole-arm64-image-") as temporary:
        stage = pathlib.Path(temporary) / "root"
        game = stage / "games/arm-input-probe"
        boot = stage / "EFI/BOOT"
        packages = stage / "packages"
        driver_root = stage / "EFI/OpenConsole/Drivers"
        drivers = driver_root / "Network"
        trust = stage / "EFI/OpenConsole/Trust"
        host_tools = stage / "OpenConsole/Host"
        boot.mkdir(parents=True)
        game.mkdir(parents=True)
        packages.mkdir(parents=True)
        drivers.mkdir(parents=True)
        trust.mkdir(parents=True)
        host_tools.mkdir(parents=True)
        shutil.copyfile(parent, boot / "BOOTAA64.EFI")
        for source in sorted((ROOT / "host-tools").iterdir()):
            if source.is_file():
                shutil.copyfile(source, host_tools / source.name)
        for driver in network_drivers:
            shutil.copyfile(driver, drivers / driver.name)
        for relative, driver in native_drivers.items():
            destination = driver_root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(driver, destination)
        catalog_source = json.loads((ROOT / "config/ethernet-catalog-arm64.json").read_text(encoding="utf-8"))
        (drivers / "catalog.oec").write_bytes(oec.build(catalog_source))
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
        platform_key = Ed448PrivateKey.from_private_bytes(bytes(range(57, 0, -1)))
        platform_public = ocp.key_bytes(platform_key.public_key())
        (trust / "PlatformInventory.ed448.pub").write_bytes(platform_public)
        platform_specs = [
            ("parent", "EFI/BOOT/BOOTAA64.EFI", "0.1.0-dev", 1, True),
            *(("network",
               f"EFI/OpenConsole/Drivers/Network/{driver.name}",
               "edk2-pinned", 2, True) for driver in network_drivers),
            ("network", "EFI/OpenConsole/Drivers/Network/catalog.oec",
             "0.1.0-dev", 2, True),
            ("input", "EFI/OpenConsole/Drivers/Input/UsbMouseDxe.efi",
             "edk2-pinned", 2, True),
            ("crypto", "EFI/OpenConsole/Drivers/Crypto/OpenConsoleCryptoDxe.efi",
             "edk2-pinned", 2, True),
            ("audio", "EFI/OpenConsole/Drivers/Audio/AudioDxe.efi",
             "opencore-pinned", 2, True),
            ("trust", "EFI/OpenConsole/Trust/PlatformInventory.ed448.pub",
             "0.1.0-dev", 1, True),
        ]
        (stage / "EFI/OpenConsole/platform.ocp").write_bytes(
            ocp.build(stage, "development", "0.1.0-dev", True,
                      platform_specs, platform_key))
        inventory = []
        for path in sorted(item for item in stage.rglob("*") if item.is_file()):
            if path.suffix.lower() == ".efi":
                role = "driver" if "Drivers" in path.parts else "application"
                inspect_efi(path, "arm64", role)
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
    parser.add_argument("--network-stack", type=pathlib.Path,
                        default=ROOT / "build/edk2-network-stack/AARCH64")
    parser.add_argument("--input-driver", type=pathlib.Path,
                        default=ROOT / "build/edk2-input-driver/AARCH64/UsbMouseDxe.efi")
    parser.add_argument("--crypto-driver", type=pathlib.Path,
                        default=ROOT / "build/update-crypto-dxe/AARCH64/OpenConsoleCryptoDxe.efi")
    parser.add_argument("--audio-driver", type=pathlib.Path,
                        default=ROOT / "build/opencore-audiodxe/AARCH64/AudioDxe.efi")
    parser.add_argument("--output", type=pathlib.Path,
                        default=ROOT / "build/arm64/openconsole-arm64-development.fat")
    parser.add_argument("--size-mib", type=int, default=64)
    arguments = parser.parse_args()
    if not 32 <= arguments.size_mib <= 2048:
        raise SystemExit("size must be 32..2048 MiB")
    print(json.dumps(build(arguments.output, arguments.parent, arguments.child,
                           arguments.jacket, arguments.network_stack,
                           arguments.input_driver, arguments.crypto_driver,
                           arguments.audio_driver,
                           arguments.size_mib), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
