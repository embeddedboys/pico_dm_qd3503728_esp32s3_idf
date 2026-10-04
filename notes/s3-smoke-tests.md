# ESP32-S3 基础能力测试

> `s3_smoke` 已通过 `/dev/ttyACM0` 烧录，并在 `/dev/ttyUSB0` 观察到双核心心跳和 PSRAM 余量稳定。

## TL;DR

- 工程 `tests/s3_smoke` 不依赖 PUD、LovyanGFX、CherryUSB 或显示驱动。
- 烧录默认使用 `/dev/ttyACM0`；`/dev/ttyUSB0` 只作为 UART0 日志口。
- 已确认 ESP32-S3 revision v0.2、内置 8 MB PSRAM、双核任务持续运行。
- 启动早期的 PSRAM 读写 PASS 和吞吐行尚未重新捕获，不能把吞吐数值写成已验证结果。

## 测试内容

工程报告芯片信息、reset reason、Flash/PSRAM 检测、512 KB PSRAM 分配与读写校验、
简单 `memset` 吞吐，以及固定到 CPU0/CPU1 的任务心跳。

## 已验证结果

测试条件：ESP-IDF 5.2.x，CPU 240 MHz，Octal PSRAM 80 MHz，115200 baud。

- `idf.py flash` 通过 `/dev/ttyACM0` 成功完成，写入 hash 校验通过，并自动硬复位。
- `esptool` 识别到 `ESP32-S3 (revision v0.2)` 和 `Embedded PSRAM 8MB`。
- `/dev/ttyUSB0` 收到 `core0`/`core1` 同步递增的 heartbeat。
- 同一运行固件也从 `/dev/ttyACM0` 收到日志，USB Serial/JTAG 可以在应用运行时保留。
- 运行观察值：`internal_free=365 KB`、`psram_free=8189 KB`，未观察到反复重启。

## 构建与烧录

从仓库根目录执行：

```sh
./scripts/s3-smoke.sh build
./scripts/s3-smoke.sh flash
./scripts/s3-smoke.sh monitor
```

`/dev/ttyUSB0` 是 CH340 物理 UART0，默认用于日志；smoke 工程将 TX 配到 GPIO42、RX
配到 GPIO44。ESP32-S3 ROM 支持 UART 下载；此板的 ROM 接线和进入下载模式过程仍需验证。
此前执行 `esptool` 返回 `No serial data received`，只证明那次未连接成功，不能单凭此错误
确认 BOOT 状态或接线。应用的 UART 引脚重映射也不保证 ROM 下载使用相同引脚。

只有确认 ROM UART 接线且板子已进入下载模式时，才使用：

```sh
ESP_FLASH_PORT=/dev/ttyUSB0 ./scripts/s3-smoke.sh flash
```
