# 交接文档：PUD 移植到 ESP32-S3

> 当前优先级是独立 `s3_smoke` 基础设施验证；PUD 显示和触摸回归暂时暂停。

最后核对：2026-10-05。当前板上运行的是 smoke 固件。

## 当前结论

- `/dev/ttyACM0` 是当前已验证可用的 USB Serial/JTAG 烧录口。
- `/dev/ttyUSB0` 是 CH340 连接的物理 UART0，当前用于日志；已观察到 smoke 固件日志。
- smoke 固件也通过 `/dev/ttyACM0` 输出日志；看到此接口不能单独判定为 ROM 下载态。
- smoke 固件在 ESP32-S3 双核 240 MHz、Octal PSRAM 配置下持续运行，没有观察到重启。
- PUD 固件仍保留 ILI9488 16 位总线、FT6236 支持和 TSC2007 支持；没有改成 8 位总线。

## 硬件与端口

| 项目 | 当前结论 |
| --- | --- |
| 芯片 | ESP32-S3，实测 revision v0.2，内置 8 MB PSRAM |
| 烧录 | `/dev/ttyACM0`，USB Serial/JTAG；`idf.py flash` 自动复位并校验 |
| 物理串口 | `/dev/ttyUSB0`，CH340，UART0，115200 baud |
| UART0 TX/RX | smoke 配置为 TX GPIO42、RX GPIO44 |
| 显示 | ILI9488，LovyanGFX，16 位并口，逻辑尺寸 480x320 |
| 触摸 | FT6236 保留；TSC2007 地址 `0x48`，共享原 FT6236 的 SDA/SCL/IRQ 引脚 |

物理 UART 下载在 ESP32-S3 ROM 协议层面存在，但必须手动按 BOOT+RESET 进入 UART
下载模式，且须确认 TX/RX 接线符合 ROM UART 引脚要求；应用 UART GPIO 配置不代表 ROM 配置。
此前用 `esptool` 访问 `/dev/ttyUSB0`，结果为
`No serial data received`；因此“可以通过 UART 下载”仍是未完成的硬件验证，不能作为
默认烧录流程。

## 独立 smoke 工程

位置：`tests/s3_smoke`，脚本：`scripts/s3-smoke.sh`。

测试内容：

- 芯片、revision、CPU、Flash、reset reason 信息；
- Octal PSRAM 分配、读写校验和简单 `memset` 吞吐；
- CPU0/CPU1 任务调度和周期性内存心跳。

配置为 240 MHz、Octal PSRAM 80 MHz、16 MB Flash header、双核 FreeRTOS。

已验证：

- `./scripts/s3-smoke.sh flash` 通过 `/dev/ttyACM0` 成功构建并烧录；
- `esptool` 识别到 ESP32-S3、内置 8 MB PSRAM，写入内容 hash 校验通过；
- `/dev/ttyUSB0` 收到 `s3_smoke` heartbeat，`core0` 和 `core1` 同步递增；
- 观察到 internal free 约 365 KB、PSRAM free 约 8189 KB，无连续重启。

启动早期的 chip-info/PSRAM PASS 行在本次监听开始前已经错过，因此 PSRAM
读写校验和吞吐数值仍应重新捕获；当前只能确认运行期 PSRAM 余量和双核任务正常。

复现命令：

```bash
source ~/esp/esp-idf/export.sh
./scripts/s3-smoke.sh build
./scripts/s3-smoke.sh flash                 # 默认 /dev/ttyACM0
ESP_MONITOR_PORT=/dev/ttyUSB0 ./scripts/s3-smoke.sh monitor
```

脚本支持显式覆盖端口，但 `/dev/ttyUSB0` 烧录只有在板子已进入 UART ROM 下载模式后
才应尝试：

```bash
ESP_FLASH_PORT=/dev/ttyUSB0 ./scripts/s3-smoke.sh flash
```

## PUD 当前状态

| 阶段 | 状态 | 说明 |
| --- | --- | --- |
| 环境与 LVGL 硬件基线 | 已完成 | 原厂 demo 曾验证显示和触摸 |
| CherryUSB 枚举与协议查询 | 已完成 | VID/PID `303a:3503`，协议 v2，QOI |
| QOI/ILI9488 图像通路 | 待重新验证 | 旧黑屏问题曾定位为旋转后几何使用不一致 |
| FT6236 / TSC2007 EP4 | 待重新验证 | 两套驱动代码均保留 |
| 内核驱动联调 | 待开始 | 需要确认驱动端 `303a:3503` 支持 |

历史显示、TSC2007 EP4 和性能基线见 [显示与触摸](notes/display-touch.md)、
[性能实测](notes/performance.md)。这些结果不证明当前 PSRAM/双核版本已通过回归。

当前 PUD 启用 Octal PSRAM 后曾发生 WDT 反复重启，日志已确认 PSRAM 初始化和内存
测试成功，并进入 `app_main()`/`pud_init()`；具体阻塞点尚未定位。初始化阶段日志已保留。
smoke 心跳正常不能证明 PUD 初始化问题已解决。

代码当前使用旋转后的 `480x320` 逻辑几何填充能力报告和 QOI 宽度校验，
`PUD_MAX_TRANSFER=65536`，解码任务固定 CPU1。这一版本仍需重新烧录回归。
面板硬件保持 16 位并口，不能通过软件改成 8 位并口；引脚与 PSRAM 冲突不是已证实结论。

## PUD 关键决策

- USB 栈使用 CherryUSB v1.5.2，设备 VID/PID 为 `0x303A:0x3503`。
- EP1 使用 3 个帧槽；槽忙时不重新武装 endpoint，采用 NAK 背压。
- v1 只启用 QOI（`DECODER_TYPE=3`），帧槽放在 ESP32-S3 Octal PSRAM。
- UART0 TX 使用 GPIO42；不要将屏幕 RD 所在 GPIO41/GP17 当作串口线。
- `CONFIG_PUD_LVGL_DEMO` 保留为可选硬件对照路径。

## 下一步

1. 重新捕获 smoke 固件启动头，记录 chip/Flash/PSRAM/PSRAM throughput PASS。
2. 保持 PUD 暂停；如继续 UART ROM 下载实验，先核对 ROM 引脚接线，再手动 BOOT+RESET。
   默认烧录仍使用用户指定的 `ttyACM0`。
3. 恢复 PUD 时，先通过 `ttyACM0` 烧录并定位初始化 WDT，再用屏幕验证 ILI9488 图像通路。
4. 最后验证 FT6236 和 TSC2007 的 EP4 按下、移动、释放事件。

## 工作区约束

- 不要删除或覆盖现有未提交修改、`notes/`、`tests/` 和失败构建留档。
- 不要把 ttyUSB0 当作默认烧录口；默认使用 ttyACM0。
- 不要改变 ILI9488 的 16 位硬件总线。
- 不要提交本机路径、代理、口令或临时设备状态。
