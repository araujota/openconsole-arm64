#!/usr/bin/env python3
from __future__ import annotations

import json
import pathlib
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.verify_ethernet_size_budget import verify


def main() -> None:
    config = ROOT / "config/ethernet-release-size-budget.json"
    stack = ROOT / "build/edk2-network-stack"
    result = verify(config, stack, stack / "AARCH64")
    assert result["architectures"]["x64"]["modules"] == 13
    assert result["architectures"]["arm64"]["modules"] == 13
    assert all(item["imageFractionPermille"] <= 40
               for item in result["architectures"].values())
    with tempfile.TemporaryDirectory() as temporary:
        strict = json.loads(config.read_text(encoding="utf-8"))
        strict["limits"]["perArchitectureBytes"] = 1
        path = pathlib.Path(temporary) / "strict.json"
        path.write_text(json.dumps(strict), encoding="utf-8")
        try:
            verify(path, stack, stack / "AARCH64")
        except SystemExit as error:
            assert "portfolio" in str(error)
        else:
            raise AssertionError("oversized portfolio was accepted")
    print("PASS: x64 and ARM64 Ethernet release-size budgets")


if __name__ == "__main__":
    main()
