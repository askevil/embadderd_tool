# Embedded Tools

Reusable, product-neutral embedded software components.

## Modules

- [`portable_cfg_store/`](portable_cfg_store/) — a C99, heap-free, power-loss-safe A/B configuration storage core with optional cJSON and RT-Thread adapters.
  - [English guide](portable_cfg_store/README.md)
  - [中文指南](portable_cfg_store/README_zh-CN.md)
  - [Storage format](portable_cfg_store/docs/storage-format.md)
  - [Porting guide](portable_cfg_store/docs/porting.md)
  - [Security model](portable_cfg_store/docs/security.md)

The default build is product-neutral and has no JSON/RTOS dependency. Optional adapters are disabled unless explicitly enabled.

Licensed under the [MIT License](LICENSE).
