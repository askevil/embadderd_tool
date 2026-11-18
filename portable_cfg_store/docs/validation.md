# Host validation — 2026-11-18

## Result

- GCC 13.3.0: combined core/cJSON/RT-stub/C++ suite **9/9 PASS**.
- Clang 18.1.3: combined suite **9/9 PASS**.
- Clang ASan + UBSan: combined suite **9/9 PASS**.
- Default build without optional adapters: **7/7 PASS**.
- Fault sweep: **34,768 injected cases**, each checking old-or-complete-new recovery and a successful subsequent save.
- Repository checks and `git diff --check`: PASS.

Optional cJSON tests used external cJSON commit `6d9f2443ab071f86e5d9b43025a40929ec41c46c`. Third-party source is not included in this repository.

## Reproduction

```bash
# Default core + example
cmake -S portable_cfg_store -B build-core
cmake --build build-core
ctest --test-dir build-core --output-on-failure --no-tests=error

# Obtain the test dependency OUTSIDE the module sources.
git clone https://github.com/DaveGamble/cJSON.git external/cJSON
git -C external/cJSON checkout 6d9f2443ab071f86e5d9b43025a40929ec41c46c

# Combined GCC suite
CC=gcc CXX=g++ cmake -S portable_cfg_store/tests/cjson_driver -B build-all \
  -DPCFG_CJSON_SOURCE_DIR="$PWD/external/cJSON" \
  -DPCFG_BUILD_RTTHREAD_ADAPTER=ON
cmake --build build-all
ctest --test-dir build-all --output-on-failure --no-tests=error

# Repeat with CC=clang CXX=clang++ and another build directory.
# Add -DPCFG_ENABLE_SANITIZERS=ON for ASan/UBSan.
python3 tests/run_checks.py
```

## Independent whole-branch review

A fresh reviewer examined the complete implementation baseline, design, tests, adapters and documentation. The baseline review requested changes: one Critical and eight Important findings. The author corrected these in one regression-tested pass, without claiming an independent re-review of the corrections.

Regression coverage added for:

- cold-save protection of the sole payload-valid older generation;
- cold mixed-format read-only rejection before mutation;
- failed reload preserving the retry guard;
- repeated half-range rejection, corrupt half-range fallback and equal-sequence ties;
- torn-header recovery followed by a writable next transaction;
- header readback, final commit verification and accidental metadata corruption;
- small-scratch header programming, streaming verification and cold CRC recovery;
- rejection of exact/offset scratch aliases before I/O;
- semantic rejection before output-capacity checks.

Each previously failing scenario was observed failing before its correction, then passed with the full suite. The exhaustive fault sweep includes warm and cold starts, a previously occupied target, all requested byte prefixes, and persistent/nonpersistent failed-operation outcomes.

## Deliberately deferred minor build issues

1. `PCFG_BUILD_EXAMPLES` is declared but examples currently follow the test switch.
2. The CMake project declaration requires a C++ compiler even for a C-core-only configuration.

## Boundaries

No physical NOR power-cut experiment, RT-Thread board execution, controller durability measurement, or block-device translation was validated. Host results prove behavior only under the emulator/adapter contract. CRC is not confidentiality, authenticity, or anti-rollback. GitHub-hosted CI execution is distinct from these local runs.
