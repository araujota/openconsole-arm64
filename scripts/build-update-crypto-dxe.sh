#!/usr/bin/env bash
set -euo pipefail

edk_commit=0a503bc2f5bd37c7a5d39d0ba31008f446f0298d
repo_root=$(cd "$(dirname "$0")/.." && pwd)
source_dir=${OPENCONSOLE_EDK2_SOURCE:-"$repo_root/build/vendor-src/edk2-$edk_commit"}
output_dir=${OPENCONSOLE_CRYPTO_OUTPUT:-"$repo_root/build/update-crypto-dxe"}
if [[ ! -d "$source_dir/.git" ]]; then
    echo "Pinned EDK II source is missing; run make edk2-network-stack first." >&2
    exit 2
fi
test "$(git -C "$source_dir" rev-parse HEAD)" = "$edk_commit"
mkdir -p "$source_dir/OpenConsolePkg/OpenConsoleCryptoDxe"
cp "$repo_root/vendor/edk2/OpenConsoleCryptoDxe/"* \
   "$source_dir/OpenConsolePkg/OpenConsoleCryptoDxe/"

for patch_name in 0002-networkpkg-hash2-component.patch; do
    patch="$repo_root/patches/edk2/$patch_name"
    if git -C "$source_dir" apply --ignore-space-change --reverse --check "$patch" 2>/dev/null; then
        : # already applied
    else
        git -C "$source_dir" apply --ignore-space-change --check "$patch"
        git -C "$source_dir" apply --ignore-space-change "$patch"
    fi
done

make -C "$source_dir/BaseTools" -j"${OPENCONSOLE_JOBS:-2}"
mkdir -p "$source_dir/Conf"
cd "$source_dir"
set +u
# shellcheck disable=SC1091
source edksetup.sh
set -u
export SOURCE_DATE_EPOCH
SOURCE_DATE_EPOCH=$(git show -s --format=%ct "$edk_commit")
build -a X64 -t GCC -b RELEASE -p NetworkPkg/NetworkPkg.dsc \
    -m OpenConsolePkg/OpenConsoleCryptoDxe/OpenConsoleCryptoDxe.inf \
    -n "${OPENCONSOLE_JOBS:-2}"
mkdir -p "$output_dir"
install -m 0644 Build/NetworkPkg/RELEASE_GCC/X64/OpenConsoleCryptoDxe.efi \
    "$output_dir/OpenConsoleCryptoDxe.efi"
printf '%s\n' "$edk_commit" > "$output_dir/EDK2_COMMIT"
sha256sum "$output_dir/OpenConsoleCryptoDxe.efi" > "$output_dir/SHA256SUMS"
echo "Packaged OpenConsole CryptoPkg adapter in $output_dir"
