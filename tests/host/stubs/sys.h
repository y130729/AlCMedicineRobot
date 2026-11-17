#ifndef __SYS_H
#define __SYS_H

#include "stm32f4xx.h"

#define ENABLE   1
#define DISABLE  0

extern volatile uint32_t host_gpio_f;
#define PFout(n) host_gpio_f
#define PFin(n)  host_gpio_f

u32 DWT_Micros(void);

void WDG_Feed(void);
void WDG_TaskTick(u8 id);
u8   WDG_CheckAndFeed(void);
u32  WDG_GetMissed(void);

#endif
