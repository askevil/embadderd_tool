# Portable Configuration Store — Design

Date: 2026-11-18
Status: Approved for implementation
License: MIT
Language: C99
Target repository: `https://github.com/askevil/embadderd_tool`
Target module directory: `portable_cfg_store/`

## 1. Intent

Build a reusable embedded configuration-storage module that combines a human-readable JSON configuration layer with a power-loss-safe A/B storage core. The core is portable across RTOS, bare-metal, and POSIX environments and has no dependency on an RTOS, filesystem, dynamic allocation, or JSON library.

The public repository is a clean-room implementation. It contains no copied proprietary source, product-specific addresses, product names, configuration keys, credentials, or memory layouts. RS300/RS500 are design references only.

## 2. Goals

1. Preserve the last successfully committed payload across interruption at any erase/program/sync step.
2. Detect incomplete writes and accidental corruption with explicit metadata, CRC32, and a final commit record.
3. Select the newest valid slot with wrap-safe 32-bit sequence comparison.
4. Keep the storage core C99-only, heap-free, and independent of any RTOS or device driver.
5. Support caller-owned buffers and configurable slot geometry.
6. Allow semantic payload validation and fallback from a valid-but-invalid-newest payload to the older valid payload.
7. Provide an optional external-cJSON adapter with dotted object-path helpers and embedded-default fallback.
8. Provide a POSIX flash emulator with interruption and corruption injection tests.
9. Provide an RT-Thread `rt_device` NOR adapter example without hard-coded device names or addresses.
10. Document integration, storage format, guarantees, limits, and security boundaries in English and Chinese.

## 3. Non-goals

- General-purpose filesystem, KV database, or wear-leveling filesystem.
- High-frequency counters, logs, telemetry, images, calibration arrays, or firmware images.
- Encryption, authentication, key management, or secure secret storage.
- Compatibility with any proprietary on-device format.
- Bundling a private or product-specific configuration schema.
- Guaranteeing atomicity on storage hardware that violates the declared storage contract.

## 4. Approaches considered and decision

### A. RT-Thread + cJSON implementation only

Fastest to deliver, but binds the module to one RTOS, one JSON library, and one device model. Rejected because the objective is cross-project reuse.

### B. Portable opaque-payload core + platform adapters + optional cJSON adapter

**Selected.** The storage transaction machinery handles bytes only. Storage, locking, JSON parsing, and product schema validation live behind narrow interfaces. This preserves the useful A/B design while allowing JSON, CBOR, TLV, or binary payloads.

### C. Reproduce a legacy multi-file JSON configuration service

Rejected because it would bring filesystem assumptions and product-specific complexity into the core. A legacy importer can be added later as a separate tool if a public format specification is available.

## 5. Fixed product decisions

These decisions are part of the v1 contract:

1. **Save interruption:** recovery guarantees `old valid payload OR complete new valid payload`, never a partially committed payload. If `save()` returns an error after commit or sync may have become durable, callers must call `load()` again before deciding whether a retry is needed. An error return does not prove that the new payload is absent.
2. **Factory reset:** reset is a normal A/B commit of the supplied default payload. It does not erase both slots. Therefore interruption recovers either the old payload or the complete default payload.
3. **Unknown newer format:** if a supported valid slot and an unknown newer-format slot coexist, load the supported slot but mark the store read-only; saving returns `PCFG_READ_ONLY`. If no supported slot exists, return `PCFG_UNSUPPORTED_VERSION`. Older firmware never overwrites an unknown newer slot.
4. **RT-Thread port:** the core exposes only generic storage operations. The example adapter targets the `rt_device` API used by RT-Thread 4.x/5.x-style device drivers; FAL or MTD wrappers may be built by applications around the same generic callbacks. No RT-Thread mutex is inside the core.
5. **Address and media compatibility:** v1 uses 32-bit storage addresses, little-endian fixed-width fields, and the exact media format below. A future incompatible format uses a new format version or magic; v1 is not silently reinterpreted.
6. **External transaction lock:** the caller must hold one lock around each complete `pcfg_load`, `pcfg_save`, or `pcfg_factory_reset` call. Device-level locks may be separate. The core is single-store-instance and non-reentrant.
7. **Defaults:** a default payload is validated and used by the optional codec wrapper, but is not implicitly persisted. Persistence requires an explicit factory-reset/save operation.

## 6. Repository layout

```text
portable_cfg_store/
├── CMakeLists.txt
├── LICENSE
├── README.md
├── README_zh-CN.md
├── include/
│   ├── pcfg_store.h
│   └── pcfg_crc32.h
├── src/
│   ├── pcfg_store.c
│   └── pcfg_crc32.c
├── adapters/
│   ├── cjson/
│   │   ├── pcfg_cjson.h
│   │   └── pcfg_cjson.c
│   └── rtthread_device/
│       ├── pcfg_rtthread_device.h
│       └── pcfg_rtthread_device.c
├── examples/
│   └── posix_memory/
├── tests/
│   ├── flash_emulator.c
│   ├── flash_emulator.h
│   ├── test_store.c
│   └── test_fault_injection.c
├── docs/
│   ├── storage-format.md
│   ├── porting.md
│   └── security.md
└── .github/workflows/ci.yml
```

The optional cJSON and RT-Thread adapters are not part of the default POSIX build.

## 7. Core API

The public header defines exact C99 types and signatures:

```c
typedef enum {
    PCFG_OK = 0,
    PCFG_NOT_FOUND,
    PCFG_NOT_READY,
    PCFG_INVALID_ARGUMENT,
    PCFG_BUFFER_TOO_SMALL,
    PCFG_PAYLOAD_TOO_LARGE,
    PCFG_STORAGE_ERROR,
    PCFG_CORRUPT,
    PCFG_UNSUPPORTED_VERSION,
    PCFG_VALIDATION_FAILED,
    PCFG_READ_ONLY,
    PCFG_UNSUPPORTED_GEOMETRY
} pcfg_status_t;

typedef pcfg_status_t (*pcfg_read_fn)(void *ctx, uint32_t address,
                                      void *dst, size_t size);
typedef pcfg_status_t (*pcfg_program_fn)(void *ctx, uint32_t address,
                                         const void *src, size_t size);
typedef pcfg_status_t (*pcfg_erase_fn)(void *ctx, uint32_t address,
                                       size_t size);
typedef pcfg_status_t (*pcfg_sync_fn)(void *ctx);

typedef struct {
    pcfg_read_fn read;
    pcfg_program_fn program;
    pcfg_erase_fn erase;
    pcfg_sync_fn sync; /* optional; NULL means no-op */
} pcfg_storage_ops_t;

typedef struct {
    uint32_t slot_address[2];
    size_t slot_size;
    size_t erase_unit;
    size_t program_unit;       /* valid values: 1, 2, 4, 8, or 16 */
    size_t program_page_size;  /* 0 means no page-boundary restriction */
    size_t io_chunk_size;
    uint32_t format_magic;
} pcfg_config_t;

typedef pcfg_status_t (*pcfg_validate_fn)(void *ctx,
                                          const uint8_t *payload,
                                          size_t payload_size);

typedef struct {
    pcfg_config_t config;
    pcfg_storage_ops_t storage;
    void *storage_context;
    uint8_t *scratch;
    size_t scratch_size;
    int active_slot;            /* -1 until a scan finds a valid slot */
    uint32_t active_sequence;
    bool scan_complete;
    bool read_only;
} pcfg_store_t;

pcfg_status_t pcfg_init(pcfg_store_t *store,
                        const pcfg_config_t *config,
                        const pcfg_storage_ops_t *storage,
                        void *storage_context,
                        void *scratch,
                        size_t scratch_size);

pcfg_status_t pcfg_load(pcfg_store_t *store,
                        uint8_t *payload,
                        size_t capacity,
                        size_t *payload_size,
                        pcfg_validate_fn validate,
                        void *validate_context);

pcfg_status_t pcfg_save(pcfg_store_t *store,
                        const uint8_t *payload,
                        size_t payload_size,
                        pcfg_validate_fn validate,
                        void *validate_context);

pcfg_status_t pcfg_factory_reset(pcfg_store_t *store,
                                 const uint8_t *default_payload,
                                 size_t default_size,
                                 pcfg_validate_fn validate,
                                 void *validate_context);

int pcfg_active_slot(const pcfg_store_t *store);
uint32_t pcfg_active_sequence(const pcfg_store_t *store);
bool pcfg_is_read_only(const pcfg_store_t *store);
```

API rules:

- Caller owns the store object, payload buffers, and scratch memory; the core never allocates.
- `scratch` must be non-null and at least `max(io_chunk_size, program_unit)` bytes, aligned to `program_unit`. It is used for padded writes and bounded readback.
- `io_chunk_size` must be nonzero and no larger than `scratch_size`.
- A zero-length payload is invalid. The maximum payload is `slot_size - 32 - 16` bytes.
- `pcfg_load` sets `*payload_size` to zero on entry. On success it sets the loaded length. On `PCFG_BUFFER_TOO_SMALL`, it sets the required length and leaves the output bytes unchanged. Other failures leave output bytes unchanged and set length to zero.
- Validation receives a read-only payload and may not mutate the store or call store APIs. It runs before save and after CRC verification during load.
- A blank-store scan returns `PCFG_NOT_FOUND` but establishes `scan_complete`, so the first save is legal.
- The caller must rescan with `pcfg_load` after an ambiguous failed save before retrying or interpreting the active generation.

## 8. Storage-operation contract

The platform adapter must guarantee:

- Erased bytes read as `0xFF`.
- `program` changes bits only from 1 to 0 and never modifies bytes outside the requested range.
- `erase` changes the requested erase-unit-aligned range to `0xFF` and never modifies bytes outside it.
- `read` returns the current visible bytes for the requested range.
- A successful `program`, `erase`, or `sync` means the requested operation is complete and durable according to the adapter's hardware contract.
- A failed read/program/erase may have changed any prefix or subset inside the requested range; the core never assumes a failed operation did not modify the target.
- A failed `sync` may mean operations before it are either durable or not durable; callers must reload.
- Storage callbacks are not reentrant into the same store instance.

The core treats storage errors conservatively and never writes outside either configured slot.

## 9. Geometry invariants

`pcfg_init` rejects configurations unless:

- both slot addresses and sizes are nonzero and fit in 32-bit address arithmetic;
- slot starts and sizes are aligned to `erase_unit`;
- slots do not overlap and do not share an erase unit;
- `erase_unit` is nonzero and is a multiple of `program_unit`;
- `program_unit` is 1, 2, 4, 8, or 16 and divides both 32-byte header and 16-byte commit record;
- `program_page_size` is zero or a multiple of `program_unit` and at least `program_unit`;
- `io_chunk_size >= program_unit`;
- each slot has room for the 32-byte header, positive payload, and 16-byte commit record;
- all address-plus-length calculations are overflow-checked.

The core splits writes at `program_page_size` boundaries when that limit is nonzero. Payload tail writes are padded with `0xFF` to `program_unit`; the padded bytes are excluded from payload length and CRC.

## 10. Exact v1 media format

Each slot contains a 32-byte header at the slot start, a payload region, and a 16-byte commit record at `slot_address + slot_size - 16`. All fields are little-endian. All reserved bytes and flags are zero.

### Header bytes 0..31

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | format magic, default `0x50434631` (`PCF1`) |
| 4 | 2 | format version, `1` |
| 6 | 2 | header size, `32` |
| 8 | 4 | sequence |
| 12 | 4 | payload length |
| 16 | 4 | payload CRC32 |
| 20 | 4 | flags, must be `0` in v1 |
| 24 | 4 | header CRC32 |
| 28 | 4 | reserved, must be `0` |

Header CRC32 covers bytes 0..23 followed by bytes 28..31; bytes 24..27 are excluded. The payload begins at offset 32.

### Commit bytes 0..15

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | commit magic, `0x434D5431` (`CMT1`) |
| 4 | 4 | sequence, must equal header sequence |
| 8 | 4 | payload CRC32, must equal header payload CRC |
| 12 | 4 | commit CRC32 over bytes 0..11 |

CRC32 is the reflected CRC-32/ISO-HDLC algorithm: polynomial `0xEDB88320`, initial value `0xFFFFFFFF`, final XOR `0xFFFFFFFF`, no augmented zero bytes. Golden vector: CRC32 of ASCII `123456789` is `0xCBF43926`.

A slot is valid only when header fields/ranges, header CRC, payload CRC, commit fields/CRC, and optional semantic validation all pass. Commit is programmed last. A partially written header or payload cannot be selected without a matching final commit record and CRC.

## 11. Save, load, and recovery semantics

Save sequence:

1. Validate arguments and payload before touching storage.
2. Scan if necessary; pick the inactive slot, or slot 0 when no valid slot exists.
3. Erase the complete target slot.
4. Program the header.
5. Program the payload in page-safe chunks.
6. Sync if provided.
7. Read back and verify header and payload CRC.
8. Program the commit record last.
9. Sync again.
10. Read and validate the complete slot.
11. Update in-memory active slot/sequence only after step 10 succeeds.

If an error occurs after commit may have become durable, in-memory state remains conservative but recovery may select the new slot. The API returns the error and requires a fresh load before retry.

Load scans both slots independently. Candidates are ordered by wrap-safe sequence comparison `(int32_t)(a - b) > 0`; equal sequences use the lower slot index, and the exact half-range difference is rejected as ambiguous. The newest candidate is checked first. If it is semantically invalid, the older valid candidate is tried. If the newest valid candidate is larger than the caller buffer, return `PCFG_BUFFER_TOO_SMALL` and required size rather than silently falling back.

Status precedence for a completed scan is:

1. storage I/O failure → `PCFG_STORAGE_ERROR`;
2. supported valid candidate too large → `PCFG_BUFFER_TOO_SMALL`;
3. supported valid candidate passing validation → `PCFG_OK`;
4. supported candidates exist but all validators fail → `PCFG_VALIDATION_FAILED`;
5. no supported candidate and a newer/unknown version is present → `PCFG_UNSUPPORTED_VERSION`;
6. no candidate but malformed/corrupt data exists → `PCFG_CORRUPT`;
7. both slots erased/blank → `PCFG_NOT_FOUND`.

If a supported candidate is loaded while any unknown newer-format slot exists, the load succeeds but `pcfg_is_read_only()` is true and save/reset return `PCFG_READ_ONLY`.

Factory reset calls `pcfg_save` with the supplied default payload. It is therefore power-loss-safe and never erases both slots.

## 12. Optional cJSON adapter

The cJSON adapter is separate; cJSON is not vendored initially. The target is enabled only when the caller supplies cJSON and its allocator configuration. The adapter owns its parsed tree, uses caller-provided serialization buffers, and never exposes a mutable tree to callers while dirty tracking is active.

Version-one path scope is bounded: object keys separated by literal dots, no arrays, no escaping, no implicit type replacement, and no duplicate-key acceptance. Missing intermediate objects may be created only by an explicit set operation. Numeric values are checked for finite range before conversion to signed 32-bit values. Defaults are parsed and validated but not auto-saved.

The adapter provides parse/serialize validation callbacks, embedded-default fallback, dirty tracking, and get/set for strings, signed 32-bit integers, booleans, and finite doubles. Credentials remain plaintext; CRC is not a security mechanism.

## 13. RT-Thread adapter

The example maps an application-supplied `rt_device_t` to the generic callbacks. The application supplies slot geometry and an erase callback for the RT-Thread device's control command. The adapter does not select an address, device name, mutex, or watchdog policy. One application-level mutex must cover each complete core transaction; device-level locks do not replace it.

## 14. Testing strategy

The POSIX flash emulator enforces erased `0xFF`, 1-to-0 programming, alignment/page limits, no out-of-range writes, configurable failure at every operation and byte position, direct corruption, and persistence across process reinitialization.

Required core tests include:

1. blank storage and first save;
2. round trip and slot alternation;
3. exact CRC/endian golden vectors;
4. zero/max/max+1 payload and output canaries;
5. geometry overlap/misalignment/overflow and page boundaries;
6. partial header/payload/commit/erase at every byte position followed by re-init;
7. corrupted newest data falling back to older data;
8. both slots invalid and status precedence;
9. read/program/erase/verification-read/sync errors;
10. validator fallback and both validators failing;
11. sequence wrap, equal sequence, and exact half-range ambiguity;
12. unknown/newer version read-only behavior;
13. retry after ambiguous failure only after reload;
14. factory reset interruption;
15. GCC and Clang with `-Wall -Wextra -Werror`;
16. ASan/UBSan on the host.

The cJSON adapter gets separate tests for allocation failure, exact serialization capacity, malformed/default documents, numeric range, NaN/Inf, duplicate keys, dotted-key scope, and dirty-state failures. The core suite never requires cJSON.

## 15. Build and CI

- CMake 3.16 or newer.
- Core C99 without compiler extensions.
- Tests enabled for a top-level checkout.
- cJSON and RT-Thread targets disabled by default.
- GitHub Actions builds GCC and Clang, runs CTest, and runs sanitizer tests on Linux.
- Public headers compile as C and under `extern "C"` from C++.

## 16. Documentation, security, and licensing

English and Chinese README files explain guarantees, a five-minute POSIX example, integration, format, limits, flash endurance, and security boundaries. Supporting documents explain why CRC is not atomicity, why commit is last, why two slots preserve the previous value, why codec and physical storage are separate, and when not to use the design.

The repository uses MIT: `Copyright (c) 2026 askevil contributors`. cJSON remains an optional external dependency under its own MIT license. CRC32 detects accidental corruption only; it provides no authenticity, confidentiality, or anti-rollback protection.

## 17. Acceptance criteria

1. Required core tests pass with GCC and Clang.
2. Sanitizer run is clean.
3. For every injected fault in the documented emulator model, recovery yields the old payload or a fully valid new payload, never neither; this excludes hardware/controller violations and malicious corruption.
4. The POSIX example runs from a fresh checkout.
5. Public files contain no private paths, product addresses, product names, credentials, or copied source.
6. README documents format and security boundaries.
7. The approved result is committed and pushed to `main` of `askevil/embadderd_tool`.

## 18. Implementation rulings (2026-11-18)

- Load requires full-candidate scratch to preserve failure output and support contiguous semantic validators. Save CRC-only recovery and verification stream; semantic cold recovery needs full-candidate scratch. Payload input/output may not overlap scratch.
- Unknown future media must retain the CRC-valid 32-byte envelope so v1 can distinguish it from a torn write. Different magic/version/layout with that envelope is protected; arbitrary unrecognizable future layouts require an application migration policy.
- Setters use a private tree copy so allocation/path failures preserve RAM state. Codec allocation errors use PCFG_STORAGE_ERROR in this ABI.
- Public statuses additionally include PCFG_SCRATCH_TOO_SMALL and PCFG_SEQUENCE_AMBIGUOUS; pcfg_store_t includes reload_required, with a getter. It is cleared only by trustworthy recovery or a successful commit.
- Slot offset zero is permitted; the original geometry phrase about nonzero slot addresses does not apply to byte offset zero.
