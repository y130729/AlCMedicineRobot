#include "timer.h"
#include "delay.h"

extern vu16 USART3_RX_STA;
extern vu16 USART2_RX_STA;

#define HC_SR04_SAMPLE_COUNT       1
#define HC_SR04_ECHO_TIMEOUT_US 30000

static volatile u32 tim3_overflow_count = 0;

void TIM3_Int_Init(u16 arr, u16 psc)
{
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

	TIM_TimeBaseStructInit(&TIM_TimeBaseInitStructure);
	TIM_TimeBaseInitStructure.TIM_Period = arr;
	TIM_TimeBaseInitStructure.TIM_Prescaler = psc;
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure);

	TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
	TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

	NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	/* 测量回波脉宽时才启动TIM3 */
	TIM_Cmd(TIM3, DISABLE);
}

void TIM3_IRQHandler(void)
{
	if(TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
	{
		TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
		tim3_overflow_count++;
	}
}

void HC_SR04_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF, ENABLE);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_Init(GPIOF, &GPIO_InitStructure);
	GPIO_ResetBits(GPIOF, GPIO_Pin_2);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_Init(GPIOF, &GPIO_InitStructure);
}

static u8 HC_SR04_WaitEcho(BitAction state, u32 timeout_us)
{
	while(GPIO_ReadInputDataBit(GPIOF, GPIO_Pin_3) != state)
	{
		if(timeout_us == 0)
			return 0;

		timeout_us--;
		delay_us(1);
	}

	return 1;
}

float HC_SR04_GetDistance(void)
{
	float distance_sum = 0.0f;
	u32 pulse_us;
	u16 counter;
	u8 valid_samples = 0;
	u8 i;

	for(i = 0; i < HC_SR04_SAMPLE_COUNT; i++)
	{
		HC_SR04_TRIG = 1;
		delay_us(11);
		HC_SR04_TRIG = 0;

		if(!HC_SR04_WaitEcho(Bit_SET, HC_SR04_ECHO_TIMEOUT_US))
			continue;

		tim3_overflow_count = 0;
		TIM_SetCounter(TIM3, 0);
		TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
		TIM_Cmd(TIM3, ENABLE);

		if(!HC_SR04_WaitEcho(Bit_RESET, HC_SR04_ECHO_TIMEOUT_US))
		{
			TIM_Cmd(TIM3, DISABLE);
			continue;
		}

		TIM_Cmd(TIM3, DISABLE);
		counter = TIM_GetCounter(TIM3);
		pulse_us = tim3_overflow_count * 1000UL + counter;
		distance_sum += pulse_us / 58.0f;
		valid_samples++;
	}

	if(valid_samples == 0)
		return -1.0f;

	return distance_sum / valid_samples;
}

// TIM7 用于 USART2/USART3 接收超时判断
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
