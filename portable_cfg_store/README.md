# Portable Configuration Store

[中文说明](README_zh-CN.md)

A small C99 storage core for configuration payloads that must survive interrupted erase/program operations. It stores opaque bytes in two fixed slots and commits the new slot last, so recovery yields the previous valid payload or a complete new payload under the documented storage contract.

## Why this exists

CRC detects accidental corruption; it does **not** make an in-place update atomic. If power disappears while a single copy is erased or rewritten, that copy may be unusable. This module keeps the previous slot untouched while writing the inactive slot, verifies the new content, then writes a final commit record.

```text
slot A: previous committed payload
slot B: erase -> header -> payload -> verify -> COMMIT LAST
```

A reboot scans both slots and selects the newest fully valid sequence.

## Properties

- C99, no heap allocation in the core
- no RTOS, filesystem, or JSON dependency
- fixed little-endian media format
- CRC-32/ISO-HDLC for header, payload, and commit metadata
- A/B slot rotation with wrap-safe 32-bit sequences
- optional semantic payload validator
- read-only downgrade protection when a newer unknown format is present
- explicit factory reset as another atomic commit
- host NOR emulator and interruption-injection tests
- optional cJSON and RT-Thread adapters (separate from the core)

## Five-minute build

```bash
cmake -S . -B build -DPCFG_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/pcfg_posix_memory_example
```

Expected example output:

```text
loaded: brightness=42
sequence: 1
loaded default: brightness=10
```

## Minimal integration

```c
pcfg_storage_ops_t ops = {
    .read = board_flash_read,
    .program = board_flash_program,
    .erase = board_flash_erase,
    .sync = board_flash_sync,
};

pcfg_config_t cfg = {
    .slot_address = { SLOT_A_ADDRESS, SLOT_B_ADDRESS },
    .slot_size = 4096,
    .erase_unit = 4096,
    .program_unit = 4,
    .program_page_size = 256,
    .io_chunk_size = 256,
    .format_magic = 0x50434631u,
};

static uint32_t scratch_words[1024];
pcfg_store_t store;

pcfg_status_t status = pcfg_init(&store, &cfg, &ops, board_context,
                                 scratch_words, sizeof(scratch_words));
```

Call `pcfg_load()` once after initialization. `PCFG_NOT_FOUND` means the medium is blank and the first save is permitted. Hold one application mutex around every complete `load`, `save`, or `factory_reset` call.

A failed save after erase/program/sync begins is ambiguous: the medium can contain the old committed value or the complete new committed value. Do **not** retry immediately. Call `pcfg_load()` to rescan first.

## Scratch buffer

The core never allocates. Save uses scratch in chunks. Load keeps a candidate payload in scratch before semantic validation and before touching the caller output, so scratch must hold the largest payload you want to load. This preserves the guarantee that failed loads leave the output buffer unchanged.

## Good fits

- Wi-Fi/Bluetooth/user preferences
- device feature flags and thresholds
- infrequently changed algorithm settings
- factory defaults and settings import/export

## Poor fits

- high-frequency counters or logs
- large images, models, or calibration matrices
- secret storage without an encryption/authentication layer
- very small MCUs where JSON/tree parsing is too expensive

The byte-storage core is codec-neutral: JSON, CBOR, TLV, or a versioned binary structure can all be stored as payloads.

## Security boundary

CRC detects accidental corruption only. It does not provide confidentiality, authenticity, anti-rollback, or protection from a malicious writer. Wi-Fi credentials stored in JSON remain plaintext unless the application adds real authenticated encryption and key management. See [docs/security.md](docs/security.md).

## Porting and media format

- [Porting guide](docs/porting.md)
- [Exact v1 media format](docs/storage-format.md)
- [Security model](docs/security.md)

## License

MIT. This repository is an original, product-neutral implementation; it intentionally contains no proprietary product source, memory map, credentials, or schema.
