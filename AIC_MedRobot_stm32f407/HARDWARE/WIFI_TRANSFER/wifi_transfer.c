#include "wifi_transfer.h"
#include "stm32f4xx.h"
#include <string.h>
#include "dcmi.h"
#include "usart3.h"

/* ==================== JPEG/WiFi 全局变量 ==================== */
__align(4) u32 jpeg_buf[JPEG_BUF_SIZE];
volatile u32 jpeg_data_len = 0;
volatile u8  jpeg_data_ok  = 0;
volatile u8  dma_tx_busy   = 0;
volatile uint8_t dma_tx_done = 0;
volatile u32 jpeg_last_frame_bytes = 0;

const u16 jpeg_img_size_tbl[][2] =
{
	176,144,	//QCIF
	160,120,	//QQVGA
	352,288,	//CIF
	320,240,	//QVGA
	640,480,	//VGA
	800,600,	//SVGA
	1024,768,	//XGA
	1280,1024,	//SXGA
	1600,1200,	//UXGA
};

/* ==================== CRC16 ==================== */
static uint16_t crc16_ccitt(const uint8_t *buf, uint32_t len)
{
	uint16_t crc = 0xFFFF;
	int k;
	while (len--) {
		crc ^= (uint16_t)(*buf++) << 8;
		for (k = 0; k < 8; ++k) {
			if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
			else crc <<= 1;
		}
	}
	return crc;
}

/* ==================== USART3 DMA TX ==================== */
void USART3_Send_DMA_Start(uint8_t *buf, uint32_t len)
{
	dma_tx_done = 0;
	dma_tx_busy = 1;
	DMA1_Stream3->CR &= ~DMA_SxCR_EN;
	while (DMA1_Stream3->CR & DMA_SxCR_EN) {}

	DMA1_Stream3->M0AR = (uint32_t)buf;
	DMA1_Stream3->NDTR = len;
	DMA1_Stream3->PAR  = (uint32_t)&USART3->DR;

	DMA1_Stream3->CR |= DMA_SxCR_EN;
}

/* Claim the shared TX stream atomically: 1 = started, 0 = still busy.
 * Used by both the text queue and the video frame path so a burst can never
 * be interleaved with another producer. */
u8 USART3_DMA_TryStart(uint8_t *buf, uint32_t len, u8 owner)
{
	u32 pm = __get_PRIMASK();
	u8 ok = 0;

	__disable_irq();
	if (dma_tx_busy == 0)
	{
		usart3_tx_owner = owner;
		USART3_Send_DMA_Start(buf, len);
		ok = 1;
	}
	if (pm == 0)
		__enable_irq();
	return ok;
}

u8 USART3_DMA_IsBusy(void)
{
	return dma_tx_busy;
}

void DMA1_Stream3_IRQHandler(void)
{
	u8 owner;

	if (DMA1->LISR & DMA_LISR_TCIF3)
	{
		DMA1->LIFCR |= DMA_LIFCR_CTCIF3;
		dma_tx_done = 1;
		dma_tx_busy = 0;

		/* if the finished transfer was queued text, keep draining it */
		owner = usart3_tx_owner;
		usart3_tx_owner = 0;
		if (owner == 1)
			USART3_TextPump();
	}
}

void USART3_DMA_TX_Init(void)
{
	DMA_InitTypeDef DMA_InitStruct;
	NVIC_InitTypeDef NVIC_InitStruct;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);

	DMA_DeInit(DMA1_Stream3);
	while (DMA_GetCmdStatus(DMA1_Stream3) != DISABLE);

	DMA_InitStruct.DMA_Channel = DMA_Channel_4;
	DMA_InitStruct.DMA_PeripheralBaseAddr = (uint32_t)&USART3->DR;
	DMA_InitStruct.DMA_Memory0BaseAddr = 0;
	DMA_InitStruct.DMA_DIR = DMA_DIR_MemoryToPeripheral;
	DMA_InitStruct.DMA_BufferSize = 0;
	DMA_InitStruct.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	DMA_InitStruct.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_InitStruct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	DMA_InitStruct.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
	DMA_InitStruct.DMA_Mode = DMA_Mode_Normal;
	DMA_InitStruct.DMA_Priority = DMA_Priority_High;
	DMA_InitStruct.DMA_FIFOMode = DMA_FIFOMode_Disable;
	DMA_InitStruct.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
	DMA_InitStruct.DMA_MemoryBurst = DMA_MemoryBurst_Single;
	DMA_InitStruct.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;

	DMA_Init(DMA1_Stream3, &DMA_InitStruct);

	DMA_ITConfig(DMA1_Stream3, DMA_IT_TC, ENABLE);

	NVIC_InitStruct.NVIC_IRQChannel = DMA1_Stream3_IRQn;
	NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStruct.NVIC_IRQChannelSubPriority = 1;
	NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStruct);

	USART_DMACmd(USART3, USART_DMAReq_Tx, ENABLE);
}

/* ==================== JPEG 帧回调 (DCMI ISR调用) ==================== */
void jpeg_frame_callback(void)
{
	if (jpeg_data_ok == 0)
	{
		DCMI_CaptureCmd(DISABLE);
		DMA_Cmd(DMA2_Stream1, DISABLE);
		while (DMA_GetCmdStatus(DMA2_Stream1) != DISABLE) {}

		jpeg_data_len = JPEG_BUF_SIZE - DMA_GetCurrDataCounter(DMA2_Stream1);
		if (jpeg_data_len > 0)
		{
			jpeg_last_frame_bytes = jpeg_data_len * 4;
			jpeg_data_ok = 1;
		}
		else
		{
			jpeg_last_frame_bytes = 0;
			DMA2_Stream1->NDTR = JPEG_BUF_SIZE;
			DMA_SetCurrDataCounter(DMA2_Stream1, JPEG_BUF_SIZE);
			DMA_Cmd(DMA2_Stream1, ENABLE);
			DCMI_CaptureCmd(ENABLE);
		}
	}
}

/* ==================== JPEG 帧处理与发送 ==================== */
int jpeg_process_and_send(void)
{
	u8 *p;
	u32 i, jpgstart = 0, jpglen = 0;
	u8 headok = 0;
	uint32_t bufBytes;
	uint8_t *sendPtr;
	uint32_t sendLen;
	uint32_t hdrOff;
	uint16_t crc;

	if (jpeg_data_ok != 1)
		return 0;
	p = (u8 *)jpeg_buf;
	bufBytes = jpeg_data_len * 4;

	for (i = 0; i + 1 < bufBytes; ++i)
	{
		if (!headok && p[i] == 0xFF && p[i + 1] == 0xD8) {
			jpgstart = i;
			headok = 1;
		}
		else if (headok && p[i] == 0xFF && p[i + 1] == 0xD9) {
			jpglen = i - jpgstart + 2;
			break;
		}
	}

	if (jpglen == 0) { jpeg_data_ok = 2; return 0; }

	if (jpgstart >= 6) {
		hdrOff = jpgstart - 6;
		if (jpgstart + jpglen + 2 > bufBytes) { jpeg_data_ok = 2; return 0; }
		p[hdrOff + 0] = 0xAA;
		p[hdrOff + 1] = 0x55;
		p[hdrOff + 2] = (uint8_t)(jpglen & 0xFF);
		p[hdrOff + 3] = (uint8_t)((jpglen >> 8) & 0xFF);
		p[hdrOff + 4] = (uint8_t)((jpglen >> 16) & 0xFF);
		p[hdrOff + 5] = (uint8_t)((jpglen >> 24) & 0xFF);
		sendPtr = &p[hdrOff];
		sendLen = jpgstart - hdrOff + jpglen;
	} else {
		if (6 + jpglen + 2 > bufBytes) { jpeg_data_ok = 2; return 0; }
		memmove(p + 6, p + jpgstart, jpglen);
		jpgstart = 6;
		p[0] = 0xAA;
		p[1] = 0x55;
		p[2] = (uint8_t)(jpglen & 0xFF);
		p[3] = (uint8_t)((jpglen >> 8) & 0xFF);
		p[4] = (uint8_t)((jpglen >> 16) & 0xFF);
		p[5] = (uint8_t)((jpglen >> 24) & 0xFF);
		sendPtr = p;
		sendLen = 6 + jpglen;
	}

	crc = crc16_ccitt(sendPtr + 6, jpglen);
	sendPtr[sendLen + 0] = (uint8_t)(crc & 0xFF);
	sendPtr[sendLen + 1] = (uint8_t)((crc >> 8) & 0xFF);
	sendLen += 2;

	/* Claim the shared TX stream; if text is mid-burst, retry on the next
	 * call (the frame preparation above is idempotent). */
	if (!USART3_DMA_TryStart(sendPtr, sendLen, 2))
		return 0;
	jpeg_data_ok = 2;
	return 1;
}
