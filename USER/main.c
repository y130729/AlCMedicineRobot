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
#include "car_app.h"


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


