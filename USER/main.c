//STM32F4工程-库函数版本
//整合 WiFi遥控 + JPEG图传 + 循迹小车

#include "stm32f4xx.h"
#include <string.h>
#include "usart.h"
#include "delay.h"
#include "led.h"
#include "exti.h"
#include "timer.h"
#include "adc.h"
#include "lcd.h"
#include "touch.h"
#include "ui.h"
#include "camera.h"

#include "usart2.h"
#include "usart3.h"
#include "common.h"
#include "wifista.h"
#include "dcmi.h"
#include "ov2640.h"
#include "wifi_transfer.h"
#include "motor.h"
#include "rtos_app.h"

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

/* 默认关闭，确认使用电容触摸屏后改为1，即可初始化并持续扫描 */
#define TOUCH_MODULE_ENABLE          1

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

/* ==================== WiFi 遥控命令处理 ==================== */
static u8 handle_wifi_command(char *pMsgBuff, u16 *pwmval)
{
	if (pMsgBuff == 0) return 0;

	if (strstr((const char*)pMsgBuff, (const char*)"forWard"))
		{ MOTOR_Forward(*pwmval);  return 1; }
	if (strstr((const char*)pMsgBuff, (const char*)"backWard"))
		{ MOTOR_Backward(*pwmval); return 1; }
	if (strstr((const char*)pMsgBuff, (const char*)"lefTurn"))
		{ MOTOR_TurnLeft(*pwmval); return 1; }
	if (strstr((const char*)pMsgBuff, (const char*)"righTurn"))
		{ MOTOR_TurnRight(*pwmval); return 1; }
	if (strstr((const char*)pMsgBuff, (const char*)"letStop"))
		{ MOTOR_Stop(); return 1; }
	if (strstr((const char*)pMsgBuff, (const char*)"speedUp"))
	{
		*pwmval += 50;
		if (*pwmval > 500) *pwmval = 500;
		return 1;
	}
	if (strstr((const char*)pMsgBuff, (const char*)"speedDown"))
	{
		if (*pwmval >= 100) *pwmval -= 50;
		else *pwmval = 50;
		return 1;
	}
	return 0;
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
		if (handle_wifi_command(pMsgBuff, &ctx->pwmval))
		{
			ctx->wifi_override = 1;
			ctx->wifi_override_end = ctx->now_ms + 500;
		}
	}
#else
	pMsgBuff = atk_8266_at_response_rdCmd(1);
	if (pMsgBuff)
	{
		if (handle_wifi_command(pMsgBuff, &ctx->pwmval))
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
		if (ctx->obstacle_active || ctx->camera_occlusion_active)
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

/* ==================== 主函数 ==================== */
int main(void)
{
	uart_init(115200);
	delay_init(168);
	LED_Init();
	LCD_Init();
	Adc_Init();

	/* WiFi相关初始化 */
	usart2_init(9600);
	usart3_init(115200);
	USART3_DMA_TX_Init();

#if TOUCH_MODULE_ENABLE
	tp_dev.init();
#endif
	UI_Init();

	MOTOR_Init();
	EXTIX_Init();
	HC_SR04_Init();
	TIM3_Int_Init(1000 - 1, 84 - 1);

	/* 摄像头初始化(默认RGB565模式) */
	CAMERA_Init();

	/* WiFi连接 */
	LCD_ShowString(30, 100, 210, 24, 16, "WiFi connecting...");
	atk_8266_wifista_config();
	LCD_ShowString(30, 100, 210, 24, 16, "WiFi online       ");

	/* 切换为JPEG模式以支持WiFi图传 */
	OV2640_JPEG_Mode();
	dcmi_rx_callback = jpeg_frame_callback;
	DCMI_DMA_Init((u32)jpeg_buf, JPEG_BUF_SIZE, DMA_MemoryDataSize_Word, DMA_MemoryInc_Enable);
	OV2640_OutSize_Set(jpeg_img_size_tbl[3][1], jpeg_img_size_tbl[3][0]);  // QVGA 320x240
	CAMERA_SetJpegMode(1);
	DCMI_Start();

	delay_ms(200);

	printf("System ready - WiFi CTRL + LineFollow\r\n");

	/* 黑线循迹 + WiFi遥控 + JPEG图传 */
#if RTOS_ENABLE
	/* RTOS模式: 创建任务并启动调度器(不返回) */
	RTOS_Start();
#else
	/* 裸机模式: 保持原有大循环 */
	CAR_LineFollow();
#endif
}


