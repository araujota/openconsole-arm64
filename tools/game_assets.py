#!/usr/bin/env python3
"""Bounded validators for OpenConsole package assets."""

from __future__ import annotations

import dataclasses
import re
import struct


@dataclasses.dataclass(frozen=True)
class AssetError(ValueError):
    code: str
    message: str
    line: int | None = None

    def __str__(self) -> str:
        return self.message


def validate_ocj1(data: bytes) -> dict[str, int | str]:
    if len(data) > 1024 * 1024:
        raise AssetError("OCJ1_LIMIT", "jacket exceeds 1 MiB")
    if len(data) < 8 or data[:4] != b"OCJ1":
        raise AssetError("OCJ1_MAGIC", "jacket does not start with OCJ1")
    width, height = struct.unpack_from("<HH", data, 4)
    if not 1 <= width <= 512 or not 1 <= height <= 512:
        raise AssetError("OCJ1_DIMENSIONS", "jacket dimensions must be 1..512")
    expected = 8 + width * height * 4
    if len(data) != expected:
        raise AssetError("OCJ1_LENGTH", f"jacket needs exactly {expected} bytes")
    return {"schema": "openconsole.ocj1.inspect.v1", "width": width,
            "height": height, "pixelBytes": expected - 8}


ACTION = re.compile(r"^[a-z][a-z0-9_.-]{0,63}$")
NAMED_KEYS = {"enter", "escape", "space", "left", "right", "up", "down",
              "mouse.left", "mouse.right"}


def validate_controls_v1(data: bytes) -> dict[str, object]:
    if len(data) > 16 * 1024:
        raise AssetError("CONTROLS_LIMIT", "controls asset exceeds 16 KiB")
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError as exc:
        raise AssetError("CONTROLS_ENCODING", "controls v1 must be ASCII") from exc
    mappings: dict[str, str] = {}
    for line_number, source in enumerate(text.splitlines(), 1):
        line = source.split("#", 1)[0].strip()
        if not line:
            continue
        match = re.fullmatch(r'([a-z][a-z0-9_.-]{0,63})\s*=\s*"([^"\\]*)"', line)
        if not match:
            raise AssetError("CONTROLS_SYNTAX", "expected action = \"key\"", line_number)
        action, key = match.groups()
        if action in mappings:
            raise AssetError("CONTROLS_DUPLICATE", f"duplicate action {action}", line_number)
        if not ACTION.fullmatch(action) or not (key in NAMED_KEYS or
                                                 len(key) == 1 and 0x20 <= ord(key) <= 0x7E):
            raise AssetError("CONTROLS_KEY", f"unsupported key {key!r}", line_number)
        mappings[action] = key
    return {"schema": "openconsole.controls.inspect.v1", "mappings": mappings}
