# 启动即挂住 + INT_WDT 复位：真因是 octal PSRAM 抢了面板数据线

> 本板（ESP32-S3 + NOLOGO 板型）的 PSRAM 在**芯片封装内且是 octal**，而 S3 的 octal
> 数据线在 IO_MUX 里**只有 GPIO33-37 这一组且不可改路**；面板 D8-D12 正好在那 5 个脚上
> ⇒ `Bus_Parallel16::_init_pin()` 把 PSRAM 总线抢走，CPU 停在一次永不完成的 cache miss 上，
> 约 1.2 s 后复位。**结论：本板 PSRAM 必须关闭**（`CONFIG_SPIRAM` 不能开）。

## 现象

```text
I pud: init: display begin          ← 最后一行，之后永久静默
（无 panic / Guru Meditation / backtrace）
（约 10.7 s 后复位，esp_reset_reason()==5 = ESP_RST_INT_WDT，无限循环）
```

## 根因

1. `NOLOGO_ESP32S3_PICO` 板型把 `TFT_PIN_D8..D12` 定义在 **GPIO33-37**
   （`main/LGFX_MakerFabs_Parallel_S3.hpp`）。
2. IDF 明确：octal PSRAM/flash 的数据线在 IO_MUX 里只有这一组可选，见
   [general/esp32s3-octal-psram-pin-constraint.md](general/esp32s3-octal-psram-pin-constraint.md)。
3. LovyanGFX 的 `Bus_Parallel16::_init_pin()` **无条件**把 16 根数据线全部
   `gpio_matrix_out` ⇒ 抢占 PSRAM 总线。
4. 之后任何 PSRAM/flash cache 访问都在等一次永不完成的传输 ⇒ **CPU 停住**
   （不是崩溃，是停滞），中断看门狗复位。

## 修法

```ini
# sdkconfig / sdkconfig.defaults
# CONFIG_SPIRAM is not set          ← 必须关闭
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y    # 无 PSRAM，按 4 MB flash 构建分区
```

代价与连带改动（都已在代码里）：

| 项 | 值 | 原因 |
|---|---|---|
| `PUD_MAX_TRANSFER` | **32 KB**（`main/usb/usbd_vendor.h`）| 64 KB 时 `dram0_0_seg overflowed by 79464 bytes`（内部 DRAM 放不下）|
| `frame_max`（报给主机）| **32768** | 由 `PUD_MAX_TRANSFER` 决定；主机读 `PUD_CMD_GET_CAPS` 自适应 ✓ |
| `DECODER_FRAME_SLOTS` | 3 | 3 × 32 KB 帧槽 + 32 KB 读缓冲仍在内部 RAM 内 |
| `DECODER_FRAME_ATTR` | 条件宏 | 有 PSRAM 时才把帧槽放 ext_ram 段 |
| 8 MB PSRAM | **永久不可用** | 与 16-bit 并口屏互斥，软件无解 |

## 判据（全部实测）

| 实验 | 结果 |
|---|---|
| `CONFIG_SPIRAM=y`（octal，原状） | 启动到 1.2 s 挂住 → INT_WDT 复位，无限循环 ✗ |
| `CONFIG_SPIRAM` 关闭 + `PUD_MAX_TRANSFER` 64→32 KB | 完整启动 ✓：`init: decoder ready` → `step: usb ready` → `PUD device up: 480x320, decoder QOI, frame_max 32768` |
| PSRAM 关闭后长跑 | **200 s 以上零复位** ✓ |
| `CONFIG_SPIRAM_MODE_QUAD=y`（想两者兼得） | `E quad_psram: PSRAM ID read error: 0x00ffffff ... wrong PSRAM line mode` → `abort()`，每秒重启 ✗ ⇒ 封装内**确实是 octal 接线** |
| 复位原因 | `esp_reset_reason()==5`（INT_WDT）；挂住点在主任务写 `ESP_LOGI` 里（面包屑定位），显示/背光/触模/decoder flush **都正常返回** ⇒ 与显示代码无关 ✓ |

### 被证伪的假设（都花过一次烧写，别再试）

`freq_write` 50→20 MHz ✗；跳过触模初始化 ✗；帧缓冲/绘制缓冲落 PSRAM（查代码：是 `static`）✗；
`MALLOC_CAP_ALWAYSINTERNAL` 提到 128 KB ✗；`cfg.pin_rd = -1` 导致 LCD_CAM 配置不全 ✗（未测，也没必要）；
"第一次面板 DMA 写入自旋死等" ✗（该 build 根本没有 bootlogo）；"一键升级模块抢了 console 的 UART0" ✗
（整轮关掉后卡点一模一样）。

## 边界与陷阱

- **这不是"配置写错"，是模组选型**：要有 8 MB PSRAM 又要 16-bit 并口屏在 33-37，
  必须换 **quad PSRAM**（R2/N16R2）或无 PSRAM 的模组。引脚映射本身没问题
  （Makerfabs 用同一套映射能跑 ⇒ 它的模组不是 octal PSRAM）。
- 换板型时要重新核对 `TFT_PIN_D8..D12` 是否落在 33-37：`UNKNOWN_ESP32S3_PICO`
  那份映射把它们挪到 43,44,38,39,40,41,42,21,20,19,18,17,14,13,12,11（避开 33-37）；
  但 `TFT_PIN_D1=44` 在别的板型里也是数据线 ⇒ 不能想当然地"换到空闲脚"。
- 诊断手法（面包屑、忙等探针的副作用）见
  [general/rtc-noinit-breadcrumbs.md](general/rtc-noinit-breadcrumbs.md) 与
  [general/freertos-busywait-probe-starves-idle.md](general/freertos-busywait-probe-starves-idle.md)。

## 相关

- [build-flash-recovery.md](build-flash-recovery.md)、[performance.md](performance.md)
- `../AGENTS.md` §2（硬约束汇总）
