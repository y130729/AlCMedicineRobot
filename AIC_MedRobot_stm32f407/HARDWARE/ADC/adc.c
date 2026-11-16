#include "adc.h"
#include "stm32f4xx.h"

/* ==================== Line sensor acquisition ====================
 * Three reflective sensors on PF7/PF6/PF5 (ADC3 IN5/IN4/IN15).
 * TIM2 generates a 1 kHz TRGO; every trigger converts the whole scan
 * sequence, DMA2_Stream0 moves the results into a circular buffer and the
 * half/full transfer interrupts average the last four frames per channel.
 * The control loop therefore never waits for a conversion (the old
 * Get_Adc_Average() blocked 5 ms per sample => 30 ms per loop).
 */
#define ADC_LINE_CHANNELS    3
#define ADC_LINE_FRAMES      4
#define ADC_LINE_HALF        (ADC_LINE_CHANNELS * ADC_LINE_FRAMES)
#define ADC_LINE_BUF_SIZE    (ADC_LINE_HALF * 2)

static u16 adc_dma_buf[ADC_LINE_BUF_SIZE];
static volatile u16 adc_line_value[ADC_LINE_CHANNELS];
static volatile u32 adc_frame_count = 0;

static void ADC_AverageHalf(u16 base)
{
	u32 sum_l = 0, sum_m = 0, sum_r = 0;
	u16 i;
	u16 idx;

	for (i = 0; i < ADC_LINE_FRAMES; i++)
	{
		idx = (u16)(base + i * ADC_LINE_CHANNELS);
		sum_l += adc_dma_buf[idx + 0];
		sum_m += adc_dma_buf[idx + 1];
		sum_r += adc_dma_buf[idx + 2];
	}
	adc_line_value[0] = (u16)(sum_l / ADC_LINE_FRAMES);
	adc_line_value[1] = (u16)(sum_m / ADC_LINE_FRAMES);
	adc_line_value[2] = (u16)(sum_r / ADC_LINE_FRAMES);
	adc_frame_count += ADC_LINE_FRAMES;
}

void DMA2_Stream0_IRQHandler(void)
{
	if (DMA2->LISR & DMA_LISR_HTIF0)
	{
		DMA2->LIFCR = DMA_LIFCR_CHTIF0;
		ADC_AverageHalf(0);
	}
	if (DMA2->LISR & DMA_LISR_TCIF0)
	{
		DMA2->LIFCR = DMA_LIFCR_CTCIF0;
		ADC_AverageHalf(ADC_LINE_HALF);
	}
}

void Adc_Init(void)
{
	GPIO_InitTypeDef gpio;
	ADC_CommonInitTypeDef common;
	ADC_InitTypeDef adc;
	DMA_InitTypeDef dma;
	NVIC_InitTypeDef nvic;
	TIM_TimeBaseInitTypeDef tim;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF | RCC_AHB1Periph_DMA2, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC3, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

	/* L = PF7 (IN5), M = PF6 (IN4), R = PF5 (IN15) */
	gpio.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
	gpio.GPIO_Mode = GPIO_Mode_AN;
	gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOF, &gpio);

	RCC_APB2PeriphResetCmd(RCC_APB2Periph_ADC3, ENABLE);
	RCC_APB2PeriphResetCmd(RCC_APB2Periph_ADC3, DISABLE);

	/* ---- DMA2 Stream 0 Channel 2 = ADC3 ---- */
	DMA_DeInit(DMA2_Stream0);
	while (DMA_GetCmdStatus(DMA2_Stream0) != DISABLE) { }

	dma.DMA_Channel = DMA_Channel_2;
	dma.DMA_PeripheralBaseAddr = (uint32_t)&ADC3->DR;
	dma.DMA_Memory0BaseAddr = (uint32_t)adc_dma_buf;
	dma.DMA_DIR = DMA_DIR_PeripheralToMemory;
	dma.DMA_BufferSize = ADC_LINE_BUF_SIZE;
	dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
	dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
	dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
	dma.DMA_Mode = DMA_Mode_Circular;
	dma.DMA_Priority = DMA_Priority_High;
	dma.DMA_FIFOMode = DMA_FIFOMode_Disable;
	dma.DMA_FIFOThreshold = DMA_FIFOThreshold_HalfFull;
	dma.DMA_MemoryBurst = DMA_MemoryBurst_Single;
	dma.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
	DMA_Init(DMA2_Stream0, &dma);
	DMA_ITConfig(DMA2_Stream0, DMA_IT_HT | DMA_IT_TC, ENABLE);

	nvic.NVIC_IRQChannel = DMA2_Stream0_IRQn;
	nvic.NVIC_IRQChannelPreemptionPriority = 2;
	nvic.NVIC_IRQChannelSubPriority = 1;
	nvic.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&nvic);
	DMA_Cmd(DMA2_Stream0, ENABLE);

	/* ---- TIM2: 1 MHz counter, 1 kHz update, TRGO = update ---- */
	tim.TIM_Prescaler = 84 - 1;
	tim.TIM_Period = 1000 - 1;
	tim.TIM_CounterMode = TIM_CounterMode_Up;
	tim.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseInit(TIM2, &tim);
	TIM_SelectOutputTrigger(TIM2, TIM_TRGOSource_Update);
	TIM_Cmd(TIM2, ENABLE);

	/* ---- ADC3: scan 3 channels on every TIM2 TRGO ---- */
	common.ADC_Mode = ADC_Mode_Independent;
	common.ADC_TwoSamplingDelay = ADC_TwoSamplingDelay_5Cycles;
	common.ADC_DMAAccessMode = ADC_DMAAccessMode_Disabled;
	common.ADC_Prescaler = ADC_Prescaler_Div4;   /* 84 MHz / 4 = 21 MHz */
	ADC_CommonInit(&common);

	adc.ADC_Resolution = ADC_Resolution_12b;
	adc.ADC_ScanConvMode = ENABLE;
	adc.ADC_ContinuousConvMode = DISABLE;
	adc.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_Rising;
	adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_T2_TRGO;
	adc.ADC_DataAlign = ADC_DataAlign_Right;
	adc.ADC_NbrOfConversion = ADC_LINE_CHANNELS;
	ADC_Init(ADC3, &adc);

	ADC_RegularChannelConfig(ADC3, ADC_Channel_5, 1, ADC_SampleTime_480Cycles);
	ADC_RegularChannelConfig(ADC3, ADC_Channel_4, 2, ADC_SampleTime_480Cycles);
	ADC_RegularChannelConfig(ADC3, ADC_Channel_15, 3, ADC_SampleTime_480Cycles);

	ADC_DMACmd(ADC3, ENABLE);
	ADC_DMARequestAfterLastTransferCmd(ADC3, ENABLE);
	ADC_Cmd(ADC3, ENABLE);
}

/* Latest averaged sensor values (L/M/R). Never blocks. */
void Adc_ReadLine(u16 *l, u16 *m, u16 *r)
{
	if (l) *l = adc_line_value[0];
	if (m) *m = adc_line_value[1];
	if (r) *r = adc_line_value[2];
}

u32 Adc_GetFrameCount(void)
{
	return adc_frame_count;
}
