#!/usr/bin/env python3
from __future__ import annotations

import json
import pathlib
import struct
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]


def main() -> None:
    matrix = json.loads((ROOT / "config/targets.json").read_text(encoding="utf-8"))
    assert matrix["default"] == "x64"
    expected = {"x64": 0x8664, "arm64": 0xAA64}
    with tempfile.TemporaryDirectory(prefix="openconsole-targets-") as temporary:
        temp = pathlib.Path(temporary)
        for name, machine in expected.items():
            target = matrix["targets"][name]
            assert int(target["peMachine"], 16) == machine
            output = temp / f"{name}.obj"
            subprocess.run([
                "clang", f"--target={target['compilerTriple']}", "-std=c11", "-Wall",
                "-Wextra", "-Werror", "-ffreestanding", "-fshort-wchar", "-Iinclude",
                "-c", "tests/target_abi_test.c", "-o", str(output),
            ], cwd=ROOT, check=True)
            data = output.read_bytes()
            assert struct.unpack_from("<H", data, 0)[0] == machine
    print("PASS: x64 and ARM64 target descriptors compile with stable native ABI layouts")


if __name__ == "__main__":
    main()
