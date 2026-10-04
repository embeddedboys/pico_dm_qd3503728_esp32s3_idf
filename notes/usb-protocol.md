# 设备侧 USB 协议与当前 caps 值

> 应用态 `VID/PID = 303a:3503`，协议 **v2**：EP1 显示帧（QOI）、EP2 查询、EP4 触摸；
> 当前配置 `frame_max = 32768`、`decoder_type = 3`、逻辑 `480x320`、`rotation = 1`。

## TL;DR

- `303a:1001` 是 USB Serial/JTAG（ROM 下载接口），**不是** PUD 协议；
  它在应用运行时也可能保留 ⇒ **不能靠 PID 判断芯片是否在下载态**。
- 协议字段的权威定义在 `PUD-kernel-drivers/notes/usb-protocol.md`；本文件只记设备侧现值，
  改字段要**成对改**驱动仓与两侧文档。
- 帧上限由 `main/usb/usbd_vendor.h` 的 `PUD_MAX_TRANSFER`（**32 KB**）决定，
  在 `main/usb/usb.c` 填进 caps ⇒ 主机必须读 `PUD_CMD_GET_CAPS` 自适应，不要写死。

## 端点

| 端点 | 方向 | 用途 |
|---|---|---|
| EP1 | OUT | 压缩显示帧（QOI 分带；矩形坐标与长度在帧头）|
| EP2 | IN | 能力/序列号/运行参数查询 |
| EP4 | IN | 触摸报告 |

EP1 采用**帧槽背压**：`DECODER_FRAME_SLOTS = 3` 个槽都忙时**故意不重新武装 endpoint**，
让主机的 bulk 传输等着，而不是丢帧（丢帧会让局部刷新留下残影）。

## 当前 caps（源码现值）

```text
proto_ver=2
frame_max=32768          # = PUD_MAX_TRANSFER（32 KB，内部 RAM 预算所限）
decoder_type=3           # QOI（编号是协议字段，不要重排）
xres=480  yres=320       # 旋转后的逻辑几何（TFT_ROTATION=1）
bpp=16
rotation=1
pixelclock_khz=50000     # = TFT_BUS_CLK_KHZ = cfg.freq_write
touch=True
touch_polling_period=10  # ms
```

- **未做运行期回归**：以上是源码断言的值，最后一次上板验证只到"启动 + bootlogo"，
  主机发帧后的 caps 交互尚未复测 ✗。
- 触摸报告的 8 字节布局（flags / X / Y / sequence / version / reserved），
  `version=1`，按下置 `pressed`，释放清该位。

## 边界与陷阱

- 帧上限不是"越大越好"：本板 PSRAM 关掉后 64 KB 会
  `dram0_0_seg overflowed by 79464 bytes` ⇒ 32 KB（见
  [psram-pin-conflict.md](psram-pin-conflict.md)）。
- 协议几何用**逻辑坐标**；用面板原生 `320` 做校验会静默拒绝横屏分带。
- 改协议字段时同步：本文件 + 驱动仓 `notes/usb-protocol.md`（权威）+ 两侧代码。

## 相关

- [architecture.md](architecture.md)、[display-touch.md](display-touch.md)、
  [performance.md](performance.md)
