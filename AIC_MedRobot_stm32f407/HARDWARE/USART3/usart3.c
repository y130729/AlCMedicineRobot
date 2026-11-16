#include "delay.h"
#include "usart3.h"
#include "stdarg.h"
#include "stdio.h"
#include "string.h"
#include "exti.h"
#include "timer.h"
#include "wifi_transfer.h"

/* ==================== USART3: AT + text + JPEG ====================
 * TX: bounded text ring (control/telemetry, never silently dropped) sharing
 *     DMA1_Stream3 with JPEG frames.  RX: DMA1_Stream1 circular + IDLE
 *     interrupt; TIM7 still marks the end of a frame after a 100 ms gap, so
 *     the existing AT-layer buffers (USART3_RX_BUF / USART3_RX_STA) and all
 *     callers stay source-compatible while the CPU no longer takes one
 *     interrupt per received byte (unsafe at 921600 baud).
 */

__align(8) u8 USART3_TX_BUF[USART3_MAX_SEND_LEN];
u8 USART3_RX_BUF[USART3_MAX_RECV_LEN];

u16 pwmval = 320;
vu16 USART3_RX_STA = 0;
static volatile u32 usart3_baud = 115200;

/* ---- RX DMA ---- */
static u8  usart3_rx_dma[USART3_RX_DMA_SIZE];
static volatile u16 usart3_rx_last = 0;
static volatile u32 usart3_rx_overflow = 0;
static volatile u32 usart3_rx_ore = 0;

static u16 usart3_dma_pos(void)
{
	return (u16)(USART3_RX_DMA_SIZE - DMA_GetCurrDataCounter(DMA1_Stream1));
}

/* Copy everything the DMA wrote since the last call into the line buffer.
 * Runs from the USART IDLE / DMA HT-TC interrupt; reset runs with IRQs off. */
static void usart3_rx_drain_isr(void)
{
	u16 pos = usart3_dma_pos();
	u16 last = usart3_rx_last;

	while (last != pos)
	{
		u8 b = usart3_rx_dma[last];

		last++;
		if (last >= USART3_RX_DMA_SIZE)
			last = 0;

		if (USART3_RX_STA & 0x8000)
		{
			usart3_rx_overflow++;/* frame full: drop, keep the link alive */
			continue;
		}
		if ((USART3_RX_STA & 0x7FFF) >= (USART3_MAX_RECV_LEN - 1))
		{
			USART3_RX_STA |= 0x8000;
			usart3_rx_overflow++;
			continue;
		}
		if ((USART3_RX_STA & 0x7FFF) == 0)
		{
			TIM_SetCounter(TIM7, 0);
			TIM_Cmd(TIM7, ENABLE);
		}
		USART3_RX_BUF[USART3_RX_STA & 0x7FFF] = b;
		USART3_RX_STA++;
	}
	usart3_rx_last = last;
}

void USART3_RxReset(void)
{
	u32 pm = __get_PRIMASK();

	__disable_irq();
	USART3_RX_STA = 0;
	usart3_rx_last = usart3_dma_pos();
	TIM_Cmd(TIM7, DISABLE);
	if (pm == 0)
		__enable_irq();
}

u32 USART3_GetRxOverflow(void)
{
	return usart3_rx_overflow;
}

u32 USART3_GetRxOre(void)
{
	return usart3_rx_ore;
}

u16 USART3_GetRxPending(void)
{
	return (u16)(USART3_RX_STA & 0x7FFF);
}

u32 USART3_GetBaud(void)
{
	return usart3_baud;
}

void USART3_IRQHandler(void)
{
	if (USART_GetITStatus(USART3, USART_IT_IDLE) != RESET)
	{
		volatile u32 tmp;
		tmp = USART3->SR;/* reading SR then DR clears IDLE */
		tmp = USART3->DR;
		(void)tmp;
		usart3_rx_drain_isr();
	}

	if (USART_GetFlagStatus(USART3, USART_FLAG_ORE) != RESET)
	{
		volatile u32 tmp;
		tmp = USART3->SR;
		tmp = USART3->DR;
		(void)tmp;
		usart3_rx_ore++;
		usart3_rx_drain_isr();
	}
}

void DMA1_Stream1_IRQHandler(void)
{
	if (DMA1->LISR & DMA_LISR_HTIF1)
	{
		DMA1->LIFCR |= DMA_LIFCR_CHTIF1;
		usart3_rx_drain_isr();
	}
	if (DMA1->LISR & DMA_LISR_TCIF1)
	{
		DMA1->LIFCR |= DMA_LIFCR_CTCIF1;
		usart3_rx_drain_isr();
	}
	if (DMA1->LISR & DMA_LISR_TEIF1)
	{
		DMA1->LIFCR |= DMA_LIFCR_CTEIF1;
		usart3_rx_ore++;
	}
}

static void usart3_rx_dma_init(void)
{
	DMA_InitTypeDef dma;
	NVIC_InitTypeDef nvic;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);
	DMA_DeInit(DMA1_Stream1);
	while (DMA_GetCmdStatus(DMA1_Stream1) != DISABLE) { }

	dma.DMA_Channel = DMA_Channel_4;
	dma.DMA_PeripheralBaseAddr = (u32)&USART3->DR;
	dma.DMA_Memory0BaseAddr = (u32)usart3_rx_dma;
	dma.DMA_DIR = DMA_DIR_PeripheralToMemory;
	dma.DMA_BufferSize = USART3_RX_DMA_SIZE;
	dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
	dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	dma.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
	dma.DMA_Mode = DMA_Mode_Circular;
	dma.DMA_Priority = DMA_Priority_High;
	dma.DMA_FIFOMode = DMA_FIFOMode_Disable;
	dma.DMA_FIFOThreshold = DMA_FIFOThreshold_HalfFull;
	dma.DMA_MemoryBurst = DMA_MemoryBurst_Single;
	dma.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
	DMA_Init(DMA1_Stream1, &dma);
	DMA_ITConfig(DMA1_Stream1, DMA_IT_HT | DMA_IT_TC | DMA_IT_TE, ENABLE);
	DMA_Cmd(DMA1_Stream1, ENABLE);

	nvic.NVIC_IRQChannel = DMA1_Stream1_IRQn;
	nvic.NVIC_IRQChannelPreemptionPriority = 2;
	nvic.NVIC_IRQChannelSubPriority = 2;
	nvic.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&nvic);

	USART_DMACmd(USART3, USART_DMAReq_Rx, ENABLE);
}

void usart3_init(u32 bound)
{
	NVIC_InitTypeDef nvic;
	GPIO_InitTypeDef gpio;
	USART_InitTypeDef usart;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

	USART_DeInit(USART3);

	GPIO_PinAFConfig(GPIOB, GPIO_PinSource11, GPIO_AF_USART3);
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_USART3);

	gpio.GPIO_Pin = GPIO_Pin_11 | GPIO_Pin_10;
	gpio.GPIO_Mode = GPIO_Mode_AF;
	gpio.GPIO_Speed = GPIO_Speed_50MHz;
	gpio.GPIO_OType = GPIO_OType_PP;
	gpio.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(GPIOB, &gpio);

	usart.USART_BaudRate = bound;
	usart.USART_WordLength = USART_WordLength_8b;
	usart.USART_StopBits = USART_StopBits_1;
	usart.USART_Parity = USART_Parity_No;
	usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	usart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART3, &usart);
	USART_Cmd(USART3, ENABLE);
	usart3_baud = bound;

	/* RX is DMA driven: no RXNE interrupt (one IRQ per byte at 921600 is
	 * both wasteful and lossy). IDLE marks "burst finished". */
	USART_ITConfig(USART3, USART_IT_IDLE, ENABLE);

	nvic.NVIC_IRQChannel = USART3_IRQn;
	nvic.NVIC_IRQChannelPreemptionPriority = 2;
	nvic.NVIC_IRQChannelSubPriority = 3;
	nvic.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&nvic);

	usart3_rx_dma_init();
	USART3_RxReset();

	TIM7_Int_Init(1000 - 1, 8400 - 1);/* 100 ms frame gap */
	TIM_Cmd(TIM7, DISABLE);
}

/* Runtime baud change (AT+UART_DEF). Keep DMA RX armed. */
void usart3_set_baudrate(u32 bound)
{
	USART_InitTypeDef usart;

	usart.USART_BaudRate = bound;
	usart.USART_WordLength = USART_WordLength_8b;
	usart.USART_StopBits = USART_StopBits_1;
	usart.USART_Parity = USART_Parity_No;
	usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	usart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART3, &usart);
	USART_Cmd(USART3, ENABLE);
	usart3_baud = bound;
	USART_DMACmd(USART3, USART_DMAReq_Rx, ENABLE);
	USART_ITConfig(USART3, USART_IT_IDLE, ENABLE);
	USART3_RxReset();
}

/* ==================== Text TX queue ==================== */
#define USART3_FMT_MAX   192

static u8  usart3_txq[USART3_TXQ_SIZE];
static volatile u16 usart3_txq_rd = 0;
static volatile u16 usart3_txq_wr = 0;
static volatile u16 usart3_txq_cnt = 0;
static volatile u16 usart3_tx_overflow = 0;
volatile u8 usart3_tx_owner = 0;/* 0=idle, 1=text queue, 2=video frame */

static u32 usart3_crit_enter(void)
{
	u32 pm = __get_PRIMASK();
	__disable_irq();
	return pm;
}

static void usart3_crit_exit(u32 pm)
{
	if (pm == 0)
		__enable_irq();
}

static void usart3_txq_push(const u8 *buf, u16 len)
{
	u16 i;
	u32 pm = usart3_crit_enter();

	for (i = 0; i < len; i++)
	{
		if (usart3_txq_cnt >= USART3_TXQ_SIZE)
		{
			usart3_tx_overflow++;
			break;
		}
		usart3_txq[usart3_txq_wr] = buf[i];
		usart3_txq_wr = (u16)((usart3_txq_wr + 1) % USART3_TXQ_SIZE);
		usart3_txq_cnt++;
	}
	usart3_crit_exit(pm);
}

void USART3_TextPump(void)
{
	u16 n, i;
	u32 pm;

	pm = usart3_crit_enter();
	if (usart3_txq_cnt == 0 || usart3_tx_owner != 0 || USART3_DMA_IsBusy())
	{
		usart3_crit_exit(pm);
		return;
	}
	n = usart3_txq_cnt;
	if (n > USART3_TX_CHUNK)
		n = USART3_TX_CHUNK;
	for (i = 0; i < n; i++)
	{
		USART3_TX_BUF[i] = usart3_txq[usart3_txq_rd];
		usart3_txq_rd = (u16)((usart3_txq_rd + 1) % USART3_TXQ_SIZE);
	}
	usart3_txq_cnt = (u16)(usart3_txq_cnt - n);
	USART3_DMA_TryStart(USART3_TX_BUF, (u32)n, 1);
	usart3_crit_exit(pm);
}

void USART3_TextFlush(void)
{
	u32 pm = usart3_crit_enter();

	usart3_txq_rd = 0;
	usart3_txq_wr = 0;
	usart3_txq_cnt = 0;
	usart3_crit_exit(pm);
}

/* Bounded drain used before entering AT command mode: LINK,DOWN / ACK must
 * not be thrown away just because the link is being reopened. */
void USART3_TextDrain(u32 timeout_ms)
{
	u32 t0 = EXTIX_GetMillis();

	while (USART3_TextPending() > 0 || usart3_tx_owner != 0 || USART3_DMA_IsBusy())
	{
		if ((s32)(EXTIX_GetMillis() - t0) >= (s32)timeout_ms)
			break;
		USART3_TextPump();
		delay_ms(1);
	}
}

void USART3_SendRaw(const u8 *buf, u16 len)
{
	if (buf == 0 || len == 0)
		return;
	usart3_txq_push(buf, len);
	USART3_TextPump();
}

u8 USART3_SendEscape(void)
{
	u32 pm;
	u8 ok = 0;

	pm = usart3_crit_enter();
	if (usart3_txq_cnt == 0 && usart3_tx_owner == 0 && !USART3_DMA_IsBusy())
	{
		USART3_TX_BUF[0] = 0x2B;
		USART3_TX_BUF[1] = 0x2B;
		USART3_TX_BUF[2] = 0x2B;
		USART3_DMA_TryStart(USART3_TX_BUF, 3, 1);
		ok = 1;
	}
	usart3_crit_exit(pm);
	return ok;
}

u16 USART3_TextPending(void)
{
	return usart3_txq_cnt;
}

u16 USART3_TextOverflow(void)
{
	return usart3_tx_overflow;
}

void USART3_WaitIdle(void)
{
	u32 t0 = EXTIX_GetMillis();

	while (USART3_DMA_IsBusy())
	{
		if ((s32)(EXTIX_GetMillis() - t0) > 200)
			break;
	}
}

void u3_printf(char* fmt,...)
{
	char tmp[USART3_FMT_MAX];
	va_list ap;
	int n;

	if (fmt == 0)
		return;

	va_start(ap, fmt);
	n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if (n <= 0)
		return;
	if (n > (int)(sizeof(tmp) - 1))
		n = (int)(sizeof(tmp) - 1);

	usart3_txq_push((const u8 *)tmp, (u16)n);
	USART3_TextPump();
}
