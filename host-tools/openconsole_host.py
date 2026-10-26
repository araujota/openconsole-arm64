#!/usr/bin/env python3
"""Conservative Linux host-side one-shot OpenConsole boot helper."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import subprocess
import sys
from dataclasses import dataclass

PLAN_SCHEMA = "openconsole.host-reboot-plan.v1"
MARKER_SCHEMA = "openconsole.data-volume.v1"
UUID = r"[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}"
BOOT_RE = re.compile(r"^Boot([0-9A-Fa-f]{4})(?:\*)?\s+.*?HD\([^,]+,GPT,(" + UUID + r"),[^)]*\)/File\(([^)]*)\)")
NEXT_RE = re.compile(r"^BootNext:\s*([0-9A-Fa-f]{4})\s*$", re.MULTILINE)


class HostError(RuntimeError):
    pass


@dataclass(frozen=True)
class Media:
    architecture: str
    boot_path: str
    boot_root: pathlib.Path
    boot_partuuid: str
    installation_id: str
    disk_path: str
    disk_serial: str

    @property
    def token(self) -> str:
        source = "\0".join((self.installation_id, self.boot_partuuid.lower(),
                            self.disk_path, self.disk_serial))
        return hashlib.sha256(source.encode()).hexdigest()


def load_json(path: pathlib.Path, limit: int) -> dict:
    data = path.read_bytes()
    if not data or len(data) > limit or b"\0" in data:
        raise HostError(f"invalid or oversized metadata: {path}")
    try:
        value = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise HostError(f"malformed metadata: {path}") from error
    if not isinstance(value, dict):
        raise HostError(f"metadata is not an object: {path}")
    return value


def parse_marker(path: pathlib.Path) -> str:
    value = load_json(path, 512)
    if set(value) != {"installationId", "schema"} or value.get("schema") != MARKER_SCHEMA:
        raise HostError("unsupported OpenConsole media marker")
    ident = value.get("installationId")
    if not isinstance(ident, str) or not re.fullmatch(UUID, ident):
        raise HostError("invalid OpenConsole installation identity")
    canonical = json.dumps({"installationId": ident.lower(), "schema": MARKER_SCHEMA},
                           sort_keys=True, separators=(",", ":")) + "\n"
    if path.read_text(encoding="ascii") != canonical:
        raise HostError("OpenConsole media marker is not canonical")
    return ident.lower()


def _flatten(records: list[dict]) -> list[dict]:
    result: list[dict] = []
    for record in records:
        result.append(record)
        result.extend(_flatten(record.get("children") or []))
    return result


def _mount_for(path: pathlib.Path, records: list[dict]) -> dict:
    resolved = path.resolve()
    candidates = []
    for record in records:
        for mount in record.get("mountpoints") or []:
            if mount:
                try:
                    resolved.relative_to(pathlib.Path(mount).resolve())
                    candidates.append((len(mount), record))
                except ValueError:
                    pass
    if not candidates:
        raise HostError("launcher is not on a mounted block-device partition")
    return max(candidates, key=lambda item: item[0])[1]


def discover(host_dir: pathlib.Path, lsblk_json: dict | None = None,
             machine: str | None = None) -> Media:
    host_dir = host_dir.resolve()
    if host_dir.name != "Host" or host_dir.parent.name != "OpenConsole":
        raise HostError("launcher was copied away from OpenConsole/Host")
    boot_root = host_dir.parents[1]
    manifest = load_json(host_dir / "manifest.json", 8192)
    if manifest.get("schema") != "openconsole.host-launcher.v1" or manifest.get("version") != 1:
        raise HostError("unsupported host launcher manifest")
    machine_name = (machine or platform.machine()).lower()
    matches = [(name, item) for name, item in manifest.get("architectures", {}).items()
               if machine_name in item.get("machineNames", [])]
    if len(matches) != 1:
        raise HostError(f"unsupported host architecture: {machine_name}")
    arch, arch_data = matches[0]
    boot_path = arch_data["bootPath"]
    boot_file = boot_root / boot_path.lstrip("\\").replace("\\", "/")
    if not boot_file.is_file() or not (boot_root / "EFI/OpenConsole/platform.ocp").is_file():
        raise HostError("containing media lacks the matching parent EFI or platform inventory")
    if (boot_root / ("EFI/BOOT/BOOTAA64.EFI" if arch == "x64" else "EFI/BOOT/BOOTX64.EFI")).exists():
        raise HostError("media contains ambiguous architecture boot files")
    if lsblk_json is None:
        command = ["lsblk", "--json", "--bytes", "--paths", "--output",
                   "NAME,PATH,TYPE,SIZE,MODEL,SERIAL,TRAN,RM,MOUNTPOINTS,PKNAME,PARTUUID"]
        try:
            lsblk_json = json.loads(subprocess.run(command, check=True, capture_output=True,
                                                   text=True).stdout)
        except (OSError, subprocess.SubprocessError, json.JSONDecodeError) as error:
            raise HostError("unable to inspect the containing USB device") from error
    records = _flatten(lsblk_json.get("blockdevices") or [])
    part = _mount_for(host_dir, records)
    if part.get("type") != "part" or not part.get("partuuid"):
        raise HostError("launcher mount has no stable partition identity")
    parent_name = part.get("pkname")
    disks = [item for item in records if item.get("type") == "disk" and
             (item.get("path") == parent_name or item.get("name") == parent_name or
              pathlib.Path(str(item.get("path", ""))).name == pathlib.Path(str(parent_name)).name)]
    if len(disks) != 1:
        raise HostError("unable to identify one whole device for launcher media")
    disk = disks[0]
    if not (bool(disk.get("rm")) or str(disk.get("tran", "")).lower() == "usb"):
        raise HostError("launcher media is not reported as removable USB storage")
    marker_paths = []
    for candidate in records:
        if candidate.get("pkname") != parent_name and candidate is not part:
            continue
        for mount in candidate.get("mountpoints") or []:
            if mount:
                marker = pathlib.Path(mount) / "EFI/OpenConsole/data-volume.json"
                if marker.is_file():
                    marker_paths.append(marker)
    marker_paths = sorted(set(marker_paths))
    if len(marker_paths) != 1:
        raise HostError("expected exactly one OpenConsole marker on the containing USB")
    return Media(arch, boot_path, boot_root, str(part["partuuid"]),
                 parse_marker(marker_paths[0]), str(disk.get("path") or disk.get("name")),
                 str(disk.get("serial") or ""))


def parse_efibootmgr(text: str, partuuid: str, boot_path: str) -> tuple[list[str], str | None]:
    if len(text.encode()) > 1048576 or "\0" in text:
        raise HostError("efibootmgr output is oversized or binary")
    target_path = boot_path.replace("/", "\\").lower()
    entries = []
    for line in text.splitlines()[:512]:
        if len(line) > 4096:
            raise HostError("efibootmgr line exceeds parser bound")
        match = BOOT_RE.match(line)
        if match and match.group(2).lower() == partuuid.lower() and \
                match.group(3).replace("/", "\\").lower() == target_path:
            entries.append(match.group(1).upper())
    next_match = NEXT_RE.search(text)
    return entries, next_match.group(1).upper() if next_match else None


def efibootmgr() -> str:
    if not pathlib.Path("/sys/firmware/efi").is_dir():
        raise HostError("host was not booted in UEFI mode")
    try:
        result = subprocess.run(["efibootmgr", "-v"], check=True, capture_output=True,
                                text=True, timeout=15)
    except (OSError, subprocess.SubprocessError) as error:
        raise HostError("efibootmgr is unavailable or firmware variables are inaccessible") from error
    return result.stdout


def make_plan(media: Media, entries: list[str], boot_next: str | None) -> dict:
    direct = len(entries) == 1
    return {"schema": PLAN_SCHEMA, "version": 1, "action": "boot-next" if direct else "firmware-settings",
            "directBootAvailable": direct, "entry": entries[0] if direct else None,
            "currentBootNext": boot_next, "architecture": media.architecture,
            "bootPath": media.boot_path, "bootPartitionGuid": media.boot_partuuid.lower(),
            "installationId": media.installation_id, "mediaToken": media.token,
            "device": media.disk_path, "persistentBootOrderMutation": False}


def arm_bootnext(media: Media, entry: str, expected_token: str, before: str,
                 run, read) -> None:
    entries, current = parse_efibootmgr(before, media.boot_partuuid, media.boot_path)
    if expected_token != media.token or entries != [entry.upper()]:
        raise HostError("media identity or firmware entry changed after confirmation")
    run(["efibootmgr", "--bootnext", entry.upper()])
    _, readback = parse_efibootmgr(read(), media.boot_partuuid, media.boot_path)
    if readback != entry.upper():
        run(["efibootmgr", "--bootnext", current] if current else
            ["efibootmgr", "--delete-bootnext"], check=False)
        raise HostError("BootNext readback failed; previous state was restored")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--plan", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--arm", action="store_true")
    parser.add_argument("--entry")
    parser.add_argument("--expected-token")
    parser.add_argument("--host-dir", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parent)
    args = parser.parse_args()
    try:
        media = discover(args.host_dir)
        before = efibootmgr()
        entries, current = parse_efibootmgr(before, media.boot_partuuid, media.boot_path)
        plan = make_plan(media, entries, current)
        if args.arm:
            if os.geteuid() != 0:
                raise HostError("arming BootNext requires administrator privileges")
            def runner(command, check=True):
                return subprocess.run(command, check=check, timeout=15)
            arm_bootnext(media, args.entry or "", args.expected_token or "", before,
                         runner, efibootmgr)
            plan["armed"] = True
        else:
            plan["armed"] = False
        print(json.dumps(plan, sort_keys=True, separators=(",", ":")))
        return 0
    except (HostError, subprocess.SubprocessError) as error:
        print(f"OpenConsole launcher: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
