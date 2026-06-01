/*
@Author: Jayden
@Date: 2026-06-01 22:00:00
@LastEditTime: 2026-06-01 22:00:00
@LastEditors: Jayden
@Description: 基于官方文档的freeRTOS的移植剪裁
*/

#include<FreeRTOS.h>
#include "task.h"


void vApplicationMallocFailedHook( void )
{
    taskDISABLE_INTERRUPTS();

    // 打印错误信息
    printf("!!! FreeRTOS 内存分配失败 !!!\r\n");
    printf("检查：configTOTAL_HEAP_SIZE / 任务栈大小\r\n");

    for( ;; );
}

/*---------------------------------------------------------------------------*
 *  FreeRTOS 钩子函数
 *---------------------------------------------------------------------------*/

/* 栈溢出钩子 — 当检测到任务栈溢出时被调用
 * 由 configCHECK_FOR_STACK_OVERFLOW (设为2) 激活
 * 在此处可设断点调试, 排查哪个任务栈空间不足 */
void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName )
{
    ( void ) pcTaskName;
    ( void ) xTask;

    // 打印错误信息
    printf("!!! FreeRTOS 任务栈溢出 !!!\r\n");
    printf("溢出任务: %s\r\n", pcTaskName);
    printf("检查：任务栈大小 / 任务递归调用\r\n");

    /* 死循环: 方便调试器抓取栈溢出现场
     * 查看 xTask 句柄和 pcTaskName 名称确定哪个任务栈不够 */
    for (;;) {
        __asm volatile ("bkpt #0");  /* 触发调试断点 */
    }
}