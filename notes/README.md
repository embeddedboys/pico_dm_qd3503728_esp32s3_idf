# 知识库索引

**范围**：ESP32-S3 移植（`pico_dm_qd3503728_esp32s3_idf`）的实测结论与踩坑。
通用（跨项目可复用）结论在 `general/`。交接与下一步在 `../HANDOFF.md`，
必须遵守的约束在 `../AGENTS.md`；RP2350 原版与主机驱动的知识库在各自仓库。

## 索引

| 文档 | 回答什么问题 |
|---|---|
| [psram-pin-conflict.md](psram-pin-conflict.md) | 为什么本板的 PSRAM 必须关掉？无限重启的真因是什么？ |
| [build-flash-recovery.md](build-flash-recovery.md) | 怎么构建、怎么烧、板子"烧不回去"时怎么救？ |
| [display-touch.md](display-touch.md) | 显示几何/旋转与两种触摸控制器怎么配？ |
| [usb-protocol.md](usb-protocol.md) | 设备侧协议与当前 caps 值是什么？ |
| [performance.md](performance.md) | 带宽和帧率实测是多少？在什么配置下测的？ |
| [architecture.md](architecture.md) | 固件分成哪几条数据流？不变量在哪？ |
| [smoke-tests.md](smoke-tests.md) | 独立 smoke 工程验证了什么？怎么跑？ |
| [general/esp32s3-octal-psram-pin-constraint.md](general/esp32s3-octal-psram-pin-constraint.md) | S3 的 octal PSRAM 为什么只能占 GPIO33-37？ |
| [general/esp32s3-usb-otg-vs-serial-jtag.md](general/esp32s3-usb-otg-vs-serial-jtag.md) | 为什么应用一跑起来 `/dev/ttyACM*` 就可能消失？ |
| [general/esptool-usb-reset-kills-xhci.md](general/esptool-usb-reset-kills-xhci.md) | 为什么不能用 `esptool --before usb_reset`？ |
| [general/rtc-noinit-breadcrumbs.md](general/rtc-noinit-breadcrumbs.md) | 串口已经死了，怎么看到"死前一刻"？ |
| [general/freertos-busywait-probe-starves-idle.md](general/freertos-busywait-probe-starves-idle.md) | 为什么忙等心跳探针会触发 TASK_WDT？ |

登记（**上游/vendored，未改动，不为满足预算而重写**）：`components/CherryUSB/`
（CherryUSB v1.5.2，本仓只加了 IDF ≥5.2 的编译适配）、`assets/*.png`（板子照片/示意图）。

## 维护约定

- **首屏给结论**：标题 → `> 一句话结论` → TL;DR；10 秒内能判断"解决什么、我该做什么"。
- **事实分级**：*已验证*（源码/实测/构建日志）直接陈述；*观察*注明测试条件；
  *假设*显式标"推测/未验证"，**不得写成结论**。
- **信息预算**：简单条目 20–80 行，常规 50–150 行，超 150 触发压缩审查，超 300 拆分。
- **更新用合并重写**，不要 `cat >>` 追加"更新于某日"；过时的**结论**删掉，
  历史**测量数据**保留并标注"在配置 X 下测得"。
- **漂移检查**（每次动知识库都做）：运行时事实以**代码/配置为准**逐条核对文档断言
  （默认值、常量、开关、特性是否存在），并给出漂移清单。
- 被实测否定的假设要留（`✗`）——那是下次不重复踩的依据。
