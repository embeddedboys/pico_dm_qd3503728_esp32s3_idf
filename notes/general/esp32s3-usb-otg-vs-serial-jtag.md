# ESP32-S3：USB-OTG 与 USB-Serial-JTAG 共用引脚

> 应用一旦启用**内部 USB-OTG**（GPIO19/20），它与 USB-Serial-JTAG 争同一对引脚 ⇒
> 理论上 `/dev/ttyACM*` 会消失，esptool 的 `--before usb_reset` 也救不了
> （它本身就走那个口）。**实测补充：在本板观察到的失败模式下 ACM 始终在** ✗ 部分推翻该推论。

## TL;DR

- 事实（SoC 层面）：S3 的 USB-OTG 与 USB-Serial-JTAG 复用同一对脚 ⇒ 二者不能同时用。
- 推论（"应用跑起来 ACM 就会消失"）：**在本板未观测到**。
  实测：烧上 PUD 应用后 `303a:1001` 与 `/dev/ttyACM0` **仍在** ✓，`esptool chip_id`
  仍能读到 MAC ✓；甚至在固件**无限重启**期间也一直在 ✓（崩溃点在 `init: display begin` 之后）。
- 仍未验证的一格 ✗：**一个真正跑起来、稳定占用 USB-OTG 的应用**是否保留 ACM。
  当时固件跑不到那一态，所以不能断言 ✓。
- 实用结论：**别赌 ACM 会消失，也别赌它一定在** —— 烧写方案要能从"两条路都可能"出发，
  例如让应用自己支持"请求进下载态"（走应用自己的 USB 或独立 UART）。

## 为什么要关心

USB-OTG 与 JTAG 共存 ⇒ 决定了三件事：
① 应用态还能不能用 `/dev/ttyACM0` 烧写；② 需不需要"应用配合进下载态"的机制；
③ 日志走 UART0（CH340）还是走 USB-Serial-JTAG。三者要一起设计，别事后补。

## 怎么判定当前板子的状态

```bash
lsusb | grep -i 303a                  # 303a:1001=Serial/JTAG，303a:3503=应用
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
esptool -p /dev/ttyACM0 --before no_reset chip_id   # 成功 ⇒ 芯片在下载态；失败 ⇒ 应用在跑
```

- `esptool --before no_reset chip_id` 是一次很干净的"谁在控制 USB"判据：
  应用在跑 ⇒ 失败；ROM 下载态 ⇒ 读到 MAC。
- 注意 PID 不能单独判断：`303a:1001` 在应用运行时也可能存在。

## 边界与陷阱

- **不要**用 `--before usb_reset` 去"抢回"串口：会把主机 xHCI 打死，见
  [esptool-usb-reset-kills-xhci.md](esptool-usb-reset-kills-xhci.md)。
- 结论随固件行为变化：换 USB 栈、换 Console 输出目标、或让应用主动 detach USB 时，
  都要重新验证这一格。

## 相关

- 项目内用法：`notes/build-flash-recovery.md`
