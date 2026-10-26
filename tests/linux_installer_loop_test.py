#!/usr/bin/env python3
"""Root-only destructive installer matrix, strictly confined to owned loop files."""

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import shutil
import signal
import subprocess
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
INSTALLER = ROOT / "scripts/install-openconsole-linux.py"
SIZES = (("small", 128 * 1024**2), ("64gib", 64 * 1024**3),
         ("logical-2tb", 2_000_000_000_000))


def run(*args: str, **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, text=True, capture_output=True, **kwargs)


def attach(backing: pathlib.Path) -> str:
    result = run("losetup", "--find", "--show", "--partscan", str(backing), check=True)
    device = result.stdout.strip()
    sys_backing = pathlib.Path("/sys/class/block") / pathlib.Path(device).name / "loop/backing_file"
    actual = pathlib.Path("/" + sys_backing.read_text().strip()).resolve()
    assert actual == backing.resolve(), (actual, backing)
    return device


def detach(device: str) -> None:
    subprocess.run(("losetup", "--detach", device), check=False)


def token(device: str, backing: pathlib.Path) -> str:
    result = run("python3", str(INSTALLER), "--device", device,
                 "--test-loop-backing", str(backing), check=True)
    return json.loads(result.stdout)["requiredConfirmation"]


def install(device: str, backing: pathlib.Path, payload: pathlib.Path,
            report: pathlib.Path, *, env: dict[str, str] | None = None,
            timeout: float | None = None) -> subprocess.CompletedProcess[str]:
    identity = token(device, backing)
    return run("python3", str(INSTALLER), "--device", device,
               "--test-loop-backing", str(backing), "--erase", "--confirm", identity,
               "--payload", str(payload), "--arch", "x64", "--report", str(report),
               env=env, timeout=timeout)


def make_payload(root: pathlib.Path) -> pathlib.Path:
    payload = root / "payload"
    (payload / "EFI/BOOT").mkdir(parents=True)
    (payload / "EFI/OpenConsole").mkdir(parents=True)
    (payload / "EFI/BOOT/BOOTX64.EFI").write_bytes(b"fixture-efi")
    (payload / "EFI/OpenConsole/platform.ocp").write_bytes(b"fixture-inventory")
    return payload


def sparse(path: pathlib.Path, size: int) -> None:
    with path.open("wb") as stream:
        stream.truncate(size)


def main() -> None:
    if os.geteuid() != 0:
        raise SystemExit("SKIP: loop installer matrix requires root")
    for tool in ("losetup", "sgdisk", "mkfs.vfat", "mount", "umount"):
        if shutil.which(tool) is None:
            raise SystemExit(f"SKIP: missing {tool}")
    devices: list[str] = []
    with tempfile.TemporaryDirectory(prefix="openconsole-loop-matrix-") as temporary:
        root = pathlib.Path(temporary)
        payload = make_payload(root)
        try:
            for name, size in SIZES:
                backing = root / f"{name}.img"
                sparse(backing, size)
                device = attach(backing); devices.append(device)
                report = root / f"{name}.json"
                result = install(device, backing, payload, report)
                assert result.returncode == 0, result.stderr
                evidence = json.loads(report.read_text())
                assert evidence["status"] == "verified"
                assert evidence["layout"]["disk_bytes"] == size
                detach(device); devices.remove(device)

            # A below-minimum target must remain byte-identical: refusal precedes writes.
            backing = root / "insufficient.img"; sparse(backing, 63 * 1024**2)
            before = hashlib.sha256(backing.read_bytes()).digest()
            device = attach(backing); devices.append(device)
            result = run("python3", str(INSTALLER), "--device", device,
                         "--test-loop-backing", str(backing), "--erase", "--confirm", "bad",
                         "--payload", str(payload))
            assert result.returncode == 2 and "too small" in result.stderr
            assert hashlib.sha256(backing.read_bytes()).digest() == before
            detach(device); devices.remove(device)

            # A mounted target is in use and must be refused before mutation.
            backing = root / "in-use.img"; sparse(backing, 128 * 1024**2)
            device = attach(backing); devices.append(device)
            assert install(device, backing, payload, root / "in-use-first.json").returncode == 0
            part = pathlib.Path(device + "p1"); mountpoint = root / "busy"; mountpoint.mkdir()
            subprocess.run(("mount", str(part), str(mountpoint)), check=True)
            try:
                result = run("python3", str(INSTALLER), "--device", device,
                             "--test-loop-backing", str(backing))
                assert result.returncode == 2 and "mounted" in result.stderr
            finally:
                subprocess.run(("umount", str(mountpoint)), check=True)
            detach(device); devices.remove(device)

            # Mismatched asserted identity and stale confirmation both fail closed.
            backing = root / "identity.img"; sparse(backing, 128 * 1024**2)
            other = root / "other.img"; sparse(other, 128 * 1024**2)
            device = attach(backing); devices.append(device)
            result = run("python3", str(INSTALLER), "--device", device,
                         "--test-loop-backing", str(other))
            assert result.returncode == 2 and "does not match" in result.stderr
            result = run("python3", str(INSTALLER), "--device", device,
                         "--test-loop-backing", str(backing), "--erase", "--confirm", "stale",
                         "--payload", str(payload))
            assert result.returncode == 2 and "--confirm" in result.stderr
            detach(device); devices.remove(device)

            # Interrupt after destructive partitioning, during a deliberately stalled formatter.
            backing = root / "interrupted.img"; sparse(backing, 128 * 1024**2)
            device = attach(backing); devices.append(device)
            shims = root / "shims"; shims.mkdir()
            formatter = shims / "mkfs.vfat"
            formatter.write_text("#!/bin/sh\nsleep 30\nexec /usr/sbin/mkfs.vfat \"$@\"\n")
            formatter.chmod(0o755)
            identity = token(device, backing); report = root / "interrupted.json"
            env = dict(os.environ); env["PATH"] = str(shims) + os.pathsep + env["PATH"]
            process = subprocess.Popen(("python3", str(INSTALLER), "--device", device,
                "--test-loop-backing", str(backing), "--erase", "--confirm", identity,
                "--payload", str(payload), "--report", str(report)), env=env,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                start_new_session=True)
            time.sleep(2); os.killpg(process.pid, signal.SIGTERM); process.wait(timeout=10)
            assert process.returncode != 0 and not report.exists()
            assert run("sgdisk", "--verify", device).returncode == 0
            detach(device); devices.remove(device)
        finally:
            for device in reversed(devices):
                detach(device)
    print("PASS: guarded sparse loop matrix (small, 64 GiB, 2 TB, identity, in-use, interruption, insufficient)")


if __name__ == "__main__":
    main()
