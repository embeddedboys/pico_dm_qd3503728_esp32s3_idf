# 构建、烧写与"烧不回去"时的三条恢复路径

> 正常路径是一条命令：`./scripts/flash-recover.sh`（探口 → 必要时请应用自己进 ROM
> 下载态 → `idf.py flash`），**实测不需要按 BOOT** ✓。物理 UART0 那条路本板不通，
> 原因是接线与固件监听脚不一致（不是"没接"）。

## TL;DR

- 构建：IDF **v5.2.x** + `idf.py set-target esp32s3`；`idf.py build` 即可。
- 烧写口：**`/dev/ttyACM0`**（USB Serial/JTAG）✓ 已验证；`/dev/ttyUSB0` 是 CH340 的
  物理 UART0（日志口，115200），**不是**默认烧写口。
- 应用态 VID/PID = **`303a:3503`**；`303a:1001` 是 USB Serial/JTAG，
  **看到它不能判定芯片在 ROM 下载态**（smoke 固件运行时它也在）。
- 一键恢复靠应用配合：往 UART0 RX 发 `PUD-BOOT\n`，`main/boot_request.c` 置
  `RTC_CNTL_FORCE_DOWNLOAD_BOOT` 后重启进 ROM 下载态 ⇒ `/dev/ttyACM0` 回来照烧 ✓。

## 命令

```bash
source ~/esp/esp-idf/export.sh          # IDF v5.2.x
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash

./scripts/flash-recover.sh              # 探口 → 必要时发 magic → idf.py flash
./scripts/flash-recover.sh --probe      # 只报告哪个口可用
./scripts/flash-recover.sh -- -b 460800 flash
```

为什么能救**正在崩溃/重启的**固件：`boot_request.c` 的监听在 `app_main` **第一行**，
落在崩溃点之前 ⇒ 每个启动周期里都有约 1 s 的窗口能收到 magic ✓。
实测（板上跑的正是无限重启那版）：发 magic 后 `--before no_reset chip_id` 从失败变成
读到 MAC ✓，日志里出现 `download request:` ✓。

## 三条恢复路径（按可靠性排序）

| 路径 | 是否可靠 | 说明 |
|---|---|---|
| 1. 应用自己进下载态（`flash-recover.sh` 发 magic）| ✓ 实测可靠、零硬件改动 | 崩在 USB 里时失效；监听在 app_main 最早期所以覆盖绝大多数崩溃 |
| 2. 手指：BOOT 按住 + 按一下 RESET + 松开 ⇒ 再烧 | ✓ 最后兜底 | 不需要任何固件配合 |
| 3. 抖 DTR/RTS 自动复位（CH340 → EN/GPIO0 的两管两阻）| ✗ 本板**未接线** | 接上线后最省事（正式板都带）；`flash-recover.sh` 探不到口时已是"轮询等口"而非 sleep |

**本板物理 UART0 那条路现在什么状态**：线接在 **GPIO41**（CH340 的 TX），
`CONFIG_ESP_CONSOLE_UART_RX_GPIO=41` ✓ 一致；同时面板的 RD 已经让出
（`cfg.pin_rd = -1`）✓。**但 UART ROM 下载从未实测成功过**：早期不通是"两错叠加"
（固件听 44、线在 41，且 RD 占着 41），修好之后没有重测，`esptool -p /dev/ttyUSB0`
的历史结果是 `No serial data received` ⇒ **别把它当默认流程**，默认仍走 `/dev/ttyACM0`。

## 边界与陷阱

- **不要用 `esptool --before usb_reset`**：见
  [general/esptool-usb-reset-kills-xhci.md](general/esptool-usb-reset-kills-xhci.md)。
- `idf.py flash` 成功 ≠ 应用在跑：烧完要么看日志，要么用 `303a:3503` 的 caps 查询验收。
- 改 `sdkconfig.defaults` 时留意 `CONFIG_SPIRAM`：打开它 = 无限重启（见
  [psram-pin-conflict.md](psram-pin-conflict.md)）。
- 应用态下 `/dev/ttyACM*` 会不会消失**尚未定论**：见
  [general/esp32s3-usb-otg-vs-serial-jtag.md](general/esp32s3-usb-otg-vs-serial-jtag.md)。

## 相关

- [psram-pin-conflict.md](psram-pin-conflict.md)、[usb-protocol.md](usb-protocol.md)
