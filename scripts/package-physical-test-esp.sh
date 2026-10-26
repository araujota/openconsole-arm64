#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
esp="$project_root/build/esp"
archive="$project_root/build/openconsole-physical-test-esp.zip"
manifest="$esp/OPENCONSOLE-PAYLOAD-SHA256SUMS"

for log in OPENCONSOLE-INVENTORY.LOG OPENCONSOLE-DRIVER-INVENTORY.LOG \
           OPENCONSOLE-NETWORK-EVIDENCE.LOG OPENCONSOLE-AUDIO-EVIDENCE.LOG; do
    path="$esp/$log"
    [[ -f "$path" && "$(stat -c %s "$path")" == 16384 ]] || {
        echo "FAIL: $log must be a preallocated 16-KiB file" >&2
        exit 1
    }
    [[ -z "$(tr -d '\000' < "$path")" ]] || {
        echo "FAIL: $log contains stale evidence; rebuild physical-test-esp" >&2
        exit 1
    }
done

(
    cd "$esp"
    find . -type f \
        ! -name 'OPENCONSOLE-INVENTORY.LOG' \
        ! -name 'OPENCONSOLE-DRIVER-INVENTORY.LOG' \
        ! -name 'OPENCONSOLE-NETWORK-EVIDENCE.LOG' \
        ! -name 'OPENCONSOLE-AUDIO-EVIDENCE.LOG' \
        ! -name 'OPENCONSOLE-PAYLOAD-SHA256SUMS' \
        -print0 | LC_ALL=C sort -z | xargs -0 sha256sum
) > "$manifest"

"$project_root/scripts/verify-physical-test-media.py" "$esp" \
    --output "$project_root/build/physical-media-preflight.json" >/dev/null

rm -f "$archive"
(
    cd "$esp"
    zip -q -X -r "$archive" .
)
echo "Physical test payload archived at $archive"
sha256sum "$archive"
