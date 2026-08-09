/* ==================== 小车应用层 ====================
 * 由原 main.c 拆分而来: 循迹/避障/遥控/图传的 7 个步骤函数,
 * 裸机主循环 (CAR_LineFollow) 与 RTOS 主控任务 (vTaskCarControl)
 * 共用同一套步骤函数。
 * WiFi 命令解析见 wifi_cmd.c。 */
#include "stm32f4xx.h"
#include <string.h>
#include "led.h"
#include "exti.h"
#include "timer.h"
#include "adc.h"
#include "ui.h"
#include "camera.h"
#include "common.h"
#include "dcmi.h"
#include "wifi_transfer.h"
#include "motor.h"
#include "rtos_app.h"
#include "wifi_cmd.h"
#include "car_app.h"

/* ==================== 循迹/避障参数 ==================== */
#define RANGE_SAMPLE_PERIOD_MS  100
#define ADC_SAMPLE_PERIOD_MS    250
#define OBSTACLE_DISTANCE_CM   25.0f
#define OBSTACLE_CLEAR_CM      28.0f

/* 黑线ADC值高于白色区域 */
#define LINE_BLACK_IS_HIGH          1
#define LINE_BLACK_THRESHOLD     2000
#define LINE_CENTER_TOLERANCE       0
#define LINE_FORWARD_STEP_MS      500
#define LINE_SAMPLE_TIMES           2


/* ==================== ADC/LED 杈呭姪 ==================== */
u16 adcL = 0;
u16 adcM = 0;
u16 adcR = 0;

static void ADC_ReadLineSensors(void)
{
	adcL = Get_Adc_Average(ADC_Channel_5, LINE_SAMPLE_TIMES);
	adcM = Get_Adc_Average(ADC_Channel_4, LINE_SAMPLE_TIMES);
	adcR = Get_Adc_Average(ADC_Channel_15, LINE_SAMPLE_TIMES);
}

static u8 LINE_IsBlack(u16 adc_value)
{
#if LINE_BLACK_IS_HIGH
	return adc_value >= LINE_BLACK_THRESHOLD;
#else
	return adc_value <= LINE_BLACK_THRESHOLD;
#endif
}

/* ==================== 循迹主循环(整合WiFi/JPEG) ==================== */

/* 循迹/避障/遥控共享状态: 裸机与RTOS两种模式共用同一组步骤函数 */
typedef struct
{
	float distance;
	u32 now_ms;
	u32 next_range_ms;
	u32 next_lcd_ms;
	u32 startup_end_ms;
	u8  obstacle_active;
	u8  camera_occlusion_active;
	s8  last_direction;
	u16 pwmval;
	u8  line_enabled;
	u8  wifi_override;
	u32 wifi_override_end;
} CAR_Ctx;

static void CAR_CtxInit(CAR_Ctx *ctx)
{
	ctx->distance = -1.0f;
	ctx->now_ms = 0;
	ctx->next_range_ms = 0;
	ctx->next_lcd_ms = 0;
	ctx->startup_end_ms = 0;
	ctx->obstacle_active = 0;
	ctx->camera_occlusion_active = 0;
	ctx->last_direction = 0;
	ctx->pwmval = 300;
	ctx->line_enabled = 0;
	ctx->wifi_override = 0;
	ctx->wifi_override_end = 0;
}

/* ---- 步骤1: WiFi 命令检查 ----
 * 裸机: 直接读 USART3 缓冲并解析;
 * RTOS: 从 WiFi 任务填充的命令队列取一条, 解析执行仍在主控任务完成,
 *       保证电机只由主控任务驱动。 */
static void CAR_StepWifi(CAR_Ctx *ctx)
{
	char *pMsgBuff = 0;

#if RTOS_ENABLE
	WiFiCmdMsg msg;
	if (RTOS_WifiCmdReceive(&msg))
	{
		pMsgBuff = msg.cmd;
		if (wifi_dispatch_commands(pMsgBuff, &ctx->pwmval, &ctx->line_enabled))
		{
			ctx->wifi_override = 1;
			ctx->wifi_override_end = ctx->now_ms + 500;
		}
	}
#else
	pMsgBuff = atk_8266_at_response_rdCmd(1);
	if (pMsgBuff)
	{
		if (wifi_dispatch_commands(pMsgBuff, &ctx->pwmval, &ctx->line_enabled))
		{
			ctx->wifi_override = 1;
			ctx->wifi_override_end = ctx->now_ms + 500;
		}
	}
#endif
	if (ctx->wifi_override && (ctx->now_ms >= ctx->wifi_override_end))
	{
		ctx->wifi_override = 0;
		ctx->startup_end_ms = ctx->now_ms + MOTOR_START_TIME_MS;
	}
}

/* ---- 步骤2: JPEG 帧处理与发送 ----
 * 裸机: 在主循环内轮询处理; RTOS: 由独立的图传任务调用, 本函数仅在裸机使用 */
#if !RTOS_ENABLE
static void CAR_StepJpeg(void)
{
	if (jpeg_data_ok == 1)
		jpeg_process_and_send();
	if (!USART3_DMA_IsBusy() && (jpeg_data_ok == 2))
	{
		DMA2_Stream1->NDTR = JPEG_BUF_SIZE;
		DMA_SetCurrDataCounter(DMA2_Stream1, JPEG_BUF_SIZE);
		DMA_Cmd(DMA2_Stream1, ENABLE);
		DCMI_CaptureCmd(ENABLE);
		jpeg_data_ok = 0;
	}
}
#endif

/* ---- 步骤3: UI 任务(裸机); RTOS 下由独立 UI 任务执行 ---- */
#if !RTOS_ENABLE
static void CAR_StepUi(void)
{
#if TOUCH_MODULE_ENABLE
	RTOS_LcdLock();
	UI_Task();
	RTOS_LcdUnlock();
#endif
}
#endif

/* ---- 步骤4: 摄像头任务 + 遮挡检测 ---- */
static void CAR_StepCamera(CAR_Ctx *ctx)
{
	RTOS_LcdLock();
	CAMERA_Task(ctx->now_ms);
	RTOS_LcdUnlock();

	if (CAMERA_IsOccluded())
	{
		if (!ctx->camera_occlusion_active)
		{
			ctx->camera_occlusion_active = 1;
			MOTOR_Stop();
		}
	}
	else if (ctx->camera_occlusion_active)
	{
		ctx->camera_occlusion_active = 0;
		ctx->startup_end_ms = EXTIX_GetMillis() + MOTOR_START_TIME_MS;
	}
}

/* ---- 步骤5: 超声波测距 + 避障逻辑 ---- */
static void CAR_StepRange(CAR_Ctx *ctx)
{
	if (ctx->now_ms >= ctx->next_range_ms)
	{
		ctx->distance = HC_SR04_GetDistance();
		RTOS_LcdLock();
		UI_SetDistance(ctx->distance);
		RTOS_LcdUnlock();
		ctx->next_range_ms = EXTIX_GetMillis() + 50;

		if (!UI_IsAvoidEnabled())
		{
			ctx->obstacle_active = 0;
			RTOS_LcdLock();
			UI_SetObstacle(0);
			RTOS_LcdUnlock();
		}
		else if (ctx->distance >= 0.0f)
		{
			if (!ctx->obstacle_active && (ctx->distance <= OBSTACLE_DISTANCE_CM))
			{
				ctx->obstacle_active = 1;
				MOTOR_Stop();
				RTOS_LcdLock();
				UI_SetObstacle(1);
				RTOS_LcdUnlock();
			}
			else if (ctx->obstacle_active && (ctx->distance >= OBSTACLE_CLEAR_CM))
			{
				ctx->obstacle_active = 0;
				ctx->startup_end_ms = EXTIX_GetMillis() + MOTOR_START_TIME_MS;
				RTOS_LcdLock();
				UI_SetObstacle(0);
				RTOS_LcdUnlock();
			}
		}
	}
}

/* ---- 步骤6: ADC 采样 + LED 心跳 ---- */
static void CAR_StepAdc(CAR_Ctx *ctx)
{
	ADC_ReadLineSensors();
	if (EXTIX_GetMillis() >= ctx->next_lcd_ms)
	{
		LED2 = !LED2;
		ctx->next_lcd_ms = EXTIX_GetMillis() + ADC_SAMPLE_PERIOD_MS;
	}
}

/* ---- 步骤7: 避障/遮挡/遥控暂停 -> 起步加速 -> 循迹逻辑 ---- */
static void CAR_StepMotion(CAR_Ctx *ctx)
{
	if (ctx->obstacle_active || ctx->camera_occlusion_active || ctx->wifi_override)
	{
		if (ctx->obstacle_active || ctx->camera_occlusion_active || !ctx->line_enabled)
			MOTOR_Stop();
		return;
	}

	/* 未收到 startTask 前不执行循迹, 小车保持静止 */
	if (!ctx->line_enabled)
	{
		MOTOR_Stop();
		return;
	}

	if ((s32)(ctx->startup_end_ms - EXTIX_GetMillis()) > 0)
	{
		MOTOR_ForwardDual(MOTOR_LEFT_START_SPEED, MOTOR_START_SPEED);
		return;
	}

	if (!LINE_IsBlack(adcL) && !LINE_IsBlack(adcM) && !LINE_IsBlack(adcR))
	{
		if (ctx->last_direction < 0)
			MOTOR_LineSearchLeft();
		else if (ctx->last_direction > 0)
			MOTOR_LineSearchRight();
		else
			MOTOR_Stop();
	}
	else if (((u32)adcM + LINE_CENTER_TOLERANCE >= adcL) &&
	         ((u32)adcM + LINE_CENTER_TOLERANCE >= adcR))
	{
		MOTOR_LineForward();
		ctx->last_direction = 0;
	}
	else if (adcL > adcR)
	{
		MOTOR_LineTurnLeft();
		ctx->last_direction = -1;
	}
	else
	{
		MOTOR_LineTurnRight();
		ctx->last_direction = 1;
	}
}

/* ==================== 裸机主循环: 保持原有行为 ==================== */
void CAR_LineFollow(void)
{
	CAR_Ctx ctx;

	CAR_CtxInit(&ctx);
	ctx.startup_end_ms = EXTIX_GetMillis() + MOTOR_START_TIME_MS;

	while (1)
	{
		ctx.now_ms = EXTIX_GetMillis();

		CAR_StepWifi(&ctx);
#if !RTOS_ENABLE
		CAR_StepJpeg();
		CAR_StepUi();
#endif
		CAR_StepCamera(&ctx);
		CAR_StepRange(&ctx);
		CAR_StepAdc(&ctx);
		CAR_StepMotion(&ctx);
	}
}

#if RTOS_ENABLE
/* ==================== RTOS 主控任务 ====================
 * 周期 2ms, 复用与裸机完全相同的步骤函数(除 UI/JPEG 由独立任务承担)。
 * WiFi 命令经队列送达, 超声波测距的 30ms 忙等不再阻塞 UI/图传任务。 */
#define CAR_CONTROL_PERIOD_TICKS   2

void vTaskCarControl(void *pvParameters)
{
	CAR_Ctx ctx;
	TickType_t xLastWake;

	(void)pvParameters;
	CAR_CtxInit(&ctx);
	ctx.startup_end_ms = EXTIX_GetMillis() + MOTOR_START_TIME_MS;

	xLastWake = xTaskGetTickCount();
	while (1)
	{
		vTaskDelayUntil(&xLastWake, CAR_CONTROL_PERIOD_TICKS);

		ctx.now_ms = EXTIX_GetMillis();

		CAR_StepWifi(&ctx);
		CAR_StepCamera(&ctx);
		CAR_StepRange(&ctx);
		CAR_StepAdc(&ctx);
		CAR_StepMotion(&ctx);
	}
}
#endif
