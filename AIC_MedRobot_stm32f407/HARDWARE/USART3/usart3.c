#include "delay.h"
#include "usart3.h"
#include "stdarg.h"
#include "stdio.h"
#include "string.h"
#include "exti.h"
#include "timer.h"
#include "wifi_transfer.h"
#include "lcd.h"

//////////////////////////////////////////////////////////////////////////////////
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//ALIENTEK STM32F407开发板
//串口3初始化
//正点原子@ALIENTEK
//技术论坛:www.openedv.com
//修改日期:2014/8/9
//版本：V1.0
//版权所有，盗版必究。
//Copyright(C) 广州市星翼电子科技有限公司 2009-2019
//All rights reserved
//////////////////////////////////////////////////////////////////////////////////

//发送缓冲区
__align(8) u8 USART3_TX_BUF[USART3_MAX_SEND_LEN]; 	//发送缓冲区,最大USART3_MAX_SEND_LEN字节
#ifdef USART3_RX_EN   								//如果使能了接收
//接收缓冲区
u8 USART3_RX_BUF[USART3_MAX_RECV_LEN]; 				//接收缓冲区,最大USART3_MAX_RECV_LEN个字节


u16 pwmval=320;

//通过判断接收2个字符之间的时间差不超过100ms来判断是不是一次连续的数据.
//如果2个字符接收时间间隔超过100ms,则认为不是1次连续数据,也就是超过100ms没有接收到
//任何数据,则表示本次接收完成.
//接收到的数据状态
//[15]:0,没有接收到数据;1,接收到了一帧数据.
//[14:0]:接收到的数据长度
vu16 USART3_RX_STA=0;
void USART3_IRQHandler(void)
{
	u8 res;
	if(USART_GetITStatus(USART3, USART_IT_RXNE) != RESET)//接收到数据
	{

	res =USART_ReceiveData(USART3);

	if((USART3_RX_STA&(1<<15))==0)//如果上一次没有接收到数据,并且没有标志位置1,则接收本次数据
	{
		if(USART3_RX_STA<USART3_MAX_RECV_LEN)		//还可以继续接收数据
		{

			TIM_SetCounter(TIM7,0);    //重新计时
			if(USART3_RX_STA==0)
				TIM_Cmd(TIM7, ENABLE);  //使能定时器

			USART3_RX_BUF[USART3_RX_STA++]=res;		//记录接收到的值




		}else
		{

			USART3_RX_STA|=1<<15;					//强制标记接收完成

		}
	}



 }

}
#endif
//初始化IO 串口3
//bound:波特率
void usart3_init(u32 bound)
{

	NVIC_InitTypeDef NVIC_InitStructure;
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;


	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB,ENABLE); //使能GPIOB时钟
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3,ENABLE);//使能USART3时钟

 	USART_DeInit(USART3);  //复位串口3

	GPIO_PinAFConfig(GPIOB,GPIO_PinSource11,GPIO_AF_USART3); //GPIOB11复用为USART3
	GPIO_PinAFConfig(GPIOB,GPIO_PinSource10,GPIO_AF_USART3); //GPIOB10复用为USART3

  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11 | GPIO_Pin_10; //GPIOB11和GPIOB10初始化
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;//设置复用功能
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;	//速度50MHz
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP; //推挽复用输出
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP; //上拉
	GPIO_Init(GPIOB,&GPIO_InitStructure); //初始化GPIOB11和GPIOB10




	USART_InitStructure.USART_BaudRate = bound;//波特率
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;//字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;//一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;//无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;	//收发模式

	USART_Init(USART3, &USART_InitStructure); //初始化串口

	USART_Cmd(USART3, ENABLE);               //使能串口

  USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);//开启接收中断

	NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=2 ;//抢占优先级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;		//子优先级3
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			//IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);	//根据指定的参数初始化VIC寄存器
	TIM7_Int_Init(1000-1,8400-1);		//100ms中断
	USART3_RX_STA=0;		//清空
	TIM_Cmd(TIM7, DISABLE); //关闭定时器




}

/* ==================== 运行中更改波特率 ====================
 * 仅重写 USART3 波特率寄存器并保持使能, 不触碰接收状态标志
 * (USART3_RX_STA)、TIM7 组帧定时器与 DMA 配置, 供图传前临时提速使用。
 * bound: 新波特率(如 921600) */
void usart3_set_baudrate(u32 bound)
{
	USART_InitTypeDef USART_InitStructure;

	USART_InitStructure.USART_BaudRate = bound;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;

	USART_Init(USART3, &USART_InitStructure);	/* 更新 BRR */
	USART_Cmd(USART3, ENABLE);					/* 保持使能 */
	USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);	/* 保持 RX 中断 */
}




//串口3,printf 函数
//确保一次发送数据不超过USART3_MAX_SEND_LEN字节
/* ==================== Text TX queue (shared USART3) ====================
 * Before: u3_printf() busy-waited one byte at a time and returned early while
 * the video DMA was active, so STATUS/ACK reports were silently dropped.
 * Now: the text is formatted into a bounded buffer, appended to a ring buffer
 * and handed to DMA in small chunks. Video frames and text share DMA1_Stream3,
 * therefore a burst is only started when the stream is idle and the pending
 * text is pumped as soon as the link frees up (control loop + DMA done ISR).
 * Ring access uses a short critical section so tasks and the ISR can share it.
 */
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
			usart3_tx_overflow++;/* queue full: count it, keep the link usable */
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

	/* take ring -> DMA atomically so tasks and the DMA ISR never interleave */
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
	USART3_DMA_TryStart(USART3_TX_BUF, (u32)n, 1);/* link was idle above */
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

void USART3_SendRaw(const u8 *buf, u16 len)
{
	if (buf == 0 || len == 0)
		return;
	usart3_txq_push(buf, len);
	USART3_TextPump();
}

/* Send the "+++" escape as an isolated burst: only when the queue is empty
 * and the stream is idle, so the module sees the required silence around it.
 * Returns 0 when the link is still busy; the caller retries later. */
u8 USART3_SendEscape(void)
{
	u32 pm;
	u8 ok = 0;

	pm = usart3_crit_enter();
	if (usart3_txq_cnt == 0 && usart3_tx_owner == 0 && !USART3_DMA_IsBusy())
	{
		USART3_TX_BUF[0] = 0x2B;/* + */
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
			break;/* bounded wait: never hang the caller */
	}
}

/* Bounded, non-blocking printf: format -> ring buffer -> DMA chunk. */
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
		n = (int)(sizeof(tmp) - 1);/* truncated, still bounded */

	usart3_txq_push((const u8 *)tmp, (u16)n);
	USART3_TextPump();
}
