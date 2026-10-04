# 固件架构

> 结论：当前固件将 USB 帧、QOI 解码、ILI9488 刷新和双触摸控制器分成独立路径。

## TL;DR

- EP1 接收 QOI，帧槽不足时通过 endpoint 背压等待。
- 逻辑显示尺寸是 `480x320`，面板原生尺寸是 `320x480`。
- FT6236 和 TSC2007 共用 I2C/IRQ 引脚，启动时自动探测。

## 数据流

```text
USB EP1 OUT
  -> main/usb/usb.c
  -> EP1 帧槽和背压
  -> main/decoder/decoder.c
  -> main/decoder/decoder_qoi.c
  -> LovyanGFX / ILI9488
```

触摸数据流为：

```text
FT6236 或 TSC2007
  -> main/board_touch.c
  -> 10 ms 触摸轮询任务
  -> USB EP4 IN
```

`main/main.c` 启动 PUD、USB 设备和触摸轮询。LVGL 原厂 demo 保留在树内，
由 `CONFIG_PUD_LVGL_DEMO` 控制，不作为 PUD 协议路径的必需组件。

## 当前构型

- 芯片：ESP32-S3。
- USB：CherryUSB device，Full-Speed vendor interface。
- 显示：LovyanGFX `Bus_Parallel16`，ILI9488，RGB565。
- 逻辑分辨率：`480x320`；面板原生几何为 `320x480`，`TFT_ROTATION=1`。
- 解码器：QOI，`DECODER_TYPE=3`。
- 触摸：启动时优先探测 FT6236 (`0x38`)，失败后探测 TSC2007 (`0x48`)。
- 触摸引脚：SDA `GPIO7`、SCL `GPIO8`、IRQ `GPIO5`。

EP1 采用帧槽背压：没有可用解码槽时不重新武装 endpoint，让主机等待，避免局部刷新丢帧。
