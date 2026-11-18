# Porting guide

## Required storage behavior

The adapter must satisfy these rules:

- erased bytes read as `0xFF`;
- program changes bits only from 1 to 0;
- erase and program never modify bytes outside the requested range;
- a failed program/erase may have changed any prefix inside the requested range;
- successful operations are complete according to the adapter contract;
- a failed `sync` is ambiguous: previous operations may or may not be durable;
- reads after successful writes expose the written data.

A controller that silently writes outside the requested range, erases both slots together, or reports durable success before data is durable violates the model.

## Geometry

- two non-overlapping slots;
- slot starts and sizes aligned to `erase_unit`;
- slots must not share an erase unit;
- `program_unit` is 1, 2, 4, 8, or 16;
- optional `program_page_size` is a multiple of `program_unit`;
- `io_chunk_size` is nonzero, program-unit aligned, and fits scratch;
- slot must hold 32-byte header, at least one payload byte, and 16-byte commit.

## Locking

The core is non-reentrant. Hold one application lock around the complete `pcfg_load`, `pcfg_save`, or `pcfg_factory_reset` call. A device driver's per-I/O lock does not protect the multi-step transaction.

## Watchdog and yield

If long erase/program operations require watchdog feeding or yielding, implement that inside the platform callback. The callback must not re-enter the same `pcfg_store_t` instance.

## Save failures

After a storage failure, `pcfg_reload_required()` can be true. A failed commit or sync can still have become durable; call `pcfg_load()` before retrying. Recovery may return old or complete new data.

## Endurance

Each save erases one complete slot. Keep configuration writes infrequent, coalesce dirty changes, avoid saving unchanged payloads, and use a wear-leveling/log design for high-frequency counters.

## RTOS integration

Keep serialization/codec decisions above this core. A typical RTOS wrapper owns:

- the transaction mutex;
- device discovery and power state;
- scratch and payload buffers;
- default document and schema validator;
- deferred-save policy.

The optional RT-Thread adapter maps `rt_device` read/write plus an application erase callback to these generic operations; it does not choose a device name, address, or mutex.

### Building the RT-Thread adapter

Enable `PCFG_BUILD_RTTHREAD_ADAPTER=ON`. Supply `PCFG_RTTHREAD_INCLUDE_DIR` with the application RT-Thread include paths; when empty in host tests, minimal stubs exercise callback mappings only. Real builds must not ship the stubs. `rt_device_read/write` must report **bytes**, not blocks. Devices with block semantics require an application translation layer. Short transfers are errors. The adapter rejects addresses not representable by signed `rt_off_t`. Application erase/sync callbacks own controller-specific commands and durability barriers. The application must open the device and hold the complete transaction lock.

No board/controller was validated by the stub test. Confirm byte offsets, erase units, program-page behavior, short-I/O handling, and power-cut recovery on the target board before deployment.

## Buffer and recovery contract

Payload input/output ranges must not overlap scratch. Load needs scratch for the full candidate before CRC and semantic validation; buffer-too-small is reported only for a semantically accepted candidate. Save without a validator streams both cold-start CRC recovery and verification through small scratch. If a semantic validator is supplied during cold-start save/recovery, scratch must hold candidate payloads because the callback receives contiguous bytes; insufficient scratch is rejected before erase. Prefer an explicit successful load before normal saves. A failed reload keeps the retry guard set until recovery succeeds. Exact-half-range sequence ambiguity between two fully valid candidates never permits a retry to bypass recovery.

Status priority: invalid arguments/overlaps are rejected before I/O; storage read errors and insufficient scratch stop scanning conservatively; recognizable unknown format protects media read-only; CRC-invalid candidates are skipped; semantic-invalid candidates are skipped before output capacity is checked; two valid half-range generations return `PCFG_SEQUENCE_AMBIGUOUS`. With no accepted candidates, all-semantic rejection returns `PCFG_VALIDATION_FAILED`; accidental media damage returns `PCFG_CORRUPT`; blank media returns `PCFG_NOT_FOUND`.
