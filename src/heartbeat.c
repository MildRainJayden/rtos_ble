/*
 * heartbeat.c — 板载 LED 心跳指示灯实现
 *
 * STM32F103C8T6 "Blue Pill" 板载 LED 连接在 PC13
 * 特性: PC13 配置为推挽输出, 低电平点亮 (灌电流驱动)
 */

#include "heartbeat.h"
#include "stm32f1xx_hal.h"
#include "iwdg_monitor.h"

/* FreeRTOS 心跳任务
 * 每 500ms 翻转一次 PC13 电平, 产生 1Hz 闪烁
 * 正常运行时: LED 以稳定 1Hz 频率闪烁 = 系统运行正常
 * 异常时: 闪烁停止或频率异常 = 问题指示
 * 任务优先级: 1 (最低应用优先级), 确保其他任务优先调度
 */

void heartbeat_task(void *pvParameters){
    //防止编译器警告未使用参数
    (void)pvParameters;

    //注册到 IWDG 监控系统
    iwdg_register_task(IWDG_TASK_HEARTBEAT, "Heartbeat");

    //配置 PC13 为推挽输出, 初始状态为高电平 (LED 灭)，由main.c完成时钟使能和引脚配置
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET); //初始状态: 灯灭

    for(;;){
        //通知看门狗监控系统自己还活着
        iwdg_task_alive(IWDG_TASK_HEARTBEAT);

        //翻转 PC13 电平
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

        //延迟 500ms → 完整周期 1s = 1Hz 
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_BLINK_PERIOD_MS));        
    }

}