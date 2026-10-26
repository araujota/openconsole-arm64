#!/usr/bin/env python3
"""Boot the ARM64 suitcase and prove parent -> child -> parent behavior."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import socket
import subprocess
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_CODE = pathlib.Path("/usr/share/AAVMF/AAVMF_CODE.fd")
DEFAULT_VARS = pathlib.Path("/usr/share/AAVMF/AAVMF_VARS.fd")


def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Qmp:
    def __init__(self, path: pathlib.Path):
        self.connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.connection.connect(str(path))
        self.stream = self.connection.makefile("rwb", buffering=0)
        self._read_response()
        self.command("qmp_capabilities")

    def _read_response(self) -> dict[str, object]:
        while True:
            line = self.stream.readline()
            if not line:
                raise RuntimeError("QMP connection closed")
            value = json.loads(line)
            if "event" not in value:
                return value

    def command(self, execute: str, arguments: dict[str, object] | None = None) -> object:
        request: dict[str, object] = {"execute": execute}
        if arguments is not None:
            request["arguments"] = arguments
        self.stream.write(json.dumps(request).encode("ascii") + b"\n")
        response = self._read_response()
        if "error" in response:
            raise RuntimeError(f"QMP {execute} failed: {response['error']}")
        return response.get("return")

    def key(self, qcode: str) -> None:
        self.command("send-key", {"keys": [{"type": "qcode", "data": qcode}]})

    def close(self) -> None:
        self.stream.close()
        self.connection.close()


def wait_for(path: pathlib.Path, marker: str, deadline: float) -> str:
    while time.monotonic() < deadline:
        if path.exists():
            text = path.read_text(encoding="utf-8", errors="replace")
            if marker in text:
                return text
        time.sleep(0.05)
    tail = path.read_text(encoding="utf-8", errors="replace")[-4000:] if path.exists() else ""
    raise TimeoutError(f"timed out waiting for {marker!r}\n{tail}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=pathlib.Path,
                        default=ROOT / "build/arm64/openconsole-arm64-development.fat")
    parser.add_argument("--evidence", type=pathlib.Path,
                        default=ROOT / "build/arm64/evidence")
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument("--data-image", action="append", type=pathlib.Path, default=[])
    parser.add_argument("--expect-data-volume")
    arguments = parser.parse_args()
    qemu_name = os.environ.get("OPENCONSOLE_AARCH64_QEMU", "qemu-system-aarch64")
    qemu = shutil.which(qemu_name)
    code = pathlib.Path(os.environ.get("OPENCONSOLE_AAVMF_CODE", str(DEFAULT_CODE)))
    variables = pathlib.Path(os.environ.get("OPENCONSOLE_AAVMF_VARS", str(DEFAULT_VARS)))
    missing = [str(item) for item in (arguments.image, code, variables) if not item.is_file()]
    if qemu is None:
        missing.append(qemu_name)
    if missing:
        raise SystemExit("ARM64 VM preflight missing: " + ", ".join(missing))
    arguments.evidence.mkdir(parents=True, exist_ok=True)
    serial = arguments.evidence / "serial.log"
    dashboard = arguments.evidence / "dashboard.ppm"
    child = arguments.evidence / "child.ppm"
    for path in (serial, dashboard, child):
        if path.exists():
            path.unlink()
    started = time.monotonic()
    deadline = started + arguments.timeout
    image_digest = digest(arguments.image)
    with tempfile.TemporaryDirectory(prefix="openconsole-aavmf-") as temporary:
        temp = pathlib.Path(temporary)
        vars_copy = temp / "vars.fd"
        qmp_path = temp / "qmp.sock"
        shutil.copyfile(variables, vars_copy)
        command = [
            qemu, "-machine", "virt", "-cpu", "cortex-a57", "-m", "1024",
            "-nodefaults", "-no-reboot",
            "-drive", f"if=pflash,format=raw,readonly=on,file={code}",
            "-drive", f"if=pflash,format=raw,file={vars_copy}",
            "-drive", f"if=none,format=raw,snapshot=on,id=suitcase,file={arguments.image.resolve()}",
            "-device", "virtio-blk-pci,drive=suitcase", "-device", "virtio-gpu-pci",
            "-device", "qemu-xhci", "-device", "usb-kbd",
            "-netdev", "user,id=net0", "-device", "virtio-net-pci,netdev=net0",
            "-serial", f"file:{serial.resolve()}", "-monitor", "none", "-display", "none",
            "-qmp", f"unix:{qmp_path},server=on,wait=off",
        ]
        for index, data_image in enumerate(arguments.data_image):
            if not data_image.is_file():
                raise RuntimeError(f"missing data image: {data_image}")
            command += ["-drive", f"if=none,format=raw,snapshot=on,id=ocdata{index},file={data_image.resolve()}",
                        "-device", f"virtio-blk-pci,drive=ocdata{index}"]
        process = subprocess.Popen(command, cwd=ROOT)
        qmp = None
        try:
            while not qmp_path.exists() and time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(f"QEMU exited early with {process.returncode}")
                time.sleep(0.05)
            qmp = Qmp(qmp_path)
            wait_for(serial, "OPENCONSOLE_MENU_READY", deadline)
            qmp.command("screendump", {"filename": str(dashboard.resolve())})
            qmp.key("ret")
            wait_for(serial, "OPENCONSOLE_CHILD_READY", deadline)
            qmp.command("screendump", {"filename": str(child.resolve())})
            qmp.key("a")
            log = wait_for(serial, "OPENCONSOLE_CHILD_RETURNED", deadline)
            try:
                process.wait(timeout=max(1.0, deadline - time.monotonic()))
            except subprocess.TimeoutExpired as error:
                raise TimeoutError("parent did not terminate after successful child return") from error
            required = [
                "OPENCONSOLE_PARENT_BOOTED", "OPENCONSOLE_CRYPTO_PROVIDER_READY",
                "OPENCONSOLE_INPUT_DRIVER_STARTED", "OPENCONSOLE_POINTER_READY",
                "OPENCONSOLE_IO_AUDIO_DRIVER_NO_COMPATIBLE_BACKEND_IDENTIFIED",
                "OPENCONSOLE_NET_SNP_FIRMWARE_PRESENT", "OPENCONSOLE_NET_PROVIDER_READY",
                "OPENCONSOLE_DASHBOARD_HEADER_READY",
                "OPENCONSOLE_DASHBOARD_RESULTS_0000000000000001",
                "OPENCONSOLE_DASHBOARD_LAUNCH_ID_arm-input-probe",
                "OPENCONSOLE_CHILD_ABI_NEGOTIATED_UEFI_INPUT_1_0",
                "OPENCONSOLE_CHILD_READY", "OPENCONSOLE_CHILD_KEY_RECEIVED",
                "OPENCONSOLE_CHILD_RETURNED",
            ]
            positions = [log.index(marker) for marker in required]
            if positions != sorted(positions):
                raise RuntimeError("ARM64 lifecycle checkpoints were not ordered")
            if arguments.expect_data_volume and arguments.expect_data_volume not in log:
                raise RuntimeError(f"missing data-volume routing evidence: {arguments.expect_data_volume}")
            if not dashboard.is_file() or dashboard.stat().st_size < 1024:
                raise RuntimeError("dashboard framebuffer evidence is missing")
            if not child.is_file() or child.stat().st_size < 1024:
                raise RuntimeError("child framebuffer evidence is missing")
            report = {
                "schema": "openconsole.arm64-aavmf-evidence.v1",
                "result": "passed",
                "evidenceLevel": "aavmf-validated-development-preview",
                "image": {"path": str(arguments.image), "sha256": image_digest},
                "qemu": subprocess.check_output([qemu, "--version"], text=True).splitlines()[0],
                "firmware": {"path": str(code), "sha256": digest(code)},
                "checkpoints": required,
                "screenshots": {
                    "dashboard": {"path": str(dashboard), "sha256": digest(dashboard)},
                    "child": {"path": str(child), "sha256": digest(child)},
                },
                "capabilities": {
                    "parentBoot": "aavmf-validated", "dashboard": "aavmf-validated",
                    "arm64ChildLaunch": "aavmf-validated", "keyboardInput": "aavmf-validated",
                    "physicalHardware": "unverified", "networking": "aavmf-virtio-snp-validated",
                    "audio": "aavmf-no-device-nonfatal", "gpuAcceleration": "unverified",
                },
                "elapsedSeconds": round(time.monotonic() - started, 3),
            }
            report_path = arguments.evidence / "report.json"
            report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                                   encoding="utf-8")
            print(json.dumps(report, sort_keys=True))
            return 0
        finally:
            if qmp is not None:
                qmp.close()
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())
