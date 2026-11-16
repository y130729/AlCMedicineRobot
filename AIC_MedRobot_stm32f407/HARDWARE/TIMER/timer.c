#include "timer.h"
#include "delay.h"

extern vu16 USART3_RX_STA;
extern vu16 USART2_RX_STA;

/* ==================== HC-SR04 (non blocking) ====================
 * PF3 is not a timer channel on the STM32F407, so the echo pulse is timed
 * with EXTI3 (both edges) plus the DWT cycle counter instead of a blocking
 * delay loop. HC_SR04_Start() only emits the 11 us trigger; the control loop
 * polls HC_SR04_Poll() and never stalls waiting for the echo.
 * The old implementation blocked the control loop for up to 60 ms.
 */
#define HC_SR04_TRIG_US            11U
#define HC_SR04_ECHO_TIMEOUT_US 45000UL
#define HC_SR04_MIN_PULSE_US      100UL
#define HC_SR04_MAX_PULSE_US    30000UL

#define HC_SR04_IDLE        0
#define HC_SR04_WAIT_RISE   1
#define HC_SR04_WAIT_FALL   2
#define HC_SR04_DONE        3

static volatile u8  hc_state = HC_SR04_IDLE;
static volatile u32 hc_t0_us = 0;
static volatile u32 hc_start_us = 0;
static volatile u32 hc_pulse_us = 0;
static volatile u32 hc_timeouts = 0;

void HC_SR04_Init(void)
{
	GPIO_InitTypeDef gpio;
	EXTI_InitTypeDef exti;
	NVIC_InitTypeDef nvic;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);

	/* TRIG = PF2 (output) */
	gpio.GPIO_Pin = GPIO_Pin_2;
	gpio.GPIO_Mode = GPIO_Mode_OUT;
	gpio.GPIO_OType = GPIO_OType_PP;
	gpio.GPIO_Speed = GPIO_Speed_50MHz;
	gpio.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_Init(GPIOF, &gpio);
	GPIO_ResetBits(GPIOF, GPIO_Pin_2);

	/* ECHO = PF3 (input, both edges) */
	gpio.GPIO_Pin = GPIO_Pin_3;
	gpio.GPIO_Mode = GPIO_Mode_IN;
	gpio.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_Init(GPIOF, &gpio);

	SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOF, EXTI_PinSource3);
	exti.EXTI_Line = EXTI_Line3;
	exti.EXTI_Mode = EXTI_Mode_Interrupt;
	exti.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
	exti.EXTI_LineCmd = ENABLE;
	EXTI_Init(&exti);

	nvic.NVIC_IRQChannel = EXTI3_IRQn;
	nvic.NVIC_IRQChannelPreemptionPriority = 1;/* above the 1 ms tick */
	nvic.NVIC_IRQChannelSubPriority = 1;
	nvic.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&nvic);
}

void EXTI3_IRQHandler(void)
{
	u32 now;

	if (EXTI_GetITStatus(EXTI_Line3) == RESET)
		return;
	EXTI_ClearITPendingBit(EXTI_Line3);

	now = DWT_Micros();
	if (GPIO_ReadInputDataBit(GPIOF, GPIO_Pin_3))
	{
		if (hc_state == HC_SR04_WAIT_RISE)
		{
			hc_t0_us = now;
			hc_state = HC_SR04_WAIT_FALL;
		}
	}
	else if (hc_state == HC_SR04_WAIT_FALL)
	{
		hc_pulse_us = now - hc_t0_us;
		hc_state = HC_SR04_DONE;
	}
}

/* 1 = trigger emitted, 0 = previous measurement still in flight */
u8 HC_SR04_Start(void)
{
	if (hc_state != HC_SR04_IDLE)
		return 0;

	HC_SR04_TRIG = 1;
	delay_us(HC_SR04_TRIG_US);
	HC_SR04_TRIG = 0;

	hc_start_us = DWT_Micros();
	hc_state = HC_SR04_WAIT_RISE;
	return 1;
}

/* 1 = result ready (distance valid, or -1.0f on timeout) */
u8 HC_SR04_Poll(float *distance)
{
	u32 pulse;

	if (hc_state == HC_SR04_DONE)
	{
		pulse = hc_pulse_us;
		hc_state = HC_SR04_IDLE;
		if (pulse >= HC_SR04_MIN_PULSE_US && pulse <= HC_SR04_MAX_PULSE_US)
		{
			if (distance) *distance = (float)pulse / 58.0f;
			return 1;
		}
		if (distance) *distance = -1.0f;
		return 1;
	}

	if (hc_state == HC_SR04_WAIT_RISE || hc_state == HC_SR04_WAIT_FALL)
	{
		if ((DWT_Micros() - hc_start_us) > HC_SR04_ECHO_TIMEOUT_US)
		{
			hc_state = HC_SR04_IDLE;
			hc_timeouts++;
			if (distance) *distance = -1.0f;
			return 1;
		}
	}
	return 0;
}

u32 HC_SR04_GetTimeouts(void)
{
	return hc_timeouts;
}

/* TIM7 drives the USART2/USART3 receive frame gap (100 ms) */
void TIM7_Int_Init(u16 arr, u16 psc)
{
	NVIC_InitTypeDef NVIC_InitStructure;
	TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM7, ENABLE);

	TIM_TimeBaseStructure.TIM_Period = arr;
	TIM_TimeBaseStructure.TIM_Prescaler = psc;
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInit(TIM7, &TIM_TimeBaseStructure);

	TIM_ITConfig(TIM7, TIM_IT_Update, ENABLE);

	NVIC_InitStructure.NVIC_IRQChannel = TIM7_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
}

void TIM7_IRQHandler(void)
{
	if (TIM_GetITStatus(TIM7, TIM_IT_Update) != RESET)
	{
		USART3_RX_STA |= 1 << 15;
		USART2_RX_STA |= 1 << 15;
		TIM_ClearITPendingBit(TIM7, TIM_IT_Update);
		TIM_Cmd(TIM7, DISABLE);
	}
}
