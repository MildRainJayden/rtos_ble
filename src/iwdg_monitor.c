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

/*---------------------------------------------------------------------------*
 *  IWDG 初始化
 *
 *  配置: LSI(~40kHz) → /256 分频 → 156.25Hz → ×625 → ~4s 超时
 *
 *  关键顺序: 必须先写 KR=0x5555 解除写保护, 再配置 PR/RLR,
 *            等待配置完成 (检查 SR), 最后写 KR=0xCCCC 启动。
 *
 *  调试支持: 如果定义了 DBGMCU (调试模式), 使能 DBG_IWDG_STOP,
 *            使得 MCU 在 halted 状态 (断点/单步) 时 IWDG 暂停计数。
 *            这防止在 IDE 中断点调试时系统被看门狗复位。
 *---------------------------------------------------------------------------*/
void iwdg_init(void)
{
    //使能 LSI 时钟（IWDG的时钟源），LSI 用于 IWDG 和 RTC, 默认可能是关闭的
    RCC_OscInitTypeDef rcc_osc = {0};
    rcc_osc.OscillatorType = RCC_OSCILLATORTYPE_LSI;
    rcc_osc.LSIState = RCC_LSI_ON;
    //如果 LSI 已经在其他地方启用，这里再次启用不会有问题
    HAL_RCC_OscConfig(&rcc_osc);

    //调试模式：使能halted状态下 IWDG 暂停 (断点调试时不复位)
    //DBGMCU_CR 寄存器 bit 8 (DBG_IWDG_STOP): 置 1 则调试 halted 时 IWDG 停止STM32F103 的 DBGMCU 寄存器无需额外时钟使能即可访问
    DBGMCU->CR |= DBGMCU_CR_DBG_IWDG_STOP;

    //配置 IWDG
    IWDG_HandleTypeDef hiwdg = {0};
    hiwdg.Instance = IWDG;
    //IWDG_PRESCALER_256: LSI 40kHz / 256 = 156.25 Hz 计数器频率
    hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
    //重装载值 625: 625 / 156.25 = 4.0s 超时
    hiwdg.Init.Reload = 625;

    /* HAL_IWDG_Init 内部:
     *   1. 写 KR=0x5555 解除写保护
     *   2. 写 PR 和 RLR
     *   3. 等待 SR 标志清零 (配置完成)
     *   4. 写 KR=0xCCCC 启动 IWDG
     *   5. 写 KR=0xAAAA 喂第一次狗 */
    HAL_IWDG_Init(&hiwdg);

    //IWDG已启动，从现在起必须定期喂狗
}

/*---------------------------------------------------------------------------*
 *  注册被监控任务
 *---------------------------------------------------------------------------*/
void iwdg_register_task(iwdg_task_id_t id,const char *name)
{
    if(id < IWDG_MAX_TASKS){
        g_task_heartbeats[id].task_name = name;
        g_task_heartbeats[id].heartbeat_count = 0;
        g_task_heartbeats[id].last_heartbeat = 0;
        g_task_heartbeats[id].registered = 1;
    }
}

/*---------------------------------------------------------------------------*
 *  更新任务心跳
 *
 *  每个被监控的任务在其主循环中调用此函数。
 *  调用频率: 至少每 IWDG_CHECK_INTERVAL_MS 调用一次 (越频越好)
 *
 *  此函数在任务上下文中调用, 无需临界区保护:
 *  - heartbeat 是 32 位对齐的, ARM Cortex-M3 支持原子 32 位读写
 *  - 写者和读者在不同优先级的任务中, 不会有竞争条件 (最坏情况是读旧值)
 *---------------------------------------------------------------------------*/
void iwdg_task_alive(iwdg_task_id_t id)
{
    if(id < IWDG_MAX_TASKS && g_task_heartbeats[id].registered){
        g_task_heartbeats[id].heartbeat_count++;
    }
}

/*---------------------------------------------------------------------------*
 *  看门狗监控任务
 *
 *  工作循环:
 *    1. 注册自身
 *    2. 休眠 IWDG_CHECK_INTERVAL_MS
 *    3. 唤醒 → 更新自身心跳
 *    4. 检查所有已注册任务的心跳是否递增
 *    5. 全部递增 → 各任务存活 → 喂 IWDG
 *    6. 任一未递增 → 某任务卡死 → 不喂狗 → 等待 IWDG 复位
 *    7. 回到步骤 2
 *
 *  异常恢复:
 *    如果某任务心跳未递增, 监控任务进入死循环 (不喂狗)。
 *    IWDG 超时后系统复位, 从 main() 重新开始。
 *
 *  注意: 这个任务自身的存活性通过两步保证:
 *    a. iwdg_task_alive(IWDG_TASK_MONITOR) 在检查循环中更新
 *    b. 如果此任务自身卡死, 其他任务调用 iwdg_task_alive() 无用 —
 *       因为只有监控任务才喂狗。所以监控任务卡死 = 不喂狗 = 系统复位。
 *       这利用了 IWDG 的硬件独立性。
 *---------------------------------------------------------------------------*/
void iwdg_monitor_task(void *pvParameters)
{
    uint8_t all_alive;
    uint8_t stall_count = 0;//死循环迭代计数，用于软件复位兜底

    (void)pvParameters; //未使用参数，避免编译警告
    
    //注册自身
    iwdg_register_task(IWDG_TASK_MONITOR, "IWDG Monitor");
    iwdg_task_alive(IWDG_TASK_MONITOR); //初始心跳

    for(;;){
        //按固定间隔休眠
        vTaskDelay(pdMS_TO_TICKS(IWDG_CHECK_INTERVAL_MS));

        //更新自身心跳
        iwdg_task_alive(IWDG_TASK_MONITOR);

        //检查所有已注册任务的心跳
        all_alive = 1; //假设全部存活

        for(uint8_t i = 0; i < IWDG_MAX_TASKS; i++){
            if(!g_task_heartbeats[i].registered) continue; //未注册的任务不检查

            //检查心跳计数是否在上次检查后递增
            if(g_task_heartbeats[i].heartbeat_count == g_task_heartbeats[i].last_heartbeat){
                //未递增 → 任务可能卡死
                all_alive = 0;
                //记录此任务的名称为挂起（如果需要，可以在这里添加日志输出）
                break;
            }
            //更新上次检查的心跳值，为下一轮检查做准备
            g_task_heartbeats[i].last_heartbeat = g_task_heartbeats[i].heartbeat_count;
        }

        //根据检查结果决定是否喂狗
        if(all_alive){
            //所有任务都存活 → 喂狗 (直接写 IWDG_KR=0xAAAA)
            IWDG->KR = 0xAAAA; //写 KR=0xAAAA 喂狗
            g_last_feed_tick = xTaskGetTickCount(); //记录喂狗时的系统滴答
        }else{
            /* 有任务卡死 → 不喂狗!
             * 进入死循环等待 IWDG 超时复位。
             * 此处不调用 taskDISABLE_INTERRUPTS 或 while(1);
             * 而是让任务继续循环 (下一轮检查时仍会失败, 继续不喂狗)
             *
             * 实际上为了快速响应, 在第一次检测到故障时就直接停止喂狗:
             * 后续无论任务是否恢复, 都不再喂狗 — IWDG 会在 4 秒内复位系统。
             */
            stall_count = 0;
            for(;;){
                /* 死等复位 — 不喂狗
                 * IWDG 超时 ~4s, 每 2s 等一轮:
                 *   - 2 轮 ≈ 4s: IWDG 大概率已触发复位
                 *   - 3 轮 ≈ 6s: 超时 50% 余量, IWDG 仍不触发则软件强制复位 */
                vTaskDelay(pdMS_TO_TICKS(IWDG_CHECK_INTERVAL_MS * 2)); //等待 2 个检查周期
                
                if(++stall_count >= 3){
                    //软件复位: 如果 IWDG 仍未触发复位(极端异常: LSI 停振 / IWDG 被意外关闭), 强制复位系统
                    NVIC_SystemReset();
                }
            }
        }
    }
}