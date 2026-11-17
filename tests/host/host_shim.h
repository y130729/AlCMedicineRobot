#ifndef HOST_SHIM_H
#define HOST_SHIM_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stm32f4xx.h"
#include "sys.h"
#include "led.h"
#include "exti.h"
#include "timer.h"
#include "adc.h"
#include "ui.h"
#include "camera.h"
#include "common.h"
#include "dcmi.h"
#include "wifi_transfer.h"
#include "motor.h"
#include "rtos_app.h"
#include "wifi_cmd.h"
#include "car_app.h"
#include "usart3.h"
#include "wifista.h"

void t_reset(void);
void t_set_ms(u32 ms);
void t_advance_ms(u32 ms);
u32  t_now_ms(void);
int  t_line_count(void);
const char *t_line(int index);
int  t_has(const char *prefix);
int  t_count_prefix(const char *prefix);

extern int t_motor_forward_calls;
extern int t_motor_forward_dual_calls;
extern int t_motor_backward_calls;
extern int t_motor_turn_left_calls;
extern int t_motor_turn_right_calls;
extern int t_motor_stop_calls;
extern u16 t_motor_last_left;
extern u16 t_motor_last_right;
extern u16 t_motor_last_speed;

extern u16 adcL;
extern u16 adcM;
extern u16 adcR;

#endif
