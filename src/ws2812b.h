/*
 * ws2812b.h — WS2812B 智能 LED 灯带驱动 (TIM4 CH3 PB8 + PWM + DMA)
 *
 * 驱动原理:
 *   WS2812B 使用单线 NRZ 协议, 每个数据位由一个脉冲的高电平宽度来区分:
 *     - 0 码: 高电平 0.35μs, 低电平 0.80μs (周期 1.15μs)
 *     - 1 码: 高电平 0.70μs, 低电平 0.60μs (周期 1.30μs)
 *     - 复位: 低电平 >50μs
 *
 *   本驱动使用 TIM4 CH3 输出 PWM 波形, 通过 DMA 将预编码的 CCR 值
 *   逐个送入定时器, 每个 PWM 周期正好对应一个数据位。
 *
 * 定时器配置 (TIM4, APB1=36MHz, 定时器时钟=72MHz 因为 APB1 预分频器=1):
 *   - PSC = 0, ARR = 89 → PWM 周期 = 90/72MHz = 1.25μs
 *   - 0 码: CCR = 25 → 高电平 25/72MHz = 0.347μs
 *   - 1 码: CCR = 50 → 高电平 50/72MHz = 0.694μs
 *   - 复位: CCR = 0  → 连续低电平
 *
 * 颜色数据顺序: WS2812B 使用 GRB 顺序 (不是 RGB!)
 *   每个 LED 需要 24 位数据: G7..G0  R7..R0  B7..B0 (MSB 优先)
 *
 * DMA 配置:
 *   - DMA1_Channel3 (TIM4_CH3), Memory-to-Peripheral, 16-bit
 *   - 源地址递增 (遍历 PWM 缓冲), 目标地址固定 (TIM4->CCR3)
 *   - 普通模式 (非循环), 由 ISR 重新启动
 *
 * 内存使用:
 *   - PWM 缓冲: 60 LED × 24 bit/LED × 2 byte/bit + 50 reset × 2 = ~2980 字节
 *     静态分配在 .bss 段
 */

#ifndef WS2812B_H
#define WS2812B_H

#include "FreeRTOS.h"
#include "FreeRTOS.h"
#include "semphr.h"

/*---------------------------------------------------------------------------*
 *  硬件引脚定义
 *---------------------------------------------------------------------------*/
//WS2812B 数据线连接在 PB8 (TIM4 CH3,AF push-pull)
#define WS2812B_GPIO            GPIOB
#define WS2812B_PIN             GPIO_PIN_8
#define WS2812B_TIM             TIM4
#define WS2812B_TIM_CHANNEL     TIM_CHANNEL_3
//定时器内部使能宏
#define WS2812B_TIM_ENABLE    __HAL_RCC_TIM4_CLK_ENABLE
#define WS2812B_GPIO_ENABLE   __HAL_RCC_GPIOB_CLK_ENABLE
#define WS2812B_AFIO_ENABLE   __HAL_RCC_AFIO_CLK_ENABLE
//DMA
#define WS2812B_DMA            DMA1_Channel3
#define WS2812B_DMA_TC_FLAG     DMA1_FLAG_TC3

/*---------------------------------------------------------------------------*
 *  WS2812B 时序参数 (基于 72MHz 定时器时钟, ARR=89)
 *---------------------------------------------------------------------------*/
#define WS2812B_PWM_PERIOD       89  //ARR值, 定时器周期 = (ARR+1)/72MHz = 1.25μs
#define WS2812B_BIT_0_CCR       25  //0码高电平时间 = CCR/72MHz ≈ 0.347μs
#define WS2812B_BIT_1_CCR       50  //1码高电平时间 = CCR/72MHz ≈ 0.694μs
#define WS2812B_RESET_CCR       0   //复位码, 连续低电平

//每个 LED 需要 24 个 PWM 脉冲 (GRB, 每色 8 位, MSB 优先)
#define WS2812B_BITS_PER_LED     24

//复位脉冲数量: 52 个 PWM 周期 × 1.25μs = 65μs > 50μs 最小要求，复位脉冲 = 一帧数据发送完毕后，必须发送的一段 持续≥50μs 的低电平信号。
#define WS2812_RESET_PULSES     52

/*---------------------------------------------------------------------------*
 *  LED 灯带配置
 *---------------------------------------------------------------------------*/
//灯带LED数量
#define LED_COUNT  60

//全局亮度缩放(0~255, 最终输出 = 原始值 × BRIGHTNESS / 255)
extern uint8_t g_led_brightness;

/*---------------------------------------------------------------------------*
 *  LED 帧缓冲区 (GRB 格式)
 *
 *  每个 LED 占 3 字节: [G, R, B]
 *  总大小: LED_COUNT × 3 = 180 字节
 *
 *  访问规则: 必须持有 g_frame_mutex 才能读写!
 *  写入者: led_effect_task (计算特效帧)
 *  读取者: led_render_task (编码为 PWM 缓冲后发送)
 *---------------------------------------------------------------------------*/
extern uint8_t g_led_frame[LED_COUNT * 3];

//帧缓冲区互斥锁 (由 main.c 创建, 此处声明为 extern) 
extern SemaphoreHandle_t g_frame_mutex;

//DMA传输完成信号量 (由 ws2812b_init 创建) 
extern SemaphoreHandle_t g_dma_done_sem;

/*---------------------------------------------------------------------------*
 *  API 函数
 *---------------------------------------------------------------------------*/

/* 初始化 WS2812B 驱动
 * - 使能 GPIOB, TIM4, DMA1 时钟
 * - 配置 PB8 为 AF push-pull (TIM4_CH3)
 * - 配置 TIM4 PWM 模式, ARR=89, PSC=0
 * - 配置 DMA1_Channel3 Memory-to-Peripheral
 * - 创建 DMA 完成信号量
 * - 配置中断优先级 (NVIC)
 *
 * 调用时机: main.c 中 HAL_Init() 之后, 创建任务之前
 */
void ws2812b_init(void);

/* 更新 LED 灯带显示 (启动 DMA 传输)
 *
 * 参数: pwm_buffer — PWM CCR 值缓冲 (uint16_t 数组)
 *        pwm_len    — 缓冲长度 (LED_COUNT * 24 + WS2812_RESET_PULSES)
 *
 * 此函数将编码好的 PWM 缓冲通过 DMA 发送到 TIM4_CH3。
 * 调用前必须已完成编码, 调用后通过 g_dma_done_sem 等待完成。
 *
 * 注意: 此函数不阻塞, DMA 在后台传输。
 *       调用后应等待 g_dma_done_sem 信号量。
 */
void ws2812b_start_dma(const uint16_t *pwm_buffer,uint32_t pwm_len);

/* 将 GRB 帧缓冲编码为 PWM CCR 值缓冲
 *
 * 参数: frame      — GRB 格式的 LED 颜色数据 (LED_COUNT × 3 字节)
 *        led_count  — LED 数量 (最大 LED_COUNT)
 *        pwm_buffer — 输出: PWM CCR 值缓冲
 *                     大小必须 >= led_count * 24 + WS2812_RESET_PULSES
 *
 * 编码规则 (WS2812B GRB MSB-first):
 *   LED 0: G7 G6 ... G0  R7 ... R0  B7 ... B0
 *   LED 1: G7 G6 ... G0  R7 ... R0  B7 ... B0
 *   ...
 *   末尾: 52 个 CCR=0 (复位信号)
 */
void ws2812b_encode_frame(const uint8_t *frame, uint16_t led_count, uint16_t *pwm_buffer);

/* DMA 传输完成中断服务程序
 * 在 stm32f1xx_it.c 中调用, 或直接作为 IRQ 处理函数
 * 负责: 清除 DMA 标志, 停止 PWM, 拉低 PB8, 释放 g_dma_done_sem
 */
void DMA1_Channel3_IRQHandler(void);

#endif /* WS2812B_H */
