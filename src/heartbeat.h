/*
 * heartbeat.h — 板载 LED 心跳指示灯
 *
 * STM32F103C8T6 Blue Pill 板载 LED 连接在 PC13
 * 硬件特性: PC13 低电平点亮(灌电流驱动), 高电平熄灭
 * 因此初始化时设为高电平(灯灭)
 */

 #ifndef HEARTBEAT_H
 #define HEARTBEAT_H

#include "FreeRTOS.h"
#include "task.h"

//心跳闪烁周期(ms):500ms 亮 + 500ms 灭 = 1 Hz
#define HEARTBEAT_BLINK_PERIOD_MS    1000

/* 心跳任务函数 — 由 main.c 通过 xTaskCreate 启动
 * 参数: pvParameters — 未使用 (传 NULL)
 * 功能: 每 500ms 翻转一次 PC13, 产生 1Hz 闪烁
 * 优先级: 1 (最低应用优先级)
 * 栈深度: 100 字 (400 字节)
 */
void heartbeat_task(void *pvParameters);

#endif /* HEARTBEAT_H */