#!/usr/bin/env python3
"""Enforce architecture-specific baseline Ethernet release-size budgets."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"ethernet-size-budget: {message}")


def verify(config_path: Path, x64_dir: Path, arm64_dir: Path) -> dict:
    config = json.loads(config_path.read_text(encoding="utf-8"))
    if config.get("schema") != "openconsole.ethernet-release-size-budget.v1":
        fail("unsupported schema")
    limits = config.get("limits", {})
    per_arch = limits.get("perArchitectureBytes")
    per_module = limits.get("perModuleBytes")
    permille = limits.get("maximumImageFractionPermille")
    image_bytes = config.get("imageBytes")
    if not all(isinstance(value, int) and value > 0
               for value in (per_arch, per_module, permille, image_bytes)):
        fail("limits must be positive integers")
    required = config.get("requiredModules")
    if not isinstance(required, list) or not required or len(required) != len(set(required)):
        fail("requiredModules must be a nonempty unique list")
    result = {}
    for architecture, directory in (("x64", x64_dir), ("arm64", arm64_dir)):
        files = {path.name: path for path in directory.glob("*.efi")}
        if set(files) != set(required):
            fail(f"{architecture} module set differs from the release baseline")
        sizes = {name: path.stat().st_size for name, path in sorted(files.items())}
        oversized = [name for name, size in sizes.items() if size > per_module]
        if oversized:
            fail(f"{architecture} modules exceed per-module limit: {oversized}")
        total = sum(sizes.values())
        if total > per_arch:
            fail(f"{architecture} portfolio is {total} bytes; limit is {per_arch}")
        if total * 1000 > image_bytes * permille:
            fail(f"{architecture} portfolio exceeds image fraction limit")
        result[architecture] = {
            "bytes": total,
            "imageFractionPermille": (total * 1000 + image_bytes - 1) // image_bytes,
            "largestModuleBytes": max(sizes.values()),
            "modules": len(sizes),
        }
    return {"schema": config["schema"], "limits": limits, "architectures": result}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path,
                        default=Path("config/ethernet-release-size-budget.json"))
    parser.add_argument("--x64", type=Path, default=Path("build/edk2-network-stack"))
    parser.add_argument("--arm64", type=Path,
                        default=Path("build/edk2-network-stack/AARCH64"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = verify(args.config, args.x64, args.arm64)
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
