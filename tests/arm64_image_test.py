#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


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
        print("PASS: deterministic ARM64 FAT and complete native EFI inventory " +
              hashlib.sha256(first).hexdigest())


if __name__ == "__main__":
    main()
