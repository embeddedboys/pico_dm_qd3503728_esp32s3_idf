# 待办

> 结论：优先补齐独立 S3 测试；历史显示/触摸/协议基线不代表当前 PSRAM 版本已通过回归。

## TL;DR

- [ ] 捕获 smoke 启动日志，确认实际 Flash 容量、PSRAM 校验和吞吐；双核心跳已观察正常。
- [ ] 如继续 UART 下载实验，核对 ROM 接线与 BOOT 状态；默认使用 `ttyACM0` 烧录。
- [ ] 恢复 PUD 后定位 `pud_init()` 的 WDT 重启，再回归 64 KB 帧上限、显示和触摸。
- [ ] 增加运行时 reset interface，让主机命令触发应用进入 BOOT/下载态。
- [ ] 评估运行时清除 Flash 头部后重启进入 ROM 下载模式的安全实现。
- [ ] 增加 decoder/EP1 计数器查询，便于无调试器验收 `submitted/drawn/dropped`。
- [ ] 在 FT6236 实际硬件上完成 EP4 回归测试；TSC2007 已完成基础验证。
- [ ] 评估减少全屏分带刷新次数并重新测量 ILI9488 FPS。
