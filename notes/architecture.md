# 固件架构与数据流

> 固件分成三条互不阻塞的路径：**EP1 帧 → 帧槽 → decoder task → 面板**、
> **触摸轮询 → EP4**、以及**控制请求（EP2/厂商请求）**；面板只有一个写者。

## TL;DR

- 面板写者**只有 decoder task**（固定 CPU **1**）；它也是唯一做总线写的地方。
- EP1 用 **3 个帧槽**做背压：槽满就不武装 endpoint，让主机等（不丢帧）。
- 逻辑几何 `480x320`（面板原生 `320x480`，`TFT_ROTATION=1`）。
- 触摸探测 FT6236(`0x38`) → TSC2007(`0x48`)，共用 SDA/SCL/IRQ=7/8/5，10 ms 轮询。
- **不变量与禁令**（不要放进解码→刷屏的历史里）见 `../AGENTS.md` §6 与 §4。

## 数据流

```text
USB EP1 OUT ──> main/usb/usb.c ──> 帧槽 s_frames[3] ──> decoder_task (CPU1)
                                                        └─> decoder_qoi.c ─> tft_* (LovyanGFX)
触摸控制器 ──> main/board_touch.c ──> indev_* 变换 ──> 10 ms 任务 ──> USB EP4 IN
控制请求  ──> main/usb/usb.c ──> 记账（几何/indev 方向）──> pud_params_flush_display() 在下一帧前写 MADCTL
```

关键点：

- **解码不在 USB 中断里做**：在 `usbd_vendor_ep1_bulk_out()`（中断上下文）里解码会因
  中断栈不足 HardFault（`CFSR.STKERR`），并长时间阻塞 USB 中断 ⇒ 一律交给 decoder task。
- **背压而不是丢帧**：槽全忙时不重新武装 EP1，主机 bulk 传输自然等待
  （判据：`dropped == 0`，且 `drawn` 落后 `submitted` 不超过 `SLOTS - 1`）。
- **总线写在任务里**：运行期改旋转时，中断里只记几何与方向，真正的 MADCTL 写延到
  decoder task 里做（等硬件的操作不能占着 USB 中断）。
- **异步刷新的缓冲契约**：`tft_async_video_flush()` 返回时传输仍在飞，
  该缓冲必须等 `tft_async_video_wait()` 之后才能复用（QOI 默认路径靠两带乒乓满足）。

## 构建单元

| 文件 | 职责 |
|---|---|
| `main/main.c` | 启动 PUD / USB / 触摸任务、打印设备摘要 |
| `main/pud.c` | PUD 设备数据、caps 字段、触摸轮询任务 |
| `main/usb/usb.c`、`usbd_vendor.c` | CherryUSB 设备类、EP1/EP2/EP4、厂商请求 → 参数通道 |
| `main/decoder/decoder.c` | 帧槽、decoder task、按 `DECODER_TYPE` 分发、bootlogo |
| `main/decoder/decoder_qoi.c`、`rgb565_qoi.c` | QOI 解码与绘制（vendored 编解码库）|
| `main/board_lgfx.cpp` | LovyanGFX 显示层（`tft_*` 契约实现）+ 背光 |
| `main/board_touch.c` | FT6236/TSC2007 驱动 + indev 坐标变换 |
| `main/boot_request.c` | 启动最早期监听 `PUD-BOOT` magic → 进 ROM 下载态 |
| `main/lvgl_demo.cpp` | 原厂 LVGL demo（`CONFIG_PUD_LVGL_DEMO`，与 PUD 路径二选一）|

## 相关

- `../AGENTS.md`（不变量、禁令、当前配置）；[usb-protocol.md](usb-protocol.md)、
  [display-touch.md](display-touch.md)、[psram-pin-conflict.md](psram-pin-conflict.md)
