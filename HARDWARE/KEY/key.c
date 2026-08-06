#include "key.h"
#include "delay.h" 
//按键初始化函数
void KEY_Init(void)
{
	GPIO_InitTypeDef  GPIO_InitStructure;
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOB, ENABLE);//使能GPIOA,GPIOB时钟
	 
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;//WK_UP对应引脚PA0
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN ;//下拉
  GPIO_Init(GPIOA, &GPIO_InitStructure);//初始化GPIOA0

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;//KEY1(PB8),KEY0(PB9)
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;//按下时为低电平
	GPIO_Init(GPIOB, &GPIO_InitStructure);
 
}

//mode=0: 每次按下只返回一次; mode=1: 支持连续检测
u8 KEY_Scan(u8 mode)
{
	static u8 key_up = 1;

	if(mode) key_up = 1;
	if(key_up && ((KEY0 == 0) || (KEY1 == 0) || (WK_UP == 1)))
	{
		delay_ms(10);
		key_up = 0;
		if(KEY0 == 0) return KEY0_PRES;
		if(KEY1 == 0) return KEY1_PRES;
		if(WK_UP == 1) return WKUP_PRES;
	}
	else if((KEY0 == 1) && (KEY1 == 1) && (WK_UP == 0))
	{
		key_up = 1;
	}

	return 0;
}


















