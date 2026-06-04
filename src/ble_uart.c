/*
 * ble_uart.c — BLE UART 驱动实现 (JDY 蓝牙模块通信)
 *
 * 接收实现: 中断驱动 + 行缓冲
 *   - RXNE (接收非空) 中断: 每收到一个字符触发
 *   - 字符存入行缓冲, 遇到 \n 视为完整命令
 *   - 命令通过 FreeRTOS 队列发送到 ble_cmd_task 解析
 *   - IDLE 中断: 用于检测帧间隔 (未收到完整行时作为超时处理)
 *
 * 设计原因: 不使用 DMA 接收, 因为:
 *   - 命令是变长文本行, DMA 接收定长数据不适用
 *   - 115200 波特率下字符间隔 ~87μs, CPU 有足够时间处理每个字符
 *   - 简化代码, 避免 DMA 循环缓冲管理复杂度
 *
 * 注意: JDY 模块在蓝牙连接成功后自动进入透传模式,
 * 所有 UART 数据直接转发到 BLE 对端 (树莓派 Qt APP)
 */

#include "ble_uart.h"
#include "stm32f1xx_hal.h"
#include "task.h"

/*---------------------------------------------------------------------------*
 *  全局变量定义
 *---------------------------------------------------------------------------*/
UART_HandleTypeDef g_ble_huart; //UART 句柄, 在 ble_uart_init() 中初始化
QueueHandle_t g_cmd_queue; //命令队列句柄, 由 main.c 创建, ble_uart ISR 写入, ble_cmd_task 读取

/*---------------------------------------------------------------------------*
 *  静态变量：行缓冲（在ISR中使用, 临界区保护）
 *---------------------------------------------------------------------------*/
static uint8_t g_uart_line_buf[BLE_UART_LINE_BUF_SIZE]; //当前行缓冲区, 存储当前接收的命令行
static volatile uint16_t g_uart_line_idx = 0; //当前行已写入字符数

/*---------------------------------------------------------------------------*
 *  UART MSP 初始化回调 (由 HAL_UART_Init 自动调用)
 *  HAL 设计: HAL_UART_Init → HAL_UART_MspInit (由用户实现, 配置 GPIO/时钟)
 *---------------------------------------------------------------------------*/
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio_init = {0};

    if(huart->Instance == BLE_UART){

        //使能 USART1 和 GPIOA 时钟
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        //配置 PA9 (TX) 为 AF push-pull 推挽输出
        gpio_init.Pin = BLE_UART_TX_PIN;
        gpio_init.Mode = GPIO_MODE_AF_PP;
        gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(BLE_UART_GPIO, &gpio_init);

        //配置 PA10 (RX) 为浮空输入
        gpio_init.Pin = BLE_UART_RX_PIN;
        gpio_init.Mode = GPIO_MODE_INPUT;
        gpio_init.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(BLE_UART_GPIO, &gpio_init);
    }
}

/*---------------------------------------------------------------------------*
 *  初始化 BLE UART
 *
 *  配置 USART1: 115200-8-N-1, RXNE+IDLE 中断
 *  NVIC 优先级设为 7 (允许调用 FreeRTOS FromISR API)
 *---------------------------------------------------------------------------*/
void ble_uart_init(void)
{
    //初始化 UART 外设
    g_ble_huart.Instance = BLE_UART;
    g_ble_huart.Init.BaudRate = BLE_UART_BAUDRATE;
    g_ble_huart.Init.WordLength = UART_WORDLENGTH_8B;
    g_ble_huart.Init.StopBits = UART_STOPBITS_1;
    g_ble_huart.Init.Parity = UART_PARITY_NONE;
    g_ble_huart.Init.Mode = UART_MODE_TX_RX;
    g_ble_huart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_ble_huart.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&g_ble_huart); //内部调用 HAL_UART_MspInit 配置 GPIO/时钟

    //使能 RXNE 中断，字符接收
    __HAL_UART_ENABLE_IT(&g_ble_huart, UART_IT_RXNE);

    //使能 IDLE 中断，线路空间检测，IDLE 在 RX 线路上检测到一个完整帧的间隔后触发，用于作为备用帧检测机制 (即使没有收到 \n)
    __HAL_UART_ENABLE_IT(&g_ble_huart, UART_IT_IDLE);

    //配置 NVIC 优先级 (7, 0)，逻辑优先级 7, 寄存器值: 7 << 4 = 112，该优先级 > configMAX_SYSCALL_INTERRUPT_PRIORITY (5<<4=80)，因此 USART1 ISR 可以安全调用 xQueueSendFromISR
    HAL_NVIC_SetPriority(USART1_IRQn, 7, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

/*---------------------------------------------------------------------------*
 *  发送数据 (阻塞式)
 *
 *  直接调用 HAL_UART_Transmit, 在发送期间阻塞当前任务。
 *  JSON 响应通常 < 200 字节, 115200 波特率下约需 17ms。
 *  对于 LED 控制场景, 这个延迟可以接受。
 *---------------------------------------------------------------------------*/
void ble_uart_send(const uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit(&g_ble_huart, (uint8_t *)data, len, HAL_MAX_DELAY);
}

/*---------------------------------------------------------------------------*
 *  USART1 全局中断服务程序
 *
 *  处理两种中断源:
 *    RXNE — 字符接收就绪, 将字符写入行缓冲, 检测 \n 提交命令
 *    IDLE — 线路空闲, 作为帧结束的备选检测 (提交不完整行)
 *
 *  关键点:
 *    - ISR 中使用 FromISR 后缀的 FreeRTOS API
 *    - 使用 portYIELD_FROM_ISR 进行上下文切换
 *
 *  注意: STM32F1 清除 IDLE 标志需读 SR→DR 序列, 由 __HAL_UART_CLEAR_IDLEFLAG 完成
 *---------------------------------------------------------------------------*/
void USART1_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskwoken = pdFALSE;

    //处理 RXNE 中断: 字符接收就绪
    if(__HAL_UART_GET_FLAG(&g_ble_huart, UART_FLAG_RXNE) != RESET){
        //读取接收到的字符（读DR寄存器会自动清除 RXNE 标志）
        uint8_t ch = (uint8_t)(g_ble_huart.Instance->DR & 0xFF);

        //检测命令结束符：\n或\r
        if(ch == '\n' || ch == '\r'){
            if (g_uart_line_idx >0){
                //终止字符串
                g_uart_line_buf[g_uart_line_idx] = '\0';

                //将完整命令行推送到FreeRTOS队列，如果队列已满则丢弃
                xQueueSendFromISR(g_cmd_queue, g_uart_line_buf, &xHigherPriorityTaskwoken);
                //重置行缓冲索引，准备接收下一行
                g_uart_line_idx = 0;
            }
        }else if (g_uart_line_idx < BLE_UART_LINE_BUF_SIZE - 1){
            //普通字符，将字符存入行缓冲
            g_uart_line_buf[g_uart_line_idx++] = ch;
        }
        //如果行缓冲已满但未收到换行符，丢弃后续字符，等待下一行，避免缓冲区溢出
    }

    /* 处理 IDLE (线路空闲) 中断
     * IDLE 在 RX 线路保持空闲 (高电平) 一个字符帧后触发
     * 用于检测不完整行 (用户没有发送 \n 的情况) */
    if(__HAL_UART_GET_FLAG(&g_ble_huart, UART_FLAG_IDLE) != RESET){
        //清除 IDLE 标志：先读 SR 再读 DR, CLEAR_IDLEFLAG 内部即读 SR → 读 DR 序列
        __HAL_UART_CLEAR_IDLEFLAG(&g_ble_huart);

        //如果行缓冲中有数据但未收到换行符，视为一行结束，提交到队列
        if (g_uart_line_idx > 0){
            g_uart_line_buf[g_uart_line_idx] = '\0'; //终止字符串
            xQueueSendFromISR(g_cmd_queue, (const void *)g_uart_line_buf, &xHigherPriorityTaskwoken);
            g_uart_line_idx = 0; //重置行缓冲索引
        }
    }

        //如果有更高优先级任务就绪，进行上下文切换
        portYIELD_FROM_ISR(xHigherPriorityTaskwoken);
}
