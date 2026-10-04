#include "sys.h"
#include "exti.h"  
//////////////////////////////////////////////////////////////////////////////////	 
//STM32F407开发板
//********************************************************************************
//修改说明
//无
//////////////////////////////////////////////////////////////////////////////////  


//THUMB指令不支持汇编内联
//采用如下方法实现执行汇编指令WFI  
__asm void WFI_SET(void)
{
	WFI;		  
}
//关闭所有中断(但是不包括fault和NMI中断)
__asm void INTX_DISABLE(void)
{
	CPSID   I
	BX      LR	  
}
//开启所有中断
__asm void INTX_ENABLE(void)
{
	CPSIE   I
	BX      LR  
}
//设置栈顶地址
//addr:栈顶地址
__asm void MSR_MSP(u32 addr) 
{
	MSR MSP, r0 			//set Main Stack value
	BX r14
}

















/* ==================== Watchdog (IWDG) ====================
 * LSI ~32kHz -> prescaler 32 -> 1kHz, reload 1600 -> ~1.6s timeout.
 * WDG_CheckAndFeed() reloads the counter only while every RTOS task has
 * ticked within WDG_TASK_ALIVE_MS, so a hung task resets the MCU instead of
 * leaving the car in an undefined state. A generous window is used because a
 * task may legitimately block for a moment (e.g. AT reconnect steps).
 */
#define WDG_TASK_ALIVE_MS   1200

static volatile u32 wdg_tick[WDG_TASK_COUNT];
static u32 wdg_last_ms[WDG_TASK_COUNT];

void WDG_Init(void)
{
	u8 i;
	u32 now = EXTIX_GetMillis();

	for (i = 0; i < WDG_TASK_COUNT; i++)
	{
		wdg_tick[i] = 0;
		wdg_last_ms[i] = now;
	}

	IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
	IWDG_SetPrescaler(IWDG_Prescaler_32);
	IWDG_SetReload(1600);
	IWDG_ReloadCounter();
	IWDG_Enable();
}

void WDG_Feed(void)
{
	IWDG_ReloadCounter();
}

void WDG_TaskTick(u8 id)
{
	if (id < WDG_TASK_COUNT)
	{
		wdg_tick[id]++;
		wdg_last_ms[id] = EXTIX_GetMillis();
	}
}

u8 WDG_CheckAndFeed(void)
{
	u8 i;
	u8 ok = 1;
	u32 now = EXTIX_GetMillis();

	for (i = 0; i < WDG_TASK_COUNT; i++)
	{
		if ((s32)(now - wdg_last_ms[i]) > WDG_TASK_ALIVE_MS)
			ok = 0;/* this task stopped reporting */
	}
	if (ok)
		WDG_Feed();
	return ok;
}
