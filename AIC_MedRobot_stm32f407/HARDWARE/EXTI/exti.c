#include "exti.h"
#include "key.h"

#define KEY_DEBOUNCE_MS          30
#define WKUP_DOUBLE_CLICK_MS    600
#define IMAGE_VALUE_STEP          1
#define IMAGE_VALUE_MAX           4

static volatile u32 system_ms = 0;
static volatile u8 selected_param = IMAGE_PARAM_BRIGHTNESS;
static volatile u8 image_brightness = 2;
static volatile u8 image_contrast = 2;
static volatile u8 image_saturation = 2;
static volatile u8 image_settings_changed = 1;
static volatile u8 image_menu_visible = 0;
static volatile u8 wkup_click_pending = 0;
static volatile u32 wkup_first_click_ms = 0;

static volatile u32 key0_last_ms = 0;
static volatile u32 key1_last_ms = 0;
static volatile u32 wkup_last_ms = 0;

u32 EXTIX_GetMillis(void)
{
	return system_ms;
}

static void IMAGE_IncreaseSelected(void)
{
	volatile u8 *value;
	if(selected_param == IMAGE_PARAM_BRIGHTNESS) value = &image_brightness;
	else if(selected_param == IMAGE_PARAM_CONTRAST) value = &image_contrast;
	else value = &image_saturation;

	if(*value <= (IMAGE_VALUE_MAX - IMAGE_VALUE_STEP))
		*value += IMAGE_VALUE_STEP;
	else
		*value = IMAGE_VALUE_MAX;
	image_settings_changed = 1;
}

static void IMAGE_DecreaseSelected(void)
{
	volatile u8 *value;
	if(selected_param == IMAGE_PARAM_BRIGHTNESS) value = &image_brightness;
	else if(selected_param == IMAGE_PARAM_CONTRAST) value = &image_contrast;
	else value = &image_saturation;

	if(*value >= IMAGE_VALUE_STEP)
		*value -= IMAGE_VALUE_STEP;
	else
		*value = 0;
	image_settings_changed = 1;
}

u8 EXTIX_GetImageSettings(u8 *selected, u8 *brightness,
	                      u8 *contrast, u8 *saturation, u8 *visible)
{
	u8 changed = image_settings_changed;
	/* 先清除标志；若随后有按键中断，新变化会保留到下一轮处理。 */
	image_settings_changed = 0;
	*selected = selected_param;
	*brightness = image_brightness;
	*contrast = image_contrast;
	*saturation = image_saturation;
	*visible = image_menu_visible;
	return changed;
}

u8 EXTIX_IsImageMenuVisible(void)
{
	return image_menu_visible;
}

void TIM6_DAC_IRQHandler(void)
{
	if(TIM_GetITStatus(TIM6, TIM_IT_Update) != RESET)
	{
		TIM_ClearITPendingBit(TIM6, TIM_IT_Update);
		system_ms++;
		if(wkup_click_pending &&
		   ((system_ms - wkup_first_click_ms) > WKUP_DOUBLE_CLICK_MS))
		{
			wkup_click_pending = 0;
			if(image_menu_visible)
			{
				selected_param++;
				if(selected_param > IMAGE_PARAM_SATURATION)
					selected_param = IMAGE_PARAM_BRIGHTNESS;
				image_settings_changed = 1;
			}
		}
	}
}

/* WK_UP：PA0，上升沿触发 */
void EXTI0_IRQHandler(void)
{
	u32 now;

	if(EXTI_GetITStatus(EXTI_Line0) != RESET)
	{
		EXTI_ClearITPendingBit(EXTI_Line0);
		now = system_ms;
		if((WK_UP == 1) && ((now - wkup_last_ms) >= KEY_DEBOUNCE_MS))
		{
			if(wkup_click_pending &&
			   ((now - wkup_first_click_ms) <= WKUP_DOUBLE_CLICK_MS))
			{
				wkup_click_pending = 0;
				image_menu_visible = !image_menu_visible;
				image_settings_changed = 1;
			}
			else
			{
				wkup_click_pending = 1;
				wkup_first_click_ms = now;
			}
			wkup_last_ms = now;
		}
	}
}

/* K0(PB9)、K1(PB8)：下降沿触发，共用EXTI9_5中断 */
void EXTI9_5_IRQHandler(void)
{
	u32 now = system_ms;

	if(EXTI_GetITStatus(EXTI_Line9) != RESET)
	{
		EXTI_ClearITPendingBit(EXTI_Line9);
		if(image_menu_visible && (KEY0 == 0) &&
		   ((now - key0_last_ms) >= KEY_DEBOUNCE_MS))
		{
			key0_last_ms = now;
			IMAGE_IncreaseSelected();
		}
	}

	if(EXTI_GetITStatus(EXTI_Line8) != RESET)
	{
		EXTI_ClearITPendingBit(EXTI_Line8);
		if(image_menu_visible && (KEY1 == 0) &&
		   ((now - key1_last_ms) >= KEY_DEBOUNCE_MS))
		{
			key1_last_ms = now;
			IMAGE_DecreaseSelected();
		}
	}
}

void EXTIX_Init(void)
{
	NVIC_InitTypeDef NVIC_InitStructure;
	EXTI_InitTypeDef EXTI_InitStructure;
	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;

	KEY_Init();
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);

	SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOA, EXTI_PinSource0);
	SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, EXTI_PinSource8);
	SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, EXTI_PinSource9);
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

	EXTI_StructInit(&EXTI_InitStructure);
	EXTI_InitStructure.EXTI_Line = EXTI_Line0;
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
	EXTI_Init(&EXTI_InitStructure);

	EXTI_InitStructure.EXTI_Line = EXTI_Line8 | EXTI_Line9;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
	EXTI_Init(&EXTI_InitStructure);

	NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	NVIC_InitStructure.NVIC_IRQChannel = EXTI9_5_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_Init(&NVIC_InitStructure);

	/* TIM6每1ms产生一次更新中断，APB1定时器时钟为84MHz */
	TIM_TimeBaseStructInit(&TIM_TimeBaseStructure);
	TIM_TimeBaseStructure.TIM_Prescaler = 84 - 1;
	TIM_TimeBaseStructure.TIM_Period = 1000 - 1;
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInit(TIM6, &TIM_TimeBaseStructure);
	TIM_ClearITPendingBit(TIM6, TIM_IT_Update);
	TIM_ITConfig(TIM6, TIM_IT_Update, ENABLE);

	NVIC_InitStructure.NVIC_IRQChannel = TIM6_DAC_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_Init(&NVIC_InitStructure);

	TIM_Cmd(TIM6, ENABLE);
}
