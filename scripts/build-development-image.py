#!/usr/bin/env python3
"""Build the canonical, deterministic OpenConsole development FAT image."""

from __future__ import annotations

import argparse, hashlib, json, os, pathlib, shutil, subprocess, sys, tempfile
from cryptography.hazmat.primitives import serialization

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import platform_inventory as ocp
import publisher_keys as ocpk
import ethernet_catalog as oec

DRIVER_ROLES = {"Input": "input", "Network": "network", "Audio": "audio",
                "Crypto": "crypto", "Graphics": "graphics"}


def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--payload", type=pathlib.Path, default=ROOT / "build/esp")
    p.add_argument("--output", type=pathlib.Path, default=ROOT / "build/development-image")
    p.add_argument("--platform-key", type=pathlib.Path, required=True)
    p.add_argument("--parent", type=pathlib.Path)
    p.add_argument("--parent-version", default="0.1.0-dev")
    p.add_argument("--publisher-key-store", type=pathlib.Path)
    p.add_argument("--installed-games", type=pathlib.Path)
    p.add_argument("--size-mib", type=int, default=64)
    a = p.parse_args()
    for tool in ("mkfs.vfat", "mmd", "mcopy"):
        if shutil.which(tool) is None: raise SystemExit(f"missing required tool: {tool}")
    parent = a.payload / "EFI/BOOT/BOOTX64.EFI"
    if not parent.is_file(): raise SystemExit(f"missing parent EFI: {parent}")
    if not 32 <= a.size_mib <= 2048: raise SystemExit("size must be 32..2048 MiB")
    output = a.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    image = output / "openconsole-development.fat"
    with tempfile.TemporaryDirectory(prefix="openconsole-image-") as td:
        stage = pathlib.Path(td) / "root"
        shutil.copytree(a.payload, stage, ignore=shutil.ignore_patterns("SHA256SUMS", "OPENCONSOLE-*", "games"))
        if a.parent is not None:
            if not a.parent.is_file(): raise SystemExit(f"missing parent EFI: {a.parent}")
            shutil.copyfile(a.parent, stage / "EFI/BOOT/BOOTX64.EFI")
        ocroot = stage / "EFI/OpenConsole"
        for rel in ("Drivers/Input", "Drivers/Network", "Drivers/Audio", "Drivers/Crypto", "Drivers/Graphics",
                    "Trust", "State/Games", "State/Launch", "State/Recovery", "Updates", "Logs", "Notices"):
            (ocroot / rel).mkdir(parents=True, exist_ok=True)
        # The old physical-package target can retain a stale Drivers/Input
        # directory because Make does not model copied files individually.
        # The canonical builder resolves every required artifact explicitly.
        required = {
            ROOT / "build/edk2-input-driver/UsbMouseDxe.efi": ocroot / "Drivers/Input/UsbMouseDxe.efi",
        }
        for source, target in required.items():
            if not source.is_file(): raise SystemExit(f"missing baseline artifact: {source}")
            shutil.copyfile(source, target)
        if not any((ocroot / "Drivers/Network").glob("*.efi")):
            raise SystemExit("missing baseline packaged network stack")
        catalog_source = json.loads((ROOT / "config/ethernet-catalog-x64.json").read_text(encoding="utf-8"))
        (ocroot / "Drivers/Network/catalog.oec").write_bytes(oec.build(catalog_source))
        if not (ocroot / "Drivers/Crypto/OpenConsoleCryptoDxe.efi").is_file():
            raise SystemExit("missing baseline integrity provider")
        key = ocp.private_key(a.platform_key)
        public = ocp.key_bytes(key.public_key())
        (ocroot / "Trust/PlatformInventory.ed448.pub").write_bytes(public)
        if a.publisher_key_store is not None:
            shutil.copyfile(a.publisher_key_store, ocroot / "Trust/PublisherKeys.ocpk")
        else:
            (ocroot / "Trust/PublisherKeys.ocpk").write_bytes(ocpk.build([]))
        if a.installed_games is not None:
            if not a.installed_games.is_dir(): raise SystemExit("installed-games must be a directory")
            shutil.copytree(a.installed_games, ocroot / "State/Games", dirs_exist_ok=True)
        # Detached copy is distribution evidence. The in-volume copy is only
        # an identifier/inspection aid and MUST NOT bootstrap its own trust.
        (output / "platform-inventory.ed448.pub").write_bytes(public)
        files = []
        specs = [("parent", "EFI/BOOT/BOOTX64.EFI", a.parent_version, 1, True)]
        for path in sorted(stage.rglob("*")):
            if not path.is_file() or path.name == "platform.ocp" or path.relative_to(stage).as_posix() == "EFI/BOOT/BOOTX64.EFI":
                continue
            rel = path.relative_to(stage).as_posix()
            # Host launchers are inert OS-side convenience files. They are
            # distributed on the volume but are not part of the EFI execution
            # authorization inventory.
            if pathlib.PurePosixPath(rel).parts[:2] == ("OpenConsole", "Host"):
                continue
            role, baseline, scope = "notices", False, 1
            parts = pathlib.PurePosixPath(rel).parts
            if len(parts) >= 5 and parts[:3] == ("EFI", "OpenConsole", "Drivers"):
                role = DRIVER_ROLES.get(parts[3], "notices")
                baseline = parts[3] in ("Input", "Network", "Crypto")
                scope = 2
            elif "/Trust/" in f"/{rel}": role = "trust"
            specs.append((role, rel, a.parent_version, scope, baseline))
        inventory = ocp.build(stage, "development", a.parent_version, True, specs, key)
        (ocroot / "platform.ocp").write_bytes(inventory)
        for path in sorted(p for p in stage.rglob("*") if p.is_file()):
            rel = path.relative_to(stage).as_posix()
            files.append({"path": rel, "bytes": path.stat().st_size, "sha256": digest(path)})
        source = {"schema": "openconsole.image-inputs.v1", "profile": "development",
                  "parentVersion": a.parent_version, "files": files}
        write_json(output / "source-inventory.json", source)
        write_json(output / "sbom-inputs.cdx.json", {"bomFormat":"CycloneDX", "specVersion":"1.5",
                   "version":1, "metadata":{"component":{"type":"firmware","name":"openconsole-development","version":a.parent_version}},
                   "components":[{"type":"file","name":f["path"],"hashes":[{"alg":"SHA-256","content":f["sha256"]}]} for f in files]})
        write_json(output / "profile.json", {"schema":"openconsole.image-profile.v1", "profile":"development",
                   "unsignedSecureBoot":True, "allowsUnverifiedLocalGames":False,
                   "testFixturesIncluded":False, "platformKeyId":hashlib.sha256(public).hexdigest()})
        if image.exists(): image.unlink()
        subprocess.run(["mkfs.vfat", "-C", "-F", "32", "--invariant", "-i", "4F434F4E", "-n", "OPENCONSOLE", str(image), str(a.size_mib * 1024)], check=True, stdout=subprocess.DEVNULL)
        env = {**os.environ, "MTOOLS_SKIP_CHECK":"1", "TZ":"UTC"}
        directories = sorted((p for p in stage.rglob("*") if p.is_dir()), key=lambda p:(len(p.parts), p.as_posix()))
        for directory in directories:
            subprocess.run(["mmd", "-i", str(image), "::/" + directory.relative_to(stage).as_posix()], check=True, env=env, stdout=subprocess.DEVNULL)
        for path in sorted(p for p in stage.rglob("*") if p.is_file()):
            subprocess.run(["mcopy", "-m", "-o", "-i", str(image), str(path), "::/" + path.relative_to(stage).as_posix()], check=True, env=env, stdout=subprocess.DEVNULL)
        subprocess.run([sys.executable, str(ROOT / "scripts/normalize-fat.py"), str(image)], check=True)
    sums = {name:digest(output/name) for name in ("openconsole-development.fat", "platform-inventory.ed448.pub", "source-inventory.json", "sbom-inputs.cdx.json", "profile.json")}
    (output / "SHA256SUMS").write_text("".join(f"{value}  {name}\n" for name,value in sorted(sums.items())), encoding="ascii")
    print(json.dumps({"image":str(image),"sha256":sums["openconsole-development.fat"],"files":len(files)},sort_keys=True))
    return 0


if __name__ == "__main__": raise SystemExit(main())
