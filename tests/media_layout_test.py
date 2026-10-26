#!/usr/bin/env python3
from __future__ import annotations

import json
import pathlib
import sys
import tempfile
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import media_layout as ml


def rejected(function, *arguments):
    try:
        function(*arguments)
    except ml.LayoutError:
        return
    raise AssertionError("invalid input accepted")


def main() -> None:
    small = ml.plan(4 * ml.GIB)
    assert small.mode == "single" and len(small.partitions) == 1
    large = ml.plan(2_000_000_000_000)
    assert large.mode == "split" and len(large.partitions) == 2
    assert large.partitions[0].bytes == ml.GIB
    assert large.partitions[1].end_lba < large.disk_bytes // large.sector_bytes
    assert large.expected_unallocated_bytes < 4 * ml.MIB
    assert sum(item.bytes for item in large.partitions) + large.expected_unallocated_bytes == large.disk_bytes
    assert ml.plan(2_000_000_000_000, 4096).partitions[1].bytes > 1_900_000_000_000
    rejected(ml.plan, 63 * ml.MIB)
    rejected(ml.plan, 2_000_000_000_001)
    rejected(ml.plan, 2_000_000_000_000, 1024)

    identifier = str(uuid.UUID("01234567-89ab-4def-8123-456789abcdef"))
    encoded = ml.marker(identifier)
    assert ml.parse_marker(encoded) == identifier
    rejected(ml.parse_marker, b"")
    rejected(ml.parse_marker, encoded + b" ")
    rejected(ml.parse_marker, b'{"schema":"openconsole.data-volume.v1","schema":"x","installationId":"x"}\n')
    rejected(ml.parse_marker, b"{" + b"x" * ml.MAX_MARKER_BYTES + b"}")
    parsed = json.loads(json.dumps(ml.to_dict(large)))
    assert parsed["schema"] == ml.SCHEMA and parsed["fat32_max_file_bytes"] == 4294967295
    with tempfile.TemporaryDirectory(prefix="openconsole-sparse-") as directory:
        sparse = pathlib.Path(directory) / "two-terabyte.img"
        with sparse.open("wb") as stream:
            stream.truncate(2_000_000_000_000)
        assert sparse.stat().st_size == large.disk_bytes
    print("PASS: bounded marker and overflow-checked 2 TB GPT/FAT32 layout")


if __name__ == "__main__":
    main()
