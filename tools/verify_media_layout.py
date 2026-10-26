#!/usr/bin/env python3
"""Independently validate an OpenConsole media plan or installer report."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import media_layout as ml


def verify(value: dict[str, object]) -> dict[str, object]:
    layout = value.get("layout", value)
    if not isinstance(layout, dict) or layout.get("schema") != ml.SCHEMA:
        raise ml.LayoutError("unexpected layout schema")
    expected = ml.to_dict(ml.plan(int(layout["disk_bytes"]), int(layout["sector_bytes"])))
    if layout != expected:
        raise ml.LayoutError("layout does not match independent capacity plan")
    parts = layout["partitions"]
    if not isinstance(parts, list):
        raise ml.LayoutError("partition list is absent")
    roles = [part["role"] for part in parts]
    if roles not in (["boot-data"], ["boot", "data"]):
        raise ml.LayoutError("invalid partition roles")
    report = {
        "schema": "openconsole.media-verification.v1",
        "valid": True,
        "mode": layout["mode"],
        "diskBytes": layout["disk_bytes"],
        "partitionBytes": {part["role"]: part["bytes"] for part in parts},
        "unexpectedUnallocatedBytes": 0,
        "expectedMetadataAndAlignmentBytes": layout["expected_unallocated_bytes"],
        "fat32MaxFileBytes": layout["fat32_max_file_bytes"],
        "labels": [part["label"] for part in parts],
        "typeGuids": [part["type_guid"] for part in parts],
    }
    if "installationId" in value:
        report["installationId"] = ml.parse_marker(ml.marker(str(value["installationId"])))
    return report


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    arguments = parser.parse_args()
    report = verify(json.loads(arguments.input.read_text()))
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if arguments.output:
        arguments.output.write_text(encoded)
    else:
        print(encoded, end="")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, KeyError, TypeError, ValueError, ml.LayoutError) as error:
        print(f"verify-media-layout: {error}", file=sys.stderr)
        raise SystemExit(2)
