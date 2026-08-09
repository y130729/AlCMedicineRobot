#ifndef __CAR_APP_H
#define __CAR_APP_H

#include "rtos_app.h"

/* 电容触摸屏总开关: 1 = 初始化并持续扫描(默认开启) */
#define TOUCH_MODULE_ENABLE  1

/* 裸机模式主循环(内部 while(1), 不返回) */
void CAR_LineFollow(void);

#if RTOS_ENABLE
/* RTOS 主控任务(周期 2ms, 复用与裸机相同的步骤函数) */
void vTaskCarControl(void *pvParameters);
#endif

#endif /* __CAR_APP_H */
