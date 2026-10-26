#!/usr/bin/env python3
import importlib.util
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("game_assets", ROOT / "tools" / "game_assets.py")
assert SPEC and SPEC.loader
assets = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = assets
SPEC.loader.exec_module(assets)


def rejects(code, callback):
    try:
        callback()
    except assets.AssetError as exc:
        assert exc.code == code
    else:
        raise AssertionError(f"expected {code}")


def main():
    jacket = b"OCJ1" + struct.pack("<HH", 2, 2) + bytes(range(16))
    assert assets.validate_ocj1(jacket)["pixelBytes"] == 16
    rejects("OCJ1_MAGIC", lambda: assets.validate_ocj1(b"PNG"))
    rejects("OCJ1_DIMENSIONS", lambda: assets.validate_ocj1(b"OCJ1" + bytes(4)))
    rejects("OCJ1_LENGTH", lambda: assets.validate_ocj1(jacket[:-1]))
    result = assets.validate_controls_v1(b'left = "j"\nexit = "escape"\nmouse = "mouse.left"\n')
    assert result["mappings"]["left"] == "j"
    rejects("CONTROLS_DUPLICATE", lambda: assets.validate_controls_v1(b'a = "x"\na = "y"\n'))
    rejects("CONTROLS_KEY", lambda: assets.validate_controls_v1(b'a = "not-a-key"\n'))
    rejects("CONTROLS_SYNTAX", lambda: assets.validate_controls_v1(b'a: "x"\n'))
    print("PASS: bounded OCJ1 and controls-v1 validators")


if __name__ == "__main__":
    main()
