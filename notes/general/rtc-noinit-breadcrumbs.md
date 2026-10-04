# 串口死了怎么看到"死前一刻"：RTC_NOINIT 面包屑

> 板子挂住时，串口往往**一个字都出不来**（挂住的线程既不出 `ESP_LOG` 也不出
> `esp_rom_printf`）。出路是把现场写进 **RTC 内存**（跨复位存活），
> 下一次启动时串口是好的，用**一行**正常日志把它打出来。

## TL;DR

- 做法：`RTC_NOINIT_ATTR` 变量里存"阶段号 + 计数器 + 关键寄存器快照"，
  下次启动 `ESP_LOGW` 一行打出来 ⇒ 得到"复位前最后到达的位置"。
- 要点：**必须跨复位存活**（RTC 内存不被复位清零）且**不依赖串口**写路径。
- 本项目的用法与实录见 `notes/psram-pin-conflict.md`（一个字节的面包屑把"卡在
  写 `ESP_LOGI` 里"和"显示初始化返回正常"这两件事同时定死了 ✓）。

## 怎么用（最小形态）

```c
RTC_NOINIT_ATTR static struct { u32 magic, cyc, stage, n0, n1, snap[4]; } bc;

/* 启动最早处：magic 对上就打一行，然后清 magic（避免下一轮误报） */
if (bc.magic == MAGIC) {
    ESP_LOGW("bc", "cyc=%u rs=%d stage=%u n=%u/%u cam=%08x/%08x",
             bc.cyc, esp_reset_reason(), bc.stage, bc.n0, bc.n1,
             bc.snap[0], bc.snap[1]);
    bc.magic = 0;
}
bc.magic = MAGIC; bc.cyc++;
```

- 用 `esp_reset_reason()` 区分"真的被看门狗打死"和"上电/软复位"。
- 关键寄存器（如 `LCD_CAM.lcd_clock.val`、GPIO matrix 的 `func_out_sel_cfg`、
  UART 的 `status.txfifo_cnt`）直接读寄存器存快照，比日志更有信息量。
- **心跳要能刷新**：两个核各写自己的计数器，复位前的最后一组值告诉你
  "系统还活着吗、哪个核停了"。

## 从面包屑能得出的两类结论

| 读数形态 | 含义 |
|---|---|
| 阶段号停在 `X`，且 `X` 之前的阶段都有"进入/返回"记录 | 卡点在 `X` 内部（例如卡在写日志/写总线）|
| 心跳计数器在挂住期间**仍在增长** | 系统还活着 ⇒ 是**阻塞**（锁/资源），不是死循环 |
| 心跳计数器**冻结** | 该核整体停住（中断被关/总线等待）|

## 边界与陷阱

- **启动最早期（t < ~1000 ms）的 `esp_rom_printf` 会被 console driver 吃掉/撕裂**，
  整行连同前缀一起消失 ⇒ 关键信息放在 console 稳定之后再打。
- **探针别忙等**：用 CCOUNT 忙等 + `taskYIELD` 的"心跳"会饿死 IDLE 任务，约 200 s 后
  触发 TASK_WDT，制造一个假故障 ✗（见
  [freertos-busywait-probe-starves-idle.md](freertos-busywait-probe-starves-idle.md)）。
- **时间基准要先验证**：本项目遇到过 `esp_rom_delay_us(1000)` 其实不延时、
  `xTaskGetTickCount()` 读数与 `vTaskDelay` 生效互相矛盾 ⇒
  **次数可信、时间不可信**，没验过基准就不要下时间类结论。
- 面包屑是诊断代码：**提交前清掉**，或收进显式开关。

## 相关

- 项目内实录：`notes/psram-pin-conflict.md`
