# 踩坑与排查

> 结论：先确认运行的是哪种固件并读取日志，再检查逻辑几何和协议能力，最后分析性能。

## TL;DR

- `303a:1001` 是 USB Serial/JTAG，smoke 应用运行时也可保留，不能单独判定为 BOOT 态。
- ILI9488 旋转后必须统一使用 `480x320` 逻辑坐标。
- 全屏分带和 USB 大载荷是不同瓶颈，不能用纯色结果推算照片 FPS。

## 可复用排查

### USB 状态

烧录校验成功不代表应用已运行。PUD 用 `303a:3503` 的 caps 查询验收；smoke 用持续心跳
验收，它不接管 USB OTG，可保留 Serial/JTAG。没有日志时先检查固件、端口和复位状态。

### PUD 初始化重启

PUD 启用 Octal PSRAM 后曾出现 WDT 重启，已观察到 PSRAM 初始化/内存测试通过，并进入
`pud_init()`，具体阻塞点未定位。独立 smoke 固件双核心跳正常仅建立基础运行基线，
不代表 PUD 的显示、触摸和 USB 初始化已验证。

### 几何

ILI9488 原生几何是 `320x480`，旋转后的协议几何是 `480x320`。QOI 宽度校验、caps
和 USB 窗口坐标必须使用同一逻辑坐标空间；此前使用原生宽度会静默拒绝横屏分带。

### 性能

全屏分成 15 段时，即使纯色压缩载荷很小，也会受到每段刷新开销影响；照片和噪声
载荷较大时再叠加 USB 带宽限制。不要把两类瓶颈混为一谈。

## 约束

- 不要在每帧路径加入串口打印，会改变吞吐和时序。
- 协议字段改动必须同步 `PUD-kernel-drivers/notes/usb-protocol.md`。
- 运行时清除 Flash 头部进入 ROM 下载态尚未实现，不能作为现有烧录流程替代。


---

## PUD 初始化重启：真凶是 PSRAM 抢引脚（2026-10-05 定案）

本节旧内容把"启动即重启"归在显示/触模/日志上，**实测已全部证伪** ✗。
真凶是 **octal PSRAM 与面板数据线抢 GPIO33-37**：

- 判据一：`esp_reset_reason()==5`（INT_WDT）；挂住点由 `RTC_NOINIT` 面包屑定位在
  "主任务进 `ESP_LOGI` 没出来"，前面所有初始化（显示、背光、触模、decoder flush）
  **都正常返回** ✓ ⇒ 与显示代码无关；
- 判据二：`CONFIG_SPIRAM=y` 必卡死循环 ✗ / 关闭后 200 s 以上零复位 ✓；
- 判据三：quad 模式 `PSRAM ID read error … wrong PSRAM line mode` → abort ✗
  ⇒ 封装内 PSRAM 是 octal；
- 判据四：IDF 头文件确认 octal 数据线只能是 GPIO33-37，而面板 D8-D12 正在那里。

⇒ 这块板子上 PSRAM 与 16-bit 并口屏**互斥**，见 `AGENTS.md` §2。

## 别用 USB 层复位（新增，血的教训）

- `esptool --before usb_reset` ✗✗：实测打死主机 xHCI 控制器
  （`xHCI host not responding to stop endpoint command` → `HC died`），
  板子+CH340 一起消失，必须物理重插并重绑控制器或重启主机。
- `usb.core.find(...).reset()` ✗：同样把设备踢下总线。
- 要复位/进下载态：用 `scripts/flash-recover.sh`（发 magic 让应用自己重启），
  或普通的 `esptool chip_id`（`default_reset` / `hard_reset`）。
