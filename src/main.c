/*
@Author: Jayden
@Date: 2026-06-01 22:00:00
@LastEditTime: 2026-06-01 22:00:00
@LastEditors: Jayden
@Description: 基于官方文档的freeRTOS的移植剪裁
*/

/*
 * main.c — BLE 蓝牙控制 WS2812B LED 灯带 (FreeRTOS + STM32F103C8T6)
 *
 * ======================== 项目概述 ========================
 *
 * 本项目实现通过 BLE 蓝牙无线控制 WS2812B LED 灯带, 基于 FreeRTOS 多任务架构:
 *
 * 硬件平台:
 *   - MCU:    STM32F103C8T6 (Blue Pill, Cortex-M3, 72MHz, 20KB SRAM, 64KB Flash)
 *   - BLE:    JDY 系列蓝牙模块 (UART 透传, 连接在 USART1)
 *   - LED:    WS2812B 灯带, 60 颗灯珠 (数据线接 PB8 / TIM4_CH3)
 *   - 指示:   板载 PC13 LED (心跳指示灯)
 *
 * 硬件接线:
 *   STM32              JDY 蓝牙模块
 *   PA9  (USART1_TX) → RX
 *   PA10 (USART1_RX) → TX
 *   VCC (3.3V)       → VCC (注意: 部分 JDY 模块用 3.3V!)
 *   GND              → GND
 *
 *   STM32              WS2812B 灯带
 *   PB8  (TIM4_CH3)  → DIN (数据输入)
 *   VCC (5V 外部)    → VCC (灯带功耗较大, 不能用 STM32 3.3V)
 *   GND              → GND (共地!)
 *
 * 软件架构 (FreeRTOS 任务):
 *   - led_render_task  [优先级 6, 栈 300 字] — 取帧缓冲 → PWM 编码 → DMA 发送
 *   - ble_cmd_task     [优先级 5, 栈 256 字] — 接收命令 → 解析 → JSON 响应
 *   - led_effect_task  [优先级 4, 栈 256 字] — 特效算法 → 写帧缓冲
 *   - iwdg_monitor_task[优先级 2, 栈 128 字] — 任务心跳检查 + 喂狗
 *   - heartbeat_task   [优先级 1, 栈 100 字] — PC13 1Hz 闪烁
 *
 * 任务通信:
 *   g_cmd_queue (Queue)      — UART ISR → ble_cmd_task  (命令字符串)
 *   g_effect_queue (Queue)   — ble_cmd_task → led_effect_task (特效命令)
 *   g_frame_mutex (Mutex)    — 保护 g_led_frame 缓冲 (特效↔渲染)
 *   g_dma_done_sem (BinarySem) — DMA ISR → led_render_task (传输完成)
 *
 * 通信协议 (文本, 换行符终止):
 *   请求: CMD_NAME:param1,param2,...\n
 *   响应: {"cmd":"CMD_NAME","status":"ok","data":{...}}\n
 *
 * 上位机: 树莓派运行 Qt BLE Central APP, 通过 BLE GATT 发送文本命令
 */

#include<FreeRTOS.h>
#include "task.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

#include "ws2812b.h"
#include "ble_uart.h"
#include "cmd_protocol.h"
#include "led_effects.h"
#include "heartbeat.h"
#include "iwdg_monitor.h"

#include <stdio.h>
#include <string.h>

//FreeRTOS 底层函数声明 (由 port.c 实现, 但未在头文件中导出) 需要在此显式声明以便在 SysTick_Handler 中调用 
extern void xPortSysTickHandler(void);

/*---------------------------------------------------------------------------*
 *  全局 FreeRTOS 对象
 *---------------------------------------------------------------------------*/

/* 特效命令队列 — ble_cmd_task (生产者) → led_effect_task (消费者)
 * 队列深度: 4 (同时排队最多 4 条命令, 新命令覆盖旧特效) */
QueueHandle_t g_effect_queue = NULL;

/*---------------------------------------------------------------------------*
 *  静态缓冲区 (不在栈上分配, 节省任务栈空间)
 *---------------------------------------------------------------------------*/

/* PWM CCR 缓冲: 每个 LED 24 位 + 尾随复位脉冲
 * 大小: 60 × 24 + 52 = 1492 个 uint16_t ≈ 2984 字节
 * 静态分配在 .bss 段, 不占用 FreeRTOS 堆 */
static uint16_t g_pwm_buffer[LED_COUNT * WS2812B_BITS_PER_LED + WS2812_RESET_PULSES];

/*---------------------------------------------------------------------------*
 *  系统时钟配置 (HSE 8MHz → PLL ×9 → SYSCLK 72MHz)
 *
 *  时钟树:
 *    HSE (8MHz 外部晶振)
 *      → PLL (×9) = 72MHz
 *        → SYSCLK = 72MHz
 *          → AHB  = 72MHz (HCLK)
 *            → APB1 = 36MHz (PCLK1, 最大 36MHz)
 *              → TIM2/3/4 时钟 = 72MHz (APB1 预分频≠1 时 ×2)
 *            → APB2 = 72MHz (PCLK2)
 *              → USART1 时钟 = 72MHz
 *        → Flash 等待周期 = 2 (72MHz 运行时必须 ≥2)
 *---------------------------------------------------------------------------*/
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* ---- HSE 振荡器配置 ---- */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL     = RCC_PLL_MUL9; /* 8MHz × 9 = 72MHz */
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    /* ---- 系统时钟分配 ---- */
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK |
                                       RCC_CLOCKTYPE_SYSCLK |
                                       RCC_CLOCKTYPE_PCLK1 |
                                       RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;  /* HCLK = 72MHz */
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;    /* APB1 = 36MHz */
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;    /* APB2 = 72MHz */
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

/*---------------------------------------------------------------------------*
 *  GPIO 初始化 (PC13 板载 LED 和调试引脚)
 *---------------------------------------------------------------------------*/
static void GPIO_Init_Hardware(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    /* 使能 GPIOC 时钟 (PC13 板载 LED) */
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* PC13: 推挽输出, 初始高电平 (LED 灭)
     * Blue Pill 板载 LED 是灌电流驱动, 低电平点亮 */
    gpio_init.Pin   = GPIO_PIN_13;
    gpio_init.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_init.Pull  = GPIO_PULLUP;  /* 内部上拉, 防止浮空闪烁 */
    HAL_GPIO_Init(GPIOC, &gpio_init);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET); /* 初始 = 灭 */
}

/*---------------------------------------------------------------------------*
 *  SysTick 中断服务程序
 *
 *  FreeRTOS 需要 SysTick 作为系统节拍 (1ms 周期)。
 *  STM32 HAL 也需要 SysTick 来维护 HAL_GetTick() 计数器。
 *
 *  本函数同时为两者服务:
 *    1. HAL_IncTick()      — 增加 HAL 内部滴答计数器
 *    2. xPortSysTickHandler() — FreeRTOS 内核 tick 处理
 *
 *  注意: FreeRTOSConfig.h 中没有将 xPortSysTickHandler 映射为 SysTick_Handler,
 *  因此本函数是真正的 ISR 入口, 手动调用 FreeRTOS 的 tick 处理。
 *---------------------------------------------------------------------------*/
void SysTick_Handler(void)
{
    /* HAL 滴答: 提供给 HAL_Delay() 和 HAL_GetTick() */
    HAL_IncTick();

    /* FreeRTOS 内核滴答: 任务调度 + 软件定时器 */
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}

/*---------------------------------------------------------------------------*
 *  ble_cmd_task — BLE 命令处理任务 [优先级 5]
 *
 *  职责:
 *    1. 阻塞等待 g_cmd_queue 中的命令行
 *    2. 调用 cmd_parse() 解析文本命令
 *    3. 调用 cmd_execute() 验证并执行命令
 *    4. 发送 JSON 响应给 JDY 模块 (→ 树莓派 Qt APP)
 *    5. 如果是 SET 命令, 将特效命令发送到 g_effect_queue
 *
 *  周期: 等待队列 (阻塞), 有命令时立即处理
 *---------------------------------------------------------------------------*/
static void ble_cmd_task(void *pvParameters)
{
    char    raw_line[BLE_CMD_QUEUE_ITEM_SIZE];
    /* 静态分配: response 256 字节放在 .bss 段而非任务栈上,
     * 节省 256 字节栈空间, 避免命令处理期间栈溢出导致 HardFault */
    static char response[CMD_RESPONSE_MAX_LEN];
    parsed_cmd_t parsed;
    effect_cmd_t effect;

    (void)pvParameters;

    /* 注册到看门狗监控 */
    iwdg_register_task(IWDG_TASK_BLE_CMD, "BLE_Cmd");

    for (;;) {
        /* 通知看门狗: 本任务存活 */
        iwdg_task_alive(IWDG_TASK_BLE_CMD);

        /* 等待 UART ISR 推送命令行 (超时 1000ms 防止死等) */
        if (xQueueReceive(g_cmd_queue, raw_line, pdMS_TO_TICKS(1000)) == pdPASS) {

            snprintf(response,sizeof(response),"{\"debug\":\"RAW=%s\"}\n",raw_line);

            ble_uart_send((uint8_t*)response,strlen(response));

            /* ---- 第 1 步: 解析命令行 ---- */
            if (cmd_parse(raw_line, &parsed) != 0) {
                /* 解析失败 (不应该发生, cmd_parse 极宽容) */
                cmd_build_error("unknown", "parse error",
                                response, sizeof(response));
                ble_uart_send((uint8_t *)response, (uint16_t)strlen(response));
                continue;
            }

            /* ---- 第 2 步: 执行命令 (验证 + 转换 + 响应) ---- */
            if (cmd_execute(&parsed, &effect, response, sizeof(response)) != 0) {
                /* 命令无效: 发送错误响应 */
                ble_uart_send((uint8_t *)response, (uint16_t)strlen(response));
                continue;
            }

            /* ---- 第 3 步: 发送 JSON 响应 ---- */
            ble_uart_send((uint8_t *)response, (uint16_t)strlen(response));

            /* ---- 第 4 步: 如果是非亮度调整的 SET 命令, 推送到特效任务 ---- */
            /* SET:brightness 仅更新全局变量 g_led_brightness, 无需推送到特效队列 */
            if (!parsed.is_get && strcmp(parsed.cmd_name, "SET:brightness") != 0) {

                snprintf(response,sizeof(response),"{\"debug\":\"before queue\"}\n");
                ble_uart_send((uint8_t*)response,strlen(response));

                /* 将特效命令发送给 led_effect_task (非阻塞, 队列满则丢弃) */
                if (xQueueSend(g_effect_queue,&effect,pdMS_TO_TICKS(10)) != pdPASS){
                    snprintf(response,sizeof(response),"{\"debug\":\"queue full\"}\n");

                    ble_uart_send((uint8_t *)response,strlen(response));
                }
                else
                {
                    snprintf(response,sizeof(response),"{\"debug\":\"queue ok\"}\n");

                    ble_uart_send((uint8_t *)response,strlen(response));
                }

                snprintf(response,sizeof(response),"{\"debug\":\"after queue\"}\n");
                ble_uart_send((uint8_t*)response,strlen(response));

            }

        }
        /* 超时: 无命令, 继续循环等待 */
    }
}

/*---------------------------------------------------------------------------*
 *  led_effect_task — LED 特效计算任务 [优先级 4]
 *
 *  职责:
 *    1. 等待 g_effect_queue 中的特效命令
 *    2. 收到新命令时更新特效状态
 *    3. 根据当前特效类型, 定期计算下一帧 GRB 数据
 *    4. 通过 g_frame_mutex 安全写入 g_led_frame
 *
 *  帧率自适应:
 *    - 动画特效 (彩虹/呼吸/追色): 持续按固定间隔计算新帧
 *    - 静态特效 (纯色/渐变): 仅在新命令到来时计算一帧
 *
 *  帧间隔:
 *    RAINBOW:  50ms (20 FPS)
 *    BREATHE:  30ms (33 FPS)
 *    CHASE:    80ms (12.5 FPS)
 *    STATIC:   不自动更新
 *---------------------------------------------------------------------------*/
static void led_effect_task(void *pvParameters)
{


    effect_cmd_t new_cmd;
    TickType_t   frame_interval = pdMS_TO_TICKS(50);  /* 默认帧间隔 */
    TickType_t   last_wake_time = xTaskGetTickCount();

    (void)pvParameters;

    /* 注册到看门狗监控 */
    iwdg_register_task(IWDG_TASK_LED_EFFECT, "LED_Effect");

    for (;;) {
        /* 通知看门狗: 本任务存活 */
        iwdg_task_alive(IWDG_TASK_LED_EFFECT);

        /* 等待新特效命令 (非阻塞: 超时后继续当前特效) */
        if (xQueueReceive(g_effect_queue, &new_cmd, pdMS_TO_TICKS(10)) == pdPASS) {

            char dbg[64];

            snprintf(dbg,sizeof(dbg),"{\"debug\":\"recv effect=%d\"}\n",new_cmd.type);

            ble_uart_send((uint8_t*)dbg,strlen(dbg));

            /* 收到新命令 → 更新特效状态 */
            led_effect_set_command(&new_cmd);

            /* 根据特效类型设定帧间隔 */
            switch (g_current_effect) {
                case EFFECT_RAINBOW:
                    frame_interval = pdMS_TO_TICKS(50);   /* 20 FPS */
                    break;
                case EFFECT_BREATHE:
                    frame_interval = pdMS_TO_TICKS(30);   /* 33 FPS */
                    break;
                case EFFECT_CHASE:
                    frame_interval = pdMS_TO_TICKS(80);   /* 12.5 FPS */
                    break;
                default:
                    frame_interval = pdMS_TO_TICKS(100);  /* 静态: 不频繁更新 */
                    break;
            }
        }

        /* 对于非动画特效, 只在运行标志位为 1 时计算一次 */
        if (g_effect_running) {
            /* 获取帧缓冲锁 */
            if (xSemaphoreTake(g_frame_mutex, pdMS_TO_TICKS(10)) == pdPASS) {

                /* 计算一帧特效数据 → 写入 g_led_frame */
                led_effect_step(g_led_frame, LED_COUNT);

                /* 静态特效: 计算一次后暂停 */
                if (g_current_effect == EFFECT_STATIC ||
                    g_current_effect == EFFECT_GRADIENT ||
                    g_current_effect == EFFECT_OFF) {
                    g_effect_running = 0;
                }

                /* 释放锁 */
                xSemaphoreGive(g_frame_mutex);
            }
        }

        /* 按帧间隔延时 */
        vTaskDelayUntil(&last_wake_time, frame_interval);
    }
}

/*---------------------------------------------------------------------------*
 *  led_render_task — LED 渲染任务 [优先级 6, 最高应用优先级]
 *
 *  职责:
 *    1. 从 g_led_frame 读取 GRB 数据 (持锁)
 *    2. 编码为 PWM CCR 值缓冲
 *    3. 启动 DMA 传输到 WS2812B 灯带
 *    4. 等待 DMA 传输完成信号量
 *    5. 短暂延时后开始下一帧
 *
 *  为什么这是最高优先级?
 *    DMA 传输期间如果被抢占, 可能导致 CCR 更新不及时。
 *    虽然 DMA 在后台传输 (不占用 CPU), 但 DMA 的重新配置
 *    (编码+启动) 需要原子性, 高优先级确保不被中断。
 *
 *  时序余量:
 *    60 LED × 24 bit × 1.25μs + 52 × 1.25μs ≈ 1.87ms 传输时间
 *    编码时间 < 500μs (纯整数运算, 72MHz)
 *    帧间隔最小 30ms → CPU 占用率 < 8%
 *---------------------------------------------------------------------------*/
static void led_render_task(void *pvParameters)
{
    (void)pvParameters;

    /* 注册到看门狗监控 */
    iwdg_register_task(IWDG_TASK_LED_RENDER, "LED_Render");

    /* 第一次先给信号量 (让首次等待不阻塞) */
    xSemaphoreGive(g_dma_done_sem);

    for (;;) {
        /* 通知看门狗: 本任务存活 */
        iwdg_task_alive(IWDG_TASK_LED_RENDER);
        /* ---- 第 1 步: 获取帧缓冲锁, 读出并编码 ---- */
        if (xSemaphoreTake(g_frame_mutex, pdMS_TO_TICKS(50)) == pdPASS) {

            /* 将 GRB 帧数据编码为 PWM CCR 值 */
            ws2812b_encode_frame(g_led_frame, LED_COUNT, g_pwm_buffer);

            /* 释放锁 (编码已完成, DMA 传输期间帧缓冲可被更新) */
            xSemaphoreGive(g_frame_mutex);

            /* ---- 第 2 步: 等待上次 DMA 完成 (如果还在传输中) ---- */
            /* 超时 50ms: 正常传输只需 2ms, 50ms 是安全余量 */
            if (xSemaphoreTake(g_dma_done_sem, pdMS_TO_TICKS(50)) == pdPASS) {

                /* ---- 第 3 步: 启动 DMA 传输 ---- */
                uint32_t pwm_len = LED_COUNT * WS2812B_BITS_PER_LED
                                   + WS2812_RESET_PULSES;
                ws2812b_start_dma(g_pwm_buffer, pwm_len);
            }
            /* DMA 超时: 跳过这一帧, 不阻塞 (罕见, 仅硬件异常时发生) */
        }
        /* 帧缓冲锁超时: 跳过这一帧 */

        /* ---- 第 4 步: 最小帧间隔延时 ---- */
        /* 不同特效需要不同的刷新率:
         *   动画特效: ~5ms 间隔 (等待特效任务生成新数据)
         *   静态特效: ~20ms 间隔 (省 CPU) */
        if (g_current_effect == EFFECT_STATIC ||
            g_current_effect == EFFECT_GRADIENT ||
            g_current_effect == EFFECT_OFF) {
            vTaskDelay(pdMS_TO_TICKS(20));
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

/*---------------------------------------------------------------------------*
 *  FreeRTOS 钩子函数
 *---------------------------------------------------------------------------*/

/* 栈溢出钩子 — 当检测到任务栈溢出时被调用
 * 由 configCHECK_FOR_STACK_OVERFLOW (设为2) 激活
 * 在此处可设断点调试, 排查哪个任务栈空间不足 */
// void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
// {
//     (void)xTask;
//     (void)pcTaskName;
//     /* 死循环: 方便调试器抓取栈溢出现场
//      * 查看 xTask 句柄和 pcTaskName 名称确定哪个任务栈不够 */
//     for (;;) {
//         __asm volatile ("bkpt #0");  /* 触发调试断点 */
//     }
// }

void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName )
{
    ( void ) pcTaskName;
    ( void ) xTask;

    char buf[128];

    // 打印错误信息
    // printf("!!! FreeRTOS 任务栈溢出 !!!\r\n");
    // printf("溢出任务: %s\r\n", pcTaskName);
    // printf("检查：任务栈大小 / 任务递归调用\r\n");

    snprintf(buf,sizeof(buf),"{\"debug\":\"!!! FreeRTOS 任务栈溢出 !!!\"}\n");
    ble_uart_send((uint8_t*)buf,strlen(buf));

    snprintf(buf,sizeof(buf),"{\"debug\":\"溢出任务:%s\"}\n",pcTaskName);
    ble_uart_send((uint8_t*)buf,strlen(buf));

    snprintf(buf,sizeof(buf),"{\"debug\":\"检查：任务栈大小 / 任务递归调用\"}\n");
    ble_uart_send((uint8_t*)buf,strlen(buf));


    /* 死循环: 方便调试器抓取栈溢出现场
     * 查看 xTask 句柄和 pcTaskName 名称确定哪个任务栈不够 */
    for (;;) {
        __asm volatile ("bkpt #0");  /* 触发调试断点 */
    }
}

/*---------------------------------------------------------------------------*
 *  main() — 系统入口
 *
 *  启动流程:
 *    1. HAL 库初始化
 *    2. 系统时钟配置 (72MHz)
 *    3. GPIO 初始化 (PC13)
 *    4. 外设初始化 (WS2812B TIM+DMA, BLE UART 中断)
 *    5. IWDG 看门狗初始化 (4s 超时, 调试 halted 时暂停)
 *    6. 特效状态初始化
 *    7. 创建 FreeRTOS 通信对象 (队列/互斥量)
 *    8. 创建 FreeRTOS 任务
 *    9. 启动 FreeRTOS 调度器 (永不返回)
 *---------------------------------------------------------------------------*/
int main(void)
{
    /* ---- 第 1 步: HAL 初始化 ---- */
    HAL_Init();

    /* ---- 第 2 步: 系统时钟配置 72MHz ---- */
    SystemClock_Config();

    /* ---- 第 3 步: GPIO 初始化 ---- */
    GPIO_Init_Hardware();

    /* ---- 第 4 步: 外设初始化 ---- */
    ws2812b_init();   /* TIM4 CH3 PB8 PWM+DMA (WS2812B 驱动) */
    ble_uart_init();  /* USART1 115200 中断接收 (JDY BLE 模块) */

    /* BOOT 消息: 必须在 ble_uart_init() 之后发送 */
    {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "{\"debug\":\"BOOT!MCU发生HardFault然后重启!\"}\n");
        ble_uart_send((uint8_t*)buf, strlen(buf));
    }

    /* ---- 第 5 步: IWDG 看门狗初始化 (必须在创建任务前启动) ---- */
    iwdg_init();       /* LSI ~40kHz, 4s 超时, 调试 halted 时暂停 */

    /* ---- 第 6 步: 特效状态初始化 (默认: 全灭) ---- */
    led_effect_init();

    /* ---- 第 7 步: 创建 FreeRTOS 通信对象 ---- */

    /* 命令队列: UART ISR → ble_cmd_task
     * 深度 8, 每项 128 字节 (存放命令行字符串) */
    g_cmd_queue = xQueueCreate(BLE_CMD_QUEUE_DEPTH, BLE_CMD_QUEUE_ITEM_SIZE);
    configASSERT(g_cmd_queue != NULL);

    /* 特效队列: ble_cmd_task → led_effect_task
     * 深度 4, 每项 sizeof(effect_cmd_t) */
    g_effect_queue = xQueueCreate(4, sizeof(effect_cmd_t));
    configASSERT(g_effect_queue != NULL);

    /* 帧缓冲互斥量: 保护 g_led_frame 的读写 */
    g_frame_mutex = xSemaphoreCreateMutex();
    configASSERT(g_frame_mutex != NULL);

    /* ---- 第 8 步: 创建 FreeRTOS 任务 ---- */
    /* 参数: (函数指针, 名称, 栈字数, 参数, 优先级, 句柄) */

    /* LED 渲染任务: 优先级最高 (6) — 实时编码+DMA */
    xTaskCreate(led_render_task,  "LED_Render",
                300, NULL, 6, NULL);

    /* BLE 命令处理: 优先级 5 — 及时响应上位机 */
    xTaskCreate(ble_cmd_task,     "BLE_Cmd",
                256, NULL, 5, NULL);

    /* LED 特效计算: 优先级 4 — 非实时计算 */
    xTaskCreate(led_effect_task,  "LED_Effect",
                256, NULL, 4, NULL);

    /* IWDG 看门狗监控: 优先级 2 — 仅高于心跳任务 */
    xTaskCreate(iwdg_monitor_task, "IWDG_Monitor",
                128, NULL, 2, NULL);

    /* 心跳指示灯: 优先级 1 — 最低应用优先级 */
    xTaskCreate(heartbeat_task,   "Heartbeat",
                100, NULL, 1, NULL);

    /* ---- 第 9 步: 启动 FreeRTOS 调度器 (永不返回) ---- */
    vTaskStartScheduler();

    /* 调度器启动失败 (一般是 FreeRTOSConfig.h 配置错误或堆不足) */
    for (;;) {
        /* 死循环 → 调试器可在此处设断点排查 */
    }
}

void vApplicationMallocFailedHook( void )
{
    taskDISABLE_INTERRUPTS();

    // 打印错误信息
    printf("!!! FreeRTOS 内存分配失败 !!!\r\n");
    printf("检查：configTOTAL_HEAP_SIZE / 任务栈大小\r\n");

    for( ;; );
}
