#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

project_root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$project_root"
key="$project_root/build/ci-platform-key.pem"
root="$project_root/build/runtime-integrity"
rm -rf -- "$root"
mkdir -p "$root"

python3 tests/prepare_runtime_integrity_fixtures.py "$root/hang-fixture"

common=(
  --platform-key "$key"
  --publisher-key-store "$project_root/build/authenticated-game-fixture/publishers.ocpk"
  --installed-games "$project_root/build/authenticated-installed-games"
)
python3 scripts/build-development-image.py "${common[@]}" \
  --parent "$project_root/build/BOOTX64-DEVELOPMENT-MUTATION-TEST.EFI" \
  --output "$root/mutation"
python3 scripts/build-development-image.py "${common[@]}" \
  --parent "$project_root/build/BOOTX64-DEVELOPMENT-WATCHDOG-TEST.EFI" \
  --output "$root/watchdog"
python3 scripts/build-development-image.py \
  --platform-key "$key" \
  --publisher-key-store "$root/hang-fixture/fixture/publishers.ocpk" \
  --installed-games "$root/hang-fixture/installed" \
  --parent "$project_root/build/BOOTX64-DEVELOPMENT.EFI" \
  --output "$root/hang-reset"

cp -- "$project_root/build/ci-image-a/openconsole-development.fat" \
  "$root/corrupt-driver.fat"
driver="$root/OpenConsoleCryptoDxe.corrupt.efi"
mcopy -i "$root/corrupt-driver.fat" \
  '::/EFI/OpenConsole/Drivers/Audio/AudioDxe.efi' "$driver"
python3 - "$driver" <<'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
data = bytearray(path.read_bytes())
if not data:
    raise SystemExit("audio driver is empty")
data[len(data) // 2] ^= 0x80
path.write_bytes(data)
PY
mcopy -o -i "$root/corrupt-driver.fat" "$driver" \
  '::/EFI/OpenConsole/Drivers/Audio/AudioDxe.efi'

run_case() {
  local scene="$1"
  local image="$2"
  OPENCONSOLE_TEST_SCENE="$scene" \
    OPENCONSOLE_TEST_ESP="runtime-integrity/$image" \
    OPENCONSOLE_TEST_NETWORK=none \
    ./scripts/test-ovmf.sh
}

run_case mutation-after-discovery mutation/openconsole-development.fat
run_case unsupported-watchdog watchdog/openconsole-development.fat
run_case corrupt-driver corrupt-driver.fat
run_case hang-reset hang-reset/openconsole-development.fat
printf 'PASS: mutation, corrupt-driver, unsupported-watchdog, and hang/reset OVMF cases\n'
