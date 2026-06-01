/*
 * heartbeat.c — 板载 LED 心跳指示灯实现
 *
 * STM32F103C8T6 "Blue Pill" 板载 LED 连接在 PC13
 * 特性: PC13 配置为推挽输出, 低电平点亮 (灌电流驱动)
 */

#include "heartbeat.h"
#include "stm32f1xx_hal.h"

/* FreeRTOS 心跳任务
 * 每 500ms 翻转一次 PC13 电平, 产生 1Hz 闪烁
 * 正常运行时: LED 以稳定 1Hz 频率闪烁 = 系统运行正常
 * 异常时: 闪烁停止或频率异常 = 问题指示
 * 任务优先级: 1 (最低应用优先级), 确保其他任务优先调度
 */

void heartbeat_task(void *pvParameters){

}