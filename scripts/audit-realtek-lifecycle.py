#!/usr/bin/env python3
"""Record bounded static lifecycle evidence from audited Realtek UNDI images."""

from __future__ import annotations
import argparse, hashlib, json, pathlib

EXPECTED = {
    "x64": "2f7f22e0a76230308d1a8ea67b9be51dc23d55705f40efef7e8c3d964f0bba20",
    "arm64": "5792be910d2522a2c002a2f032b13f34cebd5fae2f67ce7cda3aba12a3ff5398",
}
MARKERS = (
    "Realtek Ethernet Controller Configuration",
    "Save RealManage Configuration to NvRam",
    "Warning Reset Required",
)


def inspect(path: pathlib.Path, architecture: str) -> dict:
    blob = path.read_bytes(); digest = hashlib.sha256(blob).hexdigest()
    if digest != EXPECTED[architecture]:
        raise SystemExit(f"unexpected {architecture} image digest: {digest}")
    markers = {}
    for text in MARKERS:
        wide = blob.find(text.encode("utf-16le")); narrow = blob.find(text.encode("ascii"))
        markers[text] = {"utf16leOffset": wide if wide >= 0 else None,
                         "asciiOffset": narrow if narrow >= 0 else None}
    return {"architecture": architecture, "bytes": len(blob), "sha256": digest,
            "markers": markers}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--x64", required=True, type=pathlib.Path)
    parser.add_argument("--arm64", required=True, type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    report = {"schema": "openconsole.realtek-static-lifecycle-evidence.v1",
              "version": "2.081", "images": [inspect(args.x64, "x64"),
                                                inspect(args.arm64, "arm64")],
              "interpretation": {
                  "x64": "persistent-configuration-capability-present-release-rejected",
                  "arm64": "markers-absent-not-proof-of-no-persistent-write",
                  "dma": "not-provable-by-string-scan-physical-evidence-required"}}
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output: args.output.write_text(encoded, encoding="utf-8")
    else: print(encoded, end="")
    return 0


if __name__ == "__main__": raise SystemExit(main())
