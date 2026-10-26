#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import os
import pathlib
import subprocess
import sys
import tempfile

from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PublicKey

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import platform_inventory as ocp
import ethernet_catalog as oec

NETWORK_DRIVERS = {
    "ArpDxe.efi", "Dhcp4Dxe.efi", "DnsDxe.efi", "DpcDxe.efi",
    "HttpDxe.efi", "HttpUtilitiesDxe.efi", "Ip4Dxe.efi", "MnpDxe.efi",
    "SnpDxe.efi", "TcpDxe.efi", "TlsDxe.efi", "Udp4Dxe.efi",
}
CRYPTO_DRIVERS = {"Hash2DxeCrypto.efi"}
ROLE_DRIVERS = {
    "Input": {"UsbMouseDxe.efi"},
    "Crypto": {"OpenConsoleCryptoDxe.efi"},
    "Audio": {"AudioDxe.efi"},
}


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="openconsole-arm64-determinism-") as temporary:
        temp = pathlib.Path(temporary)
        images = [temp / "first.fat", temp / "second.fat"]
        for image in images:
            subprocess.run([sys.executable, "scripts/build-arm64-image.py", "--output", str(image)],
                           cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        first, second = (path.read_bytes() for path in images)
        assert first == second
        extract = temp / "extract"
        extract.mkdir()
        environment = {**os.environ, "MTOOLS_SKIP_CHECK": "1", "TZ": "UTC"}
        subprocess.run(["mcopy", "-s", "-i", str(images[0]), "::/*", str(extract)],
                       check=True, env=environment, stdout=subprocess.DEVNULL)
        inventory = subprocess.run([
            sys.executable, "tools/verify_efi_tree.py", "--arch", "arm64", str(extract),
        ], cwd=ROOT, check=True, text=True, capture_output=True).stdout
        assert "BOOTAA64.EFI" in inventory and "game.efi" in inventory
        assert not list(extract.rglob("BOOTX64.EFI"))
        host = extract / "OpenConsole/Host"
        assert (host / "manifest.json").is_file()
        assert (host / "Open OpenConsole on next reboot.cmd").is_file()
        assert (host / "Open OpenConsole on next reboot.sh").is_file()
        driver_dir = extract / "EFI/OpenConsole/Drivers/Network"
        assert {path.name for path in driver_dir.glob("*.efi")} == (NETWORK_DRIVERS |
                                                                         CRYPTO_DRIVERS)
        for role, names in ROLE_DRIVERS.items():
            assert {path.name for path in
                    (extract / "EFI/OpenConsole/Drivers" / role).glob("*.efi")} == names
        catalog = driver_dir / "catalog.oec"
        assert catalog.read_bytes() == oec.build({
            "schema": "openconsole.ethernet-catalog.v1", "entries": []})
        key = Ed448PublicKey.from_public_bytes(
            (extract / "EFI/OpenConsole/Trust/PlatformInventory.ed448.pub").read_bytes())
        signed = ocp.decode(
            (extract / "EFI/OpenConsole/platform.ocp").read_bytes(), key, extract)
        mutated_driver = extract / "EFI/OpenConsole/Drivers/Input/UsbMouseDxe.efi"
        original_driver = mutated_driver.read_bytes()
        mutated_driver.write_bytes(original_driver[:-1] + bytes([original_driver[-1] ^ 1]))
        try:
            ocp.decode((extract / "EFI/OpenConsole/platform.ocp").read_bytes(), key, extract)
        except ocp.InventoryError:
            pass
        else:
            raise AssertionError("signed ARM64 inventory accepted a mutated baseline driver")
        mutated_driver.write_bytes(original_driver)
        network_records = {pathlib.PurePosixPath(record.path).name
                           for record in signed.records
                           if ocp.ROLE_NAMES[record.role] == "network"}
        assert signed.signature_verified and network_records == NETWORK_DRIVERS | CRYPTO_DRIVERS | {"catalog.oec"}
        assert {pathlib.PurePosixPath(record.path).name for record in signed.records
                if ocp.ROLE_NAMES[record.role] == "crypto"} == ROLE_DRIVERS["Crypto"]
        assert all(record.baseline and record.key_scope == 2
                   for record in signed.records
                   if ocp.ROLE_NAMES[record.role] in ("network", "crypto"))
        for role, names in (("input", ROLE_DRIVERS["Input"]),
                            ("audio", ROLE_DRIVERS["Audio"]),
                            ("crypto", ROLE_DRIVERS["Crypto"])):
            assert {pathlib.PurePosixPath(record.path).name for record in signed.records
                    if ocp.ROLE_NAMES[record.role] == role} == names
        print("PASS: deterministic ARM64 FAT and complete native EFI inventory " +
              hashlib.sha256(first).hexdigest())


if __name__ == "__main__":
    main()
