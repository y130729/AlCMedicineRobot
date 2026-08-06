#include "sys.h"
#include "usart2.h"

#include "delay.h"

#include "stdarg.h"
#include "stdio.h"
#include "string.h"
#include "timer.h"
#include "lcd.h"

//////////////////////////////////////////////////////////////////////////////////
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//ALIENTEK STM32F407开发板
//串口2初始化
//正点原子@ALIENTEK
//技术论坛:www.openedv.com
//修改日期:2014/5/14
//版本：V1.0
//版权所有，盗版必究。
//Copyright(C) 广州市星翼电子科技有限公司 2014-2024
//All rights reserved
//********************************************************************************
//修改说明
//无
//////////////////////////////////////////////////////////////////////////////////


//发送缓冲区
__align(8) u8 USART2_TX_BUF[USART2_MAX_SEND_LEN]; 	//发送缓冲区,最大USART2_MAX_SEND_LEN字节
#ifdef USART2_RX_EN   								//如果使能了接收
//接收缓冲区
u8 USART2_RX_BUF[USART2_MAX_RECV_LEN]; 				//接收缓冲区,最大USART2_MAX_RECV_LEN个字节.

u16 pwmva=320;




//通过判断接收2个字符之间的时间差不大于100ms来判断是不是一次连续的数据.
//如果2个字符接收间隔超过100ms,则认为不是1次连续数据.也就是超过100ms没有接收到
//任何数据,则表示此次接收完毕.
//接收到的数据状态
//[15]:0,没有接收到数据;1,接收到了一帧数据.
//[14:0]:接收到的数据长度
vu16 USART2_RX_STA=0;
void USART2_IRQHandler(void)
{
	u8 res;
	if(USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)//接收到数据
	{

	res =USART_ReceiveData(USART2);



	if((USART2_RX_STA&(1<<15))==0)//如果上一次没有接收到数据,并且没有标志位置1,则接收本次数据
	{
		if(USART2_RX_STA<USART2_MAX_RECV_LEN)		//还可以继续接收数据
		{
			TIM_SetCounter(TIM7,0);//重新计时
			if(USART2_RX_STA==0)
				TIM_Cmd(TIM7, ENABLE);  //使能定时器7
			USART2_RX_BUF[USART2_RX_STA++]=res;		//记录接收到的值



		}else
		{
			USART2_RX_STA|=1<<15;					//强制标记接收完成
		}
	}
 }
}
#endif








//初始化IO 串口2
//pclk1:PCLK1时钟频率(Mhz)
//bound:波特率
void usart2_init(u32 bound)
{


	NVIC_InitTypeDef NVIC_InitStructure;
  GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;


	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA,ENABLE); //使能GPIOA时钟
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2,ENABLE);//使能USART2时钟


	USART_DeInit(USART2);


	GPIO_PinAFConfig(GPIOA,GPIO_PinSource2,GPIO_AF_USART2); //PA2复用为USART2
	GPIO_PinAFConfig(GPIOA,GPIO_PinSource3,GPIO_AF_USART2);
	//USART2_TX   PA.2 PA.3
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3; //PA2,PA3,设置复用功能,推挽输出
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;//设置复用功能
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;	//速度50MHz
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP; //推挽复用输出
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP; //上拉
	GPIO_Init(GPIOA,&GPIO_InitStructure); //初始化PA2和PA3




	USART_InitStructure.USART_BaudRate = bound;//波特率一般为9600;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;//字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;//一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;//无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//无硬件数据流控制
	USART_InitStructure.USART_Mode =     USART_Mode_Rx | USART_Mode_Tx; // 收发模式

  USART_Init(USART2, &USART_InitStructure); //初始化串口
  USART_Cmd(USART2, ENABLE);  //使能串口


	//USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);//开启接收中断



	//USART_ClearFlag(USART1, USART_FLAG_TC);

//#if EN_USART2_RX
	USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);//开启接收中断

	//Usart2 NVIC 配置
  NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;//串口2中断通道
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=3;//抢占优先级3
	NVIC_InitStructure.NVIC_IRQChannelSubPriority =2;		//子优先级2
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			//IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);	//根据指定的参数初始化VIC寄存器


//	NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
//	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=2 ;//抢占优先级2
//	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;		//子优先级3
//	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			//IRQ通道使能
//	NVIC_Init(&NVIC_InitStructure);	//根据指定的参数初始化VIC寄存器
	TIM7_Int_Init(1000-1,8400-1);		//100ms中断
	USART2_RX_STA=0;		//清空
	TIM_Cmd(TIM7, DISABLE); //关闭定时器7

//#endif

}






//串口2,printf 函数
//确保一次发送数据不超过USART2_MAX_SEND_LEN字节
void u2_printf(char* fmt,...)
{
	u16 i,j;
	va_list ap;
	va_start(ap,fmt);
	vsprintf((char*)USART2_TX_BUF,fmt,ap);
	va_end(ap);
	i=strlen((const char*)USART2_TX_BUF);//此次发送数据的长度
	for(j=0;j<i;j++)//循环发送数据
	{
	  while(USART_GetFlagStatus(USART2,USART_FLAG_TC)==RESET);  //等待上次发送完成
		USART_SendData(USART2,(uint8_t)USART2_TX_BUF[j]); 	 //发送数据到串口2
	}

}
