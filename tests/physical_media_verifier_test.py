#!/usr/bin/env python3
import hashlib
import importlib.util
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "verify-physical-test-media.py"
SPEC = importlib.util.spec_from_file_location("physical_media_verifier", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def make_media(root: Path, complete=False):
    payloads = {
        "EFI/BOOT/BOOTX64.EFI": b"parent",
        "games/audio/game.efi": b"audio",
        "games/physical-network/game.efi": b"network",
    }
    lines = []
    for name, data in payloads.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        lines.append(f"{hashlib.sha256(data).hexdigest()}  ./{name}\n")
    (root / MODULE.MANIFEST).write_text("".join(lines), encoding="ascii")
    for name in MODULE.EVIDENCE_LOGS:
        content = b"\0" * 16384
        if complete and name in MODULE.EVIDENCE_LOGS[:2]:
            marker = MODULE.INVENTORY_END
            content = marker + content[len(marker):]
        (root / name).write_bytes(content)


def rejected(root: Path, phrase: str):
    try:
        MODULE.verify(root, False)
    except ValueError as error:
        assert phrase in str(error), error
    else:
        raise AssertionError(f"expected rejection containing {phrase!r}")


def main():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        make_media(root)
        receipt = MODULE.verify(root, False)
        assert receipt["payloads_verified"]
        assert not receipt["inventory_stages_complete"]
        (root / "EFI/BOOT/BOOTX64.EFI").write_bytes(b"tampered")
        rejected(root, "digest mismatch")

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        make_media(root)
        (root / "unexpected.bin").write_bytes(b"extra")
        rejected(root, "coverage mismatch")

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        make_media(root)
        with (root / MODULE.MANIFEST).open("a", encoding="ascii") as stream:
            stream.write(f"{'0' * 64}  ../escape\n")
        rejected(root, "unsafe manifest path")

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        make_media(root, complete=True)
        receipt = MODULE.verify(root, True)
        assert receipt["inventory_stages_complete"]

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        make_media(root)
        try:
            MODULE.verify(root, True)
        except ValueError as error:
            assert "inventory is incomplete" in str(error), error
        else:
            raise AssertionError("empty returned inventories were accepted")
    print("PASS: physical media preflight and collection verifier")


if __name__ == "__main__":
    main()
