# ESP32-S3 的 octal PSRAM/flash 数据线只能是 GPIO33-37

> S3 的**内部**（封装内）octal PSRAM/flash 数据线在 IO_MUX 里只有一组可选引脚
> **GPIO33-37**，且**不可改路** ⇒ 选了 octal PSRAM 的模组，这 5 个脚就永久属于内存，
> 想用它们接 16-bit 并口 LCD 就只能放弃 PSRAM（或换 quad PSRAM 的模组）。

## TL;DR

- 判据（Espressif 自己的头文件，IDF 5.2 实查）：

  ```text
  components/soc/esp32s3/include/soc/io_mux_reg.h
    #define FUNC_GPIO33_SPIIO4   4      #define FUNC_GPIO36_SPIIO7   4
    #define FUNC_GPIO34_SPIIO5   4      #define FUNC_GPIO37_SPIDQS   4
    #define FUNC_GPIO35_SPIIO6   4
  components/soc/esp32s3/include/soc/spi_pins.h
    // However, there is only one set of GPIO pins which could be routed to
    // FSPIIO4, FSPIIO5, FSPIIO6, FSPIIO7.
  ```
- 封装内 PSRAM 是 octal 还是 quad 由**模组型号**决定，软件改不了：
  对 octal 模组设 `CONFIG_SPIRAM_MODE_QUAD=y` 会
  `E quad_psram: PSRAM ID read error: 0x00ffffff ... wrong PSRAM line mode` → `abort()` ✗。
- 症状不是编译错误，而是**运行期 CPU 停住**：并口驱动 `gpio_matrix_out` 抢走这 5 个脚后，
  任何 PSRAM/flash cache 访问都在等一次永不完成的传输，表现为挂住 + 中断看门狗复位。

## 什么时候会遇到

- 16-bit（或 8-bit）8080 并口 LCD 的数据线/控制线落在 GPIO33-37；
- 用 I2S/LCD_CAM/SPI 之外的外设（如 PDM、摄像头 DVP）占用 33-37；
- 抄一份"已知能跑"的引脚映射，但模组的 PSRAM 类型不同（**这是最常见的一坑**）。

## 怎么判断与处置

1. 列出面板/外设要用的脚，与 **33-37** 取交集；有交集就必须做取舍。
2. 查模组型号后缀（如 N16R8 = 8 MB octal；N16R2 = 2 MB quad）与数据手册的
   PSRAM 类型；不要靠"跑起来看看"来判断，代价是一次无限重启。
3. 处置顺序：① 换 quad PSRAM / 无 PSRAM 模组（最干净）；② 关闭 PSRAM
   （`# CONFIG_SPIRAM is not set`，换回内部 RAM 的容量预算）；③ 改映射避开 33-37。

## 边界与陷阱

- 关掉 PSRAM 会连带影响依赖它的容量决策（帧槽大小、缓冲位置），
  例如本项目 `PUD_MAX_TRANSFER` 因此从 64 KB 降到 32 KB。
- 症状与"外设没配好"很像（都是挂住），先查**寄存器读到 0**（如
  `LCD_CAM.lcd_clock.val == 0`）会更快指向"控制权被抢/外设没使能"这一类。
- 这条是 **ESP32-S3** 的 IO_MUX 事实；其它芯片的 octal 引脚组不同，不要外推
  （例如 ESP32-P4 的 PSRAM 在封装内且与排针 GPIO 无冲突）。

## 相关

- 项目内案例：`notes/psram-pin-conflict.md`
- 相关调试手法：`notes/general/rtc-noinit-breadcrumbs.md`
