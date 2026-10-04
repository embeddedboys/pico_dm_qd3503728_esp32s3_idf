# AGENTS.md — pico_dm_qd3503728_esp32s3_idf

PUD（Pico-USB-Display）设备端固件的 **ESP32-S3 移植**。本文件是接手须知：
仓库约定、踩过的坑、工具心得。协议细节在 `notes/`，现状与下一步在 `HANDOFF.md`。

## 0. 现状（2026-10-05）

| 项 | 状态 |
|---|---|
| 启动 | 完整启动到 `PUD device up: 480x320, decoder QOI, frame_max 32768` ✓ |
| 显示 | 开机画 QOI bootlogo（480x320，背光打开前画完）✓ 人眼已确认 ✓ |
| 烧写 | `scripts/flash-recover.sh` 一键升级，**不需要按 BOOT** ✓ |
| PSRAM | **关闭**（原因见 §2，这是硬约束，不是偏好）✗ 不能用 |
| 未验证 | 主机发帧后的实际显示效果（本轮只验证到"启动 + bootlogo"） |

## 1. 仓库约定

- **未经用户明确说"提交"，不要 `git commit` / `git push`。**
- 提交：`git commit -s`（带 `Signed-off-by`），摘要用 kernel 风格
  `子系统: 祈使句`（如 `esp32s3: turn octal PSRAM off`）；身份沿用仓库既有作者。
- **一轮只做一件事**；诊断用的探针代码**提交前必须清干净**（临时开关、
  面包屑、额外日志、被 sed 出来的格式改动都算）。
- 仓库里不写绝对主机路径 / IP / 私有设备名。
- `notes/` 是知识库，**按工作区的 developer-knowledge skill 维护**（首屏给结论、
  事实分级 ✓/✗、一文档一问题、信息预算 150 行、更新用合并重写、每次改都做**漂移检查**）；
  索引在 `notes/README.md`，通用结论放 `notes/general/`。别用 `cat >>` 追加"更新于某日"。

## 2. 硬件事实：PSRAM 与面板抢引脚（本项目最大的坑）

- 本板的 PSRAM **在芯片封装内**（ESP32-S3R8 那一类），而且是 **octal**。
- S3 的 octal 数据线在 IO_MUX 里**只有 GPIO33-37 这一组，且不可改路**：
  - IDF `components/soc/esp32s3/include/soc/io_mux_reg.h`：
    `FUNC_GPIO33_SPIIO4=4` / `FUNC_GPIO34_SPIIO5=4` / `FUNC_GPIO35_SPIIO6=4` /
    `FUNC_GPIO36_SPIIO7=4` / `FUNC_GPIO37_SPIDQS=4`
  - IDF `components/soc/esp32s3/include/soc/spi_pins.h`：
    *"there is only one set of GPIO pins which could be routed to FSPIIO4…FSPIIO7"*
- 而本仓 NOLOGO 板型把面板 **D8…D12 就接在 GPIO33-37** 上 ⇒
  `Bus_Parallel16::_init_pin()` 会把 16 根数据线全部 `gpio_matrix_out`，
  **抢掉 PSRAM 总线** ⇒ 之后任何 PSRAM/flash cache 访问都在等一次永远完不成的
  传输，CPU 停住（不是崩溃，是停滞！）⇒ 1.2 s 处被中断看门狗复位。
- 对照实验（决定性）：`CONFIG_SPIRAM=y` → 必卡死循环 ✗；
  `CONFIG_SPIRAM` 关闭 → 完整启动、连续 200 s 以上零复位 ✓。
- `CONFIG_SPIRAM_MODE_QUAD=y` 也试过：`E quad_psram: PSRAM ID read error:
  0x00ffffff … wrong PSRAM line mode` → `abort()` ⇒ 封装内**确实是 octal** ✗。
- **⇒ 这块板子上"8 MB 片内 PSRAM"与"16-bit 并口屏"互斥**，软件无解；
  想要两者兼得只能换 quad PSRAM（R2/N16R2）或无 PSRAM 的模组型号。
- 无 PSRAM 的代价：`PUD_MAX_TRANSFER` 只能 **32 KB**（64 KB 时
  `dram0_0_seg overflowed by 79464 bytes`）；`decoder.c` 的
  `DECODER_FRAME_ATTR` 是条件宏（有 PSRAM 才放 ext_ram 段）。
- 其它引脚占用：`TFT_PIN_RD = GPIO41` 与 CH340 的 TX（UART0 RX）**同一根线**
  ⇒ `cfg.pin_rd = -1` 让出；console TX=GPIO42 / RX=GPIO41。

## 3. 烧写：一键升级

```bash
./scripts/flash-recover.sh            # 探测 + 必要时请应用进下载态 + idf.py flash
./scripts/flash-recover.sh --probe    # 只报告
```

原理：本应用占用内部 USB-OTG（GPIO19/20），而它与 USB-Serial-JTAG **共用引脚**
⇒ 应用跑起来后 `/dev/ttyACM*` 消失、按 BOOT 变成常态。出路是让**应用自己配合**：
主机向 UART0 RX（CH340 TX → GPIO41）发 `PUD-BOOT\n`，`main/boot_request.c` 置
`RTC_CNTL_FORCE_DOWNLOAD_BOOT` 后重启 ⇒ 芯片进 ROM 下载态 ⇒ 照旧烧写。
细节与实测见 `notes/build-flash-recovery.md`。

注意：应用**挂在 USB 初始化之前**时 USB-Serial-JTAG 一直活着，`/dev/ttyACM0`
直接能烧（连 magic 都不需要）。

## 4. 绝对不要做的事

- ✗✗ **不要用 `esptool --before usb_reset`**：实测把主机的 xHCI 控制器打死
  （`xHCI host not responding to stop endpoint command` → `HC died`），
  板子和 CH340 一起从总线消失，需要物理重插 + 重绑控制器
  （`echo 1 | sudo tee /sys/bus/pci/devices/<BDF>/remove` 再 `rescan`）或重启主机。
- ✗ 不要用 `usb.core.find(...).reset()`：同样把设备踢下总线，不会自己回来。
- ✗ 不要 `pkill -f 'esptool[.]py'` 这种写法会把自己的 shell 一起杀掉
  （按 PID 杀，或把名字写成 `名字[.]py`）。
- ✗ 探针任务不要忙等：用 CCOUNT 忙等 + `taskYIELD` 的"心跳"会饿死
  IDLE0/IDLE1，约 200 s 后触发 **TASK_WDT** 复位（串口会点名 `beat0`/`beat1`）。

## 5. 调试方法论（工具心得）

- **串口死了就搬现场**：用 `RTC_NOINIT_ATTR` 在 RTC 内存里留"面包屑"
  （阶段号 + 计数器 + 关键寄存器快照），跨 WDT 复位存活，下一次启动用**一句**
  正常日志打出来。挂住的线程既不出 `ESP_LOG` 也不出 `esp_rom_printf`，
  这是唯一能看到"死前一刻"的办法。
- **启动最早期（t < ~1000 ms）的 `esp_rom_printf` 会被 console driver 吃掉/撕裂**
  （整行连同 `[BEAT …]` 一起消失）⇒ 关键信息放在 console 稳定之后再打。
- **时间基准要先验证**：`esp_rom_delay_us(1000)` 实测在 ~15 ms 里跑了 726 次
  （基准是坏的）✗；`xTaskGetTickCount()` 的读数又与 `vTaskDelay(200)` 明显生效
  相矛盾 ✗ ⇒ **没验过基准就不要下任何时间类结论**（次数可信、时间不可信）。
- **四行前置门**（改代码前先写下来）：已验证什么 / 仍未知什么 / 最小改动是什么 /
  生效判据是什么。
- **轮询就绪，不要盲等 sleep**：
  `while (g_pud_data.disp.xres == 0) vTaskDelay(pdMS_TO_TICKS(5));`
- 卡住时先问"是谁在等什么资源"，而不是急着改参数：本例是 PSRAM 总线
  （引脚被抢后 CPU 在等一次永不完成的 cache miss）。
- 一次只动一个变量，并且**每次都留判据**：本项目靠这条把 6 个假设逐个证伪，
  最后一个是"面板 DMA/时钟/触模/日志锁"，真凶是第 7 个（PSRAM 抢引脚）。
- 大段工具输出会淹掉关键信息（尤其 ninja/linker 的长命令行）⇒ 用
  `grep -oE "error: .*|Project build complete"` 这类过滤，别把 raw 输出全量倒出来。

## 6. 写代码的规矩（这块代码自己的约定）

- **面板只有一个写者**（decoder task）。`tft_async_video_flush()` **故意不
  `endWrite()`**：传输靠下一次 `setAddrWindow` 等 LCD_CAM `START` 清掉来同步，
  显式同步用 `tft_async_video_wait()`。
- `qoi_drawimg(xs, ys, xe, ye, data, size)` 要求宽度 ≤ `g_pud_data.disp.xres`，
  而该值要到 `pud_config_init()` 才填 ⇒ **画之前先轮询**（否则得到
  `W qoi: reject: width 480 > logical 0`）。
- bootlogo 用 `main/decoder/bootlogo_qoi.h`（从 Pico-USB-Display 生成的
  `include/bootlogo.h` 里抽的 QOI 分支），在背光打开**之前**画完。
- 注释写**为什么**和**实测判据**（含 ✗/✓），不写"是什么"。

## 7. 移植到别的平台（ESP32-P4）能继承什么

**可以直接搬**：

- 主机侧 PUD 协议**不变**（EP1 分帧、`PUD_CMD_*`、`GET_CAPS` 报 `frame_max`、
  `DECODER_TYPE`、序列号）；HANDOFF/notes 的文档结构；
- `scripts/flash-recover.sh` + `main/boot_request.c` 这套**一键进下载态**
  （P4 同样有 USB-OTG 与 USB-Serial-JTAG 共用引脚的同类问题）；
- decoder 分层（`decoder.c` 调度 + `decoder_qoi.c` 解码，坐标/窗口参数化）；
- §5 的调试方法论（面包屑、证据分级、四行门）。

**必须重想**：

- P4 有**硬件 JPEG 编解码** ⇒ "CPU 软解越强越好"这个前提变了，压缩算法/码流的
  取舍要重新算（可能反而该把 CPU 让给别的事）；
- P4 是 **USB High-Speed 480 Mbps** ⇒ 带宽不再是天花板（S3 与 RP2350 都是
  全速 12 Mbps，这也是本移植"没有新信息"的原因）；
- 面板接口**别照搬**"P4 走 MIPI-DSI/RGB、所以 LCD_CAM 经验作废"这个说法 —— 那是本
  文件早期写的，**已被 P4 上的实测否定** ✗：P4 有 `SOC_LCDCAM_I80_LCD_SUPPORTED`
  ✓，同一块 i80 屏用 IDF `esp_lcd_panel_io_i80` 就跑起来了。**要换的是库
  （LovyanGFX → IDF `esp_lcd`），不是总线**。P4 那边实测到的差异（都已上板确认）：
  - 写时钟只能整数分频、基数 80 MHz ⇒ 请求值被**向下取整到 80/n**，本项目定档 40 MHz
    （80 MHz 显示不对 ✗）；
  - 每"窗口 + 一笔"有 ≈90~120 µs 固定开销（IDF `tx_param` 的结构性行为）⇒ **能整帧
    就整帧**（40 行一条带只剩 36.4 MB/s，整帧 77.6 MB/s）；
  - **P4 的 i80 能直接读 PSRAM**（实测与内部 RAM 源同速）⇒ 本文件 §2 那条"PSRAM 与
    面板互斥"是 **S3 独有**的引脚冲突，P4 上不存在（P4 有 32 MB PSRAM，无冲突）；
  - **JPEG 用硬件解码**（1.76 ms/帧 @480x320），别再走软解那套取舍。

**最重要的一条教训**：**先确认内存（PSRAM）占用了哪些引脚，再接面板总线。**
这个项目全部的时间都花在没先做这一步上。
