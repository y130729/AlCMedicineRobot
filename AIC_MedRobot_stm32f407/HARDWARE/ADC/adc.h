#ifndef __ADC_H
#define __ADC_H
#include "sys.h"

void Adc_Init(void);                 /* TIM2-triggered ADC3 scan + DMA */
void Adc_ReadLine(u16 *l, u16 *m, u16 *r);   /* latest averaged L/M/R   */
u32  Adc_GetFrameCount(void);        /* converted frames (diagnostics) */

#endif
