# 显示与触摸：几何、旋转与两种触摸控制器

> ILI9488 面板原生 `320x480`，在 `TFT_ROTATION=1` 下对主机报告逻辑 `480x320`；
> 触摸的坐标变换**只在一个地方**（`main/board_touch.c` 里的 indev 层），
> 控制器驱动只返回原始值。

## TL;DR

- 协议/解码器/caps 必须统一使用**旋转后的逻辑几何** `480x320`（用原生 `320` 做宽度校验
  会把所有横屏分带静默拒掉 ✗）。
- 触摸：启动时先探 FT6236（`0x38`），失败再探 TSC2007（`0x48`），共用
  SDA `GPIO7` / SCL `GPIO8` / IRQ `GPIO5`，轮询周期 `INDEV_POLLING_PERIOD_MS = 10`。
- 旋转可以在**运行期**改（`PUD_PARAM_ROTATION`）：几何与 `indev_set_dir()` 在中断里记账，
  MADCTL 的寄存器写由 decoder task 在画下一帧前执行（总线写不进中断）。
- TSC2007 的按下/释放/坐标已通过 EP4 验证 ✓；FT6236 的兼容路径保留但**未在真机上回归** ✗。

## 显示

- LovyanGFX `Bus_Parallel16` + ILI9488，RGB565，16 位 8080 并口；
  `cfg.freq_write = 50000000`（= `TFT_BUS_CLK_KHZ`，也是上报给主机的 `pixelclock_khz`）。
- 面板原生 `320x480`；`TFT_ROTATION=1` ⇒ 逻辑 `480x320`。
- `cfg.pin_rd = -1`：**RD 让出** —— GP17/GPIO41 是这块板子上的串口 RX 位，
  不由显示总线占用（早期固件占用它，导致 UART 那条路不通，见
  [build-flash-recovery.md](build-flash-recovery.md)）。
- 开机流程：命令行 → 背光打开**之前**画完 QOI bootlogo（`main/decoder/bootlogo_qoi.h`）。

### 运行期旋转（容易漏的一条）

| 步骤 | 在哪做 | 为什么 |
|---|---|---|
| 记几何（xres/yres 对调）+ `indev_set_dir(indev_dir_for_rotation(rot))` | `main/usb/usb.c` 的控制请求回调（中断上下文）| 只有几次赋值，安全 |
| 真正写 MADCTL、切面板方向 | `pud_params_flush_display()`，由 decoder task 在下一帧前执行 | **面板寄存器写要等硬件，不能在 USB 中断里做** |
| 只允许一个写者改面板状态 | decoder task 是唯一写者 | 避免方向与窗口不一致的中间态 |

## 触摸

```text
FT6236 (0x38) ──┐
                ├─> main/board_touch.c ─> indev_* 变换 ─> 10 ms 轮询任务 ─> USB EP4 IN
TSC2007 (0x48) ─┘
```

- 两个控制器共用同一组 I2C/IRQ 引脚（`TSC2007_PIN_SDA/SCL/IRQ = 7/8/5`，见
  `main/include/config.h`）；启动时按地址探测，谁应答就用谁。
- **坐标变换只有一处**：`indev_dir_for_rotation()` / `indev_set_dir()`（`board_touch.c`），
  驱动里不要再加 `set_dir()` 常量 —— 那样旋转不会跟着走，而且调用两次会把轴序转回去 ✗。
- EP4 上报 8 字节（见 [usb-protocol.md](usb-protocol.md)），坐标系是当前的逻辑几何。

## 验证与边界

- 换屏或改旋转后**先读 caps**，再用 `tests/touch_test.py` 验轴序/反向/按下/释放，
  不要套用旧校准值。
- FT6236 真机回归仍缺（代码路径已验证可编译、能探测，但块上没有 FT6236 模组）✗。
- LVGL 原厂 demo 保留在树内，由 `CONFIG_PUD_LVGL_DEMO`（`main/Kconfig`）选择，
  不属于 PUD 路径。
