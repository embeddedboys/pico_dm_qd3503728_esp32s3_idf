# 交接文档：PUD 移植到 ESP32-S3（Pico_DM_QD3503728 + Nologo ESP32-S3 Pico）

> 本文件是跨 agent / 跨会话的交接，随 git 提交。最后更新：阶段 3 调试中，黑屏根因已定位、修复方向已定、代码**尚未修复提交验证**。

## 1. 目标一句话

把 PUD 设备协议移植到 `pico_dm_qd3503728_esp32s3_idf` 仓（载体已拍板）：ESP32-S3 通过 USB Full-Speed 接收压缩图像流，QOI 解码，LovyanGFX 推 ILI9488（480×320 横屏），FT6236 触摸经 EP4 上报。LVGL 保留在树内，后续把设备解码刷新逻辑做进 LVGL（v1 不耦合）。

## 2. 关键决策（已与用户确认，勿推翻）

| 项 | 决策 | 理由 |
| --- | --- | --- |
| USB 栈 | **CherryUSB v1.5.2**（与 Pico 固件 vendored 同版本，`components/CherryUSB` 子模块 @ `v1.5.2` = commit `4d6b12c7`） | 其 dwc2 port 含乐鑫官方 `usb_glue_esp.c`，协议层可搬运；TinyUSB 要重写 class driver |
| VID/PID | **`0x303A:0x3503`**（不沿用树莓派 `2e8a:0001`） | 避开与 Pico/ZX 设备同号；3503 取面板 QD**3503**728 |
| 显示后端 | **LovyanGFX**（`components/LovyanGFX` 子模块，`Bus_Parallel16`+`Panel_ILI9488`+`Light_PWM`） | 原厂 demo 已在这块板验证；ILI9488 不在 IDF 5.2 esp_lcd 核心驱动里 |
| EP1 流控 | **按 Pico 模型**：3 帧槽，槽忙**不武装 EP1**（NAK 背压） | ZX 版放弃了背压改丢帧计数——**不能学**；工作区铁律 |
| 解码 | v1 只做 **QOI（DECODER_TYPE=3）**；触摸 EP4 要做；RLE/QOIZ/QOID 不做 | 用户选定范围 |
| Console | UART0 **TX=GPIO42**（Pico 排针 GP16 位），RX=GPIO44 悬空（GP17/GPIO41 是屏 RD，接 UART 会总线竞争） | 用户要求与 Pico GP16 TX 同位 |
| LVGL | 保留为 menuconfig 选项 `CONFIG_PUD_LVGL_DEMO`（默认 n） | 保留原厂 demo 作硬件对照 |

## 3. 环境（已装好，可直接用）

- ESP-IDF **v5.2.3**：`~/esp/esp-idf`（从 gitee 镜像克隆，失败的子模块回退 github 补齐；工具链已装）。
- 每个 shell 用前：`source ~/esp/esp-idf/export.sh`。
- 串口：USB-Serial-JTAG（烧录用，板载 USB 口）；**跑 PUD 后该口消失（PHY 归 OTG）**，日志只在 UART GPIO42。
- UART 适配器：用户侧在 **/dev/ttyUSB1**（注意新插的适配器默认 9600，要先 `stty -F /dev/ttyUSB1 115200 raw -echo`）。
- 拉代码可挂本机代理：`http://127.0.0.1:7897`（**勿写进任何仓文件**，工作区规范）。
- udev 规则（免 root 跑 pyusb）：已临时装到 `/etc/udev/rules.d/99-pud-esp32s3.rules`（系统文件不随仓，换机需重建）：
  ```
  SUBSYSTEM=="usb", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="3503", MODE="0660", GROUP="plugdev"
  ```
- 验证用 pyusb 库：`Pico-USB-Display/tools/pud_usb.py`（复用，open_device(vid=0x303a, pid=0x3503)；类名是 **`Display`**；`open_device` 已返回 Display，勿二次包装；caps 是 dict）。

## 4. 进度：阶段 0/1/2 完成，阶段 3 卡在黑屏

| 阶段 | 状态 | 证据 |
| --- | --- | --- |
| 0 环境+基线 | ✅ | 原厂 LVGL demo 构建/烧录/运行，画面+触摸正常（用户确认） |
| 1 骨架 | ✅ | 平台层 + CherryUSB 编进工程 |
| 2 USB 枚举+查询 | ✅ | 枚举成 `303a:3503 embeddedboys PUD Display`；pyusb `GET_SN`=`64e8334f1fe04847`；`GET_CAPS` 全字段 oracle **PASS**（proto2/frame_max32768/QOI/bpp16/touch=False） |
| 3 图像通路 | ❌ **黑屏** | pyusb send_rgb565 返回成功（15 bands/2742B），屏不变（停在黑） |
| 4 触摸 EP4 | ⏳ 未开始 | — |
| 5 内核驱动联调 | ⏳ | 需在驱动 `usb.c` 的 `pud_ids[]` 加 `303a:3503` |
| 6 收尾 | ⏳ | AGENTS.md+notes/、README 重写、协议文档两处同步 |

## 5. 黑屏根因（已定位，两处关联 bug）

**Bug A（直接原因，高置信，已被代码事实证明）：QOI band 宽度校验用错几何，所有帧被静默拒绝。**

- Host（pud_usb）从 caps 的 frame_max(32768) 算 `band_pixels=10913`，按帧宽 480 → 每 band **480 宽 × 22 行**（15 bands 铺满 320 行）。
- 设备 `main/decoder/decoder_qoi.c` 的 `qoi_drawimg` 入口有：
  ```c
  uint16_t width = xe - xs + 1;                 // = 480
  if (... || width > TFT_HOR_RES) return;       // TFT_HOR_RES=320（面板"原生"宽）→ 拒绝，return
  ```
  → 480 > 320，**每一 band 直接 return，什么都不画**；而 EP1/EP2 层照常 ACK，pyusb 看到成功。
- `main/decoder/decoder_qoi.c` 里已埋了临时调试日志（`ESP_LOGI/W ESP_LOGI("qoi", ...)`：drawimg 入口、reject、decoded、flush、decompress short）——**修复后用它们确认走到 flush；确认后可删**。

**Bug B（caps 几何错，会导致内核驱动模式错）：旋转后没有交换 xres/yres。**

- 设备 `main/pud.c pud_config_init()` 直接填 `xres=TFT_HOR_RES(320)/yres=TFT_VER_RES(480)`，但 TFT_ROTATION=1 后设备帧是 **480×320**。
- 实测 caps 返回 `xres=320,yres=480`（oracle 当时按此错误值断言 PASS——修复后**测试断言也要改成 480/320**）。
- 注：当前 host 的 `Display.width` 默认 480 绕过了 caps，所以 pyusb 没立刻暴露 Bug B；内核驱动会直接信 caps → 模式错。

### 修复方向（照 Pico 固件的成熟做法，勿自创）

Pico-USB-Display 用"原生 + 旋转后"两套几何解决：
- `lib/pico-display-lib/drivers/display/CMakeLists.txt:8-13`：旋转奇数次 → `TFT_X_RES=TFT_VER_RES(480)`、`TFT_Y_RES=TFT_HOR_RES(320)`；原生 `TFT_HOR/VER_RES` 不动。
- 面板与 caps 用旋转后的 `TFT_X_RES/TFT_Y_RES`；面板宽度 `display.xres` 在 probe / `tft_set_rotation` 里交换（见 tft_ili9488.c / tft.c:258-261）。

具体到本仓要改：
1. `main/include/config.h`：加旋转后几何，如 `#define TFT_X_RES 480 / #define TFT_Y_RES 320`（或按 TFT_ROTATION 推导）。
2. `main/decoder/decoder_qoi.c`：宽度校验 `width > TFT_HOR_RES` 改成 `> TFT_X_RES(480)`；回调路径的 `QOI_BATCH_PIXELS` 等如涉及原生宽也要核对（Pico 原版这里用的是原生，需逐处对照 `Pico-USB-Display/src/decoders/decoder_qoi.c` 确认它到底用哪套——**移植要逐行对齐上游**）。
3. `main/pud.c pud_config_init()` 与 `pud_params_apply`（运行期旋转分支）：caps 的 xres/yres 用旋转后值（对照 Pico src/pud.c，注意它如何与 config 配合）。
4. flush 坐标空间确认：`main/board_lgfx.cpp` 用 LovyanGFX，已 `setRotation(TFT_ROTATION)`，480×320 帧的 setAddrWindow 应正确——修完 A/B 后**以屏显实测为准**；若位置仍错再查 LGFX 旋转/坐标语义。
5. 验证：重烧后 `stty ttyUSB1`，pyusb 发纯色（红/绿/蓝，每帧 sleep 2s）+ checker/真实图像（`img_viewer.py` 或 pud_usb.load_image），看 UART 的 qoi 日志走到 flush，屏显正确；更新 oracle 断言（caps 480×320）。

## 6. 设备侧计数器（阶段 3/5 的最终 oracle）

- `decoder.c`：`g_decoder_stat_{submitted,dropped,drawn,oversize}`；`usb.c`：`g_ep1_stat.{oversize,bad,stale}`。健康标准：**`dropped==0`、`drawn≈submitted`**。
- 读取难点：19/20 归 OYG 后没有 JTAG 通道。当前未实现 EP2 debug 查询——阶段 5 可加一个 vendor 查询导出计数器，或先靠现象/日志。留给下一个 agent 决定。

## 7. 当前工程结构（改完后的样子）

```
pico_dm_qd3503728_esp32s3_idf/
├─ CMakeLists.txt            project(pud_qd3503728_esp32s3) + DMA_ATTR 兼容映射 + Wno-array-bounds
├─ sdkconfig.defaults        + CHERRYUSB(_DEVICE) + console CUSTOM TX=42/RX=44
├─ HANDOFF.md                本文件
├─ components/
│  ├─ CherryUSB/             子模块 @v1.5.2（dwc2 port）
│  ├─ LovyanGFX/             子模块（显示）
│  └─ lvgl/                  子模块 v9.2.x（保留）
└─ main/
   ├─ main.c                 app_main：pud_init()+usb_device_init()
   ├─ pud.c                  g_pud_data + SN(MAC) + 触摸 poll
   ├─ board_lgfx.cpp         tft/backlight glue over LovyanGFX（含 PUD_BOARD_NO_TOUCH）
   ├─ board_touch.c          阶段1 占位（indev stub；dir_for_rotation 真实）→ 阶段4 替换
   ├─ lvgl_demo.cpp          原厂 demo（原 main.cpp，入口 lvgl_demo_run）
   ├─ LGFX_MakerFabs_Parallel_S3.hpp   板级 LGFX（触摸部分已加 #ifndef PUD_BOARD_NO_TOUCH）
   ├─ Kconfig                CONFIG_PUD_LVGL_DEMO
   ├─ include/  pud.h decoder.h config.h tft.h indev.h backlight.h
   ├─ usb/      usb.c usbd_vendor.c usbd_vendor.h usb.h
   └─ decoder/  decoder.c decoder_qoi.c decoder_internal.h rgb565_qoi.c rgb565_qoi.h（rgb565_qoi.* 与上游逐字节一致，cmp 验证过）
```

注意：
- CherryUSB 在 IDF5.2 缺 `DRAM_DMA_ALIGNED_ATTR`（5.3+ 才有），顶层 CMakeLists 用 `-DDRAM_DMA_ALIGNED_ATTR=DMA_ATTR` 兼容，vendored 代码零改动。
- config descriptor 长度表达式在 IDF 的 -Werror=parentheses 下要加括号（已处理：`(9+9+7+7+7)`）。

## 8. 给下一个 agent 的起手式

1. 读本文件 + `PUD-kernel-drivers/notes/usb-protocol.md`（协议权威）。
2. `source ~/esp/esp-idf/export.sh`。
3. 板子若停在 PUD（3503）要重烧：按 BOOT+RESET 进下载模式（ttyACM 回来）或确认烧录口。
4. 按第 5 节修 Bug A/B → 构建烧录 → pyusb 发图验证（UART ttyUSB1 看 qoi 日志）。
5. 继续阶段 4（触摸 FT5x06：参考 `Pico-USB-Display/lib/pico-display-lib/drivers/input/{ft6236.c,indev.c}`，EP4 push 已在 usb.c）→ 5（驱动加 id）→ 6（收尾文档）。
6. 全程遵守工作区规范：不动协议字段、不 commit 本机路径/代理/口令、每次改动写四行闸门、oracle 给出 PASS/FAIL。
