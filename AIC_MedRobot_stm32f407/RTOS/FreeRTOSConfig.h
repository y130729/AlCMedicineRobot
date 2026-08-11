#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

//////////////////////////////////////////////////////////////////////////////////
// FreeRTOS Kernel V11.1.0 配置文件 (STM32F407ZG / ARMCC V5 / 168MHz)
// 与项目原有 RTOS 无关的设置均按最简配置，以节省 RAM。
//////////////////////////////////////////////////////////////////////////////////

#include "stm32f4xx.h"
#include "rtos_conf.h"

/* ------------------------ 基础配置 ------------------------ */
#define configUSE_PREEMPTION                      1      // 抢占式调度
#define configUSE_TIME_SLICING                    1      // 同优先级时间片轮转
#define configUSE_PORT_OPTIMISED_TASK_SELECTION   1      // 使用硬件 CLZ 查找最高优先级任务
#define configUSE_TICKLESS_IDLE                   0      // 不用低功耗 tickless
#define configCPU_CLOCK_HZ                        ( 168000000UL )   // 系统主频 168MHz
#define configTICK_RATE_HZ                        ( 1000 )          // 系统节拍 1ms
#define configMAX_PRIORITIES                      ( 5 )             // 最大任务优先级数
#define configMINIMAL_STACK_SIZE                  ( 128 )           // 空闲任务栈(字)
#define configTOTAL_HEAP_SIZE                     ( 10 * 1024 )     // FreeRTOS 堆(字节)
#define configMAX_TASK_NAME_LEN                   ( 16 )
#define configUSE_16_BIT_TICKS                    0                 // 32 位 Tick
#define configIDLE_SHOULD_YIELD                   1
#define configUSE_TASK_NOTIFICATIONS              1
#define configUSE_MUTEXES                         1                 // 互斥量(用于 LCD 访问保护)
#define configUSE_RECURSIVE_MUTEXES               0
#define configUSE_COUNTING_SEMAPHORES             0
#define configUSE_QUEUE_SETS                      0
#define configQUEUE_REGISTRY_SIZE                 4
#define configUSE_NEWLIB_REENTRANT                0
#define configENABLE_BACKWARD_COMPATIBILITY       0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS   0
#define configSUPPORT_STATIC_ALLOCATION           0
#define configSUPPORT_DYNAMIC_ALLOCATION          1

/* ------------------------ 调试与钩子 ------------------------ */
/* 钩子仅在 RTOS 模式下启用: 裸机模式下内核不会被链接, 钩子若被引用会导致链接失败 */
#if RTOS_ENABLE
#define configCHECK_FOR_STACK_OVERFLOW            2      // 检查任务栈溢出(第2种)
#define configUSE_MALLOC_FAILED_HOOK              1      // 堆分配失败钩子
#else
#define configCHECK_FOR_STACK_OVERFLOW            0
#define configUSE_MALLOC_FAILED_HOOK              0
#endif
#define configUSE_IDLE_HOOK                       0
#define configUSE_TICK_HOOK                       0
#define configUSE_DAEMON_TASK_STARTUP_HOOK        0

/* ------------------------ 中断优先级 ------------------------ */
// STM32F407 实现 4 位优先级, 0x50 = 数值 5(逻辑优先级较高一侧的边界)
#define configPRIO_BITS                           4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY            15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY       5
#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

/* ------------------------ 断言 ------------------------ */
// 断言失败时关中断并死循环，方便调试时停在现场
#define configASSERT( x ) if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ;; ); }

/* ------------------------ 应用钩子(在 rtos_app.c 实现) ------------------------ */
/* vApplicationStackOverflowHook 由 task.h 声明, 这里只需声明内存失败钩子 */
void vApplicationMallocFailedHook( void );

/* ------------------------ API 裁剪 ------------------------ */
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    0
#define INCLUDE_vTaskResume                     0
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     0
#define INCLUDE_xQueueGetMutexHolder            0
#define INCLUDE_eTaskGetState                   0
#define INCLUDE_xTaskGetIdleTaskHandle          0
#define INCLUDE_xTimerPendFunctionCall          0
#define INCLUDE_pcTaskGetTaskName               0

#endif /* FREERTOS_CONFIG_H */
