#!/usr/bin/env python3
"""Overflow-checked OpenConsole removable-media layout and marker contract."""

from __future__ import annotations

import argparse
import json
import re
import uuid
from dataclasses import asdict, dataclass

SCHEMA = "openconsole.media-layout.v1"
MARKER_SCHEMA = "openconsole.data-volume.v1"
MARKER_PATH = "EFI/OpenConsole/data-volume.json"
ESP_TYPE = "c12a7328-f81f-11d2-ba4b-00a0c93ec93b"
DATA_TYPE = "0fc63daf-8483-4772-8e79-3d69d8477de4"
ESP_LABEL = "OC_BOOT"
DATA_LABEL = "OC_DATA"
MIB = 1024 * 1024
GIB = 1024 * MIB
FAT32_MAX_FILE = (4 * GIB) - 1
MAX_MARKER_BYTES = 512
UUID_RE = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")


class LayoutError(ValueError):
    pass


@dataclass(frozen=True)
class Partition:
    number: int
    role: str
    start_lba: int
    end_lba: int
    bytes: int
    type_guid: str
    label: str


@dataclass(frozen=True)
class Layout:
    schema: str
    disk_bytes: int
    sector_bytes: int
    alignment_bytes: int
    mode: str
    partitions: tuple[Partition, ...]
    expected_unallocated_bytes: int
    fat32_max_file_bytes: int


def _align_up(value: int, alignment: int) -> int:
    if value < 0 or alignment <= 0:
        raise LayoutError("invalid alignment input")
    return ((value + alignment - 1) // alignment) * alignment


def _align_down(value: int, alignment: int) -> int:
    if value < 0 or alignment <= 0:
        raise LayoutError("invalid alignment input")
    return (value // alignment) * alignment


def plan(disk_bytes: int, sector_bytes: int = 512, *, split_threshold_bytes: int = 8 * GIB,
         esp_bytes: int = GIB, alignment_bytes: int = MIB) -> Layout:
    if sector_bytes not in (512, 4096):
        raise LayoutError("logical sector must be 512 or 4096 bytes")
    if disk_bytes < 64 * MIB or disk_bytes % sector_bytes:
        raise LayoutError("device is too small or not sector aligned")
    if alignment_bytes % sector_bytes or esp_bytes % alignment_bytes:
        raise LayoutError("ESP/alignment is not sector aligned")
    # Reserve primary and backup GPT areas, then align both usable edges.
    first = _align_up(34 * sector_bytes, alignment_bytes)
    usable_end = _align_down(disk_bytes - (33 * sector_bytes), alignment_bytes) - sector_bytes
    if usable_end <= first:
        raise LayoutError("no aligned GPT data area")
    split = disk_bytes > split_threshold_bytes
    parts: list[Partition] = []
    if split:
        esp_end = first + esp_bytes - sector_bytes
        data_start = _align_up(esp_end + sector_bytes, alignment_bytes)
        if data_start >= usable_end or usable_end - data_start + sector_bytes < 64 * MIB:
            raise LayoutError("device cannot hold split layout")
        parts.append(Partition(1, "boot", first // sector_bytes, esp_end // sector_bytes,
                               esp_bytes, ESP_TYPE, ESP_LABEL))
        parts.append(Partition(2, "data", data_start // sector_bytes,
                               usable_end // sector_bytes,
                               usable_end - data_start + sector_bytes,
                               DATA_TYPE, DATA_LABEL))
        mode = "split"
    else:
        size = usable_end - first + sector_bytes
        parts.append(Partition(1, "boot-data", first // sector_bytes,
                               usable_end // sector_bytes, size, ESP_TYPE, ESP_LABEL))
        mode = "single"
    allocated = sum(part.bytes for part in parts)
    return Layout(SCHEMA, disk_bytes, sector_bytes, alignment_bytes, mode, tuple(parts),
                  disk_bytes - allocated, FAT32_MAX_FILE)


def marker(installation_id: str) -> bytes:
    try:
        canonical = str(uuid.UUID(installation_id))
    except (ValueError, AttributeError) as error:
        raise LayoutError("invalid installation UUID") from error
    if not UUID_RE.fullmatch(canonical):
        raise LayoutError("installation UUID must be canonical random UUIDv4")
    return (json.dumps({"installationId": canonical, "schema": MARKER_SCHEMA},
                       sort_keys=True, separators=(",", ":")) + "\n").encode("ascii")


def parse_marker(data: bytes) -> str:
    if not data or len(data) > MAX_MARKER_BYTES or b"\x00" in data:
        raise LayoutError("data-volume marker is empty, oversized, or binary")
    try:
        value = json.loads(data.decode("ascii"), object_pairs_hook=_unique_object)
    except (UnicodeDecodeError, json.JSONDecodeError, LayoutError) as error:
        raise LayoutError("malformed data-volume marker") from error
    if set(value) != {"schema", "installationId"} or value["schema"] != MARKER_SCHEMA:
        raise LayoutError("unsupported data-volume marker fields or schema")
    canonical = marker(value["installationId"])
    if canonical != data:
        raise LayoutError("data-volume marker is not canonical")
    return value["installationId"]


def _unique_object(items: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in items:
        if key in result:
            raise LayoutError("duplicate marker field")
        result[key] = value
    return result


def to_dict(layout: Layout) -> dict[str, object]:
    value = asdict(layout)
    value["partitions"] = [asdict(item) for item in layout.partitions]
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bytes", type=int, required=True)
    parser.add_argument("--sector-bytes", type=int, default=512)
    parser.add_argument("--installation-id")
    arguments = parser.parse_args()
    value = to_dict(plan(arguments.bytes, arguments.sector_bytes))
    if arguments.installation_id:
        value["marker"] = marker(arguments.installation_id).decode("ascii").rstrip()
    print(json.dumps(value, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
