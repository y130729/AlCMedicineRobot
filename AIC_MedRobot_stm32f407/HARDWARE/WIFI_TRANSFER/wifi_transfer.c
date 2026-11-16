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
		/* both queue drains and finished video chunks release the link:
		 * send any control text before the next chunk starts */
		if (owner != 0)
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
/* ==================== JPEG chunked TX (QoS) ====================
 * A frame no longer monopolises the link for 200-400 ms. It is split into
 * VIDEO_CHUNK_SIZE payloads, each in a self-delimiting packet:
 *
 *   AA 55 A5 5A | total(2) | offset(2) | len(2) | payload(len) | CRC16(2)
 *
 * USART3_TextPump() gets a chance between every chunk, so an ACK or
 * LINK,DOWN queued while a frame is in flight goes out after at most one
 * chunk (~2.6 ms at 921600) instead of waiting for the whole JPEG. The host
 * reassembles by offset; a lost/reordered packet is detected by CRC/length.
 * At low baud (115200) video is skipped entirely to protect the 500 ms ACK
 * budget; control traffic always wins.
 */
#define VIDEO_CHUNK_SIZE   256
#define VIDEO_PKT_HEADER   10
#define VIDEO_PKT_MAX      (VIDEO_PKT_HEADER + VIDEO_CHUNK_SIZE + 2)
#define VIDEO_MIN_BAUD     460800UL
#define VIDEO_MAX_TOTAL    60000UL

static u8  video_pkt[VIDEO_PKT_MAX];
static u32 video_total = 0;
static u32 video_off = 0;
static u32 video_start = 0;
static u8  video_active = 0;
static u32 video_dropped = 0;

void JPEG_TxAbort(void)
{
	if (video_active)
		video_dropped++;/* aborted mid-frame: host waits for a fresh offset 0 */
	video_active = 0;
	video_total = 0;
	video_off = 0;
	if (jpeg_data_ok == 1)
		jpeg_data_ok = 2;
}

u32 JPEG_TxDropped(void)
{
	return video_dropped;
}

int jpeg_process_and_send(void)
{
	u8 *p;
	u32 i, jpgstart = 0, jpglen = 0;
	u8 headok = 0;
	uint32_t bufBytes;
	uint16_t crc;
	u32 n;

	if (jpeg_data_ok != 1)
		return 0;
	if (USART3_GetBaud() < VIDEO_MIN_BAUD)
		return 0;/* protect the control ACK budget on a slow link */

	p = (u8 *)jpeg_buf;
	bufBytes = jpeg_data_len * 4;

	if (!video_active)
	{
		for (i = 0; i + 1 < bufBytes; ++i)
		{
			if (!headok && p[i] == 0xFF && p[i + 1] == 0xD8)
			{
				jpgstart = i;
				headok = 1;
			}
			else if (headok && p[i] == 0xFF && p[i + 1] == 0xD9)
			{
				jpglen = i - jpgstart + 2;
				break;
			}
		}
		if (jpglen == 0 || jpglen > VIDEO_MAX_TOTAL)
		{
			jpeg_data_ok = 2;
			return 0;
		}
		video_start = jpgstart;
		video_total = jpglen;
		video_off = 0;
		video_active = 1;
	}

	/* Control traffic has priority: wait for the text queue to drain. */
	if (USART3_TextPending() > 0)
		return 0;/* deferred, not lost: the same frame resumes next tick */
	if (USART3_DMA_IsBusy())
		return 0;

	n = video_total - video_off;
	if (n > VIDEO_CHUNK_SIZE)
		n = VIDEO_CHUNK_SIZE;

	video_pkt[0] = 0xAA;
	video_pkt[1] = 0x55;
	video_pkt[2] = 0xA5;
	video_pkt[3] = 0x5A;
	video_pkt[4] = (u8)(video_total & 0xFF);
	video_pkt[5] = (u8)((video_total >> 8) & 0xFF);
	video_pkt[6] = (u8)(video_off & 0xFF);
	video_pkt[7] = (u8)((video_off >> 8) & 0xFF);
	video_pkt[8] = (u8)(n & 0xFF);
	video_pkt[9] = (u8)((n >> 8) & 0xFF);
	memcpy(&video_pkt[VIDEO_PKT_HEADER], &p[video_start + video_off], n);
	crc = crc16_ccitt(&video_pkt[VIDEO_PKT_HEADER], n);
	video_pkt[VIDEO_PKT_HEADER + n] = (u8)(crc & 0xFF);
	video_pkt[VIDEO_PKT_HEADER + n + 1] = (u8)((crc >> 8) & 0xFF);

	if (!USART3_DMA_TryStart(video_pkt, VIDEO_PKT_HEADER + n + 2, 2))
		return 0;

	video_off += n;
	if (video_off >= video_total)
	{
		video_active = 0;
		jpeg_data_ok = 2;
	}
	return 1;
}
