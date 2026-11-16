#include "sys.h"
#include "exti.h"
#if RTOS_ENABLE
#include "FreeRTOS.h"
#include "task.h"
#endif
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

















/* ==================== Independent time base (DWT) ====================
 * The task health check must not use EXTIX_GetMillis() (TIM6): if that
 * interrupt ever stops, every "now - last" delta becomes 0 and the watchdog
 * would keep feeding itself. RTOS mode uses the FreeRTOS tick (a dead tick
 * means a dead scheduler, so nothing feeds either); bare metal uses the DWT
 * cycle counter, which is independent of all peripheral interrupts.
 */
void DWT_Init(void)
{
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* 32-bit microsecond stamp; wraps every ~25.6 s -> only use differences. */
u32 DWT_Micros(void)
{
	return DWT->CYCCNT / (SystemCoreClock / 1000000U);
}

static u32 WDG_Now(void)
{
#if RTOS_ENABLE
	return (u32)xTaskGetTickCount() * (u32)portTICK_PERIOD_MS;
#else
	return DWT->CYCCNT / (SystemCoreClock / 1000U);
#endif
}

/* ==================== Watchdog (IWDG) ====================
 * LSI ~32kHz -> prescaler 32 -> 1kHz, reload 1600 -> ~1.6s timeout.
 * WDG_CheckAndFeed() reloads the counter only while every task has advanced
 * its progress counter AND reported within WDG_TASK_ALIVE_MS. A task that
 * still runs but stopped making progress (for example a control loop that
 * spins on a stale flag) therefore still triggers a reset, which a plain
 * "kick the dog" scheme cannot detect.
 */
#define WDG_TASK_ALIVE_MS   1200

static volatile u32 wdg_progress[WDG_TASK_COUNT];
static volatile u32 wdg_seen[WDG_TASK_COUNT];
static volatile u32 wdg_last_ms[WDG_TASK_COUNT];
static volatile u32 wdg_missed = 0;

void WDG_Init(void)
{
	u8 i;
	u32 now = WDG_Now();

	for (i = 0; i < WDG_TASK_COUNT; i++)
	{
		wdg_progress[i] = 0;
		wdg_seen[i] = 0;
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
	if (id >= WDG_TASK_COUNT)
		return;
	wdg_progress[id]++;
	wdg_last_ms[id] = WDG_Now();
}

/* Progress report from a bounded blocking driver (AT command waits).
 * RTOS: it only advances that task's progress counter; it must NOT feed the
 * IWDG, otherwise a hung WiFi task that still runs AT waits would mask a
 * dead control/UI/JPEG task. Bare metal: the main loop is the only context,
 * so the same call also feeds the watchdog. */
void WDG_TaskProgress(u8 id)
{
	WDG_TaskTick(id);
#if !RTOS_ENABLE
	WDG_Feed();
#else
	/* Before the scheduler starts, the boot-time AT wait is the only
	 * context and nothing else can feed the dog: feed it here as well. */
	if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
		WDG_Feed();
#endif
}

u8 WDG_CheckAndFeed(void)
{
	u8 i;
	u8 ok = 1;
	u32 now = WDG_Now();

	for (i = 0; i < WDG_TASK_COUNT; i++)
	{
		if (wdg_progress[i] == wdg_seen[i])          /* no progress at all  */
			ok = 0;
		else if ((s32)(now - wdg_last_ms[i]) > WDG_TASK_ALIVE_MS)
			ok = 0;                                   /* progress too old    */
	}
	if (!ok)
	{
		wdg_missed++;
		return 0;              /* stop feeding: IWDG resets in ~1.6 s */
	}
	for (i = 0; i < WDG_TASK_COUNT; i++)
		wdg_seen[i] = wdg_progress[i];
	WDG_Feed();
	return 1;
}

u32 WDG_GetMissed(void)
{
	return wdg_missed;
}
