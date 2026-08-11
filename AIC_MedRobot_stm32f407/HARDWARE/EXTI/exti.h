
#ifndef __exti_H
#define __exti_H	 
#include "sys.h"  	
//////////////////////////////////////////////////////////////////////////////////	 
// STM32F407开发板
//外部中断 驱动代码	   					  
////////////////////////////////////////////////////////////////////////////////// 	 

typedef enum
{
	IMAGE_PARAM_BRIGHTNESS = 0,
	IMAGE_PARAM_CONTRAST,
	IMAGE_PARAM_SATURATION
} ImageParam;

void EXTIX_Init(void);
u32 EXTIX_GetMillis(void);
u8 EXTIX_GetImageSettings(u8 *selected, u8 *brightness,
                          u8 *contrast, u8 *saturation, u8 *visible);
u8 EXTIX_IsImageMenuVisible(void);
#endif

























