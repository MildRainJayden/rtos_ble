/*
 * iwdg_monitor.h — IWDG 独立看门狗 + FreeRTOS 多任务健康监控
 *
 * 工作原理:
 *   STM32F103 的 IWDG 由独立的 LSI 振荡器 (~40kHz) 驱动, 不受系统时钟影响。
 *   一旦使能 IWDG, 必须在超时前通过写 KR 寄存器 0xAAAA 进行"喂狗";
 *   否则 IWDG 触发系统复位。IWDG 启动后软件无法关闭 (硬件安全设计)。
 *
 * 多任务监控策略:
 *   每个关键任务注册一个"心跳槽位", 在自身主循环中调用 iwdg_task_alive()
 *   更新心跳计数。看门狗监控任务 (优先级 2) 每秒唤醒一次:
 *     - 检查所有已注册任务的心跳是否在上次检查后递增
 *     - 全部健康 → 喂狗 (写 IWDG_KR 0xAAAA)
 *     - 任一任务卡死 → 不喂狗 → IWDG 超时复位
 *
 * 这样就实现了: 单个任务挂起 → 整个系统自动复位, 无需人工干预。
 *
 * IWDG 时序 (LSI=40kHz 典型值):
 *   预分频器: IWDG_PRESCALER_256 → 计数器时钟 = 40000/256 ≈ 156.25 Hz
 *   重装载值: 625 → 超时 = 625/156.25 ≈ 4.0 秒
 *   监控检查间隔: 1000ms → 任务卡死后最多 4 秒内复位
 *
 * 注意: LSI 精度较差 (30kHz~60kHz), 实际超时在 2.7s~5.3s 之间。
 *       对看门狗来说这个范围是可接受的 (只要能可靠复位即可)。
 */

#ifndef IWDG_MONITOR_H
#define IWDG_MONITOR_H

#include "FreeRTOS.h"
#include "task.h"

//最大监控任务数
#define IWDG_MAX_TASKS 8

//看门狗监控任务检查间隔 (ms)
#define IWDG_CHECK_INTERVAL_MS 1000





#endif /* IWDG_MONITOR_H */