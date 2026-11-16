//STM32F4 medicine delivery car: WiFi link + JPEG stream + line following

#include "stm32f4xx.h"
#include <string.h>
#include "usart.h"
#include "sys.h"
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


/* ==================== Boot ====================
 * Order matters: DWT and the IWDG are started first so that a hang in any
 * later initialisation is caught. The WiFi bring-up is bounded (see
 * atk_8266_wifista_config): a missing AP or server no longer keeps the car
 * out of the main loop forever. On timeout the car keeps running offline and
 * the link recovery state machine retries in the background.
 */
int main(void)
{
	u8 wifi_ok;

	uart_init(115200);
	delay_init(168);
	DWT_Init();          /* cycle counter: jitter stats + echo timestamps */
	WDG_Init();
	LED_Init();

	LCD_Init();
	WDG_Feed();
	Adc_Init();          /* TIM2 -> ADC3 scan -> DMA (non blocking) */

	usart2_init(9600);
	usart3_init(115200);
	USART3_DMA_TX_Init();

#if TOUCH_MODULE_ENABLE
	tp_dev.init();
#endif
	UI_Init();
	WDG_Feed();

	MOTOR_Init();
	EXTIX_Init();
	HC_SR04_Init();      /* EXTI3 echo capture, no blocking measurement */
	WDG_Feed();

	CAMERA_Init();
	WDG_Feed();

	LCD_ShowString(30, 100, 210, 24, 16, "WiFi connecting...");
	wifi_ok = atk_8266_wifista_config();
	WDG_Feed();
	if (wifi_ok == 0)
	{
		LCD_ShowString(30, 100, 210, 24, 16, "WiFi online       ");
	}
	else
	{
		/* boot timeout: offline / manual mode, retry from the WiFi task */
		LCD_ShowString(30, 100, 210, 24, 16, "WiFi OFFLINE      ");
		printf("BOOT_TIMEOUT\r\n");
		WIFI_LinkStartRecovery();
	}

	OV2640_JPEG_Mode();
	dcmi_rx_callback = jpeg_frame_callback;
	DCMI_DMA_Init((u32)jpeg_buf, JPEG_BUF_SIZE, DMA_MemoryDataSize_Word, DMA_MemoryInc_Enable);
	OV2640_OutSize_Set(jpeg_img_size_tbl[3][1], jpeg_img_size_tbl[3][0]);  // QVGA 320x240
	CAMERA_SetJpegMode(1);
	DCMI_Start();

	delay_ms(200);
	WDG_Feed();

	printf("System ready - WiFi CTRL + LineFollow\r\n");

#if RTOS_ENABLE
	RTOS_Start();
#else
	CAR_LineFollow();
#endif
}
