#!/usr/bin/env bash
set -euo pipefail

edk_commit=0a503bc2f5bd37c7a5d39d0ba31008f446f0298d
edk_url=https://github.com/tianocore/edk2.git
repo_root=$(cd "$(dirname "$0")/.." && pwd)
managed_source=0
if [[ -z "${OPENCONSOLE_EDK2_SOURCE:-}" ]]; then managed_source=1; fi
source_dir=${OPENCONSOLE_EDK2_SOURCE:-"$repo_root/build/vendor-src/edk2-$edk_commit"}
output_dir=${OPENCONSOLE_EDK2_OUTPUT:-"$repo_root/build/edk2-network-stack"}
if [[ "$source_dir" != /* ]]; then source_dir="$repo_root/$source_dir"; fi
if [[ "$output_dir" != /* ]]; then output_dir="$repo_root/$output_dir"; fi

if [[ ! -d "$source_dir/.git" ]]; then
    mkdir -p "$(dirname "$source_dir")"
    git clone --filter=blob:none --no-checkout "$edk_url" "$source_dir"
    git -C "$source_dir" sparse-checkout init --cone
    git -C "$source_dir" sparse-checkout set BaseTools CryptoPkg MdePkg MdeModulePkg NetworkPkg SecurityPkg ShellPkg
fi
git -C "$source_dir" fetch --depth 1 origin "$edk_commit"
git -C "$source_dir" checkout --detach "$edk_commit"
test "$(git -C "$source_dir" rev-parse HEAD)" = "$edk_commit"
# The default checkout is a generated build cache shared by several EDK II
# builders, each of which applies a different pinned patch set. Restore only
# this managed cache before applying our patches so prior builds cannot make a
# later build order-dependent. Never overwrite a caller-supplied source tree.
if [[ "$managed_source" = 1 ]]; then
    git -C "$source_dir" restore --source="$edk_commit" --staged --worktree -- .
elif ! git -C "$source_dir" diff --quiet ||
     ! git -C "$source_dir" diff --cached --quiet; then
    echo "Refusing to modify dirty OPENCONSOLE_EDK2_SOURCE: $source_dir" >&2
    exit 2
fi
git -C "$source_dir" sparse-checkout add ShellPkg
mkdir -p "$source_dir/OpenConsolePkg/OpenConsoleCryptoDxe"
cp "$repo_root/vendor/edk2/OpenConsoleCryptoDxe/"* \
   "$source_dir/OpenConsolePkg/OpenConsoleCryptoDxe/"

for patch_name in \
    0002-networkpkg-hash2-component.patch \
    0003-tls-skip-sni-for-ip-literals.patch \
    0004-networkpkg-http-timeout-fixed.patch; do
    patch="$repo_root/patches/edk2/$patch_name"
    if git -C "$source_dir" apply --ignore-space-change --reverse --check "$patch" 2>/dev/null; then
        : # already applied
    else
        git -C "$source_dir" apply --ignore-space-change --check "$patch"
        git -C "$source_dir" apply --ignore-space-change "$patch"
    fi
done

submodules=(
    BaseTools/Source/C/BrotliCompress/brotli
    MdeModulePkg/Library/BrotliCustomDecompressLib/brotli
    CryptoPkg/Library/OpensslLib/openssl
    CryptoPkg/Library/MbedTlsLib/mbedtls
    MdePkg/Library/MipiSysTLib/mipisyst
    MdePkg/Library/BaseFdtLib/libfdt
)
git -C "$source_dir" submodule update --init --depth 1 -- "${submodules[@]}"

make -C "$source_dir/BaseTools" -j"${OPENCONSOLE_JOBS:-2}"
mkdir -p "$source_dir/Conf"
cd "$source_dir"
# shellcheck disable=SC1091
set +u
source edksetup.sh
set -u
export SOURCE_DATE_EPOCH
SOURCE_DATE_EPOCH=$(git show -s --format=%ct "$edk_commit")
modules=(DpcDxe MnpDxe ArpDxe Dhcp4Dxe Ip4Dxe Udp4Dxe TcpDxe DnsDxe TlsDxe HttpUtilitiesDxe HttpDxe SnpDxe)

# Ubuntu installs the LLVM tools with versioned names. Give EDK II's
# CLANGDWARF profile a private, generated bin directory with the conventional
# names instead of modifying the host or requiring an AArch64 GCC toolchain.
llvm_bin="$output_dir/.llvm-bin"
mkdir -p "$llvm_bin"
find_tool() {
    local name=$1 candidate
    if candidate=$(command -v "$name" 2>/dev/null); then
        printf '%s\n' "$candidate"
        return
    fi
    for suffix in 20 19 18 17 16; do
        if candidate=$(command -v "$name-$suffix" 2>/dev/null); then
            printf '%s\n' "$candidate"
            return
        fi
    done
    echo "Missing LLVM tool: $name" >&2
    return 1
}
ln -sfn "$(find_tool clang)" "$llvm_bin/clang"
ln -sfn "$(find_tool llvm-ar)" "$llvm_bin/llvm-ar"
ln -sfn "$(find_tool llvm-objcopy)" "$llvm_bin/llvm-objcopy"

for module in "${modules[@]}"; do
    inf=$(find NetworkPkg -mindepth 2 -maxdepth 2 -type f -name "$module.inf" -print -quit)
    test -n "$inf"
    build -a X64 -t GCC -b RELEASE -p NetworkPkg/NetworkPkg.dsc -m "$inf" -n "${OPENCONSOLE_JOBS:-2}"
    CLANGDWARF_BIN="$llvm_bin/" build -a AARCH64 -t CLANGDWARF -b RELEASE \
        -p NetworkPkg/NetworkPkg.dsc -m "$inf" -n "${OPENCONSOLE_JOBS:-2}"
done
build -a X64 -t GCC -b RELEASE -p NetworkPkg/NetworkPkg.dsc \
    -m SecurityPkg/Hash2DxeCrypto/Hash2DxeCrypto.inf -n "${OPENCONSOLE_JOBS:-2}"
CLANGDWARF_BIN="$llvm_bin/" build -a AARCH64 -t CLANGDWARF -b RELEASE \
    -p NetworkPkg/NetworkPkg.dsc \
    -m SecurityPkg/Hash2DxeCrypto/Hash2DxeCrypto.inf -n "${OPENCONSOLE_JOBS:-2}"

mkdir -p "$output_dir/AARCH64"
for module in "${modules[@]}"; do
    install -m 0644 "Build/NetworkPkg/RELEASE_GCC/X64/$module.efi" "$output_dir/$module.efi"
    install -m 0644 "Build/NetworkPkg/RELEASE_CLANGDWARF/AARCH64/$module.efi" \
        "$output_dir/AARCH64/$module.efi"
done
install -m 0644 "Build/NetworkPkg/RELEASE_GCC/X64/Hash2DxeCrypto.efi" \
    "$output_dir/Hash2DxeCrypto.efi"
install -m 0644 "Build/NetworkPkg/RELEASE_CLANGDWARF/AARCH64/Hash2DxeCrypto.efi" \
    "$output_dir/AARCH64/Hash2DxeCrypto.efi"
for module in "${modules[@]}" Hash2DxeCrypto; do
    python3 "$repo_root/tools/verify_efi.py" "$output_dir/$module.efi" \
        --arch x64 --role driver --require-zero-timestamp >/dev/null
    python3 "$repo_root/tools/verify_efi.py" "$output_dir/AARCH64/$module.efi" \
        --arch arm64 --role driver --require-zero-timestamp >/dev/null
done
(
    cd "$output_dir"
    find . -type f -name '*.efi' -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
)
printf '%s\n' "$edk_commit" > "$output_dir/EDK2_COMMIT"
printf '%s\n' 'x64:GCC' 'arm64:CLANGDWARF' > "$output_dir/ARCHITECTURES"
python3 "$repo_root/tools/verify_ethernet_size_budget.py" \
    --config "$repo_root/config/ethernet-release-size-budget.json" \
    --x64 "$output_dir" --arm64 "$output_dir/AARCH64" \
    --output "$output_dir/SIZE-BUDGET.json" >/dev/null
echo "Packaged pinned x64 and ARM64 EDK II network stack in $output_dir"
