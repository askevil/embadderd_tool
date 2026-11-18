# Portable Configuration Store Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Publish a clean-room, reusable C99 A/B configuration store with deterministic media format, fault-injection-tested power-loss recovery, optional cJSON and RT-Thread adapters, bilingual documentation, and CI.

**Architecture:** The heap-free core stores opaque payload bytes through caller-supplied storage callbacks. Two fixed slots, CRC-32/ISO-HDLC metadata, and a final commit record provide old-or-new recovery; JSON and RT-Thread integration remain optional adapters outside the transaction core.

**Tech Stack:** C99, CMake 3.16+, CTest, GCC 13, Clang 18, ASan/UBSan, optional external cJSON, RT-Thread `rt_device` adapter.

**Spec:** `docs/superpowers/specs/2026-11-18-portable-config-store-design.md`

## Global Constraints

- Public implementation must be clean-room: no copied proprietary code, product names, private paths, product addresses, credentials, or product schemas.
- Core is C99, heap-free, RTOS-independent, filesystem-independent, and JSON-independent.
- Media v1 is fixed little-endian with 32-byte header, payload, and 16-byte final commit record.
- CRC is reflected CRC-32/ISO-HDLC: polynomial `0xEDB88320`, init/xor `0xFFFFFFFF`; `"123456789"` must produce `0xCBF43926`.
- Recovery guarantee is old valid payload or complete new valid payload under the documented emulator fault model; a failed save after possible commit requires reload.
- Unknown newer format plus a supported slot loads supported data read-only; no supported slot returns `PCFG_UNSUPPORTED_VERSION`.
- Factory reset is a normal A/B commit of validated default bytes, never a two-slot erase.
- A caller-held lock covers each full load/save/reset transaction; core and adapters are non-reentrant.
- MIT license with `Copyright (c) 2026 askevil contributors`.
- cJSON and RT-Thread targets are disabled in the default POSIX build.

## Review Focus

- Geometry overflow or shared erase units must fail at init without issuing storage I/O; Task 2 tests overlap, misalignment, and `UINT32_MAX` boundaries.
- Failed commit/sync can leave old or complete new durable; Task 4 reinitializes after every injected operation/byte fault and verifies never neither.
- A newer unsupported slot must never be overwritten by old firmware; Task 3 tests mixed supported/newer slots and `PCFG_READ_ONLY`.
- Output bytes must remain unchanged on load failure or insufficient capacity; Task 3 uses canaries and required-length assertions.
- Partial or page-boundary-constrained writes must never touch the commit region early; Tasks 2 and 4 test program-unit padding, page splits, and partial commit writes.

---

## File map

### Repository-level files

- `README.md` — collection-level repository index and link to the module.
- `LICENSE` — MIT license for original repository content.
- `.gitignore` — CMake, compiler, coverage, and editor artifacts.
- `.github/workflows/ci.yml` — GCC, Clang, sanitizers, examples, and public-header C++ check.

### Module core

- `portable_cfg_store/include/pcfg_store.h` — public status, geometry, storage callback, store state, validation, and transaction APIs.
- `portable_cfg_store/include/pcfg_crc32.h` — streaming and one-shot CRC API.
- `portable_cfg_store/src/pcfg_crc32.c` — reflected CRC-32/ISO-HDLC implementation.
- `portable_cfg_store/src/pcfg_media_internal.h` — private v1 offsets, endian helpers, decoded metadata, and slot-inspection helpers.
- `portable_cfg_store/src/pcfg_store.c` — geometry validation, slot scan/load, save/reset transactions, and state inspection.
- `portable_cfg_store/CMakeLists.txt` — core, optional adapters, examples, and tests.

### Tests and examples

- `portable_cfg_store/tests/test_harness.h` — minimal assertion and test-runner helpers.
- `portable_cfg_store/tests/flash_emulator.h/.c` — persistent NOR model, alignment/page enforcement, corruption, operation/byte fault injection, and operation log.
- `portable_cfg_store/tests/test_crc_media.c` — CRC and exact media-byte tests.
- `portable_cfg_store/tests/test_store.c` — geometry, load/save, status, validation, and sequence behavior.
- `portable_cfg_store/tests/test_fault_injection.c` — exhaustive interruption and restart recovery sweeps.
- `portable_cfg_store/examples/posix_memory/main.c` — self-contained save/load/reset example using an in-memory adapter.

### Optional adapters

- `portable_cfg_store/adapters/cjson/pcfg_cjson.h/.c` — document ownership, defaults, bounded serialization, object-only dotted paths, typed get/set, dirty/save/reset.
- `portable_cfg_store/adapters/cjson/test_pcfg_cjson.c` — adapter behavior and failure tests, built only when cJSON is supplied.
- `portable_cfg_store/adapters/rtthread_device/pcfg_rtthread_device.h/.c` — generic `rt_device_t` read/program mapping plus application-supplied erase callback.
- `portable_cfg_store/adapters/rtthread_device/test_stubs/` — compile-only RT-Thread API stubs; no product implementation.

### Documentation

- `portable_cfg_store/README.md` and `README_zh-CN.md` — use cases, guarantees, quick start, limitations, and learning explanation.
- `portable_cfg_store/docs/storage-format.md` — exact byte format and golden image.
- `portable_cfg_store/docs/porting.md` — storage contract, lock placement, geometry, and RTOS integration.
- `portable_cfg_store/docs/security.md` — CRC limits, plaintext secrets, rollback and authenticity exclusions.

---

### Task 1: Repository bootstrap, CRC, and exact media primitives

**Files:**
- Create: `LICENSE`
- Create: `.gitignore`
- Create: `README.md`
- Create: `portable_cfg_store/CMakeLists.txt`
- Create: `portable_cfg_store/include/pcfg_crc32.h`
- Create: `portable_cfg_store/src/pcfg_crc32.c`
- Create: `portable_cfg_store/src/pcfg_media_internal.h`
- Create: `portable_cfg_store/tests/test_harness.h`
- Create: `portable_cfg_store/tests/test_crc_media.c`

**Interfaces:**
- Produces: `uint32_t pcfg_crc32_init(void)`, `uint32_t pcfg_crc32_update(uint32_t state, const void *data, size_t size)`, `uint32_t pcfg_crc32_finish(uint32_t state)`, `uint32_t pcfg_crc32(const void *data, size_t size)`.
- Produces private helpers `pcfg_get_le16/32`, `pcfg_put_le16/32`, `pcfg_encode_header`, `pcfg_decode_header`, `pcfg_encode_commit`, and `pcfg_decode_commit` with the exact spec offsets.
- Consumed by: all later tasks.

- [ ] **Step 1: Write failing CRC and media-vector tests**

Add tests named:

```c
static void test_crc32_iso_hdlc_golden_vector(void);
static void test_crc32_streaming_matches_one_shot(void);
static void test_header_encoding_is_exact_little_endian(void);
static void test_header_crc_excludes_its_own_field(void);
static void test_commit_encoding_is_exact_little_endian(void);
static void test_decode_rejects_nonzero_v1_flags_and_reserved(void);
```

Assert the `123456789` CRC is `0xCBF43926`, exact header/commit bytes match a hard-coded golden vector, and changing any CRC-covered byte invalidates decoding.

- [ ] **Step 2: Run the focused test and confirm red state**

Run: `cmake -S portable_cfg_store -B build -DPCFG_BUILD_TESTS=ON && cmake --build build --target test_crc_media && ctest --test-dir build -R pcfg_crc_media --output-on-failure`

Expected: configure or compile failure because the API and implementation do not exist.

- [ ] **Step 3: Implement CRC and private wire-format helpers**

Use byte-wise C99 code without packed structs or host-endian casts. Header CRC covers bytes `0..23` then `28..31`; commit CRC covers bytes `0..11`.

- [ ] **Step 4: Re-run focused tests with GCC and Clang**

Run GCC command above, then: `CC=clang cmake -S portable_cfg_store -B build-clang -DPCFG_BUILD_TESTS=ON && cmake --build build-clang --target test_crc_media && ctest --test-dir build-clang -R pcfg_crc_media --output-on-failure`

Expected: both test runs PASS with warnings treated as errors.

- [ ] **Step 5: Commit the independently testable media foundation**

```bash
git add LICENSE .gitignore README.md portable_cfg_store
git commit -m "feat: add portable config media primitives"
```

### Task 2: Public core API, geometry checks, and NOR emulator

**Files:**
- Create: `portable_cfg_store/include/pcfg_store.h`
- Create: `portable_cfg_store/src/pcfg_store.c`
- Create: `portable_cfg_store/tests/flash_emulator.h`
- Create: `portable_cfg_store/tests/flash_emulator.c`
- Create: `portable_cfg_store/tests/test_store.c`
- Modify: `portable_cfg_store/CMakeLists.txt`

**Interfaces:**
- Consumes: CRC and media helpers from Task 1.
- Produces: exact `pcfg_status_t`, `pcfg_storage_ops_t`, `pcfg_config_t`, `pcfg_validate_fn`, and `pcfg_store_t` types from the approved spec.
- Produces: `pcfg_init`, `pcfg_load`, `pcfg_save`, `pcfg_factory_reset`, `pcfg_active_slot`, `pcfg_active_sequence`, `pcfg_is_read_only` declarations; Task 2 implements init and state inspection, with load/save stubs returning `PCFG_NOT_READY` until Task 3.
- Produces emulator operations `pcfg_flash_init`, `pcfg_flash_reset_faults`, `pcfg_flash_fail_operation`, `pcfg_flash_fail_byte`, `pcfg_flash_corrupt`, and `pcfg_flash_storage_ops`.

- [ ] **Step 1: Write failing geometry and emulator-contract tests**

Add tests for valid geometry and rejection of zero sizes, slot overlap, shared erase unit, misalignment, invalid program unit, invalid page size, too-small slot, insufficient/misaligned scratch, zero chunk, `io_chunk_size > scratch_size`, and address overflow. Assert emulator rejects 0→1 programming, page crossing, unaligned erase, and out-of-range access without changing outside bytes.

- [ ] **Step 2: Run geometry tests and confirm failure**

Run: `cmake --build build --target test_store && ctest --test-dir build -R pcfg_store --output-on-failure`

Expected: compile/link failure for missing public API and emulator functions.

- [ ] **Step 3: Implement the public header, geometry validation, and emulator**

`pcfg_init` copies configuration and callbacks only after all invariants pass, initializes `active_slot=-1`, `active_sequence=0`, `scan_complete=false`, and `read_only=false`, and performs no storage I/O. All address arithmetic uses explicit overflow checks.

- [ ] **Step 4: Run geometry tests under GCC, Clang, and sanitizers**

Run: GCC and Clang focused CTest runs; then `cmake -S portable_cfg_store -B build-asan -DPCFG_BUILD_TESTS=ON -DPCFG_ENABLE_SANITIZERS=ON && cmake --build build-asan && ctest --test-dir build-asan -R pcfg_store --output-on-failure`.

Expected: PASS and no sanitizer diagnostics.

- [ ] **Step 5: Commit public ABI and platform test double**

```bash
git add portable_cfg_store
git commit -m "feat: define config store API and geometry"
```

### Task 3: Load scanning, validation fallback, and downgrade protection

**Files:**
- Modify: `portable_cfg_store/src/pcfg_store.c`
- Modify: `portable_cfg_store/src/pcfg_media_internal.h`
- Modify: `portable_cfg_store/tests/test_store.c`
- Modify: `portable_cfg_store/tests/flash_emulator.c`

**Interfaces:**
- Consumes: public types/emulator from Task 2 and media helpers from Task 1.
- Produces: fully implemented `pcfg_load` and private `pcfg_inspect_slot`/`pcfg_read_payload_crc` helpers.
- Produces observable rules: blank scan permits future first save; supported+unknown loads read-only; output is unchanged on all load failures; required length is reported only for newest supported valid candidate.

- [ ] **Step 1: Write failing load and status-precedence tests**

Add tests for blank slots, a manually encoded valid slot, newest/older ordering, equal-sequence lower-slot tie-break, `UINT32_MAX→0` wrap, exact-half-range ambiguity, corrupt newest fallback, semantic-invalid newest fallback, both validators failing, output canaries, small output buffer, storage read failure, malformed metadata, unsupported-only slot, and supported+unsupported read-only behavior.

- [ ] **Step 2: Run focused load tests and confirm red state**

Run: `cmake --build build --target test_store && ctest --test-dir build -R pcfg_store --output-on-failure`

Expected: assertions fail because load returns its Task 2 stub status.

- [ ] **Step 3: Implement bounded slot inspection and load**

Read metadata and payload in scratch-sized chunks, calculate CRC without heap allocation, copy to caller output only after candidate selection/validation succeeds, and preserve output on errors. A complete blank/corrupt scan sets `scan_complete=true`; supported+unknown sets `read_only=true`.

- [ ] **Step 4: Run load tests under GCC, Clang, and sanitizers**

Expected: all focused tests PASS with no diagnostic.

- [ ] **Step 5: Commit recovery scanning**

```bash
git add portable_cfg_store
git commit -m "feat: add validated slot recovery"
```

### Task 4: Save/reset transaction and exhaustive fault injection

**Files:**
- Modify: `portable_cfg_store/src/pcfg_store.c`
- Modify: `portable_cfg_store/tests/test_store.c`
- Create: `portable_cfg_store/tests/test_fault_injection.c`
- Modify: `portable_cfg_store/tests/flash_emulator.h`
- Modify: `portable_cfg_store/tests/flash_emulator.c`
- Modify: `portable_cfg_store/CMakeLists.txt`

**Interfaces:**
- Consumes: full load scan and emulator.
- Produces: fully implemented `pcfg_save` and `pcfg_factory_reset`.
- Produces emulator enumeration of every mutable operation/byte failure point for deterministic restart sweeps.

- [ ] **Step 1: Write failing normal-save tests**

Test first save uses slot 0/sequence 1, subsequent saves alternate slots, zero/max/max+1 payloads, validator-before-write, payload-tail `0xFF` padding, program-page splitting, readback failure, commit written last, read-only rejection, explicit factory reset, and retry prohibition until reload after ambiguous failure.

- [ ] **Step 2: Write failing exhaustive interruption tests**

For every erase/program/sync operation and every writable byte position in header, payload, and commit:

1. start with a committed `old` payload;
2. inject one failure during save of `new`;
3. destroy/reinitialize the store object while preserving flash;
4. call load;
5. assert result is exactly `old` or exactly `new`, never not-found/corrupt/partial.

Repeat for factory reset and for failures before/after possible sync durability. Assert no write touches the active slot or target commit region before the final commit operation.

- [ ] **Step 3: Run tests and confirm current implementation fails**

Run: `cmake --build build --target test_store test_fault_injection && ctest --test-dir build -R 'pcfg_(store|fault)' --output-on-failure`

Expected: save/reset and fault tests fail against Task 3 stubs.

- [ ] **Step 4: Implement page-safe program helpers and transactions**

Split writes by scratch capacity, program unit, and page boundary; pad only the final payload fragment with erased bytes. Verify header/payload before commit, program commit last, sync, then validate the finished target. On any failure leave in-memory active state conservative and document reload requirement.

- [ ] **Step 5: Run full core matrix**

Run GCC, Clang, and ASan/UBSan CTest suites, including the complete fault sweep.

Expected: all tests PASS; test summary prints number of injected fault points exercised.

- [ ] **Step 6: Commit atomic save and reset**

```bash
git add portable_cfg_store
git commit -m "feat: add power-loss-safe save transactions"
```

### Task 5: POSIX example and user-facing core documentation

**Files:**
- Create: `portable_cfg_store/examples/posix_memory/main.c`
- Create: `portable_cfg_store/README.md`
- Create: `portable_cfg_store/README_zh-CN.md`
- Create: `portable_cfg_store/docs/storage-format.md`
- Create: `portable_cfg_store/docs/porting.md`
- Create: `portable_cfg_store/docs/security.md`
- Modify: `portable_cfg_store/CMakeLists.txt`
- Modify: `README.md`

**Interfaces:**
- Consumes: complete core API and emulator-like in-memory callbacks.
- Produces: `pcfg_posix_memory_example` executable and published integration contract.

- [ ] **Step 1: Add example smoke test before implementation**

Register a CTest that expects the example to print `loaded: brightness=42`, `sequence: 1`, then after factory reset `loaded default: brightness=10`.

- [ ] **Step 2: Run example target and confirm red state**

Expected: target or test is missing.

- [ ] **Step 3: Implement the standalone memory example**

Keep it product-neutral; demonstrate caller lock boundary in comments, explicit default reset, and no JSON dependency.

- [ ] **Step 4: Write English/Chinese documentation**

Document exact format, save timeline, status precedence, geometry examples, lock placement, endurance guidance, save-frequency anti-patterns, ambiguous failure reload, unknown-version read-only behavior, security exclusions, and why CRC is not atomicity. Include a minimal integration snippet compiled by CI.

- [ ] **Step 5: Run example and documentation-code compile checks**

Run: `cmake --build build && ctest --test-dir build -R pcfg_posix_example --output-on-failure`.

Expected: PASS with exact smoke output.

- [ ] **Step 6: Commit learning material and example**

```bash
git add README.md portable_cfg_store
git commit -m "docs: add config store guide and example"
```

### Task 6: Optional cJSON adapter

**Files:**
- Create: `portable_cfg_store/adapters/cjson/pcfg_cjson.h`
- Create: `portable_cfg_store/adapters/cjson/pcfg_cjson.c`
- Create: `portable_cfg_store/adapters/cjson/test_pcfg_cjson.c`
- Modify: `portable_cfg_store/CMakeLists.txt`
- Modify: `portable_cfg_store/README.md`
- Modify: `portable_cfg_store/README_zh-CN.md`

**Interfaces:**
- Consumes: core save/load/reset API and externally supplied `<cJSON.h>` target/path.
- Produces opaque `pcfg_cjson_document_t` ownership API: init/load/destroy, bounded serialize/save/reset, typed object-only dotted-path get/set, dirty query, and product semantic-validator callback.
- Produces CMake option `PCFG_BUILD_CJSON_ADAPTER=OFF` and cache target name `PCFG_CJSON_TARGET`.

- [ ] **Step 1: Write adapter tests against an externally fetched test-only cJSON dependency**

Tests cover malformed stored/default JSON, explicit fallback without auto-save, missing-parent creation only on set, rejected arrays/dotted keys/type replacement/duplicate keys, string capacity, `INT32_MIN/MAX`, fractional/out-of-range integers, NaN/Inf rejection, exact serialization capacity, allocation failure hooks, direct-tree non-exposure, dirty transitions, save failure retaining dirty, and reset behavior.

- [ ] **Step 2: Run optional adapter build and confirm red state**

Run CMake with adapter and tests enabled plus an external cJSON target provided by the test build. Expected: compile failure because adapter API is absent.

- [ ] **Step 3: Implement opaque document and path helpers**

The adapter owns/deletes its cJSON tree, never vendors cJSON, and accepts caller serialization buffers. Dotted paths support object keys only; arrays, literal dots, duplicates, and implicit type replacement are rejected.

- [ ] **Step 4: Run adapter tests with sanitizers**

Expected: PASS, no leaks or OOM-path crashes.

- [ ] **Step 5: Commit optional JSON integration**

```bash
git add portable_cfg_store/adapters/cjson portable_cfg_store/CMakeLists.txt portable_cfg_store/README*
git commit -m "feat: add optional cJSON configuration adapter"
```

### Task 7: RT-Thread `rt_device` adapter and compile-only contract

**Files:**
- Create: `portable_cfg_store/adapters/rtthread_device/pcfg_rtthread_device.h`
- Create: `portable_cfg_store/adapters/rtthread_device/pcfg_rtthread_device.c`
- Create: `portable_cfg_store/adapters/rtthread_device/test_stubs/rtthread.h`
- Create: `portable_cfg_store/adapters/rtthread_device/test_stubs/rtdevice.h`
- Create: `portable_cfg_store/adapters/rtthread_device/test_compile.c`
- Modify: `portable_cfg_store/CMakeLists.txt`
- Modify: `portable_cfg_store/docs/porting.md`

**Interfaces:**
- Consumes: `pcfg_storage_ops_t`.
- Produces `pcfg_rtthread_device_t` with application-supplied `rt_device_t`, address-is-byte-offset policy, and erase callback `pcfg_status_t (*erase)(void *, uint32_t, size_t)`.
- Produces `pcfg_rtthread_device_ops()`; it does not own transaction locks, addresses, watchdog policy, or device discovery.

- [ ] **Step 1: Write compile-only and callback-mapping tests**

Use minimal RT-Thread stubs to assert headers compile as C/C++, short reads/writes map to `PCFG_STORAGE_ERROR`, exact-size operations map to `PCFG_OK`, and erase delegates to the application callback.

- [ ] **Step 2: Run adapter target and confirm red state**

Expected: missing adapter symbols.

- [ ] **Step 3: Implement the narrow `rt_device` mapping**

No fixed `nor0`, no private erase-control struct, no mutex, no hidden allocation, and no product address.

- [ ] **Step 4: Run compile-only target with GCC and Clang**

Expected: PASS under warnings-as-errors.

- [ ] **Step 5: Commit RT-Thread port example**

```bash
git add portable_cfg_store/adapters/rtthread_device portable_cfg_store/CMakeLists.txt portable_cfg_store/docs/porting.md
git commit -m "feat: add RT-Thread device adapter"
```

### Task 8: CI, clean-room audit, final review, and GitHub publication

**Files:**
- Create: `.github/workflows/ci.yml`
- Modify: repository/module README and CMake files only if verification reveals omissions.
- Verify: all public files.

**Interfaces:**
- Consumes: all prior deliverables.
- Produces: reproducible public build and pushed `main` branch.

- [ ] **Step 1: Add CI matrix**

Linux jobs: GCC core/tests/example, Clang core/tests/example, ASan+UBSan, optional cJSON adapter tests, RT-Thread stub compile, and C++ public-header compile. Use pinned released action majors and CMake configure/build/CTest commands identical to local verification.

- [ ] **Step 2: Run fresh local verification**

Remove build directories after verifying their resolved absolute paths, then configure from scratch for GCC, Clang, sanitizers, cJSON, and RT adapter. Run all CTests and the example.

Expected: every command exits 0; record compiler versions, test counts, and fault-injection count.

- [ ] **Step 3: Run publication audit**

Search tracked files for private absolute paths, `RS300`, `RS500`, product addresses, credentials, private device names, and proprietary copyright strings. Run `git diff --check`, inspect `git status`, and ensure no build artifact is tracked.

Expected: zero prohibited matches outside the design document's explicit clean-room historical statement; no whitespace errors; clean intended file set.

- [ ] **Step 4: Request independent whole-branch code review**

Reviewer checks media compatibility, geometry arithmetic, transaction ordering, error/status precedence, tests, optional adapter ownership, public documentation, and clean-room compliance. Resolve all blocking/high findings and rerun affected/full tests.

- [ ] **Step 5: Commit release automation and final corrections**

```bash
git add .github README.md LICENSE portable_cfg_store docs/superpowers
git commit -m "ci: publish portable config store checks"
```

- [ ] **Step 6: Push the verified branch**

Run: `git push -u origin main`

Expected: GitHub accepts the push and `git ls-remote --heads origin main` reports the local final commit SHA.

- [ ] **Step 7: Report exact delivery status**

Provide repository URL, final commit SHA, local verification commands/results, CI status if visible, design/plan links, and explicit limits: no real NOR power-cut or RT-Thread board validation was performed.

---

## Self-review result

- **Spec coverage:** Every approved spec section maps to Tasks 1–8. Core/media, transaction recovery, downgrade policy, reset semantics, optional adapters, documentation, CI, and clean-room publication are covered.
- **Type consistency:** Core names and signatures are introduced in Task 2 and consumed unchanged by later tasks. Adapter callbacks map only to `pcfg_storage_ops_t`.
- **Review Focus coverage:** Geometry (Task 2), ambiguous commits and fault sweep (Task 4), unknown versions (Task 3), output canaries (Task 3), and page/commit isolation (Tasks 2/4) each have explicit tests.
- **Scope control:** Core reaches a publishable state after Task 5; cJSON and RT-Thread remain optional, independently reviewable Tasks 6/7.
- **Verification boundary:** Host fault injection validates the documented model, not real controller durability or physical power-cut behavior.
