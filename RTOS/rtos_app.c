#include "rtos_app.h"
#include "common.h"        /* atk_8266_at_response_rdCmd */
#include "wifi_transfer.h" /* JPEG 图传接口 */
#include "ui.h"            /* UI_Task */
#include "led.h"
#include <string.h>

#if RTOS_ENABLE

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* 中断向量说明:
 * 启动文件向量表仍使用 SVC_Handler / PendSV_Handler / SysTick_Handler 名称。
 * RTOS 模式下由 stm32f4xx_it.c 的 __asm 跳板提供这三个名字, 直接 B 到
 * FreeRTOS 端口函数 vPortSVCHandler / xPortPendSVHandler / xPortSysTickHandler,
 * B 跳转不破坏 r14 的 EXC_RETURN 语义; 裸机模式由 stm32f4xx_it.c 提供空实现。
 */
/* ============================================================================
 * 任务参数
 * 内存预算: 控制2KB + UI1.5KB + WiFi1KB + JPEG1KB + 空闲0.5KB + TCB/队列等,
 * 总堆 10KB, 剩余约 2~3KB 余量(见 README.md「FreeRTOS 移植设计」)。
 * ==========================================================================*/
#define TASK_PRIO_CONTROL   4   /* 运动控制: 最高, 保证循迹实时性 */
#define TASK_PRIO_WIFI      3   /* WiFi 命令接收 */
#define TASK_PRIO_JPEG      3   /* JPEG 图传(与 WiFi 同优先级, 时间片轮转) */
#define TASK_PRIO_UI        1   /* 触摸/界面刷新: 最低, 可被随时抢占 */

#define TASK_STACK_CONTROL  512 /* 字 = 2KB */
#define TASK_STACK_WIFI     256 /* 字 = 1KB */
#define TASK_STACK_JPEG     256 /* 字 = 1KB */
#define TASK_STACK_UI       384 /* 字 = 1.5KB */

/* 周期(节拍, 1 tick = 1ms) */
#define PERIOD_UI_MS        20
#define PERIOD_WIFI_MS      10
#define PERIOD_JPEG_MS       5

static TaskHandle_t xUiTaskHandle   = NULL;
static TaskHandle_t xWifiTaskHandle = NULL;
static TaskHandle_t xJpegTaskHandle = NULL;
static SemaphoreHandle_t xLcdMutex  = NULL;
static QueueHandle_t xWifiCmdQueue  = NULL;

extern void vTaskCarControl(void *pvParameters);

/* ============================ 钩子函数 ============================ */
void vApplicationMallocFailedHook(void)
{
	/* 堆内存耗尽: 点亮 LED1 并停在这里, 便于发现内存不足 */
	LED1 = 0;
	for(;;) {}
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
	/* 任务栈溢出: 点亮 LED1 并停在这里 */
	(void)xTask;
	(void)pcTaskName;
	LED1 = 0;
	for(;;) {}
}

/* ============================ LCD 互斥 ============================ */
void RTOS_LcdLock(void)
{
	xSemaphoreTake(xLcdMutex, portMAX_DELAY);
}

void RTOS_LcdUnlock(void)
{
	xSemaphoreGive(xLcdMutex);
}

/* ========================= WiFi 命令队列 ========================= */
BaseType_t RTOS_WifiCmdReceive(WiFiCmdMsg *pMsg)
{
	return xQueueReceive(xWifiCmdQueue, pMsg, 0);
}

/* ======================== UI 任务 ======================== */
/* 触摸扫描 + 界面刷新 + 图像参数调节, 周期 20ms。
 * 与主控任务共用 LCD 互斥量, 避免两任务同时写屏造成显示撕裂。 */
static void vTaskUi(void *pvParameters)
{
	TickType_t xLastWake;

	(void)pvParameters;
	xLastWake = xTaskGetTickCount();
	for(;;)
	{
		vTaskDelayUntil(&xLastWake, PERIOD_UI_MS);
		RTOS_LcdLock();
		UI_Task();
		RTOS_LcdUnlock();
	}
}

/* ======================== WiFi 命令任务 ======================== */
/* 周期轮询 USART3 接收状态, 把原始命令字符串拷贝进队列,
 * 具体解析与电机动作仍由主控任务完成(电机只能有一个写者)。 */
static void vTaskWifi(void *pvParameters)
{
	TickType_t xLastWake;
	WiFiCmdMsg msg;
	char *pCmd;
	u32 len;

	(void)pvParameters;
	xLastWake = xTaskGetTickCount();
	for(;;)
	{
		vTaskDelayUntil(&xLastWake, PERIOD_WIFI_MS);//每10ms轮询一次WiFi命令

		pCmd = atk_8266_at_response_rdCmd(1);
		if(pCmd == 0)
			continue;

		len = strlen(pCmd);
		if(len == 0)
			continue;
		if(len >= WIFI_CMD_MSG_LEN)
			len = WIFI_CMD_MSG_LEN - 1;/* 队列消息长度限制, 避免溢出 */
		memcpy(msg.cmd, pCmd, len);/* 拷贝命令字符串 */
		msg.cmd[len] = 0;

		/* 队列满则丢弃: 遥控命令不积压, 保证最新命令尽快生效 */
		xQueueSend(xWifiCmdQueue, &msg, 0);
	}
}

/* ======================== JPEG 图传任务 ======================== */
/* 周期 5ms: 处理一帧 JPEG(加帧头/CRC)并通过 DMA 发出, 发送完成后重启
 * DCMI 采集。与主控任务的摄像头遮挡检测共享 jpeg_last_frame_bytes,
 * 由 DCMI 中断写、本任务读, 32 位读为原子操作, 无需加锁。 */
static void vTaskJpeg(void *pvParameters)
{
	TickType_t xLastWake;

	(void)pvParameters;
	xLastWake = xTaskGetTickCount();
	for(;;)
	{
		vTaskDelayUntil(&xLastWake, PERIOD_JPEG_MS);

		if(jpeg_data_ok == 1)
			jpeg_process_and_send();

		if(!USART3_DMA_IsBusy() && (jpeg_data_ok == 2))/* 发送完成, 重启 DCMI 采集 */
		{
			DMA2_Stream1->NDTR = JPEG_BUF_SIZE;
			DMA_SetCurrDataCounter(DMA2_Stream1, JPEG_BUF_SIZE);
			DMA_Cmd(DMA2_Stream1, ENABLE);
			DCMI_CaptureCmd(ENABLE);
			jpeg_data_ok = 0;
		}
	}
}

/* ============================ 入口 ============================ */
/* 硬件初始化(含 WiFi 配网、JPEG 切换)完成后调用, 内部创建任务并启动调度器 */
void RTOS_Start(void)
{
	BaseType_t xRet;

	xLcdMutex = xSemaphoreCreateMutex();/* 创建 LCD 互斥量 */
	xWifiCmdQueue = xQueueCreate(4, sizeof(WiFiCmdMsg));/* 创建 WiFi 命令队列, 最多 4 条命令 */
	if((xLcdMutex == NULL) || (xWifiCmdQueue == NULL))
	{
		vApplicationMallocFailedHook();
	}
    /* 创建任务 */
	xRet = xTaskCreate(vTaskCarControl, "CarCtrl", TASK_STACK_CONTROL,
	                   NULL, TASK_PRIO_CONTROL, NULL);
	if(xRet != pdPASS) vApplicationMallocFailedHook();

	xRet = xTaskCreate(vTaskWifi, "WifiCmd", TASK_STACK_WIFI,
	                   NULL, TASK_PRIO_WIFI, &xWifiTaskHandle);
	if(xRet != pdPASS) vApplicationMallocFailedHook();

	xRet = xTaskCreate(vTaskJpeg, "JpegTx", TASK_STACK_JPEG,
	                   NULL, TASK_PRIO_JPEG, &xJpegTaskHandle);
	if(xRet != pdPASS) vApplicationMallocFailedHook();

	xRet = xTaskCreate(vTaskUi, "UiTask", TASK_STACK_UI,
	                   NULL, TASK_PRIO_UI, &xUiTaskHandle);
	if(xRet != pdPASS) vApplicationMallocFailedHook();

	/* 启动调度器(不返回) */
	vTaskStartScheduler();

	/* 理论不可达: 调度器启动失败 */
	for(;;) {}
}

#endif /* RTOS_ENABLE */
