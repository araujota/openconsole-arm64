#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$root"
fixture="$root/build/split-volume-fixtures"
rm -rf -- "$fixture"
mkdir -p "$fixture/data/EFI/OpenConsole" "$fixture/empty-a/EFI/OpenConsole" "$fixture/empty-b/EFI/OpenConsole"
marker='{"installationId":"01234567-89ab-4def-8123-456789abcdef","schema":"openconsole.data-volume.v1"}'
printf '%s\n' "$marker" | tee "$fixture/data/EFI/OpenConsole/data-volume.json" "$fixture/empty-a/EFI/OpenConsole/data-volume.json" "$fixture/empty-b/EFI/OpenConsole/data-volume.json" >/dev/null

make game-details-test-esp test-esp ovmf-pointer
cp -a build/game-details-test-esp/games "$fixture/data/games"
OPENCONSOLE_OVMF_CODE="$root/build/ovmf-pointer/OVMF_CODE.fd" \
OPENCONSOLE_OVMF_VARS="$root/build/ovmf-pointer/OVMF_VARS.fd" \
OPENCONSOLE_POINTER_DISPLAY=none OPENCONSOLE_TEST_SCENE=game-details \
OPENCONSOLE_TEST_PACKAGED_NETWORK=1 OPENCONSOLE_TEST_ESP=game-details-test-esp \
OPENCONSOLE_TEST_EXTRA_DRIVES="$fixture/data" \
OPENCONSOLE_EXPECT_DATA_VOLUME=OPENCONSOLE_DATA_VOLUME_SELECTED ./scripts/test-ovmf.sh

OPENCONSOLE_TEST_ESP=test-esp OPENCONSOLE_EXPECT_DATA_VOLUME=OPENCONSOLE_DATA_VOLUME_BOOT_FALLBACK ./scripts/test-ovmf.sh
OPENCONSOLE_TEST_ESP=test-esp OPENCONSOLE_TEST_EXTRA_DRIVES="$fixture/empty-a:$fixture/empty-b" \
OPENCONSOLE_EXPECT_DATA_VOLUME=OPENCONSOLE_DATA_VOLUME_AMBIGUOUS_MUTATIONS_DISABLED ./scripts/test-ovmf.sh

make arm64-image
arm_data="$fixture/arm-data.fat"
truncate -s 64M "$arm_data"
mkfs.vfat -n OC_DATA "$arm_data" >/dev/null
mmd -i "$arm_data" ::/EFI ::/EFI/OpenConsole ::/games
printf '%s\n' "$marker" > "$fixture/data-volume.json"
mcopy -i "$arm_data" "$fixture/data-volume.json" ::/EFI/OpenConsole/data-volume.json
mkdir "$fixture/arm-games"
mcopy -s -i build/arm64/openconsole-arm64-development.fat '::/games/*' "$fixture/arm-games/"
mcopy -s -i "$arm_data" "$fixture/arm-games"/* ::/games/
python3 scripts/test-arm64-aavmf.py --data-image "$arm_data" \
  --expect-data-volume OPENCONSOLE_DATA_VOLUME_SELECTED
printf 'PASS: x64 OVMF and ARM64 AAVMF split/fallback/ambiguity/launch/delete routing\n'
