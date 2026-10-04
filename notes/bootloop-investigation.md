# 无限重启定位（进行中）

板子：ESP32-S3 + Pico_DM_QD3503728（NOLOGO 板型，ILI9488 16-bit i80）。
症状：应用跑到 `decoder task up, backlight 100%` 之后**永久静默**，随后被
**中断看门狗**复位（`esp_reset_reason()==5`），周期约 10.7 s。

## 1. 复位不是"循环重启"，是"挂住 + 看门狗"

实测（`stty -F /dev/ttyUSB0` + `cat` 抓 25 s，探针版 IWDT=3000 ms）：

- 一次启动只出现**一个**启动块，`t≈1.2 s` 之后到下一次复位之间没有任何输出；
- 复位原因是 `5`（`ESP_RST_INT_WDT`）。INT_WDT stage0 在 3 s，
  硬件 stage1 在 9 s 复位 —— 而复位发生在挂住之后约 **9 s** ⇒
  **stage0 的中断服务程序根本没跑起来** ⇒ 出问题的那个核**中断被关了** ✓。

## 2. 卡死点在 `ESP_LOGI("init: decoder ready")` **里面**（不是显示通路）

串口在挂住之后既不回 `ESP_LOG` 也不回 `esp_rom_printf`，所以现场必须存在
不依赖串口的地方：`main/diag.c` 的 `RTC_NOINIT` 面包屑（跨复位存活，
下一次启动用一行 `ESP_LOGW` 打出来，那时串口是好的）。

实测一行：

```
W bc: cyc=49 rs=5 stage=1 b0=726 b1=1074 ml=1/0 df=1/1 dl=1/1 br=0/0 t0=3 t1=3 us=862920
```

| 字段 | 值 | 含义 |
|---|---|---|
| `rs` | 5 | 上一轮由中断看门狗复位 |
| `ml` | **1/0** | 主任务**进了** `ESP_LOGI("init: decoder ready")`，**没出来** ✓ |
| `df` | 1/1 | `pud_params_flush_display()` 正常返回 ✓ |
| `dl` | 1/1 | 解码任务的 `ESP_LOGI("decoder task up…")` 正常返回 ✓ |
| `br` | 0/0 | 本模块（bootreq）被关掉的那一轮，符合预期 |
| `t0/t1` | 3 / 3 | 心跳任务看到的 `xTaskGetTickCount()` |
| `us` | 862920 | 心跳任务最后看到的 `esp_timer_get_time()` |

**推论**：卡点是**写 UART0 TX 这条路**（凡是写 UART 的都卡住：主任务的
`ESP_LOGI`、两个核的心跳 `esp_rom_printf`）。这块板子的显示初始化、触模、
解码任务的 flush 都已经正常返回 ✓ —— 所以之前"第一次写面板的 DMA 等不到
完成"的说法**被证伪** ✗：这个 build 里根本没有 bootlogo，那段代码没被调用。

## 3. 已证伪的假设（每个都是一次烧写换来的）

| 假设 | 实验 | 结果 |
|---|---|---|
| 帧缓冲/绘制缓冲在 PSRAM | 查代码：是 `static` | ✗ |
| 写时钟太快 | `freq_write` 50 → 20 MHz | ✗ 症状一样 |
| 触模驱动阻塞 | 跳过 `indev_driver_init` | ✗ 卡点只是下移 |
| 分配落进 PSRAM | `ALWAYSINTERNAL` 16 KB → 128 KB | ✗ |
| `cfg.pin_rd = -1` 让 LCD_CAM 配置不全 | — | 未测（现在看也没必要） |
| 第一次面板 DMA 写入自旋死等 | 本 build 没有 bootlogo | ✗ 证伪 |
| **我的一键升级模块（bootreq）抢了 console 的 UART0** | `diag_disable_bootreq = true` 整轮 | ✗ **无罪**（`ml=1/0` 一模一样） |

## 4. 第三轮实测（寄存器快照，core1 心跳持续刷新 ⇒ 值=复位前一刻）

```
W bc: cyc=59 rs=5 stage=1 b0=106 b1=849 ml=1/0 df=1/1 dl=1/1 br=6/5
      t0=23 t1=23 us=0 | cc=120781139 span=49200324
      g42=0000000c g41=00000100 cam=00000000/00000000 uart=03500000/e000c000
```

| 读数 | 解释 | 结论 |
|---|---|---|
| `uart=03500000/e000c000` | `clk_conf.tx_sclk_en` 已置位；`status.txfifo_cnt=0`（FIFO **空**） | **UART TX 没有卡住/没满** ⇒ "UART 排水停止"**被证伪** ✗ |
| `g42=0000000c` | GPIO42 的 `func_out_sel` 非 0，仍被路由 | console TX 的路由**没被显示初始化抢走** ✗ |
| `g41=00000100` | `func_sel=0`（RX 本来就不需要 out 路由），`oen_sel=1` | 正常 |
| `cam=00000000/00000000` | `LCD_CAM.lcd_clock.val == 0`、`lcd_user.val == 0` | **LCD_CAM 根本没在跑** ✓ 新疑点（见下） |
| `b0=106 b1=849` | 两个核的心跳在挂住期间**一直在跑** | **系统是活的** ⇒ 卡住的主任务更像**阻塞在锁上**，不是自旋 ✗ 推翻上一轮的"全停" |

### 由此得到的下一批问题

1. **`LCD_CAM.lcd_clock.val == 0` 是重大异常**：说明 `lcd.init()` 报"ready"的
   那 280 ms 里，LCD_CAM 的时钟使能位根本没被写上（或外设被 gate 掉、
   寄存器读出恒 0）。若如此，面板从来没被真正时钟过 ——
   `display ready` 只是"函数返回了"，不是"面板收到了"。要顺着
   `esp_lcd_new_i80_bus()` 的返回值 + `periph_module_enable(PERIPH_LCD_CAM)`
   查（当前 `LovyanGFX Bus_Parallel16.cpp:98` **丢弃了返回值**）。
2. **主任务更像阻塞而非自旋**：UART 硬件是好的、FIFO 是空的，
   所以它卡在写 UART 之前 —— 优先怀疑 **`ESP_LOG` 的互斥量**被谁一直握着
   （或 logger 的 `xSemaphoreTake` 依赖的 tick 出了问题，见 3）。
3. **时间基准仍然不可信，必须先修掉再下时间类结论** ✓：
   本轮 `t0=t1=23`（tick 冻结在 230 ms？）却与"`vTaskDelay(200)` 明显生效
   （860→1060 ms）"矛盾；CCOUNT 计数与迭代数也不自洽（849 次 × 1.2 M 周期
   ≠ span 49.2 M）。**次数可信，时间不可信。**

## 5. 下一步（未做，留给下一轮）

1. **判断是"UART 硬件不再排水"还是"日志/锁死锁"**：`us=862920` 与
   `t=3` 说明这一轮的探针**时间基准本身有问题**（`esp_rom_delay_us(1000)`
   在 ~15 ms 内跑了 726/1074 次 ⇒ 那个 delay 没在延时），所以"心跳停了"
   这个结论**不可信**，需要换成不依赖 ROM delay 的计数方式重测。
2. 在 `tft_driver_init()` 之后把 **GPIO42（console TX）的 GPIO matrix
   `func_out_sel_cfg`、`LCD_CAM.lcd_clock/lcd_user` 存进面包屑**，
   看显示初始化有没有把 UART0 TX 的路由抢走（这是"UART 排水停止"最像的
   硬件原因）。
3. 把 `ESP_LOGI("init: decoder ready")` 换成 `esp_rom_printf` 单点替换，
   看是否仍然卡 —— 分辨"logger 锁"与"UART 硬件"。注意：`[RAW] decoder_init
   returned`（`esp_rom_printf`）是好的 ✓，紧接着的 `ESP_LOGI` 卡住，
   再往后的 `[RAW]` 也没出来 ⇒ 两种机制**在同一瞬间一起死**。

## 5. 诊断代码清单（都要在提交前清掉）

- `main/diag.c`、`main/include/diag.h`（RTC 面包屑，本轮新增）
- `main/main.c`：`diag_beat_task` / `diag_beat_start` / `esp_rom_printf` / `PUD_DIAG_STAGE`
- `main/pud.c`：`esp_rom_printf` 探针 + `PUD_DIAG_MARK(main_log_*)`
- `main/decoder/decoder.c`：`PUD_DIAG_*` 探针
- `main/boot_request.c`：`diag_disable_bootreq`（现为 `false`）+ `bootreq_loops/nowait`
- `main/pud.c`：`pud_skip_touch_diag = true`（触模仍被跳过）
- `sdkconfig`：`CONFIG_ESP_INT_WDT_TIMEOUT_MS=3000`、
  `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=131072`
- `main/LGFX_MakerFabs_Parallel_S3.hpp`：`freq_write=20000000`、`pin_rd=-1`

## 6. 一键升级（与本故障无关，已实测可用 ✓）

`scripts/flash-recover.sh`：探 `/dev/ttyACM*` → 再探 `/dev/ttyUSB*` →
必要时向 `/dev/ttyUSB*` 发 `PUD-BOOT\n` 让应用自己进 ROM 下载态。实测
3 次 `Hash of data verified` + `Done` ✓，**不需要按 BOOT** ✓。

顺带发现：**应用挂在 USB 初始化之前时，USB-Serial-JTAG 一直活着**，
所以这个状态下 `/dev/ttyACM0` 直接就能烧 ✓（连 magic 都不需要）。

---

# 结论（2026-10-05）：根因是 octal PSRAM 抢了面板的 D8-D12

## 判据（全部实测）

| 实验 | 结果 |
|---|---|
| `CONFIG_SPIRAM=y`（OCT，原状） | 启动到 1.2 s 卡死 → INT_WDT 复位，无限循环 ✗ |
| **`CONFIG_SPIRAM` 关掉** + `PUD_MAX_TRANSFER` 64→32 KB | **完整启动** ✓：`init: decoder ready` → `step: usb ready` → `PUD device up: 480x320, decoder QOI, frame_max 32768` ✓✓ |
| PSRAM 关掉后的长跑 | **200 s 以上没有任何复位** ✓（抓到的第一段就是它的尾巴） |
| `CONFIG_SPIRAM_MODE_QUAD=y`（想两者都要） | 启动**完全没有输出** ✗，而且 USB-Serial-JTAG 也不再应答 ⇒ 模块的 PSRAM **确实是 octal 接线** ✓ |
| `cfg.pin_rd` / 时钟 / 触模 / DMA / logger 等假设 | 全部被证伪 ✗（见上文） |

## 为什么抢引脚（Espressif 自己的头文件，本机 IDF 5.2 实查）

```
components/soc/esp32s3/include/soc/io_mux_reg.h
  #define FUNC_GPIO33_SPIIO4   4      #define FUNC_GPIO36_SPIIO7   4
  #define FUNC_GPIO34_SPIIO5   4      #define FUNC_GPIO37_SPIDQS   4
  #define FUNC_GPIO35_SPIIO6   4
components/soc/esp32s3/include/soc/spi_pins.h
  // However, there is only one set of GPIO pins which could be routed to
  // FSPIIO4, FSPIIO5, FSPIIO6, FSPIIO7.
```

⇒ S3 的 **内部 flash/PSRAM octal 数据线在 IO_MUX 里只有 GPIO33-37 这一组，不可改路**。
所以带 octal PSRAM 的模组（如 WROOM-1-N16R8）**物理上就占掉 GPIO33-37**；
而本仓 NOLOGO 板型的 `TFT_PIN_D8..D12 = 33,34,35,36,37` 正在这 5 个脚上。
`Bus_Parallel16::_init_pin()` 会把 16 根数据线全部 `gpio_matrix_out`，
于是 PSRAM 总线被抢 ⇒ 任何 PSRAM/flash cache 访问永远等不到数据 ⇒ CPU 停住 ✗。

**这不是板子设计的锅，是"模组选了 octal PSRAM"的锅**：想要 16-bit 并口 LCD
用 33-37，模组就必须是 **quad PSRAM**（如 N16R2）或没有 PSRAM。
Makerfabs 那份例程用的是同一套引脚映射 ⇒ 它的板子模组不是 octal PSRAM。

## 副作用与坑（都记下来）

1. **探针自己的错**：`diag_beat_task` 用 CCOUNT 忙等 + `taskYIELD` 会饿死
   IDLE0/IDLE1，约 200 s 后触发 **TASK_WDT** 复位 ✗（串口实录）。
   已停用（`diag_beat_start()` 现在什么都不建）。
2. **`esptool --before usb_reset` 会把板子踢下 USB 总线** ✗（/dev/ttyACM* 与
   /dev/ttyUSB* 一起消失），必须物理重插。**别再用它**。
3. 现在 `CONFIG_SPIRAM_MODE_QUAD` 那个 build 是烧在板子上的坏版本，
   USB 回来之后要用本文件的 PSRAM-off 配置重烧。

## 仍未验证

面板是否**真的显示**（现在只证明启动通了、没有再复位）。判据是接上主机发帧
看画面：画面正确 ⇒ NOLOGO 映射正确、PSRAM 永久关掉；画面是花的 ⇒ 试
`UNKNOWN_ESP32S3_PICO` 那份映射（数据线 43,44,38,39,40,41,42,21,20,19,18,17,14,13,12,11，
完全避开 33-37），成了就能把 8 MB PSRAM 要回来 ✓。

## 定案（2026-10-05 晚）：模组的 PSRAM 是**芯片内封装**的 octal，quad 模式已实测否掉

- 用户确认：**核心板上没有外置 PSRAM，PSRAM 在芯片封装内**（ESP32-S3R8 那一类）。
  ⇒ 那 5 根线是**封装内部**bonding 到 PSRAM die 的，不是板子走线的问题：
  **GPIO33-37 永久属于 PSRAM，任何固件都改不了** ✓。
- quad 模式（`CONFIG_SPIRAM_MODE_QUAD=y`）实测：
  `E quad_psram: PSRAM ID read error: 0x00ffffff ... wrong PSRAM line mode`
  → `abort() was called at PC 0x403755dc on core 0`，每秒一次重启 ✗
  ⇒ **封装内的 PSRAM 明确是 octal 接线**，quad 想都不要想 ✓✓
- 所以这不是"配置写错"，是**模组选型**：要用 16-bit 并口 LCD 且数据线在 33-37，
  就必须用 **quad PSRAM** 的模组（R2/N16R2）或不带 PSRAM 的型号。
  本板的引脚映射本身没问题（Makerfabs 用同一套映射能跑，说明它那块模组不是 octal）。

**最终状态（已烧进板子、实测 ✓）**：
`CONFIG_SPIRAM` 关闭 + `PUD_MAX_TRANSFER` 32 KB + `diag_beat_start()` 停用
⇒ 完整启动，`PUD device up: 480x320, decoder QOI, frame_max 32768` ✓
