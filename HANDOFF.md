# HANDOFF —— ESP32-S3 移植交接

> **当前状态：这块板子上的 PUD 固件能完整启动并画出 QOI bootlogo（人眼确认 ✓），
> 一键升级不需要按 BOOT ✓；唯一没做的验证是"主机发帧之后的实际显示效果"。**
> 本板 PSRAM **必须关闭**（原因见 [notes/psram-pin-conflict.md](notes/psram-pin-conflict.md)）。

## 1. 现状

| 项 | 状态 |
|---|---|
| 启动 | `PUD device up: 480x320, decoder QOI, frame_max 32768` ✓ |
| 显示 | 开机在背光之前画完 QOI bootlogo（480x320）✓ 人眼确认 |
| 烧写 | `./scripts/flash-recover.sh` 一键升级，**不需要按 BOOT** ✓ |
| PSRAM | **关闭**（硬约束，非偏好）✗ 8 MB 用不了 |
| 解码 | QOI（`DECODER_TYPE=3`），decoder task 固定 **CPU1**，3 个 32 KB 帧槽 |
| 触摸 | TSC2007 EP4 已验（按下/释放/坐标）✓；FT6236 代码保留但真机未回归 ✗ |
| 未验证 | 主机发帧后的显示效果、caps 的完整回读、内核驱动联调 |

板的实测知识库：`notes/`（索引 `notes/README.md`）；必须遵守的约束：`AGENTS.md`。

## 2. 硬件与端口

| 项 | 值 |
|---|---|
| 芯片 | ESP32-S3，revision **v0.2**，240 MHz 双核，内置 **8 MB octal PSRAM（不可用）** |
| 显示 | ILI9488，LovyanGFX `Bus_Parallel16`，16 位 8080，逻辑 `480x320`（`TFT_ROTATION=1`）|
| 触摸 | FT6236(0x38) / TSC2007(0x48)，共用 SDA/SCL/IRQ = GPIO7/8/5，10 ms 轮询 |
| 烧写口 | **`/dev/ttyACM0`**（USB Serial/JTAG，应用态 VID/PID `303a:3503`）|
| 日志口 | `/dev/ttyUSB0`（CH340，UART0）；console TX = **GPIO42**（RX 配置 41，面板 RD 已让出）|
| 独立自检 | `tests/s3_smoke` + `scripts/s3-smoke.sh`（见 [notes/smoke-tests.md](notes/smoke-tests.md)）|

## 3. 构建与烧写

```bash
source ~/esp/esp-idf/export.sh          # IDF v5.2.x
idf.py build
./scripts/flash-recover.sh              # 探口 → 必要时发 magic → idf.py flash
./scripts/flash-recover.sh --probe      # 只报告
```

细节（三条恢复路径、为什么 UART 那条不通、别用 `esptool --before usb_reset`）见
[notes/build-flash-recovery.md](notes/build-flash-recovery.md)。

## 4. 关键决策（改前先读 notes）

- USB 栈用 **CherryUSB v1.5.2**（`components/`，vendored，未改动；CMake 侧只加了
  IDF ≥5.2 的 `DRAM_DMA_ALIGNED_ATTR` 适配）。
- EP1 **3 个帧槽 + 背压**：槽满不重新武装 endpoint，让主机等（不丢帧）。
  解码**不在 USB 中断里做**（会 HardFault），全部由 decoder task 承担。
- 只启用 **QOI**；帧槽在内部 RAM ⇒ `PUD_MAX_TRANSFER` = **32 KB**（64 KB 会
  `dram0_0_seg overflowed`），`frame_max` 由 caps 上报，主机自适应。
- Console TX 走 **GPIO42**；**GPIO41/GP17 是面板 RD**，当前已 `cfg.pin_rd = -1` 让出。
- `CONFIG_PUD_LVGL_DEMO`（`main/Kconfig`）保留原厂 LVGL demo 作为硬件对照路径，
  与 PUD 路径二选一。

## 5. 下一步

1. **验证主机发帧后的显示效果**（唯一的空白）：接主机 → `GET_CAPS` 回读全部字段 →
   发一帧局部刷新 + 一帧全屏，确认画面与 `dropped == 0`。
2. 增加 **decoder/EP1 计数器查询**（`submitted/drawn/dropped`），便于无调试器验收。
3. **FT6236 真机回归**（当前只有 TSC2007 验过）。
4. 评估**减少全屏分带次数**并重测 FPS（当前全屏 15 分带只有 ~6 FPS，瓶颈是每带开销，
   见 [notes/performance.md](notes/performance.md)）。
5. 内核驱动联调（确认驱动端支持 `303a:3503`），并同步两侧协议文档。
6. 可选：给 CH340 的 DTR/RTS 接上 EN/GPIO0（两管两阻）⇒ 连"应用崩在 USB 里"也能自动恢复。

## 6. 工作区约束

- **未经明确指令不要 `git commit` / `git push`**；提交用 `git commit -s`，
  摘要 kernel 风格 `子系统: 祈使句`（身份沿用仓库既有作者）。
- 不要删除或覆盖未提交修改、`notes/`、`tests/`；**探针代码提交前清干净**。
- 不要把 `/dev/ttyUSB0` 当默认烧写口；不要动 ILI9488 的 **16 位**硬件总线
  （软件改不成 8 位并口）。
- 不提交本机路径、代理、口令、临时设备状态。

## 7. 为什么这个移植停在这里（给下一任的判断）

12 Mbps **Full-Speed USB** 是 PUD 整条路线共同的天花板（RP2350 版也一样），
本移植在带宽上提供不了新信息；而它本来可能领先的地方（LCD_CAM i80 纯 DMA 写屏 ⇒
多帧流水线）恰好被"**PSRAM 与面板抢引脚**"钉死。继续投入的价值低于换平台 ——
所以下一步是 ESP32-P4（硬件 JPEG、HS USB、32 MB PSRAM 无引脚冲突），
能继承什么见 `AGENTS.md` §7。
