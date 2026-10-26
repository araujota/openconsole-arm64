#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

project_root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$project_root"
base="$project_root/build/storage-recovery-base"
matrix="$project_root/build/storage-recovery-matrix"
images="$project_root/build/storage-recovery-images"
rm -rf -- "$base" "$matrix" "$images"
mkdir -p "$base"
mkdir -p "$images"
mcopy -s -i "$project_root/build/ci-image-a/openconsole-development.fat" '::*' "$base/"
python3 tests/prepare_storage_recovery_matrix.py "$base" "$matrix"

cases=(staging-write bundle-write descriptor-write package-short-write package-write selector-short-write selector-verify-corrupt selector-commit delete-tombstone)
for case_name in "${cases[@]}"; do
  image="$images/$case_name.fat"
  truncate -s 64M "$image"
  mkfs.vfat -n "OCFAULT" "$image" >/dev/null
  mcopy -s -i "$image" "$matrix/$case_name"/* '::/'
  OPENCONSOLE_TEST_ESP="storage-recovery-images/$case_name.fat" \
    OPENCONSOLE_TEST_SCENE=storage-recovery \
    OPENCONSOLE_STORAGE_CASE="$case_name" \
    OPENCONSOLE_TEST_NETWORK=none \
    ./scripts/test-ovmf.sh
done
printf 'PASS: %s OVMF persistent storage interruption states\n' "${#cases[@]}"
