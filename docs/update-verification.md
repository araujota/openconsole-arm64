# Update verification boundary

`oc_update_verify` is the freestanding, fail-closed policy composition layer. It
requires all of the following before returning an accepted policy state:

1. the HTTPS backend reports both peer-chain and hostname verification;
2. the verified hostname exactly equals the requested update hostname;
3. the metadata key ID resolves to a trusted public key;
4. Ed448 verifies the canonical `OCU1-SIGNED` byte string;
5. expiration, sequence, version, key-rotation, and artifact-size policy passes;
6. the downloaded artifact has the exact declared length and SHA-256 digest.

No candidate policy state is copied to the caller on a later size or digest
failure. Host tests use OpenSSL's real Ed448 implementation and cover valid,
expired, wrong-host, untrusted-key, bad-signature, truncated-metadata, wrong-size,
and corrupt-artifact cases.

The pinned EDK II mapping has been source-audited: HttpDxe configures
`EFI_TLS_VERIFY_PEER` and `EfiTlsVerifyHost`, while CryptoPkg provides
`EdDsaVerify` for Ed448. `OpenConsoleCryptoDxe.efi` links that API to a narrow,
versioned OpenConsole protocol containing only SHA-256 and Ed448 verification.
The module has an explicit `OpensslLibFull.inf` override because EDK II's default
NetworkPkg `OpensslLib.inf` disables ECX, which makes Ed448 raw-key import fail.
The override is local to this module and does not enlarge the other network
drivers.

The OVMF `crypto` scene proves that the parent loads the packaged driver and an
independent child, using only the public protocol, passes the SHA-256 `abc`
known answer, accepts a host-generated Ed448 signature, and rejects a one-bit
signature corruption. The same child now uses the freestanding
`oc_uefi_update_crypto` adapter to run a complete signed metadata/artifact
candidate through `oc_update_verify`. In OVMF it accepts the valid candidate and
rejects expired, wrong-host, wrong-key, rollback, truncated, bad-signature, and
corrupt-artifact variants. The current packaged image is 1,163,264 bytes with SHA-256
`b711d78c4a81856bc16d0ba955e85bb8e702a545a41b55c61812c645675a3cea`.

For HTTPS fixtures, the parent optionally reads
`\EFI\OpenConsole\Trust\ca.der`, bounds it to 8 KiB, wraps it in the canonical
X.509 `EFI_SIGNATURE_LIST` representation, and installs the
`TlsCaCertificate` UEFI variable before initializing the network provider. No
CA file leaves the existing firmware trust state untouched. Host sanitizer tests
cover exact signature-list sizing and bounded input.

The deterministic OVMF `https` scene installs that CA and exercises the packaged
EDK II TLS 1.2/HTTP stack against three isolated endpoints. A certificate with an
IP subjectAltName for `10.0.2.2` completes the handshake and returns the exact
expected body. An expired certificate and a certificate for a different host are
both rejected before either endpoint receives an HTTP request. The locally
isolated `0003-tls-skip-sni-for-ip-literals.patch` omits invalid SNI for an IP
literal while retaining OpenSSL IP-address verification; the
`0004-networkpkg-http-timeout-fixed.patch` makes the standalone NetworkPkg HTTP
timeout a fixed PCD so the packaged `HttpDxe` can read it safely.

The `update-e2e` OVMF scene composes these boundaries. It downloads a generated
catalog, signed metadata, and the actual parent EFI over three independently
verified TLS connections; binds each fetched URL to the authenticated record;
verifies Ed448, policy, exact size, and SHA-256; and commits the artifact through
the transactional FAT storage layer. The HTTPS adapter issues continuation
responses with only `Body` and `BodyLength`, as required by the UEFI HTTP
protocol, and caps reads to one 16-KiB TLS application record to avoid treating
unwritten capacity as received data in the pinned HttpDxe implementation.

The same scene rejects bad TLS identity, wrong host/key, expiration, rollback,
truncation, signature corruption, and artifact corruption. The host harness
hashes `EFI/BOOT/BOOTX64.EFI` before and after the complete matrix and requires
byte-for-byte equality. Together with the independent HTTPS identity and crypto
scenes, this completes tasks 4.2 and 4.4.
