#ifndef __WIFI_TRANSFER_H
#define __WIFI_TRANSFER_H

#include "sys.h"

#define JPEG_BUF_SIZE (10*1024)

extern u32 jpeg_buf[JPEG_BUF_SIZE];
extern volatile u32 jpeg_data_len;
extern volatile u8  jpeg_data_ok;
extern volatile u32 jpeg_last_frame_bytes;

extern const u16 jpeg_img_size_tbl[][2];

void USART3_DMA_TX_Init(void);
void USART3_Send_DMA_Start(uint8_t *buf, uint32_t len);
u8   USART3_DMA_IsBusy(void);
u8   USART3_DMA_TryStart(uint8_t *buf, uint32_t len, u8 owner);/* 0 = stream busy */
void jpeg_frame_callback(void);
int  jpeg_process_and_send(void);

#endif
