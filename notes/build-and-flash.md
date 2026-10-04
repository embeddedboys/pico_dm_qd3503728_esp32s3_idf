# 构建与烧录

> 结论：默认通过 `/dev/ttyACM0`（USB Serial/JTAG）烧录；`/dev/ttyUSB0` 是物理 UART0 日志口。

## TL;DR

- 先 `source ~/esp/esp-idf/export.sh`，目标为 `esp32s3`。
- PUD 应用态通常为 `303a:3503`；smoke 工程不使用 PUD VID/PID。
- 应用态用 `GET_CAPS` 验证协议，smoke 工程用串口 heartbeat 验证运行状态。

## 环境

当前验证环境使用 ESP-IDF `v5.2.3`。每个 shell 先导出 IDF：

```bash
source ~/esp/esp-idf/export.sh
```

首次配置目标：

```bash
idf.py set-target esp32s3
```

## PUD 构建和烧录

```bash
idf.py build
idf.py -p /dev/ttyACM0 flash
```

`idf.py -p /dev/ttyACM0 flash` 已在 smoke 工程实测成功，自动硬复位且写入校验通过。
PUD 应用烧录后，若设备仍处于 ROM 下载态，松开 BOOT 后按一次 RESET，再检查应用枚举。

## PUD 验收

以下 Python 命令在相邻 `Pico-USB-Display` 仓库中执行；当前板上 smoke 固件不提供 PUD 查询。

```bash
lsusb
python3 - <<'PY'
import sys
sys.path.insert(0, "tools")
import pud_usb
with pud_usb.open_device(vid=0x303A, pid=0x3503) as dev:
    print(dev.query_caps())
    print(dev.get_params())
PY
```

历史 32 KB 配置曾验证：`proto_ver=2`、`480x320`、`rotation=1`、`bpp=16`、
`decoder_type=3`、`frame_max=32768`、`touch=True`。当前源码将帧上限改为 `65536`，
启用 Octal PSRAM 和 CPU1 解码；这一版本仍需重新查询和运行验收。

## 端口职责

- `/dev/ttyACM0`：USB Serial/JTAG，当前默认、已验证的烧录口。
- `/dev/ttyUSB0`：CH340 物理 UART0，115200 baud，当前用于 GPIO42/GPIO44 日志。
- 物理 UART 下载需要 ROM 引脚接线正确且进入下载模式；应用 GPIO 配置不能作为 ROM 接线依据。
  此前 `esptool` 返回 `No serial data received`，本板 UART 下载尚未成功验证。
- `303a:1001` 标识 USB Serial/JTAG；smoke 应用运行时也保留该接口，PID 不能单独判定 BOOT 状态。

## 进入 BOOT

当前可靠方法是按住 BOOT 后按 RESET，再执行 `idf.py -p /dev/ttyACM0 flash`。
运行时自动清除 Flash 头部并重启进入 ROM 下载模式尚未实现，不能作为现有烧录流程的替代方案。


---

## 一键升级（2026-10-05 起：正常情况下不需要按 BOOT）

```bash
./scripts/flash-recover.sh            # 探测 → 必要时请应用进下载态 → idf.py flash
./scripts/flash-recover.sh --probe    # 只报告哪个口可用
```

应用占用 USB-OTG（GPIO19/20）后 `/dev/ttyACM*` 会消失（与 USB-Serial-JTAG 共用
引脚），此时脚本会向 `/dev/ttyUSB*` 发 `PUD-BOOT\n`，由 `main/boot_request.c`
置 `RTC_CNTL_FORCE_DOWNLOAD_BOOT` 并重启，芯片回到 ROM 下载态后再烧。
实测：3 次 `Hash of data verified` + `Done`，全程不按 BOOT ✓（详见
`notes/flash-recovery.md`）。

**PSRAM 已关闭**（原因见 `AGENTS.md` §2 / `notes/pitfalls.md`）：如果哪天把它打开，
面板的数据线会被 PSRAM 抢走 ⇒ 无限重启。改 `sdkconfig.defaults` 时留意这一点。

**不要用 `esptool --before usb_reset`**（会打死主机 USB 控制器，见 pitfalls）。
