#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Create a deterministic, secret-screened OpenConsole development candidate."""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import pathlib
import shutil
import tarfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE_DIRS = (".github", "docs", "examples", "include", "openspec", "patches",
               "scripts", "src", "tests", "tools", "vendor", "web")
SOURCE_FILES = ("CODE_OF_CONDUCT.md", "CONTRIBUTING.md", "dependency-provenance.json",
                "LICENSE", "Makefile", "NOTICE", "README.md", "requirements-analyzer.txt",
                "release-version.json", "sbom.cdx.json",
                "SECURITY.md", "THIRD_PARTY_NOTICES.md")
EXCLUDED_PARTS = {"node_modules", ".next", "test-results", "__pycache__", ".convex"}
EXCLUDED_SUFFIXES = {".pyc", ".pyo", ".pem", ".key"}
SECRET_MARKERS = (b"-----BEGIN " + b"PRIVATE KEY-----",
                  b"-----BEGIN ENCRYPTED " + b"PRIVATE KEY-----",
                  b"CONVEX_DEPLOY_KEY" + b"=", b"VERCEL_TOKEN" + b"=",
                  b"VERCEL_OIDC_TOKEN" + b"=")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def source_files() -> list[pathlib.Path]:
    files = [ROOT / name for name in SOURCE_FILES]
    public_inventory = ROOT / "PUBLIC-SOURCE-INVENTORY.json"
    if public_inventory.is_file():
        files.append(public_inventory)
    for directory in SOURCE_DIRS:
        base = ROOT / directory
        if not base.exists():
            continue
        files.extend(path for path in base.rglob("*") if path.is_file())
    selected = []
    for path in sorted(set(files), key=lambda item: item.relative_to(ROOT).as_posix()):
        rel = path.relative_to(ROOT)
        if any(part in EXCLUDED_PARTS or part.startswith(".env") for part in rel.parts):
            continue
        if path.suffix.lower() in EXCLUDED_SUFFIXES:
            continue
        if path.is_symlink():
            raise SystemExit(f"source release input must not be a symlink: {rel.as_posix()}")
        data = path.read_bytes()
        marker = next((item for item in SECRET_MARKERS if item in data), None)
        if marker is not None:
            raise SystemExit(f"secret-like content in source input: {rel.as_posix()}")
        selected.append(path)
    return selected


def make_source_archive(output: pathlib.Path, version: str) -> tuple[str, list[dict[str, object]]]:
    prefix = f"openconsole-{version}"
    inventory = []
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.PAX_FORMAT) as archive:
        for path in source_files():
            rel = path.relative_to(ROOT).as_posix()
            data = path.read_bytes()
            info = tarfile.TarInfo(f"{prefix}/{rel}")
            info.size = len(data)
            info.mtime = 0
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            info.mode = 0o755 if data.startswith(b"#!") else 0o644
            archive.addfile(info, io.BytesIO(data))
            inventory.append({"path": rel, "bytes": len(data), "sha256": digest(data)})
    with output.open("wb") as stream:
        with gzip.GzipFile(filename="", mode="wb", fileobj=stream, mtime=0) as compressed:
            compressed.write(raw.getvalue())
    return digest(output.read_bytes()), inventory


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", default="0.1.0-ovmf-preview.1")
    parser.add_argument("--image-dir", type=pathlib.Path,
                        default=ROOT / "build/development-image")
    parser.add_argument("--output", type=pathlib.Path,
                        default=ROOT / "build/release-candidate")
    args = parser.parse_args()
    args.output = args.output.resolve()
    release_root = (ROOT / "build").resolve()
    try:
        relative_output = args.output.relative_to(release_root)
    except ValueError as error:
        raise SystemExit("release output must be beneath the project build directory") from error
    if not relative_output.parts:
        raise SystemExit("release output cannot be the build directory itself")
    required = ("openconsole-development.fat", "platform-inventory.ed448.pub",
                "source-inventory.json", "sbom-inputs.cdx.json", "profile.json")
    for name in required:
        if not (args.image_dir / name).is_file():
            raise SystemExit(f"missing canonical image artifact: {name}")
    if args.output.exists():
        shutil.rmtree(args.output)
    args.output.mkdir(parents=True)
    source_name = f"openconsole-{args.version}-source.tar.gz"
    source_sha, sources = make_source_archive(args.output / source_name, args.version)
    artifacts = [{"path": source_name, "sha256": source_sha,
                  "bytes": (args.output / source_name).stat().st_size, "role": "source"}]
    names = {
        "openconsole-development.fat": f"openconsole-{args.version}.fat",
        "platform-inventory.ed448.pub": "platform-inventory.ed448.pub",
        "source-inventory.json": "image-source-inventory.json",
        "sbom-inputs.cdx.json": "image-sbom-inputs.cdx.json",
        "profile.json": "image-profile.json",
    }
    for source, target in names.items():
        shutil.copyfile(args.image_dir / source, args.output / target)
        data = (args.output / target).read_bytes()
        if any(marker in data for marker in SECRET_MARKERS):
            raise SystemExit(f"secret-like content in image artifact: {target}")
        artifacts.append({"path": target, "sha256": digest(data), "bytes": len(data),
                          "role": "development-image" if target.endswith(".fat") else "evidence"})
    manifest = {"schema": "openconsole.release-candidate.v1", "version": args.version,
                "evidenceTier": "disposable-ovmf-development-preview",
                "artifacts": sorted(artifacts, key=lambda item: str(item["path"])),
                "sourceInventory": sources}
    manifest_bytes = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    (args.output / "release-manifest.json").write_bytes(manifest_bytes)
    sums = [(digest(path.read_bytes()), path.name) for path in sorted(args.output.iterdir())
            if path.is_file() and path.name != "SHA256SUMS"]
    (args.output / "SHA256SUMS").write_text(
        "".join(f"{value}  {name}\n" for value, name in sums), encoding="ascii")
    print(json.dumps({"output": str(args.output), "version": args.version,
                      "sourceFiles": len(sources), "artifacts": len(sums)}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
