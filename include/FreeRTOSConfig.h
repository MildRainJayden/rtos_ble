#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* Here is a good place to include header files that are required across
   your application. */
#include "stm32f1xx_hal.h"

//抢占式调度(必须开启, 否则没有实时性)
#define configUSE_PREEMPTION                                     1

//是否让调度器用硬件指令加速找最高优先级就绪任务
#define configUSE_PORT_OPTIMISED_TASK_SELECTION                     1

//值为 1 时使用 低功耗无滴答模式，值为 0 时则始终保持滴答中断运行
#define configUSE_TICKLESS_IDLE                                     0

//输入用于驱动生成节拍中断所用外设的内部时钟的执行频率（单位：赫兹）——该时钟通常与驱动内部CPU时钟的时钟相同。为正确配置定时器外设，需要提供此数值
#define configCPU_CLOCK_HZ                                          (SystemCoreClock)

// SysTick定时器的时钟频率,F103（Cortex‑M3）的 SysTick 默认是 HCLK/8
#define configSYSTICK_CLOCK_HZ                                      (SystemCoreClock)

//系统节拍频率
#define configTICK_RATE_HZ                                          ((TickType_t)1000)

//最大优先级数 (0 ~ N-1), 设为 8 即 0~7 共 8 级
#define configMAX_PRIORITIES                                        (8)

//最小任务栈大小
#define configMINIMAL_STACK_SIZE                                    ((unsigned short)128)

//创建任务时为任务指定的描述性名称的最大允许长度
#define configMAX_TASK_NAME_LEN                                     (16)

/** V11.x 使用 configTICK_TYPE_WIDTH_IN_BITS 替代旧的 configUSE_16_BIT_TICKS
 * 32 位 Tick 计数器 (避免 16 位溢出, 尤其在 72MHz/1kHz 下)
 * 系统时钟节拍（Tick）用 16 位变量 还是 32 位变量
#define configUSE_16_BIT_TICKS                                     0
*/

//系统时钟节拍（Tick）类型宽度，STM32F103（72MHz/1kHz）必须用 32 位，彻底杜绝溢出
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS

////该参数控制空闲优先级任务的行为,空闲任务是否应该在有其他就绪任务时让出CPU
#define configIDLE_SHOULD_YIELD                                   1

//开启 FreeRTOS 任务通知功能
#define configUSE_TASK_NOTIFICATIONS                              1

// 给每个任务分配「多少个独立的任务通知槽位」
#define configTASK_NOTIFICATION_ARRAY_ENTRIES                     1

//启用互斥量功能,专门用来保护全局变量、共享资源，防止多任务同时操作导致数据错乱
#define configUSE_MUTEXES                           1

//是否启用 FreeRTOS 递归互斥锁
#define configUSE_RECURSIVE_MUTEXES                                 0

//是否启用 FreeRTOS 计数信号量,只用二值和互斥置0
#define configUSE_COUNTING_SEMAPHORES                            0

//将其设置为 1 可在构建中包含“替代”队列函数，设置为 0 则从构建中省略“替代”队列函数。替代 API 详见 queue.h 头文件。该替代 API 已弃用，不得在新设计中使用
#define configUSE_ALTERNATIVE_API                                 0 /* Deprecated! */

//队列注册表大小, 该表允许你为每个队列/信号量/互斥量分配一个名称, 以便在调试器中查看，除非你正在使用支持 RTOS 内核的调试器，否则队列注册表没有任何作用
#define configQUEUE_REGISTRY_SIZE               0

//队列集，让一个任务同时阻塞等待「多个队列 / 信号量」。默认 0（关闭）；只有明确要 “多源事件统一监听” 才开 1
#define configUSE_QUEUE_SETS                                        0

//时间片轮转调度,当多个同优先级任务就绪时,是否让它们轮流占用 CPU 时间，关闭以节省上下文切换开销
#define configUSE_TIME_SLICING                                      1

//newlib 可重入，是否在 STM32 上用 C 标准库（printf、malloc、字符串、时间函数），关闭0以节省内存，仅单任务调用，无需多任务重入保护
#define configUSE_NEWLIB_REENTRANT                             0

//关闭旧版 API 兼容 (V8/V9 旧的类型/函数别名), 减小编译体积
#define configENABLE_BACKWARD_COMPATIBILITY                         0

//设置每个任务的线程本地存储数组中的索引数量，以存任务自己专属的全局变量 / 指针，多任务互不干扰
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS                    0

//FreeRTOS 内部优化宏，用来精简链表结构体大小
#define configUSE_MINI_LIST_ITEM                                   1

//设置用于在调用 xTaskCreate() 以及其他使用栈大小的各种地方的堆栈深度类型。默认 uint16_t 就足够了，除非你需要创建超过 64KB 堆栈的任务
#define configSTACK_DEPTH_TYPE                                      uint16_t

//消息缓冲区长度类型: 项目未使用消息/流缓冲区, 预留 uint16_t 即可
#define configMESSAGE_BUFFER_LENGTH_TYPE                            uint16_t

//释放内存时清零，项目无安全敏感数据, 关闭省 CPU
#define configHEAP_CLEAR_MEMORY_ON_FREE                             0

//-------------------------------------------------------------------------------------------------
/* Memory allocation related definitions. */
//仅使用动态分配 (简化, 不需要提供静态内存回调)
#define configSUPPORT_STATIC_ALLOCATION                             0

//由于项目不使用静态分配，宏无效
//#define configKERNEL_PROVIDED_STATIC_MEMORY                         1

//是否允许 FreeRTOS 动态创建任务、队列、信号量（自动 malloc）
#define configSUPPORT_DYNAMIC_ALLOCATION                         1

//动态分配的堆总大小 (字节): 10KB
#define configTOTAL_HEAP_SIZE                   ((size_t)(14 * 1024))

//链接器自动分配地址，FreeRTOS 自行在 heap_4.c 中声明 ucHeap[], 无需接管0，默认情况下，FreeRTOS 堆由 FreeRTOS 声明，并由链接器放置在内存中
#define configAPPLICATION_ALLOCATED_HEAP                            0

//默认为0，所有动态东西：任务栈、TCB、队列、信号量 → 都在 10KB 堆里；置为1则想把栈挤到另一块内存，避免跟内核对象抢同一堆
#define configSTACK_ALLOCATION_FROM_SEPARATE_HEAP                 0

//-------------------------------------------------------------------------------------------------
/* Hook function related definitions. */
//当空闲任务运行时是否调用钩子函数
#define configUSE_IDLE_HOOK                            0

//启用 “系统节拍钩子函数” Tick Hook Function
#define configUSE_TICK_HOOK                              0

//栈溢出检测任务，创建时在栈顶放置哨兵模式 (0xA5A5A5A5),任务切换时检查哨兵是否被破坏, 检测栈溢出
#define configCHECK_FOR_STACK_OVERFLOW                      2

//当 malloc 失败时是否调用钩子函数
#define configUSE_MALLOC_FAILED_HOOK                      1

//当 FreeRTOS 软件定时器服务任务（Daemon Task）刚启动时，调用一次你写的函数
#define configUSE_DAEMON_TASK_STARTUP_HOOK               0

//是否启用 每个流缓冲区独立的发送/完成回调
#define configUSE_SB_COMPLETED_CALLBACK                    0

//-------------------------------------------------------------------------------------------------
/* Run time and task stats gathering related definitions. 任务运行时调试专区 */
//任务运行时间统计，统计每个任务运行了多长时间、cpu利用率等
#define configGENERATE_RUN_TIME_STATS                      0

//包含额外的结构成员和函数以辅助执行可视化和跟踪
#define configUSE_TRACE_FACILITY                            0

//开启「任务状态格式化打印函数」,用来在串口输出任务列表、CPU 使用率、栈使用率等调试信息
#define configUSE_STATS_FORMATTING_FUNCTIONS            0

//-------------------------------------------------------------------------------------------------
/* Co-routine related definitions. */
//是否启用协程，协程是比任务更轻量级的执行单元, 但功能也更有限, 适合简单的状态机等场景，关闭 (现代 FreeRTOS 任务完全能替代它)
#define configUSE_CO_ROUTINES                              0

//给协程用的优先级配置，一起废弃
#define configMAX_CO_ROUTINE_PRIORITIES                     0

//-------------------------------------------------------------------------------------------------
/* Software timer related definitions. */
//软件定时器，开启
#define configUSE_TIMERS                        1

//设置软件定时器服务/守护任务的优先级
#define configTIMER_TASK_PRIORITY                          (configMAX_PRIORITIES - 1)

//设置软件定时器命令队列的长度
#define configTIMER_QUEUE_LENGTH                            10

//设置分配给软件定时器服务/守护任务的堆栈深度
#define configTIMER_TASK_STACK_DEPTH                        256

//-------------------------------------------------------------------------------------------------
/* Interrupt nesting behaviour configuration. */
/*---------------------------------------------------------------------------*
 *  中断优先级配置 (Cortex-M3 核心)
 *
 *  STM32F1 使用 4 位优先级 (NVIC_PRIORITY_BITS = 4)
 *  有效优先级位: [7:4], 共 16 级 (0 最高, 15 最低)
 *  FreeRTOS 使用 BASEPRI 寄存器屏蔽中断:
 *    - 优先级值 > configMAX_SYSCALL_INTERRUPT_PRIORITY 的中断会被屏蔽
 *    - 优先级值 <= configMAX_SYSCALL_INTERRUPT_PRIORITY 的中断不会被屏蔽
 *    - 只有不被屏蔽的中断才能调用 FreeRTOS API (FromISR 版本)
 *
 *  推荐配置:
 *    - 逻辑优先级 0~4: 紧急中断 (不能调用 RTOS API) — DMA/定时器关键中断
 *    - 逻辑优先级 5~15: 普通中断 (可调用 RTOS API) — UART/其他通信中断
 *---------------------------------------------------------------------------*/

/* 这些宏供 Cortex-M3 底层端口代码使用 */
#define configPRIO_BITS                         4

/* 最低中断优先级 (逻辑优先级 15) */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15

/* 可调用 FreeRTOS API 的最高逻辑优先级 (5)
 * 逻辑优先级 0~4 的中断不受 FreeRTOS 管理, 实时性最高但不能调用 RTOS API */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5

/* 以下两个宏将逻辑优先级转换为寄存器值 (左移 4 位, 对齐 [7:4] 位) */
//FreeRTOS 内核自身使用的中断优先级，应设置为最低优先级，以确保内核中断不会干扰应用程序的中断处理
#define configKERNEL_INTERRUPT_PRIORITY         (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

//可以安全调用 FreeRTOS 中断 API 的最高优先级
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

//可以安全调用 FreeRTOS 中断 API 的最高优先级，与configMAX_SYSCALL_INTERRUPT_PRIORITY完全等价，新版 FreeRTOS 的别名
#define configMAX_API_CALL_INTERRUPT_PRIORITY   configMAX_SYSCALL_INTERRUPT_PRIORITY

/*---------------------------------------------------------------------------*
 *  ARM Cortex-M3 中断向量映射
 *
 *  SVC/PendSV 必须映射到启动文件的向量表名:
 *    - SVC: 启动第一个任务时触发
 *    - PendSV: 上下文切换
 *
 *  SysTick 不在 FreeRTOS 层映射:
 *    SysTick_Handler 由 main.c 实现, 在其中同时调用:
 *      1. HAL_IncTick()     — 维护 HAL 滴答计数器 (HAL_Delay 等需要)
 *      2. xPortSysTickHandler() — FreeRTOS 内核滴答处理
 *    (如果直接映射 xPortSysTickHandler -> SysTick_Handler,
 *     HAL_Delay 将无法工作, 因为 HAL_IncTick 永远不会被调用)
 *---------------------------------------------------------------------------*/
#define vPortSVCHandler                         SVC_Handler
#define xPortPendSVHandler                      PendSV_Handler

//-------------------------------------------------------------------------------------------------
/* Define to trap errors during development. */
//调试断言，开发调试时抓 bug
#define configASSERT(x)                         \
    if ((x) == 0) {                             \
        taskDISABLE_INTERRUPTS();               \
        for (;;) { ; }                          \
    }

/*-------------------------------------------------------------------------------------------------
// FreeRTOS MPU specific definitions.MPU内存保护单元相关的宏，暂不需要，注释掉
// #define [configINCLUDE_APPLICATION_DEFINED_PRIVILEGED_FUNCTIONS](#configinclude_application_defined_privileged_functions) 0
// #define [configTOTAL_MPU_REGIONS](#configtotal_mpu_regions)                                8 /* Default value */
// #define [configTEX_S_C_B_FLASH](#configtex_s_c_b_flash)                                  0x07UL /* Default value */
// #define [configTEX_S_C_B_SRAM](#configtex_s_c_b_sram)                                   0x07UL /* Default value */
// #define [configENFORCE_SYSTEM_CALLS_FROM_KERNEL_ONLY](#configenforce_system_calls_from_kernel_only)            1
// #define [configALLOW_UNPRIVILEGED_CRITICAL_SECTIONS](#configallow_unprivileged_critical_sections)             1
// #define [configENABLE_ERRATA_837070_WORKAROUND](#configenable_errata_837070_workaround)                  1

/* ARMv8-M port specific configuration definitions. ARMv8‑M（Cortex‑M33/M23）专属的 FreeRTOS 端口配置宏，暂不需要，注释掉*/
// #define [configENABLE_TRUSTZONE](#configenable_trustzone)                            1
// #define [configRUN_FREERTOS_SECURE_ONLY](#configrun_freertos_secure_only)            1
// #define [configENABLE_MPU](#configenable_mpu)                                        1
// #define [configENABLE_FPU](#configenable_fpu)                                        1
// #define [configENABLE_MVE](#configenable_mve)                                        1

/* ARMv8-M secure side port related definitions.暂不需要，注释掉 */
// #define [secureconfigMAX_SECURE_CONTEXTS](#secureconfigmax_secure_contexts)         5

/* Optional functions - most linkers will remove unused functions anyway. */
// #define [INCLUDE_vTaskPrioritySet](#include-parameters)                1
// #define [INCLUDE_uxTaskPriorityGet](#include-parameters)               1
// #define [INCLUDE_vTaskDelete](#include-parameters)                     1
// #define [INCLUDE_vTaskSuspend](#include-parameters)                    1
// #define [INCLUDE_vTaskDelayUntil](#include-parameters)                 1
// #define [INCLUDE_vTaskDelay](#include-parameters)                      1
// #define [INCLUDE_xTaskGetSchedulerState](#include-parameters)          1
// #define [INCLUDE_xTaskGetCurrentTaskHandle](#include-parameters)       1
// #define [INCLUDE_uxTaskGetStackHighWaterMark](#include-parameters)     0
// #define [INCLUDE_uxTaskGetStackHighWaterMark2](#include-parameters)    0
// #define [INCLUDE_xTaskGetIdleTaskHandle](#include-parameters)          0
// #define [INCLUDE_eTaskGetState](#include-parameters)                   0
// #define [INCLUDE_xTimerPendFunctionCall](#include-parameters)          0
// #define [INCLUDE_xTaskAbortDelay](#include-parameters)                 0
// #define [INCLUDE_xTaskGetHandle](#include-parameters)                  0
// #define [INCLUDE_xTaskResumeFromISR](#include-parameters)              1
/*---------------------------------------------------------------------------*
 *  2. 可选 API 函数使能 (FreeRTOS V11 要求显式启用)
 *
 *  FreeRTOS V11 中几乎所有 API 函数都是可选的, 必须通过 INCLUDE_* 宏
 *  显式启用才会被编译进内核。如果缺少这些定义, 链接器会报 undefined reference。
 *---------------------------------------------------------------------------*/
#define INCLUDE_vTaskDelay                      1   /* vTaskDelay */
#define INCLUDE_xTaskDelayUntil                 1   /* xTaskDelayUntil */
#define INCLUDE_xTaskGetSchedulerState          1   /* xTaskGetSchedulerState (SysTick ISR 用) */
#define INCLUDE_xSemaphoreGetMutexHolder        0   /* 不需要 */
#define INCLUDE_xTimerPendFunctionCall          0   /* 不需要 */
#define INCLUDE_eTaskGetState                   0   /* 不需要 */
#define INCLUDE_uxTaskGetStackHighWaterMark     1   /* 调试栈水位用, 建议开启 */


/* A header file that defines trace macro can be included here. */

#if defined(__GNUC__)
  #define portFORCE_INLINE  __attribute__((always_inline)) inline
#endif

#endif /* FREERTOS_CONFIG_H */
