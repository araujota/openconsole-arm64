# UEFI network backend integration

The OpenConsole production network backend uses standard UEFI 2.11 protocols
and the EDK II implementation pinned in `io-sources.lock.toml`.

## Controller selection

The backend enumerates SNP handles and prefers one reporting connected media.
It first checks that SNP handle for DHCP4, DNS4, UDP4, TCP4, HTTP, and TLS
service bindings. EDK II installs these bindings on network child handles, so
the current single-network-controller VM path falls back to the installed
standard binding instances. It does not bind a second device driver. Exact
device-path scoping for multi-NIC systems belongs to the packaged-stack work in
task 3.2 and must be complete before this is treated as physical-hardware ready.

## Cooperative operation mapping

- DHCP4 owns a configured service child and completion event through address
  acquisition, then reads the bound address, mask, and router from mode data.
- DNS4 owns a configured service child and completion token, calls `Poll`,
  bounds returned addresses, and releases both firmware allocations.
- UDP4 owns transmit and receive tokens, copies only a verified complete
  datagram, and signals the receive packet's recycle event exactly once.
- TCP4 owns connect, transmit, and receive phases and gives the firmware only
  the caller-declared response extent.
- HTTPS owns HTTP request and response tokens, rejects non-HTTPS URLs, validates
  response framing against the destination bound, and maps TLS identity failure
  separately from transport failure.

Every child, event, token, and firmware response is cancelled or reclaimed when
the public operation is released or its child generation ends.

## TLS identity boundary

OpenConsole deliberately uses the standard HTTP service binding rather than
reimplementing TLS. In pinned EDK II commit
`0a503bc2f5bd37c7a5d39d0ba31008f446f0298d`,
`NetworkPkg/HttpDxe/HttpsSupport.c` configures `EFI_TLS_VERIFY_PEER` and
`EfiTlsVerifyHost` using the URL endpoint hostname before the HTTPS session.
OpenConsole exposes HTTPS only when both HTTP and TLS service bindings exist on
the selected controller. Signed update metadata remains a separate requirement;
transport success never authorizes executable replacement.

The declarations in `uefi_net_backend.h` reproduce only the standard protocol
layouts needed by the freestanding launcher. Their provenance and
BSD-2-Clause-Patent interface status are recorded in the source policy.
## QEMU virtio-net fixture evidence

`make test-network-fixture-ovmf` boots the production provider with an explicit
`virtio-net-pci` device on QEMU user networking (`10.0.2.0/24`, DHCP start
`10.0.2.15`). The independent `NETWORK-PROBE.EFI` uses only
`OC_NET_PROTOCOL`. It verifies the exact lease, resolves `localhost` to
`127.0.0.1`, exchanges UDP with `10.0.2.2:9000`, sends an HTTP request over the
TCP operation to `10.0.2.2:9001`, exercises response overflow, cancellation,
and deadline expiry, then destroys its session.

The Python harness observes and validates the exact UDP and HTTP request bytes
outside the VM. It launches the child twice and requires two child-complete and
two parent-generation-clean markers before accepting the run. Evidence is in
`build/ovmf-test-network-fixture.log` and
`build/openconsole-network-fixture.png`. This is VM evidence only; it does not
establish physical RTL8125 support.

## Exact RTL8125 fallback candidate

Realtek's official controller download page currently lists UEFI UNDI driver
`2.081` (download ID `3413`, published 2026-10-21) for X86/X64/ARM/loongarch64
and lists the RTL8125 family, including RTL8125B variants. This is a materially
better fallback candidate than substituting an RTL8168/8169 PCI ID or porting a
Linux driver: a conforming UNDI driver should publish standard SNP beneath the
already packaged EDK II network stack.

It is not yet an OpenConsole dependency. The vendor uses a CAPTCHA-mediated
download and does not publish a redistribution license or artifact digest on the
listing. OpenConsole must not bypass that flow or redistribute an unreviewed
binary. If the returned firmware inventory reports zero associated SNP/UNDI
instances, task 7.3 begins with a dedicated change that accepts an explicitly
downloaded local archive, records its hash and terms, verifies the x64 EFI image,
then tests exact revision-05 binding and standard SNP publication. A successful
load still requires independently observed physical packet evidence.

The current `oc_net_stack_load_missing` path cannot perform this bootstrap: it
runs only after `oc_uefi_net_backend_initialize` has already selected an SNP
controller, and its connect callback targets that selected controller. Merely
copying `RtkUndiDxe.efi` beside MNP therefore cannot recover a machine with no
firmware SNP. The dedicated change must add a separate pre-SNP stage that:

1. locates the exact `10ec:8125` revision-05 PCI I/O handle and confirms that no
   associated SNP/UNDI is already present;
2. admits only the reviewed x64 driver digest, then loads and starts that image;
3. connects only the exact PCI handle using the returned driver image handle;
4. verifies that SNP is newly associated with the same device-path ancestry;
5. only then initializes the existing backend and loads the EDK II upper stack.

Failure must unload the candidate driver where safe, leave unrelated NICs
untouched, and preserve offline launcher operation. Driver unload and controller
disconnect behavior require physical review rather than being assumed from a
successful `StartImage` return.

# Packaged upper network stack

OpenConsole can now reproducibly build the pinned EDK II upper network stack
with `make edk2-network-stack` and place it on the ESP with
`make esp-network-stack`. The package contains DPC, MNP, ARP, DHCP4, IP4, UDP4,
TCP, DNS, TLS, and HTTP drivers, but deliberately contains no SNP/UNDI or NIC
hardware driver.

At boot the launcher first discovers an existing SNP controller. It checks the
protocol published by each packaged module in dependency order, skips installed
protocols, and loads only a missing module from
`EFI/OpenConsole/Drivers/Network`. Each newly started driver must publish its
expected protocol after controller connection or loading stops. In particular,
an installed MNP binding is never replaced, so an active firmware SNP path is
not rebound. OVMF reports `OPENCONSOLE_NET_STACK_FIRMWARE_COMPLETE` because its
firmware already supplies the complete graph; host policy tests cover partial
graphs, the MNP-preservation case, and fail-closed publication checks.
