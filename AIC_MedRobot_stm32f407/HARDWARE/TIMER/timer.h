#ifndef __TIMER_H
#define __TIMER_H

#include "sys.h"

#define HC_SR04_TRIG PFout(2)
#define HC_SR04_ECHO PFin(3)

void TIM3_Int_Init(u16 arr, u16 psc);
void HC_SR04_Init(void);
float HC_SR04_GetDistance(void);

void TIM7_Int_Init(u16 arr, u16 psc);

#endif
