#!/usr/bin/env python3
"""Verify every EFI image in an assembled architecture-specific tree."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

from verify_efi import VerificationError, inspect


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=pathlib.Path)
    parser.add_argument("--arch", required=True, choices=("x64", "arm64"))
    arguments = parser.parse_args()
    try:
        images = sorted(path for path in arguments.root.rglob("*")
                        if path.is_file() and path.suffix.lower() == ".efi")
        if not images:
            raise VerificationError("tree contains no EFI images")
        records = []
        for path in images:
            role = "driver" if "Drivers" in path.parts else "application"
            record = inspect(path, arguments.arch, role)
            record["path"] = path.relative_to(arguments.root).as_posix()
            records.append(record)
        print(json.dumps({"schema": "openconsole.efi-inventory.v1",
                          "architecture": arguments.arch, "images": records},
                         indent=2, sort_keys=True))
        return 0
    except (OSError, VerificationError) as error:
        print(f"verify-efi-tree: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
