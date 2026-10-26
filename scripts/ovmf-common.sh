#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ovmf_code="${OPENCONSOLE_OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}"
ovmf_vars_template="${OPENCONSOLE_OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}"

if [[ ! -r "$ovmf_code" || ! -r "$ovmf_vars_template" ]]; then
    echo "OVMF firmware not found. Set OPENCONSOLE_OVMF_CODE and OPENCONSOLE_OVMF_VARS." >&2
    exit 2
fi

prepare_vars() {
    local destination="$1"
    mkdir -p -- "$(dirname -- "$destination")"
    cp -- "$ovmf_vars_template" "$destination"
}

