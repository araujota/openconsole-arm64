#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

project_root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
candidate="${1:-$project_root/build/release-candidate}"
python3 "$project_root/scripts/verify-release-candidate.py" "$candidate"

work="$(mktemp -d "$project_root/build/release-verify.XXXXXX")"
trap 'case "$work" in "$project_root"/build/release-verify.*) rm -rf -- "$work" ;; esac' EXIT
archive="$(find "$candidate" -maxdepth 1 -type f -name 'openconsole-*-source.tar.gz' -print -quit)"
test -n "$archive"
tar -xzf "$archive" -C "$work"
source_root="$(find "$work" -mindepth 1 -maxdepth 1 -type d -print -quit)"
test -n "$source_root"
make -C "$source_root" test-host check-examples check-efi-core
python3 "$source_root/scripts/generate-release-metadata.py"
cmp "$source_root/THIRD_PARTY_NOTICES.md" "$project_root/THIRD_PARTY_NOTICES.md"
cmp "$source_root/dependency-provenance.json" "$project_root/dependency-provenance.json"
cmp "$source_root/sbom.cdx.json" "$project_root/sbom.cdx.json"
printf '%s\n' 'PASS: release source verified in a fresh extracted tree'
