/*
 * ble_uart.h — BLE UART 驱动 (JDY 蓝牙模块通信)
 *
 * 硬件连接:
 *   STM32 USART1 TX (PA9)  → JDY 模块 RX
 *   STM32 USART1 RX (PA10) → JDY 模块 TX
 *
 * JDY 模块特点:
 *   - 默认波特率 115200 (可通过 AT 指令修改)
 *   - 上电默认进入 BLE 从机模式, 等待连接
 *   - 蓝牙连接成功后自动进入串口透传模式
 *   - 透传模式下所有 UART 数据直接转发到 BLE 对端
 *   - 蓝牙断开后可发送 AT 指令重新配置
 *
 * UART 配置:
 *   - 波特率: 115200 bps
 *   - 数据位: 8
 *   - 停止位: 1
 *   - 校验位: 无
 *   - 流控制: 无
 *
 * 接收机制:
 *   - RXNE 中断: 逐字符接收, 存入行缓冲区
 *   - 检测换行符 (\n): 表示完整命令帧, 推送到 FreeRTOS 队列
 *   - IDLE 中断: 可选, 用于检测帧间隔
 *
 * 发送:
 *   - 直接调用 HAL_UART_Transmit 发送 JSON 响应
 *   - 阻塞式发送 (响应很短, 不阻塞任务太久)
 */

#ifndef BLE_UART_H
#define BLE_UART_H

#include "FreeRTOS.h"
#include "stm32f1xx_hal.h"
#include "queue.h"

/*---------------------------------------------------------------------------*
 *  硬件引脚定义
 *  USART1: PA9 (TX), PA10 (RX)
 *  USART = UART + 同步通信功能
 *---------------------------------------------------------------------------*/
#define BLE_UART                 USART1
#define BLE_UART_GPIO            GPIOA
#define BLE_UART_TX_PIN          GPIO_PIN_9
#define BLE_UART_RX_PIN          GPIO_PIN_10
#define BLE_UART_BAUDRATE        115200

/*---------------------------------------------------------------------------*
 *  缓冲区大小定义
 *---------------------------------------------------------------------------*/
//UART 接收行缓冲区大小 (字节)=单条命令最大长度+'\0'
#define BLE_UART_LINE_BUF_SIZE       128

//命令队列：深度（可排队命令数）
#define BLE_CMD_QUEUE_DEPTH              8

//命令队列：每条命令最大长度 (字节)
#define BLE_CMD_QUEUE_ITEM_SIZE                 128

/*---------------------------------------------------------------------------*
 *  全局对象
 *---------------------------------------------------------------------------*/
//UART句柄
extern UART_HandleTypeDef g_ble_huart;

//命令队列句柄 (由 main.c 创建, ble_uart ISR 写入, ble_cmd_task 读取) 每项是 char[BLE_CMD_QUEUE_ITEM_SIZE] 的字符串
extern QueueHandle_t g_cmd_queue;

/*---------------------------------------------------------------------------*
 *  API 函数
 *---------------------------------------------------------------------------*/

 /* 初始化 BLE UART 驱动
 * - 使能 USART1, GPIOA 时钟
 * - 配置 PA9 (TX) 为 AF push-pull, PA10 (RX) 为 floating input
 * - 配置 USART1 为 115200-8-N-1
 * - 使能 RXNE 和 IDLE 中断
 * - 配置 NVIC 优先级
 *
 * 调用时机: main.c 中 HAL_Init() 之后, 创建任务之前
 */
void ble_uart_init(void); 

/* 发送数据到 JDY 模块 (阻塞式)
 *
 * 参数: data — 待发送数据
 *        len  — 数据长度 (字节)
 *
 * 注意: 此函数使用 HAL_UART_Transmit, 在大数据量时会阻塞当前任务。
 *       协议响应很短 (< 256 字节), 通常只需几百微秒。
 */
void ble_uart_send(const uint8_t *data, uint16_t len);

/* USART1 全局中断服务程序
 * 负责: RXNE (字符接收), IDLE (线路空闲检测)
 * 在 stm32f1xx_it.c 中调用, 或直接作为 IRQ 处理函数
 */
void USART1_IRQHandler(void);

#endif // BLE_UART_H