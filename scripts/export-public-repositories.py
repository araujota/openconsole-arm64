#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Materialize deterministic, secret-screened public repository trees."""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
RUNTIME_DIRS = (".github", "config", "docs", "examples", "include", "patches", "research", "scripts", "src", "tests", "tools", "vendor")
RUNTIME_FILES = (".gitignore", "CODE_OF_CONDUCT.md", "CONTRIBUTING.md", "dependency-provenance.json", "LICENSE", "Makefile", "NOTICE", "README.md", "release-version.json", "requirements-analyzer.txt", "sbom.cdx.json", "SECURITY.md", "THIRD_PARTY_NOTICES.md")
CATALOG_FILES = ("CODE_OF_CONDUCT.md", "CONTRIBUTING.md", "LICENSE", "NOTICE", "SECURITY.md", "release-version.json")
SKIP_PARTS = {".git", ".next", ".vercel", ".convex", ".quickstart-logs", "node_modules", "test-results", "__pycache__", ".agents", ".claude"}
SKIP_SUFFIXES = {".pem", ".key", ".pyc", ".pyo"}
SECRET_MARKERS = (b"-----BEGIN " + b"PRIVATE KEY-----", b"-----BEGIN ENCRYPTED " + b"PRIVATE KEY-----", b"CONVEX_DEPLOY_KEY" + b"=", b"VERCEL_TOKEN" + b"=", b"VERCEL_OIDC_TOKEN" + b"=")
PRIVATE_PATTERNS = ("docs/public-release-*",)


def candidates(kind: str) -> list[tuple[pathlib.Path, pathlib.PurePosixPath]]:
    selected: list[tuple[pathlib.Path, pathlib.PurePosixPath]] = []
    if kind in ("runtime", "runtime-arm64"):
        roots = [ROOT / name for name in RUNTIME_DIRS]
        files = [ROOT / name for name in RUNTIME_FILES]
        for base in roots:
            if base.exists():
                files.extend(path for path in base.rglob("*") if path.is_file() or path.is_symlink())
        for path in files:
            selected.append((path, pathlib.PurePosixPath(path.relative_to(ROOT).as_posix())))
    else:
        web = ROOT / "web"
        files = [path for path in web.rglob("*") if path.is_file() or path.is_symlink()]
        files.extend(ROOT / name for name in CATALOG_FILES)
        for path in files:
            rel = path.relative_to(web) if path.is_relative_to(web) else path.relative_to(ROOT)
            selected.append((path, pathlib.PurePosixPath(rel.as_posix())))
    overlay_name = "arm64" if kind == "runtime-arm64" else kind
    overlay = ROOT / "public-repos" / overlay_name
    if overlay.exists():
        for path in overlay.rglob("*"):
            if path.is_file() or path.is_symlink():
                selected.append((path, pathlib.PurePosixPath(path.relative_to(overlay).as_posix())))
    by_path: dict[str, tuple[pathlib.Path, pathlib.PurePosixPath]] = {}
    for item in selected:
        by_path[item[1].as_posix()] = item
    return sorted(by_path.values(), key=lambda item: item[1].as_posix())


def checked_files(kind: str) -> list[tuple[pathlib.Path, pathlib.PurePosixPath, bytes]]:
    output = []
    seen: set[str] = set()
    for source, rel in candidates(kind):
        if any(rel.match(pattern) for pattern in PRIVATE_PATTERNS):
            continue
        if any(part in SKIP_PARTS or part.startswith(".env") for part in rel.parts):
            continue
        if source.suffix.lower() in SKIP_SUFFIXES:
            continue
        if source.is_symlink():
            raise ValueError(f"symlink rejected: {rel}")
        if not source.is_file():
            raise ValueError(f"required public input missing: {source}")
        key = rel.as_posix().casefold()
        if key in seen:
            raise ValueError(f"case-fold collision: {rel}")
        seen.add(key)
        data = source.read_bytes()
        if any(marker in data for marker in SECRET_MARKERS):
            raise ValueError(f"secret marker rejected: {rel}")
        output.append((source, rel, data))
    return output


def export(kind: str, destination: pathlib.Path) -> dict[str, object]:
    destination = destination.resolve()
    build_root = (ROOT / "build").resolve()
    try:
        relative = destination.relative_to(build_root)
    except ValueError as error:
        raise ValueError("public export destination must be beneath build/") from error
    if not relative.parts:
        raise ValueError("public export destination cannot be build/")
    files = checked_files(kind)
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    inventory = []
    for _, rel, data in files:
        target = destination / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        if data.startswith(b"#!"):
            target.chmod(0o755)
        inventory.append({"path": rel.as_posix(), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    revision = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    contract_paths = (
        "include/openconsole/target.h", "include/openconsole/uefi.h",
        "include/openconsole/ocg2.h", "include/openconsole/manifest.h",
        "include/openconsole/gles2_protocol.h", "tools/ocg2.py",
    )
    contract_hashes = {
        path: hashlib.sha256((destination / path).read_bytes()).hexdigest()
        for path in contract_paths if (destination / path).is_file()
    }
    manifest = {"schema": "openconsole.public-tree.v2", "kind": kind,
                "upstreamRevision": revision, "sharedContractHashes": contract_hashes,
                "files": inventory}
    encoded = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    (destination / "PUBLIC-SOURCE-INVENTORY.json").write_bytes(encoded)
    return {"kind": kind, "files": len(files), "inventorySha256": hashlib.sha256(encoded).hexdigest(), "output": str(destination)}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=("runtime", "runtime-arm64", "catalog"))
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    print(json.dumps(export(args.kind, args.output), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
