#!/usr/bin/env python3
from __future__ import annotations

import copy
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import media_layout as ml
import verify_media_layout as verifier


def main() -> None:
    value = ml.to_dict(ml.plan(2_000_000_000_000))
    report = verifier.verify(value)
    assert report["valid"] and report["unexpectedUnallocatedBytes"] == 0
    changed = copy.deepcopy(value)
    changed["partitions"][1]["bytes"] -= 512
    try:
        verifier.verify(changed)
    except ml.LayoutError:
        pass
    else:
        raise AssertionError("capacity mismatch accepted")
    print("PASS: independent media layout capacity and metadata verifier")


if __name__ == "__main__":
    main()
