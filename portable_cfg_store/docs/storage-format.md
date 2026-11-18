# Storage format v1

All fields are little-endian. A slot is exactly the configured `slot_size` and has three regions:

```text
offset 0                  32-byte header
offset 32                 payload bytes
...                       erased/unused bytes
slot_size - 16            16-byte commit record
```

## Header (32 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | application-selected magic; default `0x50434631` (`PCF1`) |
| 4 | 2 | format version = 1 |
| 6 | 2 | header size = 32 |
| 8 | 4 | sequence |
| 12 | 4 | payload length |
| 16 | 4 | payload CRC32 |
| 20 | 4 | flags = 0 |
| 24 | 4 | header CRC32 |
| 28 | 4 | reserved = 0 |

Header CRC covers bytes `0..23`, then `28..31`; its own field is excluded.

## Commit record (16 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | commit magic `0x434D5431` (`CMT1`) |
| 4 | 4 | sequence, equal to header |
| 8 | 4 | payload CRC32, equal to header |
| 12 | 4 | commit CRC over bytes `0..11` |

The commit record is located at a fixed end-of-slot address and programmed last. Payload padding up to the storage program unit remains `0xFF` and is excluded from length and CRC.

## CRC

Reflected CRC-32/ISO-HDLC:

- reflected polynomial `0xEDB88320`
- initial value `0xFFFFFFFF`
- final XOR `0xFFFFFFFF`
- golden vector: ASCII `123456789` → `0xCBF43926`

CRC is an accidental-corruption check, not a cryptographic authenticator.

## Sequence ordering

Sequence `a` is newer than `b` when `(int32_t)(a - b) > 0`. Equal sequences deterministically select the lower slot index. A difference of exactly `0x80000000` is ambiguous and rejected.

## Validity

A slot is selectable only if header fields and CRC, length bounds, payload CRC, commit fields, and commit CRC all pass. An optional application validator may reject a structurally valid payload, in which case the older valid slot is considered.

If a newer unknown format coexists with a supported slot, the supported slot may be loaded, but the store becomes read-only so old firmware cannot overwrite the unknown data.
