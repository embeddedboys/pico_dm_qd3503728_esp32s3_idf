# 不要用 `esptool --before usb_reset`（会把主机 xHCI 打死）

> 在 Linux + xHCI 主机上，`esptool --before usb_reset`（以及 pyusb 的
> `device.reset()`）实测**打死整条 xHCI 控制器**：板子和 USB 转串口一起从总线上消失，
> 需要物理重插 + 重绑控制器（或重启主机）才能恢复。

## TL;DR

- 症状：`xHCI host not responding to stop endpoint command` → `HC died; cleaning up`，
  之后 `/dev/ttyACM*` 与 `/dev/ttyUSB*` 同时消失。
- 恢复（不用重启主机时）：

  ```bash
  echo 1 | sudo tee /sys/bus/pci/devices/<BDF>/remove
  echo 1 | sudo tee /sys/bus/pci/devices/<BDF>/rescan
  ```

  `<BDF>` 用 `lspci | grep -i xhci` 查（例如 `0000:05:00.3`）；多数情况下物理重插更省事。
- 要复位/进下载态时用**别的**手段：普通 `esptool chip_id`（默认 `default_reset`/`hard_reset`）、
  按 BOOT+RESET、或让应用自己进下载态（本项目用 `scripts/flash-recover.sh` 发 magic）。

## 为什么会这样

`usb_reset` 走的是 USB 端口的 reset 信号路径；某些 xHCI 控制器驱动在这种情况下会
卡在"停止端点"命令上，最终判控制器死亡并清理全部下游设备。这**不是板子的问题**，
换端口/换控制器可能不复现，但代价是整机 USB 全掉，不值得赌。

## 诊断与验证

- 判断是不是它：`dmesg | grep -i -E "xhci|HC died"` 应能看到 `HC died` 那条。
- 恢复后再确认设备回来：`lsusb | grep -i 303a` + `ls /dev/ttyACM*`。
- 预防：脚本里**不要**出现 `usb_reset`；用 `esptool chip_id` 之类的只读探测代替。

## 边界与陷阱

- 只读操作是安全的：`esptool -p <port> chip_id`（默认复位策略）不会触发这个问题。
- 类似陷阱：`usb.core.find(...).reset()`（python-usb）同样会把设备踢下总线且不会自己回来。
- 杀进程时别用会误伤自己的写法：`pkill -f 'esptool[.]py'` 里的 `[.]` 是防自匹配的，
  反过来说 `pkill -f 'esptool.py'` 在某些命令行里会把当前 shell 也匹配进去。
