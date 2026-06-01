/*
 * iwdg_monitor.c — IWDG 独立看门狗 + FreeRTOS 多任务健康监控 实现
 *
 * STM32F103 的 IWDG (Independent WatchDog):
 *   - 由片内 LSI RC 振荡器驱动 (~40kHz, 精度 ±30%)
 *   - 与系统时钟完全独立: 即使 HSE 停振或 PLL 失锁, IWDG 仍能工作
 *   - 一旦写入 0xCCCC 启动后, 只有上电复位能停止它 (软件无法关)
 *   - 调试时必须在 DBGMCU 中使能 DBG_IWDG_STOP, 否则断点触发后会复位
 *
 * IWDG 寄存器 (STM32F1xx):
 *   IWDG_KR  (关键字):  写 0x5555 解除 PR/RLR 写保护
 *                       写 0xAAAA 喂狗 (刷新计数器)
 *                       写 0xCCCC 启动看门狗
 *   IWDG_PR  (预分频):  0~7, 决定计数器时钟 = LSI / prescaler
 *   IWDG_RLR (重装载):  12 位, 0~4095
 *   IWDG_SR  (状态):    读 RVU/PVU 位确定 PR/RLR 更新完毕
 *
 * 预分频对照:
 *   PR=0: /4,    PR=1: /8,    PR=2: /16,   PR=3: /32
 *   PR=4: /64,   PR=5: /128,  PR=6: /256,  PR=7: /256
 *   本项目使用 PR=6 (256 分频)
 *
 * 超时计算 (LSI=40kHz):
 *   PR=6 → 分频 = 256
 *   计数器时钟 = 40000 / 256 = 156.25 Hz
 *   重装载值 = 625
 *   超时 = 625 / 156.25 = 4.0s
 */

#include "iwdg_monitor.h"
#include "stm32f1xx_hal.h"
#include <string.h>

/*---------------------------------------------------------------------------*
 *  任务心跳记录结构
 *---------------------------------------------------------------------------*/
typedef struct {
    const char *task_name; //任务名称 (调试用)
    volatile uint32_t heartbeat_count; //心跳计数, 任务主循环中递增，由任务更新, 监控任务检查
    uint32_t last_heartbeat; //上次检查的心跳值
    uint8_t registered; //是否已注册到监控系统 (1=已注册, 0=未注册)
} iwdg_task_heartbeat_t;

//所有被监控任务的心跳记录
static iwdg_task_heartbeat_t g_task_heartbeats[IWDG_MAX_TASKS] = {0};

//上次喂狗时的系统滴答
static TickType_t g_last_feed_tick = 0;
