<!-- SPDX-License-Identifier: Apache-2.0 -->
# Transactional game-storage fault matrix

OpenConsole treats `select-a.ocgs` and `select-b.ocgs` as the only commit
points. A staged slot is not visible until a complete, CRC-valid selector names
it. Deletion similarly commits a tombstone before bounded cleanup begins.

`tests/game_transaction_test.c` injects each fallible operation by ordinal. It
covers full and short writes, flushes, reopen/reads, digest verification,
post-write validation, selector writes, tombstone writes, deletion failures,
corrupt selected content, and bounded recovery cleanup. After every injected
failure it removes the fault and verifies recovery chooses either the previous
known-good generation or the fully verified new generation—never a partial
slot.

`tests/prepare_storage_recovery_matrix.py` builds these persistent states and
`scripts/test-game-storage-ovmf-matrix.sh` boots each one from an actual FAT
image under OVMF:

| OVMF case | Persisted interruption | Required result |
| --- | --- | --- |
| `staging-write` | staging marker only | generation 1 launches; partial slot removed |
| `bundle-write` | retained bundle, no descriptor | generation 1 launches; partial slot removed |
| `descriptor-write` | descriptor present, no package | generation 1 launches; partial slot removed |
| `package-short-write` | truncated extracted file | generation 1 launches; corrupt slot removed |
| `package-write` | package complete, selector absent | generation 1 launches; uncommitted slot removed |
| `selector-short-write` | torn selector record | generation 1 launches; invalid selector/slot removed |
| `selector-verify-corrupt` | selector CRC corruption | generation 1 launches; invalid selector/slot removed |
| `selector-commit` | complete generation-2 selector | generation 2 verifies and launches |
| `delete-tombstone` | committed tombstone before cleanup | no game is visible; cleanup cannot resurrect it |

Run the host and firmware evidence with:

```sh
make build/game-transaction-test
./build/game-transaction-test
make test-game-storage-ovmf-matrix
```

The OVMF harness asserts the selected generation, visible-game count, cleanup
count, tombstone state, authenticated transaction verification, and successful
child launch/return for every non-deleted case.
