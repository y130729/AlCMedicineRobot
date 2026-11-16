#ifndef __TIMER_H
#define __TIMER_H

#include "sys.h"

#define HC_SR04_TRIG PFout(2)
#define HC_SR04_ECHO PFin(3)

void HC_SR04_Init(void);
u8   HC_SR04_Start(void);              /* emit trigger, returns 0 if busy */
u8   HC_SR04_Poll(float *distance);    /* 1 = result ready               */
u32  HC_SR04_GetTimeouts(void);

void TIM7_Int_Init(u16 arr, u16 psc);

#endif
