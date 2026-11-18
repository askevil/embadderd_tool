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
