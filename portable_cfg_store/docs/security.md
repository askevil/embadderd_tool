# Security model

Portable Configuration Store provides integrity checks against accidental corruption under a documented storage fault model. It is not a security boundary.

## It does provide

- rejection of incomplete header/payload/commit writes;
- CRC-based detection of accidental bit corruption;
- fallback to an older committed slot;
- read-only downgrade protection when an unknown newer media version is visible.

## It does not provide

- confidentiality: payload bytes are plaintext;
- authenticity: an attacker can replace bytes and recompute CRC;
- anti-rollback: an attacker can restore an old complete slot;
- secure erase: previous values remain in the inactive slot until overwritten;
- key management or access control;
- protection from malicious storage callbacks or writes outside configured ranges.

Do not store passwords, private keys, tokens, or certificates here unless the application encrypts and authenticates the payload with a real key-management design. Prefer an AEAD scheme with a protected device-unique key and define nonce, rollback, and recovery behavior before deployment.

CRC32 is intentionally used for accidental-error detection and format validation. It must never be described as encryption, signing, or tamper protection.
