#!/usr/bin/env python3
"""Guarded whole-device OpenConsole installer for Linux development hosts."""

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
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import media_layout as ml

REPORT_SCHEMA = "openconsole.usb-install-report.v1"
FAT_METADATA_RESERVE = 8 * ml.MIB


class InstallError(RuntimeError):
    pass


def lsblk() -> dict[str, object]:
    result = subprocess.run([
        "lsblk", "--json", "--bytes", "--paths",
        "--output", "NAME,PATH,TYPE,SIZE,MODEL,SERIAL,TRAN,RM,ROTA,LOG-SEC,MOUNTPOINTS,PKNAME",
    ], check=True, text=True, capture_output=True)
    return json.loads(result.stdout)


def _mounted_system(node: dict[str, object]) -> bool:
    points = node.get("mountpoints") or []
    if isinstance(points, str):
        points = [points]
    if any(point == "/" or (isinstance(point, str) and point.startswith("/boot")) for point in points):
        return True
    return any(_mounted_system(child) for child in node.get("children") or [])


def devices(document: dict[str, object]) -> list[dict[str, object]]:
    output = []
    for node in document.get("blockdevices") or []:
        if node.get("type") not in ("disk", "loop"):
            continue
        value = {
            "path": node.get("path") or node.get("name"),
            "type": node.get("type"),
            "bytes": int(node.get("size") or 0),
            "model": (node.get("model") or "").strip(),
            "serial": (node.get("serial") or "").strip(),
            "transport": (node.get("tran") or "").strip(),
            "removable": bool(node.get("rm")),
            "logicalSectorBytes": int(node.get("log-sec") or 512),
            "systemDisk": _mounted_system(node),
            "mountpoints": sorted({point for child in [node, *(node.get("children") or [])]
                                   for point in (child.get("mountpoints") or []) if point}),
        }
        value["identityToken"] = identity_token(value)
        output.append(value)
    return output


def identity_token(device: dict[str, object]) -> str:
    stable = {key: device.get(key) for key in
              ("path", "type", "bytes", "model", "serial", "transport",
               "removable", "logicalSectorBytes", "systemDisk")}
    return hashlib.sha256(json.dumps(stable, sort_keys=True,
                                     separators=(",", ":")).encode()).hexdigest()[:24]


def resolve(path: str) -> dict[str, object]:
    matches = [item for item in devices(lsblk()) if item["path"] == path]
    if len(matches) != 1:
        raise InstallError("target is absent or is not a whole block device")
    return matches[0]


def validate_target(device: dict[str, object], test_loop_backing: pathlib.Path | None) -> None:
    if device["systemDisk"]:
        raise InstallError("refusing the active root/boot system disk")
    if device["mountpoints"]:
        raise InstallError("refusing a device with mounted filesystems")
    if device["type"] == "loop":
        if test_loop_backing is None:
            raise InstallError("loop devices require --test-loop-backing")
        backing = pathlib.Path("/sys/class/block") / pathlib.Path(device["path"]).name / "loop/backing_file"
        actual = pathlib.Path("/" + backing.read_text().strip()).resolve()
        if actual != test_loop_backing.resolve():
            raise InstallError("loop backing file does not match the asserted test fixture")
    elif not device["removable"] and device["transport"] != "usb":
        raise InstallError("target is neither removable nor transported over USB")
    if device["bytes"] < 64 * ml.MIB:
        raise InstallError("target is too small")


def partition_path(device: str, number: int) -> pathlib.Path:
    suffix = "p" if device[-1:].isdigit() else ""
    return pathlib.Path(f"{device}{suffix}{number}")


def tree_digest(root: pathlib.Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        relative = path.relative_to(root).as_posix().encode()
        digest.update(len(relative).to_bytes(4, "little")); digest.update(relative)
        data = path.read_bytes()
        digest.update(len(data).to_bytes(8, "little")); digest.update(data)
    return digest.hexdigest()


def validate_payload_capacity(layout: ml.Layout, payload: pathlib.Path) -> None:
    """Reject impossible payloads before the first whole-device write."""
    boot = next(part for part in layout.partitions if part.role in ("boot", "boot-data"))
    files = [item for item in payload.rglob("*") if item.is_file()]
    oversized = next((item for item in files if item.stat().st_size > ml.FAT32_MAX_FILE), None)
    if oversized is not None:
        raise InstallError(f"payload file exceeds FAT32 individual-file limit: {oversized.relative_to(payload)}")
    required = sum(item.stat().st_size for item in files)
    if boot.bytes <= FAT_METADATA_RESERVE or required > boot.bytes - FAT_METADATA_RESERVE:
        raise InstallError("payload cannot fit in the boot filesystem")


def _run(arguments: list[str]) -> None:
    subprocess.run(arguments, check=True)


def install(device: dict[str, object], layout: ml.Layout, payload: pathlib.Path,
            architecture: str, installation_id: str, report_path: pathlib.Path) -> None:
    boot_name = "BOOTX64.EFI" if architecture == "x64" else "BOOTAA64.EFI"
    if not (payload / "EFI/BOOT" / boot_name).is_file() or not (payload / "EFI/OpenConsole/platform.ocp").is_file():
        raise InstallError(f"payload lacks {boot_name} or platform.ocp")
    source_digest = tree_digest(payload)
    target = str(device["path"])
    _run(["sgdisk", "--zap-all", target])
    for part in layout.partitions:
        _run(["sgdisk", f"--new={part.number}:{part.start_lba}:{part.end_lba}",
              f"--typecode={part.number}:{part.type_guid}",
              f"--change-name={part.number}:{part.label}", target])
    subprocess.run(["partprobe", target], check=False)
    subprocess.run(["udevadm", "settle"], check=False)
    paths = [partition_path(target, part.number) for part in layout.partitions]
    for part, path in zip(layout.partitions, paths):
        if not path.exists():
            raise InstallError(f"partition node did not appear: {path}")
        _run(["mkfs.vfat", "-F", "32", "-n", part.label, str(path)])
    encoded_marker = ml.marker(installation_id)
    with tempfile.TemporaryDirectory(prefix="openconsole-install-") as temporary:
        mount_root = pathlib.Path(temporary)
        mounted: list[pathlib.Path] = []
        try:
            for part, path in zip(layout.partitions, paths):
                point = mount_root / part.role
                point.mkdir()
                _run(["mount", str(path), str(point)])
                mounted.append(point)
                if part.role in ("boot", "boot-data"):
                    shutil.copytree(payload, point, dirs_exist_ok=True)
                if part.role in ("data", "boot-data"):
                    marker_path = point / ml.MARKER_PATH
                    marker_path.parent.mkdir(parents=True, exist_ok=True)
                    marker_path.write_bytes(encoded_marker)
                    for relative in ("EFI/OpenConsole/State/Games", "EFI/OpenConsole/State/Launch",
                                     "EFI/OpenConsole/State/Recovery", "EFI/OpenConsole/Logs",
                                     "EFI/OpenConsole/Notices"):
                        (point / relative).mkdir(parents=True, exist_ok=True)
                os.sync()
            if not (mounted[0] / "EFI/BOOT" / boot_name).is_file():
                raise InstallError("default boot file failed post-copy verification")
            for source in sorted(item for item in payload.rglob("*") if item.is_file()):
                copied = mounted[0] / source.relative_to(payload)
                if not copied.is_file() or hashlib.sha256(copied.read_bytes()).digest() != hashlib.sha256(source.read_bytes()).digest():
                    raise InstallError(f"payload byte verification failed: {source.relative_to(payload)}")
            data_mount = mounted[-1]
            if ml.parse_marker((data_mount / ml.MARKER_PATH).read_bytes()) != installation_id:
                raise InstallError("data marker failed post-copy verification")
        finally:
            for point in reversed(mounted):
                subprocess.run(["umount", str(point)], check=False)
    os.sync()
    report = {
        "schema": REPORT_SCHEMA, "status": "verified", "architecture": architecture,
        "sourcePayloadSha256": source_digest, "device": device,
        "installationId": installation_id, "layout": ml.to_dict(layout),
        "bootFilename": boot_name,
    }
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--device")
    parser.add_argument("--payload", type=pathlib.Path)
    parser.add_argument("--arch", choices=("x64", "arm64"), default="x64")
    parser.add_argument("--erase", action="store_true")
    parser.add_argument("--confirm")
    parser.add_argument("--installation-id", default=str(uuid.uuid4()))
    parser.add_argument("--report", type=pathlib.Path, default=pathlib.Path("openconsole-install-report.json"))
    parser.add_argument("--test-loop-backing", type=pathlib.Path,
                        help="CI only: exact sparse backing file for a loop target")
    arguments = parser.parse_args()
    if arguments.list:
        print(json.dumps(devices(lsblk()), indent=2, sort_keys=True)); return 0
    if not arguments.device:
        parser.error("--device is required unless --list is used")
    target = resolve(arguments.device)
    validate_target(target, arguments.test_loop_backing)
    layout = ml.plan(target["bytes"], target["logicalSectorBytes"])
    preview = {"device": target, "layout": ml.to_dict(layout),
               "requiredConfirmation": target["identityToken"]}
    if not arguments.erase:
        print(json.dumps(preview, indent=2, sort_keys=True)); return 0
    if os.geteuid() != 0:
        raise InstallError("installation requires root")
    if arguments.confirm != target["identityToken"]:
        raise InstallError("--confirm must equal the preview identity token")
    if arguments.payload is None or not arguments.payload.is_dir():
        raise InstallError("--payload must name an extracted OpenConsole payload tree")
    validate_payload_capacity(layout, arguments.payload.resolve())
    # Close the preview-to-write race as far as host APIs permit.
    current = resolve(arguments.device)
    validate_target(current, arguments.test_loop_backing)
    if current["identityToken"] != target["identityToken"]:
        raise InstallError("device identity changed after confirmation")
    install(current, layout, arguments.payload.resolve(), arguments.arch,
            ml.parse_marker(ml.marker(arguments.installation_id)), arguments.report.resolve())
    print(arguments.report.resolve())
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (InstallError, ml.LayoutError, OSError, subprocess.CalledProcessError) as error:
        print(f"openconsole-installer: {error}", file=sys.stderr)
        raise SystemExit(2)
