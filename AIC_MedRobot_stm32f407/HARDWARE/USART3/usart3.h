#ifndef __USART3_H
#define __USART3_H
#include "sys.h"
//////////////////////////////////////////////////////////////////////////////////
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//ALIENTEK STM32F407开发板
//串口3初始化
//正点原子@ALIENTEK
//技术论坛:www.openedv.com
//修改日期:2014/3/29
//版本：V1.0
//版权所有，盗版必究。
//Copyright(C) 广州市星翼电子科技有限公司 2009-2019
//All rights reserved
//////////////////////////////////////////////////////////////////////////////////

#define USART3_MAX_RECV_LEN		400					//定义最大接收缓存字节数
#define USART3_MAX_SEND_LEN		400					//定义最大发送缓存字节数
#define USART3_RX_EN 			1
#define USART3_TXQ_SIZE		768					/* text TX ring buffer (bytes)    */
#define USART3_TX_CHUNK		128					/* max bytes per DMA burst        */					//0,不接收;1,接收.

extern u8  USART3_RX_BUF[USART3_MAX_RECV_LEN]; 		//接收缓冲
extern u8  USART3_TX_BUF[USART3_MAX_SEND_LEN]; 		//发送缓冲
extern vu16 USART3_RX_STA;   						//接收数据状态

void usart3_init(u32 bound);
void usart3_set_baudrate(u32 bound);
void u3_printf(char* fmt, ...);
void USART3_TextPump(void);		/* start next text chunk if DMA is idle */
void USART3_TextFlush(void);		/* drop pending text                    */
void USART3_SendRaw(const u8 *buf, u16 len);	/* queue raw bytes (no CRLF)  */
u16  USART3_TextPending(void);		/* bytes still queued                   */
u16  USART3_TextOverflow(void);		/* dropped-message counter              */
u8   USART3_SendEscape(void);	/* isolated "+++" burst, 1 = sent         */
void USART3_WaitIdle(void);			/* bounded wait for TX DMA completion   */
extern volatile u8 usart3_tx_owner;	/* 0=idle 1=text 2=video frames       */
#endif
