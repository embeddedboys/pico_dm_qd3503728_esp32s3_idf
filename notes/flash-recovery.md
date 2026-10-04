# USB 被占用之后怎么烧录

PUD 应用用**内部 USB-OTG**（`main/main.c` 建 PUD 设备时就写着
`USB OTG device on GPIO19/20`），而 ESP32-S3 的 **USB-OTG 与 USB-Serial-JTAG 共用同一对引脚** ——
应用把 USB-OTG 拿起来之后，`/dev/ttyACM*` 会从主机上消失，`idf.py -p /dev/ttyACM0 flash` 随之失效，
连 esptool 的 `--before usb_reset` 也救不了（**它本身就是走那个口**）。

一句话：**别指望靠 USB 那条路去救 USB 被占用的板子。**

## 实测事实（2026-10-05，本机）

| 命令 | 观察到的结果 | 证据等级 |
| --- | --- | --- |
| `esptool.py -p /dev/ttyACM0 chip_id` | `MAC: 64:e8:33:4f:1f:e0`（smoke 固件运行中） | 实测 ✓ |
| `esptool.py -p /dev/ttyACM0 --before usb_reset chip_id` | 同样读到 MAC | 实测 ✓ |
| `esptool.py -p /dev/ttyUSB0 chip_id` | `Failed to connect … No serial data received` | 实测 ✗（与既有记录一致） |
| `main/main.c` 的 USB 初始化 | `USB OTG device on GPIO19/20` | 代码 ✓ |
| ESP32-S3 的 USB-OTG 与 USB-Serial-JTAG 共用引脚 | —— | SoC 事实 ✓（**本板未在 PUD 应用态直接观测到 ACM 消失** ✗） |

最后一行是**推论** ✓ —— 2026-10-05 补了实测，结论**部分被推翻** ✗：

| 观测 | 结果 |
| --- | --- |
| 烧上 PUD 应用后（`pud_qd3503728_esp32s3.bin`） | `303a:1001` 与 `/dev/ttyACM0` **仍在** ✓，`esptool chip_id` 仍能读到 MAC ✓ |
| 该固件处于**无限重启**（`init: display begin` 之后反复重启 ✗）期间 | **ACM 依然在** ✓ —— 重启了多轮也没有消失 ✓ |
| 崩溃点 | PUD/USB init 横幅之后、**显示初始化里** ✗（`I (868) pud: init: display begin` 是最后一行 ✗） |

⇒ **"应用占用 GPIO19/20 的 USB-OTG 就会让板子烧不回去"不成立** ✓：至少在这块板子的这个失败模式下，
JTAG/串口口始终在 ✓，恢复烧录从来不需要手指 ✗。
**仍未证完的一半** ✗：应用**从未跑到显示初始化之后** ⇒ 不能断言"一个正常运行的 PUD 应用也保留 ACM" ✓。
要补这一条，得先让该固件能稳定运行（见下）✓。

## 三条恢复路径

**1. 手指进 ROM 下载态（本板唯一实测可靠的）** ✓
按住 BOOT → 按一下 RESET → 松开 BOOT，然后 `idf.py -p /dev/ttyACM0 flash`。
`scripts/flash-recover.sh` 把"探口 + 该按什么"自动化了：探到就烧，探不到就打印这三条并**轮询等口出现**
（不 sleep 猜秒数）：

```bash
scripts/flash-recover.sh --probe     # 只报状态
scripts/flash-recover.sh             # 探到口就 idf.py flash
scripts/flash-recover.sh -- -b 460800 flash   # 自定义参数
```

**2. 物理 UART0（CH340）—— RX 那个脚是并口的 RD，被总线占着** ✗

先纠正一版错结论 ✗：不是"RX 没接" ✗。硬件接在 **GPIO41**（用户的接法 ✓），
但**固件里 UART0 RX 配的是 GPIO44** ✓（`CONFIG_ESP_CONSOLE_UART_RX_GPIO=44` ✓），
而 **GPIO41 是并口屏的 RD（读选通）脚** ✓：

```
main/LGFX_MakerFabs_Parallel_S3.hpp:
  #define DEFAULT_CORE_BOARD_MODEL NOLOGO_ESP32S3_PICO
  #define TFT_PIN_RD    41          ← GPIO41 = RD
  ...
  cfg.pin_rd = TFT_PIN_RD;          ← 总线确实把它配下去了（第 176 行）
```

⇒ 两个原因叠加，所以 UART0 那条路现在不通 ✓：
① 线在 41、固件在听 44 ✗；② **41 已经被显示总线占用** ✗ ——
"RX 被别的复用占用"这个说法**有代码依据** ✓，不是随口说的 ✓（换板型时 `TFT_PIN_D1=44` ✗，
也就是 44 在另一个板型里也是数据线 ✗，所以也不能想当然换到 44 ✓）。

要救活 UART 有两条子选项，**都需要动硬件或动显示配置**：
- **让出 RD**：本固件的 PUD 路径只**写**屏 ✓，同文件里有 `cfg.pin_cs = -1`（未用即置 -1 ✓）的先例 ✓
  ⇒ `cfg.pin_rd = -1` 理论可行 ✓，但 `lcd.init()` 可能靠读 ID 探测面板 ✗，且 41 若与面板 RD 同网，
  CH340 的 TX 会打到 RD 网络上 ✗ ⇒ **风险真实，必须先验证** ✓。
- **补硬件自动复位**（见下，推荐 ✓），它**不需要 UART 数据通路** ✓。

**3. 让应用自己支持"请求进入下载态"（推荐，且**零硬件改动** ✓）** ✓
关键区别：应用占用的是 **USB-OTG**，而**它自己的 USB 通信是好的** ⇒
主机仍然**能**通过 PUD 协议给它发控制请求 ✓ —— 只有 JTAG/串口那一侧没了 ✗。
所以在应用里加一条"重启进下载模式"的请求即可：

- 实现要点：置 RTC 的 force-download-boot 位 → `esp_restart()`（ESP32-S3 上即进入 ROM 下载态）；
- 触发方式：**只能走应用自己的 USB** ✓ —— 因为占用的是 USB-OTG，而它自己的 USB 通信是好的 ✓，
  JTAG/串口那一侧没了 ✗；UART0 那条又因为 RX 没接而不可用 ✗（见上）。
  所以就是一条厂商控制请求 ✓（走已有协议面 ✓，按不变量 8 与驱动仓文档成对改 ✓）。
- 之后怎么烧：应用进入下载态后 **USB-Serial-JTAG 会回来** ✓ ⇒ `esptool -p /dev/ttyACM0` 照常烧 ✓
  ⇒ **整个迭代循环不需要任何硬件改动** ✓✓。
- 边界：应用**崩在 USB 里面**时这条路也没了 ⇒ BOOT+RESET 仍是最后的兜底 ✓
  （与 PUD 在 RP2350 上的 picoboot reset 接口同一个思路 ✓）。

## 已实现并验证：一键升级（零硬件改动）✓ 2026-10-05

**做法**：应用在**启动最早处**（`app_main` 第一行 ✓）监听 UART0 的 RX（CH340 的 TX → **GPIO41** ✓，
RD 已让出 ✓，见 `main/LGFX_MakerFabs_Parallel_S3.hpp` 的 `cfg.pin_rd = -1` ✓），
收到 magic **`PUD-BOOT\n`** 就置 RTC 的 force-download-boot 位并 `esp_restart()` ✓。
代码在 `main/boot_request.c` ✓。

**为什么能救"崩着的"固件**：监听放在启动最早处 ⇒ 它落在崩溃点**之前** ✓，
所以即使应用正在无限重启，每个启动周期里也有约 1 s 的窗口能收到 magic ✓。

**上板实测**（板子上跑的正是那个无限重启的固件 ✓）：

| 步骤 | 命令 | 结果 |
| --- | --- | --- |
| A 基线 | `esptool -p /dev/ttyACM0 --before no_reset chip_id` | **失败** ✓（应用在跑 ⇒ 芯片不在下载态 ✓） |
| B 发 magic | 向 `/dev/ttyUSB0` 写 `PUD-BOOT\n`（115200 ✓ 第 2 次命中 ✓） | 同一条命令**成功读到 MAC** ✓ ⇒ 已进 ROM 下载态 ✓ |
| 旁证 | 设备日志 | 出现 `download request:` ✓ 即应用确实收到了 ✓ |

⇒ **不需要按 BOOT、不需要改硬件** ✓。之后烧写照旧走 `/dev/ttyACM0` ✓。
`scripts/flash-recover.sh` 已内置：探不到口时**自动重发 magic 并轮询**（20 s ✓），
探到就直接烧 ✓ ⇒ 一条命令完成升级 ✓：

```bash
scripts/flash-recover.sh            # 探口 → 必要时请应用进下载态 → idf.py flash
scripts/flash-recover.sh --probe    # 只报状态
```

**仍未验证的一格** ✗：应用**正常运行并稳定占用 USB-OTG**（ACM 面板态消失 ✓）时，
magic 是否同样有效 —— 机制上应当有效 ✓（UART0 与 USB 无关 ✓），
但当前固件跑不到那一态（它在显示出初始化之后就静默复位 ✗），所以没有实测 ✓。

## 备选：自动复位进下载态 + 走 USB 烧写（零固件改动）✓

**要点：进下载态和"烧写的数据通路"是两件事** ✓。CH340 的 **DTR/RTS 是独立的线** ✓，
与 UART 的 TX/RX 无关 ⇒ 把它们接到 **EN / GPIO0**（经典两管两阻电路 ✓）之后：

1. 主机对 `/dev/ttyUSB0` 抖一次 DTR/RTS ⇒ 芯片**自动进 ROM 下载态** ✓（不依赖 UART 数据 ✓，
   所以不受上面那个 RX 冲突影响 ✓✓）；
2. **USB-Serial-JTAG 随之回来** ✓ ⇒ 真正的烧写走 `/dev/ttyACM0` ✓
   （`esptool -p /dev/ttyACM0 flash` ✓）。

⇒ **不用手按 BOOT、不用改任何固件、并且应用死透也救得回来** ✓✓，
是覆盖最全的一条 ✓，也正是 Espressif 官方板一直带那两颗管子的原因 ✓。
`scripts/flash-recover.sh` 已经是"探到口就烧"，把第 1 步（抖 DTR/RTS）加进去即可 ✓ ——
**但先要有人把那两根线接上** ✗（或者用带自动下载电路的适配器 ✓）。

## 已知的板级问题：PSRAM + 显示初始化（2026-10-05）

PUD 应用（本仓根工程）烧上去后**无限重启** ✗，日志顺序是：

```
I esp_psram: SPI SRAM memory test OK          ← PSRAM 本身过了 ✓
I main_task: Calling app_main()
I pud: PUD device init (USB OTG device on GPIO19/20; console moves here, UART0 TX=GPIO42)
I pud: init: display begin                    ← 最后一行 ✗
（反复重启，没有 panic/Guru Meditation/backtrace 打印 ✗）
```

⇒ 崩在**显示初始化**（LovyanGFX / 16 位并口总线 ✓），不是 PSRAM 自检 ✗。
与"启用 PSRAM 的固件有问题"这条已知现象一致 ✓（典型怀疑方向：帧缓冲落在 PSRAM + 并口 DMA ✗）。
**未验证**：没有抓到 `rst:0x…` 与 panic 详情（读日志时只截到启动中段 ✓）⇒ 重启原因尚未确定 ✗。
在修好之前，这块板子的可用固件是 `tests/s3_smoke`（`scripts/s3-smoke.sh flash` ✓）。

## 改固件前的四行闸门（方案 3 用）

```text
已验证：ACM 路径可用（读到 MAC）；UART0 路径失败且失败原因指向接线；应用用 GPIO19/20 的内部 OTG。
仍未知：PUD 应用态下 ACM 究竟会不会消失（未观测）；方案 3 的请求走哪条控制请求号（未读协议实现）。
最小改动：只加一条"进入下载态"的厂商请求 + 其处理函数，不动现有收发路径。
生效验证：发一次请求 → 观察设备重新枚举为 ROM 设备（303a:1001 / ttyACM 重新出现）→ esptool 读到 MAC。
```
