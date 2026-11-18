# 可移植配置存储模块

[English](README.md)

这是一个面向嵌入式配置数据的 C99 存储核心。它把不透明字节存入两个固定槽位，先完整写入并校验备用槽，最后才写提交记录。因此在约定的 Flash 行为模型下，掉电恢复只能得到“上一份有效配置”或“完整的新配置”，不会选择半写数据。

## 核心思想

CRC 只能发现意外损坏，不能让原地覆盖具备原子性。如果保存过程中电源掉了，唯一一份配置可能已经被擦坏。A/B 双槽让旧槽保持不动：

```text
槽 A：上一份已提交配置
槽 B：擦除 → 写头 → 写正文 → 读回校验 → 最后写 commit
```

重启后分别检查两槽，再按序号选择最新的完整有效槽。

## 特性

- 核心为 C99，不动态分配内存
- 不依赖 RTOS、文件系统或 JSON 库
- 明确的 little-endian 媒体格式
- 头、正文、提交记录使用 CRC-32/ISO-HDLC
- A/B 轮换和 32 位回绕安全序号
- 支持业务语义校验回调
- 遇到未知新格式时可读取旧格式，但自动只读，防止降级固件覆盖新数据
- 恢复出厂也是一次普通 A/B 提交，而不是同时擦除两槽
- 带 NOR 模拟器和掉电/部分写入故障注入测试
- cJSON 和 RT-Thread 作为可选适配层，不污染核心

## 快速构建

```bash
cmake -S . -B build -DPCFG_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/pcfg_posix_memory_example
```

预期输出：

```text
loaded: brightness=42
sequence: 1
loaded default: brightness=10
```

## 接入要点

应用提供四个存储回调：`read`、`program`、`erase`、可选 `sync`，并配置两个互不重叠、互不共享擦除单元的槽位。

初始化后先调用 `pcfg_load()`：

- `PCFG_OK`：已有有效配置；
- `PCFG_NOT_FOUND`：介质为空，可以首次保存；
- `PCFG_UNSUPPORTED_VERSION`：只有新格式数据，旧固件不得覆盖；
- `PCFG_CORRUPT`：存在数据，但没有完整有效配置。

每次完整的 `load/save/factory_reset` 外围必须由应用持有同一把事务锁；只给单次 Flash I/O 加锁是不够的。

如果保存已经开始后返回失败，不能据此断定新配置一定不存在。请先重新 `pcfg_load()` 扫描，再决定是否重试。

## scratch 缓冲区

核心不申请堆内存。保存可以分块使用 scratch；加载时为了在语义校验失败或输出空间不足时保持调用方输出不变，会先把候选正文读入 scratch，因此 scratch 必须容纳允许加载的最大正文。

## 适用场景

- Wi-Fi、蓝牙和用户偏好
- 功能开关、报警阈值、低频算法参数
- 工厂默认配置、配置导入导出

## 不适用场景

- 高频计数器、日志、遥测：应使用日志式/磨损均衡方案
- 图片、模型、标定矩阵等大块二进制：应使用独立分区
- 需要保密或防篡改的凭据：CRC 不是安全机制
- RAM 特别小且无法承担完整配置缓冲的 MCU

核心只保存字节；JSON、CBOR、TLV、版本化结构体都可以作为上层编码。真正值得复用的是“A/B + 完整性校验 + 最后提交 + 默认值/迁移策略”，而不是只复用 JSON 这一种格式。

## 文档

- [存储格式](docs/storage-format.md)
- [移植说明](docs/porting.md)
- [安全边界](docs/security.md)

## 许可证

MIT。本模块是面向公开仓库的独立实现，不包含任何产品专用源码、地址、账号凭据或私有配置结构。
