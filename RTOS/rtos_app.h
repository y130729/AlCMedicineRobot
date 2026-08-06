#ifndef __RTOS_APP_H
#define __RTOS_APP_H

#include "rtos_conf.h"
#include "sys.h"

#if RTOS_ENABLE

#include "FreeRTOS.h"
#include "queue.h"

/* WiFi 命令消息: 由 WiFi 任务从 USART3 接收缓冲区拷贝入队,
 * 控制任务出队后调用原 handle_wifi_command 解析执行。 */
#define WIFI_CMD_MSG_LEN   64
typedef struct
{
	char cmd[WIFI_CMD_MSG_LEN];
} WiFiCmdMsg;

/* RTOS 业务层入口: 创建所有任务并启动调度器(不返回) */
void RTOS_Start(void);

/* LCD 访问互斥(由 UI 任务与主控任务共用, 避免显示撕裂) */
void RTOS_LcdLock(void);
void RTOS_LcdUnlock(void);

/* 从 WiFi 命令队列取一条命令(非阻塞, 0 超时) */
BaseType_t RTOS_WifiCmdReceive(WiFiCmdMsg *pMsg);

/* 主控任务(定义在 main.c, 复用原有循迹逻辑) */
void vTaskCarControl(void *pvParameters);

#else

/* 裸机模式: 互斥为空操作, 保持调用点统一 */
#define RTOS_LcdLock()    ((void)0)
#define RTOS_LcdUnlock()  ((void)0)

#endif /* RTOS_ENABLE */

#endif /* __RTOS_APP_H */
