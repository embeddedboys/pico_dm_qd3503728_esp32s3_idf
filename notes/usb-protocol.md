# USB 协议（ESP32-S3 设备侧）

> 结论：ESP32-S3 应用态通过 `303a:3503` 提供协议 v2，EP1 显示、EP2 查询、EP4 触摸。

## TL;DR

- 当前源码显示逻辑几何是 `480x320`，QOI 帧槽上限是 `65536` 字节，待运行回归。
- PUD 协议使用 `303a:3503`；`303a:1001` 是 USB Serial/JTAG，不能用它测试 PUD 协议。
- 完整字段布局以 `PUD-kernel-drivers/notes/usb-protocol.md` 为准。

## 设备标识

应用态设备：`VID=0x303A`、`PID=0x3503`。
USB Serial/JTAG：`VID=0x303A`、`PID=0x1001`，提供 ROM 下载接口，smoke 应用运行时也可保留，
不是 PUD 应用协议，不能单凭该 PID 判断芯片是否停在 ROM。

## 端点

- EP1 OUT：压缩显示帧。
- EP2 IN：能力、序列号和运行参数查询。
- EP4 IN：触摸报告。

协议版本为 v2。矩形坐标和 payload 长度位于 EP1 帧头，不再通过每个分带的 EP0
控制请求发送。QOI 帧按分带发送，当前源码设备帧槽上限为 `65536` 字节，历史基线为 `32768`。

## 能力报告

历史 32 KB 配置实测返回（当前 PSRAM/双核版本尚未重新验证）：

```text
proto_ver=2
frame_max=32768
decoder_type=3
xres=480
yres=320
bpp=16
rotation=1
pixelclock_khz=50000
touch=True
touch_polling_period=10
```

协议字段的完整布局以 `PUD-kernel-drivers/notes/usb-protocol.md` 为准；本文件只记录
ESP32-S3 构型的历史设备侧值。当前上限由 `main/usb/usbd_vendor.h` 的 `PUD_MAX_TRANSFER`
决定，并在 `main/usb/usb.c` 中填入 caps，不能继续用旧的 `32768` 断言当前源码。

## 触摸报告

EP4 每份报告 8 字节：flags、X 高低字节、Y 高低字节、sequence、version、reserved。
当前 `version=1`。按下报告带 `pressed` 标志，释放报告清除该标志。
