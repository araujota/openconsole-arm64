# Signed Ethernet driver manifest v1

An admitted supplemental Ethernet driver is accompanied by a canonical JSON
manifest with schema `openconsole.ethernet-driver-manifest.v1`. The envelope is
an exact `schema`, `payload`, and `signature` object. The Ed448 signature input
is:

```text
"OPENCONSOLE-ETHERNET-DRIVER-MANIFEST-V1\0" || canonical-json(payload)
```

Canonical JSON uses sorted keys, ASCII escaping, and no insignificant
whitespace. The signature key is a platform driver-admission key, not a game
publisher key.

The payload binds:

- manifest and driver version;
- exactly one of `x64` or `arm64`, including the corresponding PE machine;
- acquisition class and recorded license decision;
- HTTPS provenance and optional upstream archive digest;
- confined EFI path, exact byte length, SHA-256, PE machine, and EFI boot-service
  driver subsystem 11;
- sorted expected NII, UNDI, or SNP protocols;
- deterministic priority and platform ABI range;
- sorted firmware prerequisites;
- one or more exact PCI vendor, device, subsystem vendor, subsystem device,
  revision range, and Ethernet class tuples;
- exact exclusion tuples and a revocation bit.

No wildcard PCI fields are permitted in an admitted manifest. Family names and
marketing names belong in the coverage matrix, not the runtime authority.

## Offline user-acquired installation

Drivers that cannot legally be redistributed are installed before boot with:

```sh
python3 tools/admit_ethernet_driver.py MANIFEST DRIVER.EFI TRUSTED-PUBLIC-KEY SUITCASE-ROOT
```

The command performs no network access. It verifies the manifest signature and
revocation state, requires the `user-acquired` acquisition class, checks the
exact signed byte length and SHA-256 digest, and independently verifies the PE
machine and EFI driver subsystem. Only then does it stage the EFI image and its
signed manifest under `EFI/OpenConsole/Drivers/Network`; it refuses to
overwrite an existing admission.
Architecture, digest, subsystem, signature, exact-device match, prerequisites,
and revocation are all checked before the firmware may call `LoadImage`.

`tools/ethernet_driver_manifest.py sign` creates the envelope and `verify`
checks it against a raw or PEM Ed448 public key. Candidate vendor packages with
unknown digests or device tables stay in the coverage matrix and cannot be
converted into loadable manifests.
