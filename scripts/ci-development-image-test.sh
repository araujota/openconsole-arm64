#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

project_root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$project_root"

key="$project_root/build/ci-platform-key.pem"
mkdir -p "$project_root/build"
python3 - "$key" <<'PY'
import pathlib, sys
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PrivateKey
key = Ed448PrivateKey.from_private_bytes(bytes(range(1, 58)))
pathlib.Path(sys.argv[1]).write_bytes(key.private_bytes(
    serialization.Encoding.PEM,
    serialization.PrivateFormat.PKCS8,
    serialization.NoEncryption()))
PY

make build/INPUT-PROBE.EFI
python3 tests/make_authenticated_game_fixture.py
python3 tests/prepare_authenticated_installed_game.py
make development-image PLATFORM_KEY="$key"

common=(
  --platform-key "$key"
  --parent "$project_root/build/BOOTX64-DEVELOPMENT.EFI"
  --publisher-key-store "$project_root/build/authenticated-game-fixture/publishers.ocpk"
  --installed-games "$project_root/build/authenticated-installed-games"
)
python3 scripts/build-development-image.py "${common[@]}" --output "$project_root/build/ci-image-a"
python3 scripts/build-development-image.py "${common[@]}" --output "$project_root/build/ci-image-b"
python3 scripts/compare-development-images.py "$project_root/build/ci-image-a" "$project_root/build/ci-image-b"

recovery_esp="$project_root/build/ci-recovery-esp"
rm -rf -- "$recovery_esp"
mkdir -p "$recovery_esp"
mcopy -s -i "$project_root/build/ci-image-a/openconsole-development.fat" '::*' "$recovery_esp/"
python3 tests/prepare_incomplete_launch_record.py "$recovery_esp"
OPENCONSOLE_TEST_ESP=ci-recovery-esp OPENCONSOLE_TEST_SCENE=recovery \
  OPENCONSOLE_TEST_NETWORK=none ./scripts/test-ovmf.sh
