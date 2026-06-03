/*
 * ws2812b.c — WS2812B 智能 LED 灯带驱动实现
 *
 * 驱动架构:
 *   TIM4 CH3 (PB8) 输出 PWM 波形 → DMA1_Channel3 自动发送 CCR 值
 *   每个 PWM 周期 = 1.25μs, 每个 LED 需要 24 个周期 (GRB MSB)
 *   60 个 LED + 52 个复位周期 = 1492 个 PWM 脉冲 ≈ 1.87ms / 帧
 *
 * DMA 工作流程:
 *   1. 上位机编码: 将 GRB 字节转为 CCR 值数组 (led_render_task)
 *   2. 启动 DMA: 将 CCR 数组逐字发送到 TIM4->CCR3
 *   3. DMA 传输完成 → ISR 清除标志, 停止 PWM, 拉低 PB8, 释放信号量
 *   4. 渲染任务等待完成信号量后继续下一帧
 *
 * 重要: 禁用 DMA 半传输中断, 使用简单的单缓冲模式。
 * 单帧传输时间 ~1.87ms < 帧间隔, 没有必要使用双缓冲。
 */

#include "ws2812b.h"

/*---------------------------------------------------------------------------*
 *  全局变量定义
 *---------------------------------------------------------------------------*/
//LED帧缓冲区 (GRB 格式, LED_COUNT × 3 字节) 
uint8_t g_led_frame[LED_COUNT * 3] = {0};

//全局亮度 (0-255, 初始 = 128 = 50%) 
uint8_t g_led_brightness = 128;

//帧缓冲区互斥量 — 由 main.c 创建, 保护 g_led_frame
SemaphoreHandle_t g_frame_mutex = NULL;

//DMA 传输完成信号量 — ws2812b_init() 创建, DMA ISR 释放
SemaphoreHandle_t g_dma_done_sem = NULL;

/*---------------------------------------------------------------------------*
 *  静态变量
 *---------------------------------------------------------------------------*/
//TIM 和 DMA句柄
static TIM_HandleTypeDef g_htim4;
static DMA_HandleTypeDef g_hdma_tim4_ch3;

//记录当前 DMA 传输中的缓冲指针 (仅调试用)
static volatile const uint16_t *g_dma_buffer_active = NULL;

/*---------------------------------------------------------------------------*
 *  HAL MSP 回调 (由 ws2812b_init 间接触发)
 *  HAL 在调用 HAL_TIM_PWM_Init 时会自动调用 HAL_TIM_PWM_MspInit
 *  但本驱动手动完成所有初始化, 不依赖 HAL MSP 机制
 *---------------------------------------------------------------------------*/

 /* 初始化 WS2812B 驱动 (TIM4 + PWM + DMA)
 *
 * 操作步骤:
 *   1. 使能时钟 (GPIOB, TIM4, DMA1)
 *   2. 配置 PB8 为复用推挽输出 (TIM4_CH3)
 *   3. 配置 TIM4: 向上计数, PSC=0, ARR=89, CH3=PWM1
 *   4. 配置 DMA1_Channel3: 内存→外设, 16位, 普通模式
 *   5. 创建 DMA 完成信号量 (初始为 0 = 等待状态)
 *   6. 配置 NVIC 优先级
 */
void ws2812b_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};
    TIM_OC_InitTypeDef tim_oc = {0};

    //使能时钟
    WS2812B_AFIO_ENABLE();//AFIO时钟 (用于后续可能的引脚重映射)
    WS2812B_GPIO_ENABLE();//GPIOB 时钟
    WS2812B_TIM_ENABLE();//TIM4 时钟
    __HAL_RCC_DMA1_CLK_ENABLE();//DMA1时钟

    //配置PB8为复用推挽输出(TIM_CH3)
    gpio_init.Pin = WS2812B_PIN;
    gpio_init.Mode = GPIO_MODE_AF_PP;//复用推挽输出
    gpio_init.Speed = GPIO_SPEED_HIGH;//高速输出 (对于 800kHz PWM 必须用高速)
    HAL_GPIO_Init(WS2812B_GPIO, &gpio_init);

    //初始拉低 PB8 (确保在 PWM 启动前灯带处于复位状态)
    HAL_GPIO_WritePin(WS2812B_GPIO, WS2812B_PIN, GPIO_PIN_RESET);

    //配置TIM4
    g_htim4.Instance = WS2812B_TIM;
    g_htim4.Init.Prescaler = 0;//PSC=0 → 定时器时钟 = 72MHz
    g_htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_htim4.Init.Period = WS2812B_PWM_PERIOD; //ARR=89
    g_htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    g_htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    //HAL_TIM_PWM_Init 配置 TIM 基寄存器并调用 HAL_TIM_PWM_MspInit。MspInit 是 HAL 提供的 __weak 空函数, 本项目未重写,因此 GPIO/时钟由本函数内联代码管理, 不会重复初始化。
    HAL_TIM_PWM_Init(&g_htim4);

    //配置 CH3 为 PWM 模式 1
    tim_oc.OCMode     = TIM_OCMODE_PWM1;       /* PWM 模式 1: CNT<CCR 时输出高, CNT>=CCR 时输出低 */
    tim_oc.Pulse      = 0;                     /* 初始占空比 0 */
    tim_oc.OCPolarity = TIM_OCPOLARITY_HIGH;   /* 高电平有效 */
    tim_oc.OCFastMode = TIM_OCFAST_ENABLE;     /* 快速输出模式 (减少比较延迟) */
    HAL_TIM_PWM_ConfigChannel(&g_htim4, &tim_oc, WS2812B_TIM_CHANNEL);

    //配置 DMA1_Channel3，DMA1_Channel3: 内存→TIM4_CCR3, 16 位半字传输
    g_hdma_tim4_ch3.Instance = DMA1_Channel3;

    //设置外设地址为 TIM4_CCR3 寄存器地址
    g_hdma_tim4_ch3.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    g_hdma_tim4_ch3.Init.PeriphInc           = DMA_PINC_DISABLE;   /* 外设地址固定 */
    g_hdma_tim4_ch3.Init.MemInc              = DMA_MINC_ENABLE;    /* 内存地址递增 */
    g_hdma_tim4_ch3.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD; /* 16 位外设 */
    g_hdma_tim4_ch3.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD; /* 16 位内存 */
    g_hdma_tim4_ch3.Init.Mode                = DMA_NORMAL;         /* 普通模式 (非循环) */
    g_hdma_tim4_ch3.Init.Priority            = DMA_PRIORITY_HIGH;  /* 高优先级, 确保时序 */
    HAL_DMA_Init(&g_hdma_tim4_ch3);

    //将DMA关联到TIM4_CH3
    __HAL_LINKDMA(&g_htim4, hdma[TIM_DMA_ID_CC3], g_hdma_tim4_ch3);

    //创建DMA完成信号量（二值信号量，初值为0）,DMA ISR  完成后释放
    g_dma_done_sem = xSemaphoreCreateBinary();
    configASSERT(g_dma_done_sem != NULL);

    //配置 NVIC 中断优先级
     /* DMA1_Channel3 中断优先级设为 6 (逻辑优先级)
     * 寄存器值: 6 << 4 = 96
     * 该优先级 > configMAX_SYSCALL_INTERRUPT_PRIORITY (5<<4=80)
     * 因此 DMA ISR 可以安全调用 FreeRTOS API (如 xSemaphoreGiveFromISR) */
    HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);

    //TIM4中断: 不使用 (仅 DMA 中断)，注: HAL 默认会启用 TIM 中断, 我们需要确保 TIM 中断被禁用
    __HAL_TIM_DISABLE_IT(&g_htim4, TIM_IT_UPDATE);
}

/*---------------------------------------------------------------------------*
 *  GRB 帧 → PWM CCR 缓冲编码
 *
 *  WS2812B 数据协议:
 *    每个 LED 需要 24 位, 发送顺序为 G→R→B, 每个字节 MSB 先发
 *
 *    LED 0: [G7 G6 G5 G4 G3 G2 G1 G0] [R7 ... R0] [B7 ... B0]
 *    LED 1: [G7 G6 G5 G4 G3 G2 G1 G0] [R7 ... R0] [B7 ... B0]
 *    ...
 *    LED 59
 *
 *    每个数据位编码为一个 PWM 周期:
 *      - 位值 1 → CCR = WS2812_BIT_1_CCR (50)
 *      - 位值 0 → CCR = WS2812_BIT_0_CCR (25)
 *    传输完成后追加 WS2812_RESET_PULSES 个 CCR=0 (复位信号)
 *
 * 输入: frame     — GRB 格式颜色数据 (LED_COUNT × 3 字节)
 *       led_count — LED 数量
 * 输出: pwm_buffer — CCR 值缓冲 (uint16_t 数组, 调用者提供)
 *       总长度 = led_count × 24 + WS2812_RESET_PULSES
 *---------------------------------------------------------------------------*/
void ws2812b_encode_frame(const uint8_t *frame, uint16_t led_count, uint16_t *pwm_buffer)
{
    uint32_t idx = 0;

    //遍历每个LED
    for(uint16_t led = 0; led < led_count; led++)
    {

        //每个LED有3个颜色通道（GRB顺序）
        for(uint8_t ch = 0; ch < 3; ch++){
            uint8_t byte = frame[led * 3 + ch];

            //每个字节 8 位, MSB 优先输出
            for (int8_t bit = 7; bit >= 0; bit--) {
                if (byte & (1 << bit)) {
                    pwm_buffer[idx++] = WS2812B_BIT_1_CCR;  /* 1 码 */
                } else {
                    pwm_buffer[idx++] = WS2812B_BIT_0_CCR;  /* 0 码 */
                }
            }
        }
    }

    //追加复位信号 (CCR=0, 连续低电平)
    for (uint16_t i = 0; i < WS2812_RESET_PULSES; i++) {
        pwm_buffer[idx++] = WS2812B_RESET_CCR;
    }
}

/*---------------------------------------------------------------------------*
 *  启动 DMA 传输
 *
 * 此函数设置 DMA 传输参数, 启动 TIM4 PWM 输出并立即开始 DMA 传输。
 * 传输完成后由 DMA ISR 释放 g_dma_done_sem 信号量。
 *
 * 注意: 调用此函数前必须确保上一次 DMA 传输已完成 (等待信号量)。
 *
 * 参数: pwm_buffer — CCR 值缓冲 (由 ws2812b_encode_frame 填充)
 *       pwm_len    — 缓冲中的 uint16_t 元素个数
 *---------------------------------------------------------------------------*/
void ws2812b_start_dma(const uint16_t *pwm_buff, uint32_t pwm_len)
{
    g_dma_buffer_active = pwm_buff;

    //确保 TIM4 计数器从 0 开始 (避免初值导致错误脉宽)
    __HAL_TIM_SET_COUNTER(&g_htim4, 0);

    //启动 TIM4 PWM 输出 (使能 CH3) 定时器未启动前不输出 PWM, 但通道需先使能
    TIM_CCxChannelCmd(WS2812B_TIM, WS2812B_TIM_CHANNEL, TIM_CCxN_ENABLE);

    //确保PB8处于低电平(初始化状态),以避免DMA开始前出现错误的高电平脉冲
    HAL_GPIO_WritePin(WS2812B_GPIO, WS2812B_PIN, GPIO_PIN_RESET);

    //启动DMA传输
    /* HAL_TIM_PWM_Start_DMA 会:
     *   1. 设置 DMA 源地址 = pwm_buffer, 传输量 = pwm_len
     *   2. 使能 DMA 传输完成中断
     *   3. 使能 TIM4 的 DMA 请求 (CC3)
     *   4. 启动 TIM4 计数器
     *   5. 当 TIM4 需要更新 CCR3 时自动触发 DMA 传输
     */
    HAL_TIM_PWM_Start_DMA(&g_htim4, WS2812B_TIM_CHANNEL, (uint32_t *) pwm_buff, pwm_len);
}

/*---------------------------------------------------------------------------*
 *  DMA1_Channel3 中断服务程序
 *
 * 在 DMA 传输完成时触发。
 * 负责: 清除标志, 停止 PWM, 拉低 PB8, 释放信号量通知渲染任务。
 *
 * 关键时序:
 *   - 此 ISR 在最后一个 PWM 脉冲传输完成后触发
 *   - 但此时最后一个脉冲可能还在输出中 (定时器仍在运行)
 *   - 因此立即停止 PWM 并拉低 PB8, 确保复位信号干净
 *---------------------------------------------------------------------------*/
void DMA1_Channel3_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    //检查并清除传输完成标志
    uint32_t tc_flag = __HAL_DMA_GET_TC_FLAG_INDEX(&g_hdma_tim4_ch3);
    if (__HAL_DMA_GET_FLAG(&g_hdma_tim4_ch3, tc_flag) != RESET) {

        //清除 DMA 传输完成标志
        __HAL_DMA_CLEAR_FLAG(&g_hdma_tim4_ch3, tc_flag);

        //停止 PWM 输出:
        //禁用 TIM4 CH3 PWM 输出 → PB8 回到 GPIO 控制
        TIM_CCxChannelCmd(WS2812B_TIM, WS2812B_TIM_CHANNEL, TIM_CCx_DISABLE);

        //停止 TIM4 DMA 请求
        __HAL_TIM_DISABLE_DMA(&g_htim4, TIM_DMA_CC3);

        //停止 TIM4 计数器
        __HAL_TIM_DISABLE(&g_htim4);

        //强制 PB8 低电平 (确保复位信号)
        HAL_GPIO_WritePin(WS2812B_GPIO, WS2812B_PIN, GPIO_PIN_RESET);

        g_dma_buffer_active = NULL;

        //释放信号量, 通知渲染任务 DMA 已完成
        if (g_dma_done_sem != NULL) {
            xSemaphoreGiveFromISR(g_dma_done_sem, &xHigherPriorityTaskWoken);
        }
    }

    //如有更高优先级任务就绪, 触发上下文切换
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);    

}
