# 忙等的"心跳"探针会饿死 IDLE，触发 TASK_WDT

> 用"CCOUNT 忙等 + `taskYIELD`"实现的心跳/探针任务，会一直占着 CPU 且不让 IDLE 运行
> ⇒ IDLE0/IDLE1 拿不到时间片、不喂任务看门狗 ⇒ 约 200 s 后被 **TASK_WDT** 复位，
> 制造出一个和真故障一模一样的假现场。

## TL;DR

- 症状：板子在某次改动后"约 200 s 必复位"，串口点名 `beat0`/`beat1`（探针任务名）。
- 真因：探针任务优先级 ≥ IDLE 且从不阻塞 ⇒ IDLE 饥饿 ⇒ `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPUx`
  的看门狗超时。
- 修法：探针用 `vTaskDelay()`/事件等**真正的阻塞**，或把探针关掉（本项目最终是
  `diag_beat_start()` 什么都不建）✓。
- 教训：**诊断代码本身会制造故障**。看到"某个时间点必挂"的现象，先问"最近加的探针在干什么"。

## 为什么 `taskYIELD` 不够

`taskYIELD()` 只在同优先级任务之间轮转；忙等循环里即使让出，探针任务仍是就绪态，
IDLE 只有在**没有任何同/高优先级任务就绪**时才运行。所以
"优先级高于 IDLE + 从不睡眠" = IDLE 永不运行。若看门狗把 IDLE 纳入检查
（IDF 默认 `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0/1=y`），必然超时。

## 判定与验证

```text
E task_wdt: Task watchdog got triggered. The following tasks/user functions did not reset the watchdog in time:
E task_wdt:  - IDLE0 (CPU 0)
E task_wdt:  - IDLE1 (CPU 1)
E task_wdt: Tasks currently running:
E task_wdt: CPU 0: beat0        ← 探针任务名：直接点名凶手
```

- 看门狗日志里的"Tasks currently running"就是判据；点名的是探针/心跳任务 ⇒ 就是它。
- 反证：把探针改成 `vTaskDelay(1000)` 后，同一场景不再复位 ✓。

## 边界与陷阱

- 反过来也要小心：**真的**卡在自旋里时，看门狗是唯一能救回设备的机制
  ⇒ 别为了"安静"把看门狗关掉；要么修阻塞，要么保留看门狗。
- 探针任务的优先级要低于或等于业务任务，并保证有阻塞点；否则它会改变被观测系统
  的时序（观察者效应），让所有时间类结论失效。
- 与 RTC 面包屑配合使用（探针只负责"我还在跑"，面包屑负责"死在哪"），见
  [rtc-noinit-breadcrumbs.md](rtc-noinit-breadcrumbs.md)。
