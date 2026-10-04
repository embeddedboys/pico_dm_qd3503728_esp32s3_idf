# smoke 工程：与 PUD 无关的最小硬件自检

> `tests/s3_smoke` 不依赖 PUD / LovyanGFX / CherryUSB / 显示驱动，用来在
> "PUD 起不来"时回答一个问题：**板子、双核、Flash、PSRAM 本身是不是好的**。

## TL;DR

- 结论：ESP32-S3 **revision v0.2**、**内置 8 MB PSRAM**、双核任务持续运行、无反复重启 ✓。
- 它**不接管 USB-OTG** ⇒ 运行时 USB-Serial/JTAG 仍在，`/dev/ttyACM0` 可直接烧 ✓。
- 它的 PSRAM 测试能过，**不代表** PUD 也能开 PSRAM：PUD 的 PSRAM 与面板数据线冲突
  （见 [psram-pin-conflict.md](psram-pin-conflict.md)）——那是**引脚**问题，不是 PSRAM 坏。
- 启动早期的 PSRAM 读写 PASS 与吞吐行**未保存**，所以吞吐数值不作为已验证结果 ✗。

## 命令

```sh
./scripts/s3-smoke.sh build
./scripts/s3-smoke.sh flash          # 默认 /dev/ttyACM0（USB Serial/JTAG）
./scripts/s3-smoke.sh monitor
ESP_FLASH_PORT=/dev/ttyUSB0 ./scripts/s3-smoke.sh flash   # 仅当 ROM UART 接线已确认
```

## 测试内容

芯片/revision/CPU/Flash/reset reason 信息；512 KB PSRAM 分配与读写校验；简单 `memset`
吞吐；固定到 CPU0/CPU1 的任务心跳（各核同步递增）。

## 实测结果

测试条件：IDF 5.2.x、CPU 240 MHz、Octal PSRAM 80 MHz、115200 baud。

- `idf.py flash` 经 `/dev/ttyACM0` 完成，写入校验通过并自动硬复位 ✓。
- `esptool` 识别 `ESP32-S3 (revision v0.2)`、`Embedded PSRAM 8MB` ✓。
- `/dev/ttyUSB0`（CH340，UART0）看到 `core0`/`core1` 心跳同步递增 ✓；
  同一固件的日志也能从 `/dev/ttyACM0` 读到 ⇒ **运行时 Serial/JTAG 可保留** ✓。
- 运行观察值：`internal_free ≈ 365 KB`、`psram_free ≈ 8189 KB`，未观察到反复重启 ✓。

## 边界与陷阱

- 冒烟通过**只**证明基础设施好，不证明 PUD 路径（显示/触摸/USB 协议）可用。
- `/dev/ttyUSB0` 是 CH340 的 UART0；smoke 的 `sdkconfig.defaults` 把 TX 配 GPIO42、
  RX 配 **GPIO41**（与主工程一致）。⚠️ 仓库里那份**本地生成的 `sdkconfig`**（被
  `.gitignore` 忽略）写的是 RX=44，与 defaults 不一致 ⇒ 重建前先按 defaults 重新生成，
  否则日志会走到没人接的脚上。
  `esptool -p /dev/ttyUSB0` 曾返回 `No serial data received` —— 这只说明那次没连上，
  不能据此断定 ROM 接线或 BOOT 状态；应用里的 UART 重映射也不代表 ROM 下载用同一组脚。
