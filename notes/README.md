# ESP32-S3 PUD 固件知识库

本目录记录 `pico_dm_qd3503728_esp32s3_idf` 的架构、协议、构建烧录、硬件验证和已知问题。
只记录已验证结论；推测和未完成工作明确标注。

## 维护约定

- 文档用中文，命令、路径和标识符保留英文。
- 代码/配置和可复现实验优先于旧文档；发现漂移时以当前代码和实测为准。
- 一篇文档只回答一个主要问题；历史数据保留测试条件，不记录过程日志。

## 文档索引

| 文档 | 内容 |
| --- | --- |
| [architecture.md](architecture.md) | USB、解码、显示和触摸数据流 |
| [build-and-flash.md](build-and-flash.md) | ESP-IDF 环境、构建、烧录和复位 |
| [usb-protocol.md](usb-protocol.md) | 当前 ESP32-S3 设备侧协议实现和 USB 标识 |
| [display-touch.md](display-touch.md) | ILI9488、FT6236、TSC2007 和坐标空间 |
| [performance.md](performance.md) | 带宽、FPS 和测试条件 |
| [pitfalls.md](pitfalls.md) | 已定位的问题、踩坑和排查顺序 |
| [s3-smoke-tests.md](s3-smoke-tests.md) | 独立 ESP32-S3 基础能力测试 |
| [todo.md](todo.md) | 尚未完成的工作 |

协议字段的跨仓库权威镜像是 `PUD-kernel-drivers/notes/usb-protocol.md`；修改协议时需要同步两边。
