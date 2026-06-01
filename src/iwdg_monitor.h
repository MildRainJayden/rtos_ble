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

//IWDG 超时时间 (ms), 标称值，实际受 LSI 频率影响，有 ±30% 偏差
#define IWDG_TIMEOUT_MS 4000

/*---------------------------------------------------------------------------*
 *  任务心跳槽位 ID 定义 (每个被监控任务一个 ID)
 *---------------------------------------------------------------------------*/
typedef enum {
    IWDG_TASK_LED_RENDER = 0,  // LED 渲染任务
    IWDG_TASK_BLE_CMD = 1,         // BLE 命令处理任务
    IWDG_TASK_LED_EFFECT = 2,       // LED 特效任务
    IWDG_TASK_HEARTBEAT = 3,              // 心跳任务
    IWDG_TASK_MONITOR = 4,                // 看门狗监控任务自身
    IWDG_TASK_COUNT = 5,                // 已注册任务数
}iwdg_task_id_t;

/*---------------------------------------------------------------------------*
 *  API 函数
 *---------------------------------------------------------------------------*/

/* 初始化 IWDG 并启动
 * - 使能 LSI 时钟
 * - 配置预分频器 = 256, 重装载值 = 625 → ~4 秒超时
 * - 写 0xCCCC 启动 IWDG (一旦启动不可停止!)
 *
 * 调用时机: main() 中 HAL_Init 之后, 外设初始化之前
 * 重要: 必须在创建任务之前调用, 否则任务可能在 IWDG 配置期间被调度抢占
 */
void iwdg_init(void);

/* 注册一个被监控的任务
 *
 * 参数: id   — 任务 ID (来自 iwdg_task_id_t 枚举)
 *        name — 任务名称 (调试用, 可传 NULL)
 *
 * 每个需要在看门狗监控下的任务, 在自身初始化阶段调用此函数。
 * 未注册的任务不会被看门狗监控。
 */
void iwdg_register_task(iwdg_task_id_t id, const char *name);

/* 更新任务心跳 — 每个被监控的任务在主循环中调用
 *
 * 参数: id — 任务 ID
 *
 * 任务应在每次主循环迭代中调用此函数。
 * 监控任务定期检查此计数是否递增来判断任务是否存活。
 */
void iwdg_task_alive(iwdg_task_id_t id);

/* 看门狗监控任务函数 (由 main.c 通过 xTaskCreate 启动)
 *
 * 参数: pvParameters — 未使用 (传 NULL)
 * 优先级: 2 (仅高于 heartbeat_task)
 * 栈深度: 128 字 (512 字节)
 *
 * 每秒唤醒一次, 检查所有已注册任务的心跳, 全部健康则喂狗。
 */
void iwdg_monitor_task(void *pvParameters);

#endif /* IWDG_MONITOR_H */