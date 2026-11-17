#ifndef __STM32F4xx_H
#define __STM32F4xx_H

#include <stdint.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed char s8;
typedef signed short s16;
typedef signed int s32;
typedef volatile unsigned short vu16;

typedef struct
{
    volatile uint32_t NDTR;
} DMA_Stream_TypeDef;

extern DMA_Stream_TypeDef host_dma2_stream1;
#define DMA2_Stream1 (&host_dma2_stream1)

void DMA_SetCurrDataCounter(DMA_Stream_TypeDef *stream, uint16_t count);
void DMA_Cmd(DMA_Stream_TypeDef *stream, int state);
void DCMI_CaptureCmd(int state);

#endif
