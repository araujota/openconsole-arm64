#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import pathlib
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("installer", ROOT / "scripts/install-openconsole-linux.py")
assert spec and spec.loader
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


def main() -> None:
    document = {"blockdevices": [{
        "name": "/dev/sdz", "path": "/dev/sdz", "type": "disk",
        "size": 2_000_000_000_000, "model": "Fixture", "serial": "ABC",
        "tran": "usb", "rm": True, "log-sec": 512, "mountpoints": [None],
        "children": [{"type": "part", "mountpoints": [None]}],
    }, {
        "name": "/dev/nvme0n1", "path": "/dev/nvme0n1", "type": "disk",
        "size": 1_000_000_000_000, "model": "System", "serial": "SYS",
        "tran": "nvme", "rm": False, "log-sec": 4096, "mountpoints": [None],
        "children": [{"type": "part", "mountpoints": ["/"]}],
    }]}
    values = installer.devices(document)
    usb, system = values
    assert usb["removable"] and not usb["systemDisk"] and len(usb["identityToken"]) == 24
    assert system["systemDisk"]
    installer.validate_target(usb, None)
    try:
        installer.validate_target(system, None)
    except installer.InstallError as error:
        assert "system disk" in str(error)
    else:
        raise AssertionError("system disk accepted")
    changed = dict(usb); changed["serial"] = "CHANGED"
    assert installer.identity_token(changed) != usb["identityToken"]
    assert installer.partition_path("/dev/sdz", 2) == pathlib.Path("/dev/sdz2")
    assert installer.partition_path("/dev/loop7", 2) == pathlib.Path("/dev/loop7p2")
    with tempfile.TemporaryDirectory() as directory:
        payload = pathlib.Path(directory)
        (payload / "small.bin").write_bytes(b"ok")
        installer.validate_payload_capacity(installer.ml.plan(64 * installer.ml.MIB), payload)
        (payload / "too-large.bin").write_bytes(b"x" * 1024)
        tiny_boot = installer.ml.Layout(installer.ml.SCHEMA, 64 * installer.ml.MIB, 512,
            installer.ml.MIB, "single", (installer.ml.Partition(1, "boot-data", 2048,
            4095, installer.FAT_METADATA_RESERVE + 512, installer.ml.ESP_TYPE,
            installer.ml.ESP_LABEL),), 0, installer.ml.FAT32_MAX_FILE)
        try:
            installer.validate_payload_capacity(tiny_boot, payload)
        except installer.InstallError as error:
            assert "cannot fit" in str(error)
        else:
            raise AssertionError("insufficient payload capacity accepted")
    print("PASS: Linux installer discovery, refusal, identity, and capacity preflight")


if __name__ == "__main__":
    main()
