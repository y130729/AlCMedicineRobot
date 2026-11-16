#ifndef __CAR_APP_H
#define __CAR_APP_H

#include "rtos_app.h"

/* 电容触摸屏总开关: 1 = 初始化并持续扫描(默认开启) */
#define TOUCH_MODULE_ENABLE  1

/* 裸机模式主循环(内部 while(1), 不返回) */
void CAR_LineFollow(void);
/* 获取当前运动状态文本(供 UI 数据页显示), buf 至少 12 字节 */
void CAR_GetState(char *buf);
/* Print one STATS line (control-loop p50/p99/max, overruns, watchdog). */
void CAR_PrintStats(void);

/* ==================== 配送任务命令(P0 闭环协议) ==================== */
typedef enum
{
	CAR_CMD_TASK_START = 0,   /* TASK_START,<task_id>,<from>,<to> */
	CAR_CMD_STOP,             /* STOP,<task_id>,<station_id> */
	CAR_CMD_TASK_CONFIRM,     /* TASK_CONFIRM,<task_id>,<load|unload>,<user> */
	CAR_CMD_TASK_PAUSE,       /* TASK_PAUSE,<task_id> */
	CAR_CMD_TASK_RESUME,      /* TASK_RESUME,<task_id> */
	CAR_CMD_TASK_CANCEL       /* TASK_CANCEL,<task_id> */
} CAR_TaskCmd;

/* 处理上位机任务命令(wifi_cmd.c 调用); args 为去掉命令名后的参数段 */
void CAR_OnTaskCommand(u8 cmd, char *args, u8 *line_enabled);

#if RTOS_ENABLE
/* RTOS 主控任务(周期 2ms, 复用与裸机相同的步骤函数) */
void vTaskCarControl(void *pvParameters);
#endif

#endif /* __CAR_APP_H */
